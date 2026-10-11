// test_calib: camera -> table calibration on synthetic checkerboard images with a known ground truth.
// Renders the board through a known perspective transform (rotation, tilt, blur, noise), runs calibrate_table and
// checks distances, axis directions, origin and handedness.  ./test_calib [dump_dir]   exit code = failed checks
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <random>
#include <string>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "calib.hpp"

using namespace des;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (cond) g_pass++;                                           \
        else { g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__);  \
               printf(__VA_ARGS__); printf("\n"); }                   \
    } while (0)

struct Pose {
    const char *name;
    double rot_deg;      // board rotation in the image
    double mm_per_px;    // image scale on the belt
    double px, py;       // perspective terms (1/mm)
    Travel travel;
};

// board raster: (cols+1) x (rows+1) squares plus one white square of margin, 8 px per mm
static cv::Mat render_board(const BoardSpec &b, double &raster_px_per_mm, cv::Size &size_mm)
{
    raster_px_per_mm = 8;
    const double sq = b.square_mm, w = (b.cols + 3) * sq, h = (b.rows + 3) * sq;
    size_mm = cv::Size(int(w), int(h));
    cv::Mat r(int(h * raster_px_per_mm), int(w * raster_px_per_mm), CV_8UC1, cv::Scalar(225));
    for (int y = 0; y < r.rows; y++)
        for (int x = 0; x < r.cols; x++) {
            double bx = x / raster_px_per_mm - sq, by = y / raster_px_per_mm - sq;   // pattern starts one square in
            if (bx < 0 || by < 0 || bx >= (b.cols + 1) * sq || by >= (b.rows + 1) * sq) continue;
            int ix = int(bx / sq), iy = int(by / sq);
            if ((ix + iy) % 2 == 0) r.at<uint8_t>(y, x) = 25;
        }
    return r;
}

// board mm (origin at the raster's top-left) -> image px
static cv::Matx33d board_to_image(const Pose &p, cv::Size size_mm)
{
    const double s = 1.0 / p.mm_per_px, a = p.rot_deg * CV_PI / 180;
    cv::Matx33d center(1, 0, -size_mm.width / 2.0, 0, 1, -size_mm.height / 2.0, 0, 0, 1);
    cv::Matx33d rot(std::cos(a), -std::sin(a), 0, std::sin(a), std::cos(a), 0, 0, 0, 1);
    cv::Matx33d persp(1, 0, 0, 0, 1, 0, p.px, p.py, 1);
    cv::Matx33d scale(s, 0, 0, 0, s, 0, 0, 0, 1);
    cv::Matx33d to_img(1, 0, 960 + 30, 0, 1, 540 - 20, 0, 0, 1);   // slightly off-center
    return to_img * scale * persp * rot * center;
}

static Vec2 apply(const cv::Matx33d &m, Vec2 p)
{
    cv::Vec3d v = m * cv::Vec3d(p.x, p.y, 1);
    return {float(v[0] / v[2]), float(v[1] / v[2])};
}

