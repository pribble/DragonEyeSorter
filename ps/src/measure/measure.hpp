// measure.hpp — sub-pixel geometric measurement of gears (10_项目文档/齿轮齿条_测量功能与数据.md §2, §4;
// pipeline, parallax limit and measured numbers: docs/视觉与检测.md "几何测量").
//
// ROI Otsu threshold -> outer contour (+ bore hole) -> sub-pixel edge along each contour normal -> px -> table mm ->
// parallax correction (top face magnified by k = H / (H - h): edges away from the optical axis are top-face edges and
// scale back about the axis, edges facing it are bottom edges and stay; bright walls smear edges by up to
// (k - 1) * |center - axis|, which fills the tooth gaps -> tooth / gap judgments only while smear <= max_smear_mm).
// circle fit -> r(theta) -> tooth count (spec mix-up) -> tip (OD) / root fits -> bore circle, eccentricity.
// (The rack branch is gone: the product dropped racks 2026-10-18, des_app.json measures everything as a gear.)
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
    int teeth = 0;              // spec count from the pitch (28 vs 30 = spec mix-up), not the teeth present
    int short_teeth = 0, missing_teeth = 0, flash_gaps = 0;
    bool teeth_judged = true;   // false: parallax smear too large, the counts above are not valid
    float smear_mm = 0;         // parallax smear at this position (walls visible)
    Vec2 center, bore_center;   // tip-circle / bore circle center, table mm
    float fit_rms = 0;          // tip-circle fit residual
};
// gray: full-resolution 8-bit image; roi: image px around one part (with a margin); h: px -> table mm.
// r_theta: optional out, the 720-bin (0.5 deg) outer radius curve in mm for the des_tool inset; nullptr (live) skips it.
GearMeasure measure_gear(const cv::Mat &gray, cv::Rect roi, const Homography &h, const MeasureConfig &cfg,
                         std::vector<float> *r_theta = nullptr);
}  // namespace des
