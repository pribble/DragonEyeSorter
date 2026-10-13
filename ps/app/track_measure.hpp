// track_measure.hpp — geometric measurement (measure.hpp) of tracked parts from the live worker (docs/视觉与检测.md "几何测量").
//
// Each frame at most per_frame parts are measured. A part is (re)measured only when it is clearly nearer the optical
// axis than at its stored measurement, so every track ends up with about its best frame (smallest parallax smear,
// least lens distortion); unmeasured parts go first. Only parts whose whole footprint (plus margin) is inside the
// image are measured. Gear vs rack from the fused blob shape. The ROI is converted to gray straight from the
// camera buffer, so the caller must drop the results if that buffer was reused meanwhile (measure() / apply()).
#pragma once

#include <cstdint>
#include <vector>

#include <opencv2/core.hpp>

#include "measure_config.hpp"
#include "tracker.hpp"

namespace des {

struct MeasureResult {
    int id = 0;          // track id
    PartMeasure m;
    cv::Mat crop;        // BGR part crop of the same frame (roi_crop.hpp); also when the measurement failed
};

class TrackMeasurer {
public:
    TrackMeasurer(const TrackMeasureConfig &cfg, const Homography &px_to_table);

    // bgra: the frame (4 bytes / px); belt_mm: belt travel at mid-exposure (the tracker's frame position);
    // belt_span_mm: belt travel during the frame readout (rolling shutter).
    std::vector<MeasureResult> measure(const Tracker &tr, const uint8_t *bgra, int width, int height, int stride,
                                       double belt_mm, double belt_span_mm, uint32_t frame);
    // store results into the tracks (skips tracks that are gone or already decided)
    static void apply(Tracker &tr, const std::vector<MeasureResult> &res);

    const MeasureConfig &config() const { return cfg_.m; }
    double last_ms = 0;
    uint64_t measured = 0, failed = 0;

private:
    TrackMeasureConfig cfg_;
    Homography h_, hinv_;
};

}  // namespace des
