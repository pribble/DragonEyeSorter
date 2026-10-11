// measure.hpp — sub-pixel geometric measurement of gears and racks (10_项目文档/齿轮齿条_测量功能与数据.md §2, §4).
//
// Pipeline per part: ROI threshold (Otsu) -> outer contour (+ bore hole) -> sub-pixel edge along each contour normal
// (where the interpolated gray crosses the threshold) -> image px -> table mm (camera homography) -> parallax
// correction (top face magnified by k = H / (H - h): points on the side away from the optical axis are top-face
// edges and are scaled back about the axis; points facing the axis are bottom edges and stay) -> fits.
//
// Parallax limit (walls visible): the bright silhouette is the sweep of the outline from scale 1 (bottom) to k (top)
// about the axis, i.e. every edge is smeared by up to smear = (k - 1) * |center - axis| along the direction away from
// the axis. Where that runs along the tooth row it fills the gaps (H 360 / h 10 / 112 mm off-axis: 3.2 mm ~ one
// pitch). OD / bore are still corrected; tooth / gap judgments are only made while smear <= max_smear_mm (root gap
// of m = 1 is ~1 mm), otherwise teeth_judged = false -> measure in a frame nearer the axis, or route to 复检.
// Walls dark (only the top face is bright): the image is the top face scaled by k, everything scales back, no smear.
//   gear: circle fit -> r(theta) -> tooth count (spec mix-up) -> tip circle (OD) / root circle fits -> bore circle,
//         eccentricity = |bore center - tip-circle center|; per tooth: short / missing; per gap: filled (flash)
//   rack: main axis, length, tooth positions on the toothed edge -> mean pitch by linear regression (~pi for m = 1)
#pragma once

#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "geom.hpp"
#include "measure_config.hpp"

namespace des {

struct GearMeasure {
    bool ok = false;
    std::string err;
    float od = 0, root_d = 0, bore_d = 0, ecc = 0;   // mm
    int teeth = 0;           // spec tooth count from the pitch (28 vs 30 = spec mix-up), not the teeth present
    int short_teeth = 0, missing_teeth = 0, flash_gaps = 0;
    bool teeth_judged = true;   // false: parallax smear too large, the three counts above are not valid
    float smear_mm = 0;         // parallax smear at this position (walls visible)
    Vec2 center;                                      // tip-circle center, table mm
    float fit_rms = 0;                                // tip-circle fit residual
    std::vector<float> r_theta;                       // 720 bins (0.5 deg), outer radius, mm (UI curve)
};

struct RackMeasure {
    bool ok = false;
    std::string err;
    float length = 0, height = 0, pitch = 0;          // mm
    int teeth = 0;              // tooth slots from the first to the last tooth (present + missing)
    int short_teeth = 0, missing_teeth = 0, flash_gaps = 0;
    bool teeth_judged = true;
    float smear_mm = 0;
    float axis = 0;                                   // direction of the length, rad (table frame)
};

// gray: full-resolution 8-bit image; roi: image px around one part (with a margin); h: px -> table mm
GearMeasure measure_gear(const cv::Mat &gray, cv::Rect roi, const Homography &h, const MeasureConfig &cfg);
RackMeasure measure_rack(const cv::Mat &gray, cv::Rect roi, const Homography &h, const MeasureConfig &cfg);

}  // namespace des
