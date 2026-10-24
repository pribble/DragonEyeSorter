// beam.hpp — through-beam light barrier across the belt (des_io IR1, CEP PIN9; docs/运动控制与分拣.md「对射光电」).
//
// The PL latches the stepper position and the encoder count at every "beam blocked" edge (20 us debounce, v11+). The
// tracker keeps each part in belt coordinates (s = x - B), so the part's leading edge at that edge was
//     lead = s + B_edge + R            (R = half the part's extent along x: OD / 2 for a measured gear)
// and the beam sits at beam.x_mm (des_tool calib-beam, measured with the part standing still). Their difference is
// the tracking error of the moving line: camera timing + rolling shutter + belt model + vision position, against an
// independent sensor sampled on the same PL clock (赛题硬性① 多通道同步采集 / 报告「控制精度」).
//
// BeamMonitor pairs edges with tracks:
//   Matched  edge whose predicted leading edge is within match_mm of the beam (nearest track wins, one edge per track)
//   Repeat   edge while a matched part still covers the beam (tooth tips chattering at the trailing edge)
//   NoTrack  edge nobody claimed within expire_mm of belt travel: something crossed the beam that vision did not
//            track (漏检 candidate)
//   Missed   track whose leading edge went miss_mm past the beam without an edge (beam misaligned, part lifted off,
//            or a vision ghost)
// Pure logic (no hardware): the app feeds edges from des_io_read and calls update() once per processed frame.
#pragma once
#include <cstdint>
#include <deque>
#include <vector>
#include "tracker.hpp"
namespace des {
struct BeamConfig {
    bool enabled = false;      // des_app.json "beam"
    bool calibrated = false;   // calib.json "beam" (des_tool calib-beam) set x_mm
    float x_mm = 0;            // table x of the beam
    float match_mm = 12;       // |predicted leading edge - beam| accepted as this part's edge
    float expire_mm = 120;     // belt travel after which an unclaimed edge counts as NoTrack
    float miss_mm = 25;        // a waiting track this far past the beam without an edge counts as Missed
    float fov_x0 = 0;          // camera view start: a beam upstream of it sees every part before the camera does
    bool active() const { return enabled && calibrated; }
};
struct BeamEdge {
    double belt_mm = 0;        // belt travel B at the edge (IR1_POS * mm_per_step * dir)
    int32_t enc = 0;           // encoder count latched with it
};
enum class BeamKind : uint8_t { Matched, Repeat, NoTrack, Missed, Lost };
const char *to_str(BeamKind k);
struct BeamResult {            // one line of the beam log
    BeamKind kind = BeamKind::Matched;
    double belt_mm = 0;        // edge (Matched / Repeat / NoTrack) or now (Missed / Lost)
    int32_t enc = 0;
    int track = -1;
    float err = 0;             // Matched: provisional error with the track's s at match time (the record has the final one)
    uint32_t n = 1;            // Lost: edges the PL counted between two reads that were never seen
};
// half extent of the part along x: measured OD / 2 for a gear, else the fused footprint
float beam_part_r(const Track &t);
// leading edge minus beam for a track at belt travel b
inline float beam_lead_err(const Track &t, double b, float beam_x) { return float(t.s + b) + beam_part_r(t) - beam_x; }
// final error of a matched track (its s has kept averaging after the match); NaN-free: 0 when not matched
inline float beam_track_err(const Track &t, float beam_x) { return t.beam == 2 ? beam_lead_err(t, t.beam_b, beam_x) : 0.f; }
class BeamMonitor {
public:
    explicit BeamMonitor(BeamConfig cfg) : cfg_(cfg) {}
    const BeamConfig &config() const { return cfg_; }
    // IR1_CNT / IR1_POS / IR1_ENC read this cycle; the PL keeps only the last edge, so a jump > 1 loses edges
    void poll(uint32_t ir1_cnt, double edge_belt_mm, int32_t edge_enc, std::vector<BeamResult> &out);
    void edge(const BeamEdge &e) { if (pending_.size() < 64) pending_.push_back(e); else stats.overflow++; }
    // once per frame after Tracker::update (tracks in any state): settles edges, marks tracks
    void update(std::vector<Track> &tracks, double belt_mm, std::vector<BeamResult> &out);
    // a matched track's record was written: its final error goes into the running statistics
    void finalize(const Track &t);
    struct Stats {
        uint64_t edges = 0, matched = 0, repeats = 0, no_track = 0, missed = 0, lost = 0, overflow = 0;
        uint64_t n_err = 0;
        double err_sum = 0, err_sq = 0, err_min = 1e9, err_max = -1e9;
        double mean() const { return n_err ? err_sum / n_err : 0; }
        double sd() const;
    } stats;
private:
    BeamConfig cfg_;
    std::deque<BeamEdge> pending_;
    bool have_cnt_ = false;
    uint32_t last_cnt_ = 0;
};
}  // namespace des
