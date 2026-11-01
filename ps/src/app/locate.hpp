// locate.hpp — part localization on the PS (docs/视觉与检测.md "视觉定位与角度"): white POM parts on a black matte belt. Downsampled gray -> threshold (Otsu, clamped; or relative to the white level of the view, LocateConfig::rel_thresh) -> opening -> external contours -> minAreaRect,
// then mapped to table mm through the camera homography. Gives (x, y, axis, len, wid) per blob; the NPU class is attached later by matching boxes (cls stays -1 here).
#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include "config.hpp"
#include "tracker.hpp"
namespace des {
struct LocateTiming {
    uint32_t gray_us = 0, thresh_us = 0, contour_us = 0;
    uint8_t threshold = 0;
};
class Locator {
public:
    explicit Locator(const LocateConfig &cfg) : cfg_(cfg) {}
    // bgra: 4 bytes per pixel, stride in bytes.
    std::vector<Detection> run(const uint8_t *bgra, int width, int height, int stride, const Homography &h, LocateTiming *timing = nullptr);
    // Save the last gray / binary image with the found rectangles drawn (PNG), for tuning.
    bool dump(const char *path) const;
    // Corners (x0,y0..x3,y3) of each blob of the last run in full-resolution image px, same order as run()'s result (cached by run(): the overlay of the live loop reads it every frame).
    const std::vector<std::array<float, 8>> &rects_full() const { return rects_full_; }
    double gray_mean() const;   // mean of the last small gray image (运维: lamp off / lens covered), -1 before run()
private:
    LocateConfig cfg_;
    std::vector<uint8_t> gray_, bin_;
    std::vector<uint8_t> thr_;   // per-pixel threshold of the small image (rel_thresh mode), built on first use
    int thr_w_ = 0, thr_h_ = 0;
    int gw_ = 0, gh_ = 0;
    std::vector<std::array<float, 8>> rects_px_;   // 4 corners (x0,y0..x3,y3) per blob, small-image px
    std::vector<std::array<float, 8>> rects_full_; // the same in full-resolution image px
};
}  // namespace des
