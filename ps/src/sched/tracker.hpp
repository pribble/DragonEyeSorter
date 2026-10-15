// tracker.hpp — conveyor tracking table (docs/运动控制与分拣.md "跟踪表").
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

// Geometric measurement of one part (measure/measure, docs/视觉与检测.md "几何测量"), the best frame so far = the one nearest the
// optical axis (smallest parallax smear). Plain numbers, so this library stays free of OpenCV.
struct PartMeasure {
    bool valid = false;
    char kind = 0;              // 'g' gear, 'r' rack
    bool teeth_judged = false;  // false: parallax smear too large, the defect counts are not valid
    float axis_dist = 1e9f;     // mm from the optical axis when measured (selects the best frame)
    float smear = 0;            // mm, parallax smear of that frame
    uint32_t frame = 0;
    int count = 0;              // how many times this part was measured
    float size = 0;             // gear: OD; rack: length (mm)
    float bore = 0, ecc = 0;    // gear
    float pitch = 0;            // rack: mean pitch
    int teeth = 0;              // gear: spec count from the pitch; rack: tooth slots
    int short_teeth = 0, missing_teeth = 0, flash_gaps = 0;
};

// Size / spec NG from the measurement (docs/视觉与检测.md 类别表: 尺寸类 NG 不进 NPU 类别, 由测量判 → 报废). Applied only to parts
// the class vote would pick (OK parts): it never upgrades an NPU NG.
struct SizeRule {
    bool enabled = false;           // ⚠ turn on once the camera height and calibration are real
    int gear_teeth = 30;            // spec (28-tooth parts = 规格混料)
    float gear_od = 32, gear_od_tol = 0.15f;   // recipe example in 齿轮齿条_测量功能与数据.md: 32.00±0.15, 50.0±0.5
    float gear_bore = 6, gear_bore_tol = 0.3f;
    float gear_max_ecc = 0.3f;
    float rack_len = 50, rack_len_tol = 0.5f;
    int ng_outlet = 1;              // flap outlet for size / spec NG (index into outlets, 1 = scrap)
    bool defect_recheck = true;     // class says OK but the measurement found tooth defects -> recheck
    bool require = false;           // no measurement by decide_x -> recheck
};

// Product recipe (配方): the part spec the size rule checks against. Changing the recipe = changeover of the line to
// another spec, e.g. recipe "28-tooth gears": the 30-tooth parts then fail the tooth count (规格混料) and go to scrap.
// Loaded from recipes.json (config.cpp); selected by the PLC (CAN RECIPE) or the operator (HMI), never while running.
struct Recipe {
    int id = 1;                     // 1..255 (CAN RECIPE carries one byte)
    std::string name;
    int gear_teeth = 30;
    float gear_od = 32, gear_od_tol = 0.15f;
    float gear_bore = 6, gear_bore_tol = 0.3f;
    float gear_max_ecc = 0.3f;
    float rack_len = 50, rack_len_tol = 0.5f;

    void apply(SizeRule &r) const   // spec fields only: enabled / outlet / recheck policy stay as configured
    {
        r.gear_teeth = gear_teeth;
        r.gear_od = gear_od;
        r.gear_od_tol = gear_od_tol;
        r.gear_bore = gear_bore;
        r.gear_bore_tol = gear_bore_tol;
        r.gear_max_ecc = gear_max_ecc;
        r.rack_len = rack_len;
        r.rack_len_tol = rack_len_tol;
    }
    static Recipe from_rule(const SizeRule &r, int id, const std::string &name)
    {
        Recipe c;
        c.id = id;
        c.name = name;
        c.gear_teeth = r.gear_teeth;
        c.gear_od = r.gear_od;
        c.gear_od_tol = r.gear_od_tol;
        c.gear_bore = r.gear_bore;
        c.gear_bore_tol = r.gear_bore_tol;
        c.gear_max_ecc = r.gear_max_ecc;
        c.rack_len = r.rack_len;
        c.rack_len_tol = r.rack_len_tol;
        return c;
    }
};

inline const Recipe *find_recipe(const std::vector<Recipe> &list, int id)
{
    for (const Recipe &r : list)
        if (r.id == id) return &r;
    return nullptr;
}

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
    SizeRule size;
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
    int votes = 0;              // classifications summed into score (per-frame detector classes or crop votes)
    int cls = -1;
    float cls_conf = 0;     // mean confidence of the voted class
    uint32_t first_frame = 0, last_frame = 0;
    PartMeasure meas;       // filled by the app (measure/track_measure) while the part crosses the camera

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
    // Soft vote of a crop classifier (NPU on the part crop) for one track: probs[c] added to score[c]. Only while the
    // track is New / Tracking (a decided part keeps its decision). Returns false if the track is gone or decided.
    bool vote(int track_id, const std::vector<float> &probs);
    // Drop finished tracks and tracks past the belt end. Call once per cycle with the current belt travel.
    void retire(double belt_mm);

    std::vector<Track> &tracks() { return tracks_; }
    const std::vector<Track> &tracks() const { return tracks_; }
    Track *find(int id);
    const TrackerConfig &config() const { return cfg_; }
    TrackerConfig &config() { return cfg_; }   // operator changes at run time (HMI): rules, size rule, min_conf

    // statistics for the UI / test report
    struct Stats {
        uint64_t frames = 0, dets = 0, created = 0, noise = 0, lost = 0;
        uint64_t decided_pick = 0, decided_flap = 0, decided_recheck = 0;
    } stats;

private:
    void fuse(Track &t, const Detection &d, double belt_mm, uint32_t frame);
    void decide(Track &t);
    void apply_size(Track &t) const;
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
