// fit.hpp — internals shared by the measurement (measure_gear.cpp): pixel sampling, sub-pixel edge scan, robust
// statistics, contour search and the per-thread scratch. Not a public interface — measure.hpp is.
#pragma once
#include <cstddef>
#include <utility>
#include <vector>
#include <opencv2/core.hpp>
#include "geom.hpp"
#include "measure_config.hpp"
namespace des {
namespace meas {
// Edge: one sub-pixel point, table mm (parallax-corrected); n = unit normal to the dark side; far = bore edge off the
struct Edge { Vec2 p; Vec2 n; bool far = false; };
struct Run { int a, b; float peak; };
struct Scratch {
    std::vector<std::vector<cv::Point>> cs; std::vector<cv::Vec4i> hier; cv::Mat bin;   // find_part
    std::vector<cv::Point> outer, hole; std::vector<Edge> oe, he;                       // contours / edges
    std::vector<Vec2> pts, tip_pts, root_pts, hp, near;                                 // gear
    std::vector<float> prof, rad, widths, tips, centers, spacing, vmins, ph, single;
    std::vector<int> bin_of, tooth_of, gap_of; std::vector<Run> runs, kept;
    std::vector<std::pair<int, int>> gaps;
};
Scratch &scratch();                                    // the caller's thread-local buffers
float bilinear(const cv::Mat &g, float x, float y);    // -1 outside the image
void edges(const cv::Mat &g, const std::vector<cv::Point> &c, cv::Point off, float thr, const Homography &h,
           const MeasureConfig &cfg, float part_h, bool hole, std::vector<Edge> &out);   // out is cleared first
float smear_at(const MeasureConfig &cfg, float part_h, Vec2 c);
bool fit_circle(const std::vector<Vec2> &pts, Vec2 &c, float &r, float *rms = nullptr);   // geom.hpp, pointer form
float median(const std::vector<float> &v);             // pure: `v` is not reordered (scratch copy inside)
float percentile(const std::vector<float> &v, float q);
void runs_above(const std::vector<float> &prof, float mid, bool circular, std::vector<Run> &out);
// runs off the pitch grid are dropped; those reaching min_peak count as flash (a fin / blob standing in a gap), lower
// ones are noise (2026-10-28: a burr / speck on a 20-tooth flank crossed the mid level by 0.07 mm in one 0.5-degree
// bin -> "flash 1" on a good gear in 19 of 20 frames)
void filter_phase(const std::vector<float> &ph, const std::vector<Run> &runs, std::vector<Run> &kept, int &flash,
                  float min_peak = -1e9f);
float filled_valleys(const std::vector<float> &prof, const std::vector<Run> &runs, bool circular, int n_bins,
                     float flash_mm, std::vector<float> &vmins, std::vector<std::pair<int, int>> &gaps, int &flash);
bool find_part(const cv::Mat &gray, cv::Rect roi, std::vector<cv::Point> &outer, std::vector<cv::Point> &hole,
               float &thr, Scratch &s);                // largest outer contour + largest hole, one histogram pass
void fill_empty_bins(std::vector<float> &prof);        // O(NB), the original ascending-pass result
int otsu8(const int hist[256], size_t n);              // the value cv::THRESH_OTSU would pick for that histogram
bool debug_on();                                       // DES_MEASURE_DEBUG, read once
}  // namespace meas
}  // namespace des
