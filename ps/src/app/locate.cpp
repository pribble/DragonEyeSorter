// locate.cpp — see locate.hpp
#include "locate.hpp"
#include <algorithm>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include "util.hpp"
namespace des {
std::vector<Detection> Locator::run(const uint8_t *bgra, int width, int height, int stride, const Homography &h, LocateTiming *timing)
{
    const int s = std::max(1, cfg_.scale); const uint64_t t0 = mono_us();
    // 1/s gray, (B + 2G + R) / 4; only every s-th row is touched
    gw_ = width / s; gh_ = height / s;
    gray_.resize(size_t(gw_) * gh_); bin_.resize(gray_.size());
    for (int y = 0; y < gh_; y++) {
        const uint8_t *row = bgra + size_t(y) * s * stride;
        uint8_t *g = &gray_[size_t(y) * gw_];
        for (int x = 0; x < gw_; x++) {
            const uint8_t *px = row + size_t(x) * s * 4;
            g[x] = uint8_t((px[0] + 2 * px[1] + px[2]) >> 2);
        }
    }
    const uint64_t t1 = mono_us();
    cv::Mat gm(gh_, gw_, CV_8UC1, gray_.data()), bm(gh_, gw_, CV_8UC1, bin_.data());
    int th = cfg_.thresh;
    if (th <= 0) {
        th = int(cv::threshold(gm, bm, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU));
        if (th < cfg_.min_thresh) {   // nothing on the belt: Otsu splits the noise, clamp it
            th = cfg_.min_thresh;
            cv::threshold(gm, bm, th, 255, cv::THRESH_BINARY);
        }
    } else {
        cv::threshold(gm, bm, th, 255, cv::THRESH_BINARY);
    }
    if (cfg_.open_iter > 0) cv::morphologyEx(bm, bm, cv::MORPH_OPEN, cv::Mat(), cv::Point(-1, -1), cfg_.open_iter);
    const uint64_t t2 = mono_us();
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(bm, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    // mm^2 covered by one small-image pixel near the image center (area filter only)
    Vec2 c0 = h.map({width / 2.f, height / 2.f});
    Vec2 ex = h.map({width / 2.f + s, height / 2.f}) - c0, ey = h.map({width / 2.f, height / 2.f + s}) - c0;
    float px_mm2 = std::fabs(ex.x * ey.y - ex.y * ey.x);
    std::vector<Detection> out;
    rects_px_.clear();
    rects_full_.clear();
    const float sf = float(s), off = 0.5f * (sf - 1);   // small-image px -> full-resolution px (rects_full)
    const int b = cfg_.border_px;
    for (const auto &c : contours) {
        float area_mm2 = float(cv::contourArea(c)) * px_mm2;
        if (area_mm2 < cfg_.min_area_mm2) continue;
        cv::RotatedRect rr = cv::minAreaRect(c);
        cv::Rect br = cv::boundingRect(c);
        cv::Point2f pts[4];
        rr.points(pts);
        Vec2 m[4];
        for (int i = 0; i < 4; i++) m[i] = h.map({pts[i].x * sf + off, pts[i].y * sf + off});
        Detection d;
        d.p = (m[0] + m[1] + m[2] + m[3]) * 0.25f;
        Vec2 sa = m[1] - m[0], sb = m[2] - m[1];
        float la = norm(sa), lb = norm(sb);
        if (la >= lb) { d.len = la; d.wid = lb; d.axis = wrap_axis(std::atan2(sa.y, sa.x)); }
        else          { d.len = lb; d.wid = la; d.axis = wrap_axis(std::atan2(sb.y, sb.x)); }
        d.partial = br.x <= b || br.y <= b || br.x + br.width >= gw_ - b || br.y + br.height >= gh_ - b;
        d.merged = area_mm2 > cfg_.max_area_mm2;
        d.img_v = (pts[0].y + pts[1].y + pts[2].y + pts[3].y) * 0.25f / gh_;
        out.push_back(d);
        std::array<float, 8> px{}, full{};
        for (int i = 0; i < 4; i++) {
            px[2 * i] = pts[i].x;
            px[2 * i + 1] = pts[i].y;
            full[2 * i] = pts[i].x * sf + off;
            full[2 * i + 1] = pts[i].y * sf + off;
        }
        rects_px_.push_back(px);
        rects_full_.push_back(full);
    }
    if (timing) {
        timing->gray_us = uint32_t(t1 - t0);
        timing->thresh_us = uint32_t(t2 - t1);
        timing->contour_us = uint32_t(mono_us() - t2);
        timing->threshold = uint8_t(th);
    }
    return out;
}
bool Locator::dump(const char *path) const
{
    if (gray_.empty()) return false;
    cv::Mat g(gh_, gw_, CV_8UC1, const_cast<uint8_t *>(gray_.data()));
    cv::Mat bm(gh_, gw_, CV_8UC1, const_cast<uint8_t *>(bin_.data()));
    cv::Mat img;
    cv::cvtColor(g, img, cv::COLOR_GRAY2BGR);
    img.setTo(cv::Scalar(0, 90, 0), bm);   // thresholded pixels tinted green
    for (const auto &r : rects_px_)
        for (int i = 0; i < 4; i++)
            cv::line(img, cv::Point2f(r[2 * i], r[2 * i + 1]), cv::Point2f(r[(2 * i + 2) % 8], r[(2 * i + 3) % 8]), cv::Scalar(0, 0, 255), 1, cv::LINE_AA);
    return cv::imwrite(path, img);
}
}  // namespace des
