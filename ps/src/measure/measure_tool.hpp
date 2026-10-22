// measure_tool.hpp — measuring every part in one frame (des_tool measure): locate blobs -> ROI per blob -> gear / rack
// by shape -> measure_gear / measure_rack; printing, repeatability over frames of still parts, report image.
// Bring-up / accuracy work with real parts (docs/视觉与检测.md "几何测量"): compare with the caliper values, measure the
// repeatability, keep the raw frame (--out) to re-run offline (--image).
#pragma once
#include <string>
#include <vector>
#include <opencv2/core.hpp>
#include "config.hpp"
#include "measure.hpp"
#include "tracker.hpp"
namespace des {
struct PartResult {
    Vec2 p;  // locate center, table mm (matches parts across frames)
    char kind = 0;  // 'g' gear, 'r' rack
    GearMeasure g;
    RackMeasure r;
    std::vector<float> r_theta;  // gear: 720-bin outer-radius profile for the report inset (measure_gear out param)
    bool ok() const { return kind == 'g' ? g.ok : r.ok; }
};  // gray: the full frame; dets: Locator::run on the same frame (partial / merged blobs are skipped);
// belt_mm_per_row: rolling-shutter correction (0 for a still belt)
std::vector<PartResult> measure_parts(const cv::Mat &gray, const std::vector<Detection> &dets, const AppConfig &cfg, float belt_mm_per_row = 0);
std::string describe(const PartResult &r);  // fitted circles / rack outline + labels, and an r(theta) inset per gear (right edge)
void draw_parts(cv::Mat &bgr, const std::vector<PartResult> &res, const Homography &px_to_table);
// repeatability of still parts over frames: mean / sigma / range of every measured quantity per part
class RepeatStats {
public: void add(const std::vector<PartResult> &res);
    void print() const;
private: struct Acc {
        Vec2 p;
        char kind = 0;
        int fails = 0;
        std::vector<std::vector<double>> v;  // one sample list per quantity
    };
    std::vector<Acc> parts_;
};
}  // namespace des
