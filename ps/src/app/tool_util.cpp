// tool_util.cpp — see tool_util.hpp
#include "tool_util.hpp"
#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include "util.hpp"
namespace des {
BeltRates belt_rates(const AppConfig &cfg, double mm_s, double accel_mm_s2)
{
    const double mps = std::fabs(cfg.belt.mm_per_step); BeltRates r; r.vmax = mm_s / mps; r.acc = accel_mm_s2 < 0 ? r.vmax * 2 : accel_mm_s2 / mps;
    return r;
}
void belt_prepare(des_hw_t &hw)
{
    des_step_enable(&hw, 1); usleep(100000);
}
std::string belt_move(des_hw_t &hw, int32_t steps, double vmax, double acc, const std::function<void()> *sample)
{
    const bool sampled = sample != nullptr;
    if (sampled) {   // baseline of the encoder samples: the belt is still
        for (int i = 0; i < 5; i++) {
            (*sample)(); usleep(10000);
        }
    }
    if (des_estop_latched(&hw) == 1)   // v11: the PL holds the stepper until the latch is cleared
        return "e-stop latched: release the button, then desreg io estop-clr";
    const int32_t target = des_step_pos(&hw) + steps; des_step_move(&hw, target, vmax, acc);
    const double t0 = now_s(), tmax = std::abs(steps) / vmax + 2 * vmax / acc + 3; usleep(sampled ? 10000 : 20000);
    while (des_step_status(&hw) & DES_ST_STATUS_MOVING) {
        if (g_stop || now_s() - t0 > tmax) {
            des_step_stop(&hw);
            return g_stop ? "interrupted" : "move did not finish in time";
        }
        if (sampled) (*sample)();
        usleep(sampled ? 10000 : 5000);
    }
    if (sampled)   // 0.3 s: the belt / wheel settles
        for (int i = 0; i < 30; i++) {
            (*sample)(); usleep(10000);
        }
    if (des_step_status(&hw) & DES_ST_STATUS_HALTED) return "stepper halted (e-stop input?)";
    return des_step_pos(&hw) == target ? "" : "stepper stopped short of the target";
}
std::string capture_frames(CameraRig &cam, int n, CapturedFrame &f, bool keep_last)
{
    std::string err;
    for (int i = 0; i < n; i++)
        if (!(err = cam.next(f, keep_last && i == n - 1)).empty()) return err;
    return "";
}
std::string capture_gray_avg(CameraRig &cam, int discard, cv::Mat &sum, cv::Mat &gray)
{
    CapturedFrame f;
    std::string err = capture_frames(cam, discard, f);   // frames exposed during the move
    if (!err.empty()) return err;
    cv::Mat g, bgra;
    for (int i = 0; i < 4; i++) {
        if (!(err = cam.next(f, true)).empty()) return err;
        bgra = cv::Mat(f.height, f.width, CV_8UC4, const_cast<uint8_t *>(f.bgra), size_t(f.stride)); cv::cvtColor(bgra, g, cv::COLOR_BGRA2GRAY);
        if (sum.empty()) sum = cv::Mat::zeros(g.size(), CV_32F);
        else sum.setTo(0);
        cv::accumulate(g, sum);
    }
    sum.convertTo(gray, CV_8U, 0.25);
    return "";
}
std::string still_position(CameraRig &cam, Locator &loc, const AppConfig &cfg, Vec2 &p)
{
    CapturedFrame f;
    std::string err = capture_frames(cam, cfg.camera.buffers + 2, f);   // ring may still hold frames of the move
    if (!err.empty()) return err;
    std::vector<float> xs, ys; xs.reserve(7); ys.reserve(7); int blobs = 0;
    for (int i = 0; i < 7; i++) {
        if (!(err = cam.next(f, true)).empty()) return err;
        std::vector<Detection> d = loc.run(f.bgra, f.width, f.height, f.stride, cfg.px_to_table); const Detection *best = nullptr;
        for (const Detection &x : d)
            if (!x.partial && (!best || x.len * x.wid > best->len * best->wid)) best = &x;
        blobs = std::max(blobs, int(d.size()));
        if (best) {
            xs.push_back(best->p.x); ys.push_back(best->p.y);
        }
    }
    if (xs.size() < 4) return "no part fully in view (one part on the belt, inside the view, threshold ok? try locate)";
    if (blobs > 1) printf("  note: %d blobs in view, using the largest\n", blobs);
    std::nth_element(xs.begin(), xs.begin() + xs.size() / 2, xs.end()); std::nth_element(ys.begin(), ys.begin() + ys.size() / 2, ys.end());
    p = {xs[xs.size() / 2], ys[ys.size() / 2]};
    return "";
}
bool read_image(const std::string &path, cv::Mat &bgr, cv::Mat &gray)
{
    bgr = cv::imread(path, cv::IMREAD_COLOR);
    if (bgr.empty()) return false;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    return true;
}
std::string calib_out(const AppConfig &cfg, const Args &a, CalibDefault which)
{
    if (!a.out.empty()) return a.out;   // --out wins
    switch (which) {
    case CalibDefault::Camera: {   // next to the config file as given on the command line (config_dir may be stale if
                                   // load_config failed, and calib-cam still runs then)
        const size_t k = a.config.find_last_of('/');
        return (k == std::string::npos ? std::string(".") : a.config.substr(0, k)) + "/calib.json";
    }
    case CalibDefault::Belt: return cfg.calib_loaded;
    case CalibDefault::Encoder: return cfg.calib_loaded.empty() ? cfg.calib_file : cfg.calib_loaded;
    case CalibDefault::Flat: {
        const std::string fn = cfg.flat_file.empty() ? "flat.json" : cfg.flat_file;
        return fn[0] == '/' ? fn : cfg.config_dir + "/" + fn;
    }
    case CalibDefault::Arm: return cfg.calib_loaded;
    }
    return cfg.calib_loaded;
}
void report_saved(const std::string &out, const std::string &err, const char *ok_extra)
{
    printf("saved %s: %s%s\n", out.c_str(), err.empty() ? "ok" : err.c_str(), err.empty() && ok_extra ? ok_extra : "");
}
}  // namespace des
