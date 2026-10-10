// locate.hpp — part localization on the PS (docs/02 "视觉定位与角度"): white POM parts on a black matte belt.
// Downsampled gray -> threshold (Otsu, clamped) -> opening -> external contours -> minAreaRect,
// then mapped to table mm through the camera homography. Gives (x, y, axis, len, wid) per blob; the NPU
// class is attached later by matching boxes (cls stays -1 here).
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "config.hpp"
#include "tracker.hpp"

namespace des {

struct LocateTiming {
    double gray_us = 0, thresh_us = 0, contour_us = 0;
    int threshold = 0;
};

class Locator {
public:
    explicit Locator(const LocateConfig &cfg) : cfg_(cfg) {}

    // bgra: 4 bytes per pixel, stride in bytes.
    std::vector<Detection> run(const uint8_t *bgra, int width, int height, int stride, const Homography &h,
                               LocateTiming *timing = nullptr);

    // Save the last gray / binary image with the found rectangles drawn (PNG), for tuning.
    bool dump(const char *path) const;
    // Corners (x0,y0..x3,y3) of each blob of the last run in full-resolution image px, same order as run()'s result.
    std::vector<std::array<float, 8>> rects_full() const;

private:
    LocateConfig cfg_;
    std::vector<uint8_t> gray_, bin_;
    int gw_ = 0, gh_ = 0;
    std::vector<std::vector<float>> rects_px_;   // 4 corners (x0,y0..x3,y3) per blob, small-image px
};

}  // namespace des
