// calib.cpp — see calib.hpp
#include "calib.hpp"

#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>

#include <opencv2/calib3d.hpp>
#include <opencv2/core/utility.hpp>
#include <opencv2/imgproc.hpp>

#include "json.hpp"

namespace des {

bool parse_travel(const std::string &s, Travel &t)
{
    if (s == "right") t = Travel::Right;
    else if (s == "left") t = Travel::Left;
    else if (s == "down") t = Travel::Down;
    else if (s == "up") t = Travel::Up;
    else return false;
    return true;
}

const char *to_str(Travel t)
{
    switch (t) {
    case Travel::Right: return "right";
    case Travel::Left: return "left";
    case Travel::Down: return "down";
    case Travel::Up: return "up";
    }
    return "?";
}

static Vec2 image_dir(Travel t)
{
    switch (t) {
    case Travel::Right: return {1, 0};
    case Travel::Left: return {-1, 0};
    case Travel::Down: return {0, 1};
    case Travel::Up: return {0, -1};
    }
    return {1, 0};
}

// nearest board axis (unit vector along +-x or +-y of the board frame)
static Vec2 snap_axis(Vec2 v)
{
    if (std::fabs(v.x) >= std::fabs(v.y)) return {v.x >= 0 ? 1.f : -1.f, 0.f};
    return {0.f, v.y >= 0 ? 1.f : -1.f};
}

static Homography normalized(const double m[9])
{
    Homography h;
    for (int i = 0; i < 9; i++) h.h[i] = m[i] / m[8];
    return h;
}

static double ms_since(const timespec &t0)
{
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (t.tv_sec - t0.tv_sec) * 1e3 + (t.tv_nsec - t0.tv_nsec) / 1e6;
}

bool detect_board(const cv::Mat &gray, const BoardSpec &board, bool exhaustive, std::vector<cv::Point2f> &c)
{
    if (gray.empty() || gray.type() != CV_8UC1) return false;
    timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    const cv::Size pattern(board.cols, board.rows);
    const cv::TermCriteria crit(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 50, 0.01);
    const bool dbg = getenv("CALIB_DEBUG") != nullptr;
    auto stage = [&](const char *what, bool ok) {
        if (dbg) fprintf(stderr, "calib: %s %s after %.0f ms\n", what, ok ? "found" : "failed", ms_since(t0));
    };
    // Cheapest first. The classic detector takes ~0.1 s on a clean board, but can search for minutes when the
    // board in view has a different corner count than --board (hence detect_board_timeout). The sector detector
    // (findChessboardCornersSB) is more robust to blur / noise but slow on the A35: ~12 s at full resolution.
    bool found = cv::findChessboardCorners(gray, pattern, c, cv::CALIB_CB_ADAPTIVE_THRESH |
                                           cv::CALIB_CB_NORMALIZE_IMAGE | cv::CALIB_CB_FAST_CHECK);
    stage("classic", found);
    if (found) cv::cornerSubPix(gray, c, cv::Size(11, 11), cv::Size(-1, -1), crit);
    if (!found) {   // sector detector on half resolution, refined at full resolution
        cv::Mat half;
        cv::resize(gray, half, cv::Size(), 0.5, 0.5, cv::INTER_AREA);
        found = cv::findChessboardCornersSB(half, pattern, c, cv::CALIB_CB_ACCURACY);
        stage("sector 1/2", found);
        if (found) {
            for (cv::Point2f &p : c) p = p * 2.f + cv::Point2f(0.5f, 0.5f);
            cv::cornerSubPix(gray, c, cv::Size(11, 11), cv::Size(-1, -1), crit);
        }
    }
    if (!found && exhaustive) {   // last resort, only on request
        found = cv::findChessboardCornersSB(gray, pattern, c, cv::CALIB_CB_EXHAUSTIVE | cv::CALIB_CB_ACCURACY);
        stage("sector exhaustive", found);
    }
    return found && c.size() == size_t(board.cols * board.rows);
}

int detect_board_timeout(const cv::Mat &gray, const BoardSpec &board, bool exhaustive, double timeout_s,
                         std::vector<cv::Point2f> &corners)
{
    int fds[2];
    if (pipe(fds) != 0) return detect_board(gray, board, exhaustive, corners) ? 1 : 0;
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return detect_board(gray, board, exhaustive, corners) ? 1 : 0;
    }
    if (pid == 0) {   // child: detect, send the corners, exit without running the parent's destructors
        close(fds[0]);
        cv::setNumThreads(1);
        std::vector<cv::Point2f> c;
        int32_t n = detect_board(gray, board, exhaustive, c) ? int32_t(c.size()) : 0;
        if (write(fds[1], &n, sizeof n) == sizeof n && n > 0) {
            ssize_t len = ssize_t(n) * ssize_t(sizeof(cv::Point2f)), done = 0;
            while (done < len) {
                ssize_t w = write(fds[1], reinterpret_cast<const char *>(c.data()) + done, size_t(len - done));
                if (w <= 0) break;
                done += w;
            }
        }
        _exit(0);
    }
    close(fds[1]);
    pollfd pfd{fds[0], POLLIN, 0};
    int pr = poll(&pfd, 1, int(timeout_s * 1000));
    int result = -1;
    if (pr > 0) {
        int32_t n = 0;
        if (read(fds[0], &n, sizeof n) == sizeof n) {
            corners.resize(size_t(n > 0 ? n : 0));
            ssize_t len = ssize_t(n > 0 ? n : 0) * ssize_t(sizeof(cv::Point2f)), done = 0;
            while (done < len) {
                ssize_t rd = read(fds[0], reinterpret_cast<char *>(corners.data()) + done, size_t(len - done));
                if (rd <= 0) break;
                done += rd;
            }
            result = (n > 0 && done == len) ? 1 : 0;
        } else {
            result = 0;   // child died without answering
        }
    } else {
        kill(pid, SIGKILL);
    }
    waitpid(pid, nullptr, 0);
    close(fds[0]);
    return result;
}

