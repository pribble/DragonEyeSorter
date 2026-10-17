// roi_crop.hpp — the one part crop used everywhere: NPU classifier input, NG / recheck images in the part log, and
// the training set collected by `des_tool collect`. Training and inference MUST go through this same function.
//
// A square of side_mm x side_mm on the belt plane, centered on the part and rotated so the part's long axis runs
// along the crop's x axis, resampled to out_px x out_px (default 56 mm -> 224 px = 0.25 mm/px, about the camera's own
// 0.21 mm/px at H 300 mm with the 4.2 mm lens, so 1 mm defects keep ~4 px). The axis has a 180 deg ambiguity (rack
// teeth up or down) -> the training set is augmented with flips.
#pragma once

#include <cstdint>

#include <opencv2/core.hpp>

#include "geom.hpp"
#include "measure_config.hpp"

namespace des {

struct CropSpec {
    float side_mm = 56;
    int out_px = 224;
};

// src: BGRA (camera buffer, may be uncached: only the bounding box is read, once) or BGR; returns BGR out_px x out_px
cv::Mat crop_part(const cv::Mat &src, const Homography &px_to_table, Vec2 center, float axis, const CropSpec &spec);

// Colour of the part in a crop (BGR, part centred, belt dark): pixels inside the centre disc (radius 0.42 of the side)
// brighter than the Otsu level of V; hue = saturation-weighted circular mean (deg), sat / val = means (0..255).
struct PartColor {
    float hue = -1, sat = -1, val = -1;
    int pixels = 0;
};
PartColor part_color(const cv::Mat &crop_bgr);
// first class in the list that matches, -1 if none (or no part pixels)
int classify_color(const std::vector<ColorClass> &classes, const PartColor &c);

}  // namespace des