static void run_pose(const Pose &p, const BoardSpec &b, const std::string &dump_dir, std::mt19937 &rng)
{
    double rpm;
    cv::Size size_mm;
    cv::Mat raster = render_board(b, rpm, size_mm);
    cv::Matx33d raster_to_mm(1 / rpm, 0, 0, 0, 1 / rpm, 0, 0, 0, 1);
    cv::Matx33d g = board_to_image(p, size_mm);
    cv::Mat img;
    cv::warpPerspective(raster, img, cv::Mat(g * raster_to_mm), cv::Size(1920, 1080), cv::INTER_AREA,
                        cv::BORDER_CONSTANT, cv::Scalar(70));
    cv::GaussianBlur(img, img, cv::Size(5, 5), 1.0);
    cv::Mat noise(img.size(), CV_16SC1);
    cv::randn(noise, 0, 3);
    cv::Mat img16;
    img.convertTo(img16, CV_16SC1);
    img16 += noise;
    img16.convertTo(img, CV_8UC1);

    CalibResult r = calibrate_table(img, b, p.travel);
    CHECK(r.ok, "[%s] calibration failed: %s", p.name, r.err.c_str());
    if (!r.ok) return;
    printf("[%s] detect %.0f ms, rms %.4f mm, max %.4f mm, %.4f mm/px, FOV x %.1f..%.1f\n", p.name, r.detect_ms,
           r.rms_mm, r.max_mm, r.mm_per_px, r.fov_x0, r.fov_x1);
    CHECK(r.rms_mm < 0.05f, "[%s] rms %.4f mm", p.name, r.rms_mm);

    // distances: table coords must be a rigid copy of the true board coords
    cv::Matx33d ginv = g.inv();
    std::uniform_real_distribution<float> ux(500, 1400), uy(250, 830);
    float worst = 0;
    for (int k = 0; k < 200; k++) {
        Vec2 a{ux(rng), uy(rng)}, c{ux(rng), uy(rng)};
        float d_true = norm(apply(ginv, a) - apply(ginv, c));
        float d_cal = norm(r.px_to_table.map(a) - r.px_to_table.map(c));
        worst = std::max(worst, std::fabs(d_true - d_cal));
    }
    CHECK(worst < 0.1f, "[%s] distance error %.4f mm", p.name, worst);
    printf("[%s] worst distance error over 200 random pairs: %.4f mm\n", p.name, worst);

    // axes, origin, handedness
    const Vec2 c0{960, 540};
    Vec2 t = p.travel == Travel::Right ? Vec2{1, 0} : p.travel == Travel::Left ? Vec2{-1, 0}
           : p.travel == Travel::Down ? Vec2{0, 1} : Vec2{0, -1};
    Vec2 along = r.px_to_table.map(c0 + t * 100.f) - r.px_to_table.map(c0);
    CHECK(along.x > 0 && std::fabs(std::atan2(along.y, along.x)) < 0.2f, "[%s] travel not along +x (%.2f, %.2f)",
          p.name, along.x, along.y);
    Vec2 n{t.y, -t.x};
    Vec2 side = r.px_to_table.map(c0 + n * 100.f) - r.px_to_table.map(c0);
    CHECK(side.y > 0, "[%s] y not 90 deg CCW of x", p.name);
    Vec2 ex = r.px_to_table.map(c0 + Vec2{1, 0}) - r.px_to_table.map(c0);
    Vec2 ey = r.px_to_table.map(c0 + Vec2{0, -1}) - r.px_to_table.map(c0);
    CHECK(ex.x * ey.y - ex.y * ey.x > 0, "[%s] table frame is mirrored", p.name);
    CHECK(std::fabs(r.fov_x0) < 1e-3f, "[%s] fov_x0 %.4f", p.name, r.fov_x0);
    CHECK(std::fabs(r.px_to_table.map({960, 540}).y) < 1e-3f, "[%s] center y not 0", p.name);

    if (!dump_dir.empty()) {
        cv::imwrite(dump_dir + "/raw_" + p.name + ".png", img);   // input for `des_app calib-cam --image`
        cv::Mat bgr;
        cv::cvtColor(img, bgr, cv::COLOR_GRAY2BGR);
        draw_calib(bgr, r);
        cv::imwrite(dump_dir + "/calib_" + p.name + ".png", bgr);
    }
}

