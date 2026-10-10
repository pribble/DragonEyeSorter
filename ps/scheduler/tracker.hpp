// tracker.hpp — conveyor tracking table (docs/04 "跟踪表").
//
// Every part on the belt gets one Track. Positions are kept in belt coordinates (s = x - B, see geom.hpp),
// so a stopped / reversed / accelerating belt needs no special handling: x_now = s + B_now.
// Each camera frame (with the belt travel B latched for that frame) feeds update(); detections are
// associated to tracks by belt coordinate, fused over frames (position, grasp axis, size, class votes),
// and a disposition (pick / flap / recheck) is decided once the part crosses decide_x.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "geom.hpp"

namespace des {

struct Detection {
    Vec2 p;                 // table mm, center
    float len = 0, wid = 0; // mm, long / short side
    float axis = 0;         // rad, direction of the long side (wrap_axis)
    int cls = -1;           // model class id, -1 = blob only (no NPU result)
    float conf = 0;
    bool partial = false;   // touches the image border: presence only, geometry unreliable
    bool merged = false;    // blob larger than any single part: several touching parts
    float img_v = 0.5f;     // image row of the center, 0 = top .. 1 = bottom (rolling-shutter timing)
};

struct FrameObs {
    uint32_t frame = 0;     // PL frame count of the captured frame
    double belt_mm = 0;     // belt travel B at the capture instant
    std::vector<Detection> dets;
};

enum class TrackState : uint8_t { New, Tracking, Decided, Picking, Done };
enum class Disposition : uint8_t { None, Pick, Flap, Recheck };

// What a class means for sorting. Built by the app from models/label_list.json (single source).
struct ClassRule {
    std::string label;
    Disposition disp = Disposition::Recheck;
    int dest = 0;           // Pick: bin index; Flap: outlet index
};

struct TrackerConfig {
    float fov_x0 = 0, fov_x1 = 300;   // table x range seen by the camera (mm)
    float edge_margin = 10;           // tracks this close to the FOV edge are not expected to be seen
    float decide_x = 280;             // decide the disposition when x_now passes this
    float tail_x = 600;               // end of the belt; tracks are dropped past it
    float gate_s = 12, gate_y = 12;   // association gates (mm)
    int min_hits = 3;                 // New -> Tracking
    int max_miss_new = 2;             // New tracks missing this many frames in view are noise
    int max_miss = 8;                 // Tracking tracks missing this long in view are lost
    float min_conf = 0.5f;            // mean class confidence needed to trust the vote
    bool pick_unclassified = false;   // bring-up without NPU: pick blobs that have no class
    int unclassified_bin = 0;
    std::vector<ClassRule> rules;     // index = class id
};

struct Track {
    int id = 0;
    TrackState state = TrackState::New;
    Disposition disp = Disposition::None;
    int dest = 0;
    std::string why;        // short reason for the disposition (UI / log)

    float s = 0, y = 0;     // fused belt coordinate / lateral position (mm)
    float axis = 0;         // fused long-side direction (rad)
    float len = 0, wid = 0;
    int n_geom = 0;         // detections that contributed geometry
    double c2 = 0, s2 = 0;  // doubled-angle accumulators for axis
    int hits = 0, misses = 0;
    bool merged = false;
    std::vector<float> score;   // summed confidence per class
    int cls = -1;
    float cls_conf = 0;     // mean confidence of the voted class
    uint32_t first_frame = 0, last_frame = 0;

    float x_at(double belt_mm) const { return float(s + belt_mm); }
    ORect footprint(double belt_mm, float grow = 0) const
    {
        return {{x_at(belt_mm), y}, len / 2 + grow, wid / 2 + grow, axis};
    }
};

class Tracker {
public:
    explicit Tracker(TrackerConfig cfg) : cfg_(std::move(cfg)) {}

    void update(const FrameObs &obs);
    // Drop finished tracks and tracks past the belt end. Call once per cycle with the current belt travel.
    void retire(double belt_mm);

    std::vector<Track> &tracks() { return tracks_; }
    const std::vector<Track> &tracks() const { return tracks_; }
    Track *find(int id);
    const TrackerConfig &config() const { return cfg_; }

    // statistics for the UI / test report
    struct Stats {
        uint64_t frames = 0, dets = 0, created = 0, noise = 0, lost = 0;
        uint64_t decided_pick = 0, decided_flap = 0, decided_recheck = 0;
    } stats;

private:
    void fuse(Track &t, const Detection &d, double belt_mm, uint32_t frame);
    void decide(Track &t);
    // the whole footprint (extent along x from len / wid / axis) lies inside the FOV minus the edge margin
    bool fully_in_view(const Track &t, double belt_mm) const
    {
        float x = t.x_at(belt_mm);
        float ext = 0.5f * (t.len * std::fabs(std::cos(t.axis)) + t.wid * std::fabs(std::sin(t.axis)));
        return x - ext > cfg_.fov_x0 + cfg_.edge_margin && x + ext < cfg_.fov_x1 - cfg_.edge_margin;
    }

    TrackerConfig cfg_;
    std::vector<Track> tracks_;
    int next_id_ = 1;
};

const char *to_str(TrackState s);
const char *to_str(Disposition d);

}  // namespace des
