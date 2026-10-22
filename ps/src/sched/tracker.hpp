// tracker.hpp — conveyor tracking table (docs/运动控制与分拣.md "跟踪表"): one Track per part, kept in belt
// coordinates (s = x - B, see geom.hpp) so a stopped / reversed / accelerating belt needs no special handling.
// update() associates each frame's detections by belt coordinate, fuses geometry / class votes and decides the
// disposition once the part crosses decide_x.
#pragma once
#include <cstdint>
#include <utility>
#include <vector>
#include "part_types.hpp"
namespace des {
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
// One part on the belt. Hot struct: every per-track buffer is fixed size (a track never allocates).
struct Track {
    int id = 0;
    TrackState state = TrackState::New;
    Disposition disp = Disposition::None;
    int dest = 0;
    int8_t gate = -2;       // picker cache: PickerConfig::gates index (-2 = unresolved, -1 = belt end)
    char why[40] = "";      // disposition reason (UI / log): set_why() copies at most 39 chars
    float s = 0, y = 0;     // fused belt coordinate / lateral position (mm)
    float axis = 0;         // fused long-side direction (rad)
    float len = 0, wid = 0;
    float ext_x = 0;        // half extent along x (len / wid / axis): cached by fuse for fully_in_view()
    int n_geom = 0;         // detections that contributed geometry
    float c2 = 0, s2 = 0;   // doubled-angle accumulators for axis
    int hits = 0, misses = 0;
    bool merged = false;
    static constexpr int kMaxScore = 16;   // class ids >= this are not scored (model has 4)
    float score[kMaxScore] = {};           // summed confidence per class
    uint8_t n_score = 0;                   // score[0 .. n_score) may be non-zero
    int votes = 0;                         // classifications summed into score
    int cls = -1;
    float cls_conf = 0;                    // mean confidence of the voted class
    uint32_t first_frame = 0, last_frame = 0;
    PartMeasure meas;       // filled by the app (measure/track_measure) while the part crosses the camera

    void set_why(const char *w)   // bounded copy: no overflow, the tail is dropped
    {
        size_t i = 0;
        for (; w[i] && i + 1 < sizeof why; i++) why[i] = w[i];
        why[i] = 0;
    }
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
    // Soft vote of a crop classifier: probs[c] added to score[c], only while the track is New / Tracking. False when
    // the track is gone / decided, or when n > kMaxScore (fixed score array: the extra classes are dropped).
    bool vote(int track_id, const float *probs, int n);
    void retire(double belt_mm);   // drop finished tracks and tracks past the belt end (once per cycle)
    std::vector<Track> &tracks() { return tracks_; }
    const std::vector<Track> &tracks() const { return tracks_; }
    Track *find(int id);
    const Track *find(int id) const;
    const TrackerConfig &config() const { return cfg_; }
    TrackerConfig &config() { return cfg_; }   // operator changes at run time (HMI): rules, size rule, min_conf
    uint32_t rev() const { return rev_; }   // bumps on track changes: consumers cache on it (Picker::plan_grip)
    struct Stats {
        uint64_t frames = 0, dets = 0, created = 0, noise = 0, lost = 0;
        uint64_t decided_pick = 0, decided_flap = 0, decided_recheck = 0;
    } stats;
private:
    void fuse(Track &t, const Detection &d, double belt_mm, uint32_t frame);
    void decide(Track &t);
    void apply_size(Track &t) const;
    bool fully_in_view(const Track &t, double belt_mm) const   // whole footprint inside the FOV minus the margin
    {
        float x = t.x_at(belt_mm);
        return x - t.ext_x > cfg_.fov_x0 + cfg_.edge_margin && x + t.ext_x < cfg_.fov_x1 - cfg_.edge_margin;
    }
    TrackerConfig cfg_;
    std::vector<Track> tracks_;
    int next_id_ = 1;
    struct Pair { float cost; uint16_t t, d; };   // association candidate (reused scratch: no per-frame allocation)
    std::vector<Pair> pairs_;
    std::vector<uint8_t> t_used_, d_used_;
    uint32_t rev_ = 1;
};
}  // namespace des