int main(int argc, char **argv)
{
    std::string dump = argc > 1 ? argv[1] : "";
    std::mt19937 rng(7);
    BoardSpec b;
    b.cols = 9;
    b.rows = 6;
    b.square_mm = 15;
    const Pose poses[] = {
        {"tilt3", 3, 0.16, 2e-4, -1e-4, Travel::Right},
        {"upside_down", 183, 0.16, -1e-4, 2e-4, Travel::Right},
        {"rot90", -88, 0.20, 1e-4, 1e-4, Travel::Right},
        {"travel_down", 2, 0.15, 0, 1.5e-4, Travel::Down},
        {"travel_left", -4, 0.14, 1e-4, 0, Travel::Left},
    };
    for (const Pose &p : poses) run_pose(p, b, dump, rng);

    cv::Mat blank(1080, 1920, CV_8UC1, cv::Scalar(60));
    CalibResult none = calibrate_table(blank, b, Travel::Right);
    CHECK(!none.ok, "blank image must not calibrate");

    std::string path = (dump.empty() ? std::string("/tmp") : dump) + "/calib_test.json";
    CalibResult r0;
    {
        double rpm;
        cv::Size sz;
        cv::Mat raster = render_board(b, rpm, sz);
        cv::Mat img;
        cv::warpPerspective(raster, img, cv::Mat(board_to_image(poses[0], sz) *
                            cv::Matx33d(1 / rpm, 0, 0, 0, 1 / rpm, 0, 0, 0, 1)), cv::Size(1920, 1080),
                            cv::INTER_AREA, cv::BORDER_CONSTANT, cv::Scalar(70));
        r0 = calibrate_table(img, b, Travel::Right);
    }
    CHECK(save_calib(path, r0, b, Travel::Right).empty(), "save_calib to %s", path.c_str());

    // belt scale (calib-belt): 0.0098 mm/step, part moved 8 mm then 88 mm, back with 0.05 mm slip, 0.4 mm drift
    const double k = 0.0098;
    BeltCalib bc = belt_from_positions({20, 1}, {20 + 816 * k, 1.03f}, {20 + 9816 * k, 1.4f}, {20.05f, 1}, 816, 9000);
    CHECK(bc.ok && std::fabs(bc.mm_per_step - k) < 1e-6 && bc.dir == 1, "belt + dir: %s %.6f %d", bc.err.c_str(),
          bc.mm_per_step, bc.dir);
    CHECK(std::fabs(bc.return_err_mm - 0.05) < 1e-3 && std::fabs(bc.drift_mm_per_100 - 0.4 / (9816 * k) * 100) < 1e-3,
          "belt return %.4f drift %.4f", bc.return_err_mm, bc.drift_mm_per_100);
    CHECK(std::fabs(bc.roller_d_mm - k * 3200 / 3.14159265) < 1e-3, "roller %.3f", bc.roller_d_mm);
    // wiring reversed: + steps move the part upstream; the guess was wrong, the long move went the other way
    bc = belt_from_positions({20, 0}, {20 - 816 * k, 0}, {20 + 8184 * k, 0}, {20, 0}, 816, -9000);
    CHECK(bc.ok && bc.dir == -1 && std::fabs(bc.mm_per_step - k) < 1e-6, "belt reversed: %s %d %.6f", bc.err.c_str(),
          bc.dir, bc.mm_per_step);
    bc = belt_from_positions({20, 0}, {20.3f, 0}, {20.5f, 0}, {20, 0}, 816, 9000);
    CHECK(!bc.ok, "part that did not move must fail");
    bc = belt_from_positions({20, 0}, {20 + 816 * k, 0}, {20 + 816 * k + 9000 * k * 0.6, 0}, {20, 0}, 816, 9000);
    CHECK(!bc.ok, "slipping belt (scale differs by 40 %%) must fail");
    CHECK(save_belt_calib(path, belt_from_positions({20, 1}, {20 + 816 * k, 1}, {20 + 9816 * k, 1}, {20, 1}, 816, 9000))
              .empty(), "save_belt_calib");
    {
        std::ifstream f(path);
        std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        CHECK(s.find("\"px_to_table\"") != std::string::npos && s.find("\"mm_per_step\"") != std::string::npos,
              "calib.json keeps the camera part next to the belt");
        CHECK(save_calib(path, r0, b, Travel::Right).empty(), "save_calib again");
        std::ifstream g(path);
        std::string t((std::istreambuf_iterator<char>(g)), std::istreambuf_iterator<char>());
        CHECK(t.find("\"mm_per_step\"") != std::string::npos, "calib-cam keeps the belt scale");
    }
    {   // calib-arm result next to the camera and belt parts, kept by a later calib-cam
        ArmBaseFit af;
        af.ok = true;
        af.base_xy = {380, -170};
        af.base_yaw = 1.6f;
        af.belt_z = 12;
        af.resid = {0.3f, 0.2f, 0.4f};
        CHECK(save_arm_calib(path, af, true).empty(), "save_arm_calib");
        CHECK(save_calib(path, r0, b, Travel::Right).empty(), "save_calib after calib-arm");
        std::ifstream g(path);
        std::string t((std::istreambuf_iterator<char>(g)), std::istreambuf_iterator<char>());
        CHECK(t.find("\"base_yaw_deg\"") != std::string::npos && t.find("\"belt_z\"") != std::string::npos &&
                  t.find("\"mm_per_step\"") != std::string::npos,
              "calib.json keeps arm + belt sections across calib-cam");
    }
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail;
}