CalibResult calibrate_from_corners(const std::vector<cv::Point2f> &c, cv::Size image, const BoardSpec &board,
                                   Travel travel)
{
    CalibResult r;
    if (c.size() != size_t(board.cols * board.rows)) {
        r.err = "corner count does not match --board";
        return r;
    }
    r.corners = c;

    // board frame: OpenCV corner order is row-major, (i, j) -> (i * sq, j * sq); which corner is first depends on
    // how the board lies, so the axes are re-derived below from the image directions
    std::vector<cv::Point2f> obj;
    for (int j = 0; j < board.rows; j++)
        for (int i = 0; i < board.cols; i++) obj.emplace_back(i * board.square_mm, j * board.square_mm);
    cv::Mat hm = cv::findHomography(c, obj, 0);
    if (hm.empty()) { r.err = "homography fit failed"; return r; }
    double hb_raw[9];
    for (int i = 0; i < 9; i++) hb_raw[i] = hm.at<double>(i / 3, i % 3);
    const Homography hb = normalized(hb_raw);

    double se = 0;
    for (size_t k = 0; k < c.size(); k++) {
        Vec2 p = hb.map({c[k].x, c[k].y});
        float e = std::hypot(p.x - obj[k].x, p.y - obj[k].y);
        se += double(e) * e;
        r.max_mm = std::max(r.max_mm, e);
    }
    r.rms_mm = float(std::sqrt(se / c.size()));

    // table axes: x = board axis closest to the travel direction, y = 90 deg CCW seen from above.
    // The image is a top view (not mirrored): rotating an image direction t by +90 deg CCW gives (t.y, -t.x).
    const float W = float(image.width), H = float(image.height);
    const Vec2 cimg{W / 2, H / 2};
    const Vec2 t = image_dir(travel), n{t.y, -t.x};
    const Vec2 b0 = hb.map(cimg);
    const Vec2 ex = snap_axis(hb.map(cimg + t * 50.f) - b0);
    const Vec2 ey = snap_axis(hb.map(cimg + n * 50.f) - b0);
    if (std::fabs(dot(ex, ey)) > 0.5f) {
        r.err = "board axes ambiguous: lay the board with an edge along the belt";
        return r;
    }
    // origin: x = 0 at the upstream image edge (center line), y = 0 on the image center line
    Vec2 up_edge, dn_edge;
    switch (travel) {
    case Travel::Right: up_edge = {0, H / 2}; dn_edge = {W - 1, H / 2}; break;
    case Travel::Left: up_edge = {W - 1, H / 2}; dn_edge = {0, H / 2}; break;
    case Travel::Down: up_edge = {W / 2, 0}; dn_edge = {W / 2, H - 1}; break;
    case Travel::Up: up_edge = {W / 2, H - 1}; dn_edge = {W / 2, 0}; break;
    }
    const float ox = -dot(hb.map(up_edge), ex), oy = -dot(hb.map(cimg), ey);
    const double A[9] = {ex.x, ex.y, ox, ey.x, ey.y, oy, 0, 0, 1};
    double m[9];
    for (int i = 0; i < 3; i++)
        for (int k = 0; k < 3; k++)
            m[i * 3 + k] = A[i * 3] * hb.h[k] + A[i * 3 + 1] * hb.h[3 + k] + A[i * 3 + 2] * hb.h[6 + k];
    r.px_to_table = normalized(m);

    const Homography &h = r.px_to_table;
    r.mm_per_px = norm(h.map(cimg + Vec2{1, 0}) - h.map(cimg));
    r.fov_x0 = h.map(up_edge).x;
    r.fov_x1 = h.map(dn_edge).x;
    const Vec2 ic[4] = {{0, 0}, {W - 1, 0}, {W - 1, H - 1}, {0, H - 1}};
    for (int i = 0; i < 4; i++) r.fov_corners[i] = h.map(ic[i]);
    r.ok = true;
    return r;
}

CalibResult calibrate_table(const cv::Mat &gray, const BoardSpec &board, Travel travel, bool exhaustive)
{
    timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    std::vector<cv::Point2f> c;
    bool found = detect_board(gray, board, exhaustive, c);
    double ms = ms_since(t0);
    CalibResult r;
    if (!found) {
        r.err = "board not found (check --board inner-corner count, focus, lighting; whole board in view)";
    } else {
        r = calibrate_from_corners(c, gray.size(), board, travel);
    }
    r.detect_ms = ms;
    return r;
}

std::string save_calib(const std::string &path, const CalibResult &r, const BoardSpec &board, Travel travel)
{
    if (!r.ok) return "no calibration to save";
    nlohmann::ordered_json j;
    char date[32];
    time_t now = time(nullptr);
    strftime(date, sizeof date, "%Y-%m-%d %H:%M", localtime(&now));
    j["comment"] = "des_app calib-cam 生成，勿手改（重新标定会覆盖）。load_config 读完 des_app.json 后合并本文件。";
    j["date"] = date;
    j["board"] = {{"cols", board.cols}, {"rows", board.rows}, {"square_mm", board.square_mm}};
    j["travel"] = to_str(travel);
    j["rms_mm"] = r.rms_mm;
    j["max_mm"] = r.max_mm;
    j["mm_per_px"] = r.mm_per_px;
    nlohmann::json h = nlohmann::json::array();
    for (double v : r.px_to_table.h) h.push_back(v);
    j["px_to_table"] = h;
    j["tracker"] = {{"fov_x0", r.fov_x0}, {"fov_x1", r.fov_x1}};
    nlohmann::json fc = nlohmann::json::array();
    for (const Vec2 &v : r.fov_corners) fc.push_back({v.x, v.y});
    j["fov_corners"] = fc;
    std::ofstream f(path);
    if (!f) return "cannot write " + path;
    f << j.dump(2) << "\n";
    return f ? "" : "write failed: " + path;
}

void draw_calib(cv::Mat &bgr, const CalibResult &r)
{
    if (!r.ok) return;
    cv::drawChessboardCorners(bgr, cv::Size(0, 0), r.corners, false);
    for (const cv::Point2f &p : r.corners) cv::circle(bgr, p, 4, cv::Scalar(0, 0, 255), 2);
    const Homography inv = r.px_to_table.inverse();
    float ymin = 1e9f, ymax = -1e9f, xmin = 1e9f, xmax = -1e9f;
    for (const Vec2 &v : r.fov_corners) {
        ymin = std::min(ymin, v.y); ymax = std::max(ymax, v.y);
        xmin = std::min(xmin, v.x); xmax = std::max(xmax, v.x);
    }
    auto px = [&](float x, float y) { Vec2 p = inv.map({x, y}); return cv::Point2f(p.x, p.y); };
    for (float x = std::ceil(xmin / 50) * 50; x <= xmax; x += 50)
        cv::line(bgr, px(x, ymin), px(x, ymax), cv::Scalar(255, 255, 0), 1);
    for (float y = std::ceil(ymin / 50) * 50; y <= ymax; y += 50)
        cv::line(bgr, px(xmin, y), px(xmax, y), cv::Scalar(255, 255, 0), 1);
    cv::arrowedLine(bgr, px(0, 0), px(60, 0), cv::Scalar(0, 0, 255), 3, cv::LINE_8, 0, 0.2);
    cv::arrowedLine(bgr, px(0, 0), px(0, 60), cv::Scalar(0, 200, 0), 3, cv::LINE_8, 0, 0.2);
    cv::putText(bgr, "x (belt)", px(62, 0), cv::FONT_HERSHEY_SIMPLEX, 0.9, cv::Scalar(0, 0, 255), 2);
    cv::putText(bgr, "y", px(0, 62), cv::FONT_HERSHEY_SIMPLEX, 0.9, cv::Scalar(0, 200, 0), 2);
    char t[128];
    snprintf(t, sizeof t, "rms %.3f mm  max %.3f mm  %.4f mm/px  FOV x %.1f..%.1f mm", r.rms_mm, r.max_mm, r.mm_per_px,
             r.fov_x0, r.fov_x1);
    cv::rectangle(bgr, cv::Rect(0, 0, 1000, 44), cv::Scalar(0, 0, 0), cv::FILLED);
    cv::putText(bgr, t, cv::Point(12, 30), cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(255, 255, 255), 2);
}

}  // namespace des
