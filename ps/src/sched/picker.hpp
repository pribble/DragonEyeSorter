// picker.hpp — pick scheduling, belt stop decisions, side diverter gates and belt speed adaptation
// (docs/运动控制与分拣.md).
//
// The belt only stops for OK parts the arm will pick: for the most downstream pickable part it is told to stop at
// B = pick_x - s (the stepper decelerates on its own, stepper_ctrl), so the part halts exactly on the pick line.
// Everything else rides to the belt end where the flap sorts it.
//
// FlapConfig / FlapSequencer — side diverter gates (docs "NG 分流", geometry in gate.hpp): up to two swing paddles on
// the far belt edge, upstream or downstream of the pick line. Closed a paddle lies across the part lane slanting
// upstream and the belt pushes whatever reaches it along the paddle into the bin beside the belt; open it is turned
// away. Parts no gate takes ride on to the belt end = recheck box (fail-safe: an unplanned part, a lost track or a
// dead gate never ends up in scrap). The picker emits one event per NG / recheck part with its gate (-1 = belt end)
// and the belt travel at which the gate must start closing / may start opening; the sequencer turns them into
// des_pwm pulse widths, ramped (soft start / stop). A part is only diverted when: the paddle reaches its lateral
// position, the gate can be fully closed before the part enters the swept sector, and no other part (OK parts on
// their way to the pick line included) is in the sector while the paddle moves or reaches the closed paddle.
//
// SpeedAdaptConfig / adapt_speed — belt speed adaptation (赛题领域 3 高级「流水线速度自适应调节」): the belt stops for
// every OK part; when the arm is still busy the belt instead slows down so the next part reaches the pick line about
// when the arm is ready again (fewer full stops, same or better throughput; the exact stop stays the picker's StopAt).
#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include "gate.hpp"
#include "tracker.hpp"
namespace des {
struct PickerConfig {
    float pick_x = 450;          // table x of the pick line (mm)
    float pick_tol = 3;          // accepted |x - pick_x| once stopped
    float reach_y0 = -80, reach_y1 = 80;   // lateral band the arm reaches at the pick line
    float brake_margin = 5;      // added to the braking distance when deciding whether a stop is still possible
    float finger_w = 12;         // finger size across the closing direction (mm)
    float finger_t = 6;          // finger thickness along the closing direction (mm)
    float grip_clear = 4;        // opening = part width + 2 * grip_clear
    float max_open = 60;         // gripper maximum opening (product page: 60 mm)
    float round_ratio = 1.3f;    // len / wid below this: treat as round (gear), any closing angle
    bool tangential_first = false;   // round parts: try the axis tangential to the arm base first (what a tilted
    Vec2 arm_base{0, 0};             // grasp needs; set from arm.base_xy / max_tilt_deg in load_config), table mm
    float neighbor_gap = 2;      // keep-out grown around other parts
    float tail_x = 600;          // belt end: parts no gate takes fall into the recheck box there
    int recheck_outlet = 0;      // outlet index for Disposition::Recheck
    struct Gate {
        GateGeom geom;
        int outlet = 1;
        float hold_mm = 0;       // belt travel the gate stays closed after the part meets it (0 = gate_slide_mm)
    };
    std::vector<Gate> gates;     // empty = no gates: every NG / recheck part rides to the belt end
    float gate_move_s = 0.46f;   // command -> paddle fully closed / open (ramp + stagger + one cycle + PWM period)
    float gate_margin = 10;      // clearance (mm) in every gate timing / conflict check
    // Intercept ("flying") picks: a round OK part within the arm's vertical reach is met by a gripper already down on
    // the line, fingers wide open across the belt; the part rides in and the arm closes on it (PickJob::arrive_belt_mm).
    // The belt does not stop. Parts that do not qualify, or come too late / while the arm is busy, are stopped as before.
    bool flying = false;
    float flying_r0 = 50, flying_r1 = 150;   // pick point distance from the arm base axis (vertical reach) ⚠
    float flying_axis = kPi / 2;             // closing direction, table frame: across the belt
    float flying_ready_s = 1.6f;             // ready pose -> fingers down on the line (dispatch this far ahead)
    float flying_window = 40;                // dispatch while the part is need .. need + window mm away
    float flying_gap = 60;                   // no other part within this distance along the belt
    float flying_close_lead_s = 0.12f;       // close this long before the centre arrives (command -> contact)
};
struct PickJob {
    int track_id = 0;
    Vec2 p;                      // table mm (x == pick line once stopped)
    float close_axis = 0;        // direction the fingers close along (rad, table frame)
    float open_mm = 0;           // opening before descending
    float part_wid = 0;          // expected width between fingers when closed (grasp check)
    int bin = 0;
    int cls = -1;
    bool flying = false;         // intercept: belt keeps running, close when the belt travel reaches arrive_belt_mm
    double arrive_belt_mm = 0;   // (minus speed x flying_close_lead_s)
};
struct FlapEvent {
    int track_id = 0;
    int outlet = 0;
    double belt_mm = 0;          // the part meets its gate's closed paddle (or reaches tail_x) at this belt travel
    int gate = -1;               // index into PickerConfig::gates, -1 = belt end (recheck box)
    double close_b = 0;          // gate: start closing at this belt travel (closed before the part enters the sweep)
    double release_b = 0;        // gate: may start opening at this belt travel (part pushed off; or earlier when
                                 // another part would otherwise reach the closed paddle)
};
struct BeltPlan {
    enum Kind {
        Run,      // nothing to pick ahead: keep running
        StopAt,   // stop when the belt travel reaches stop_belt_mm (target part on the pick line)
        Hold,     // arm is down on the belt: do not move
    } kind = Run;
    double stop_belt_mm = 0;
    int track_id = 0;
};
class Picker {
public:
    explicit Picker(PickerConfig cfg) : cfg_(std::move(cfg)) {}
    struct Input {
        double belt_mm = 0;      // current belt travel B
        double brake_mm = 0;     // distance the belt needs to stop from its current speed
        bool belt_stopped = true;
        bool arm_idle = true;    // arm ready for a new job
        bool arm_clear = true;   // gripper is above the belt (lifted or away): belt may move
        double speed_mm_s = 0;   // current belt speed (intercept dispatch distance)
        double cruise_mm_s = 0;  // set running speed (gate timing plans with the larger of the two: a belt stopped
                                 // for a pick speeds up again)
    };
    struct Output {
        BeltPlan belt;
        bool have_job = false;
        PickJob job;
        std::vector<FlapEvent> flaps;   // pending, ordered by belt_mm
    };
    Output tick(Tracker &tr, const Input &in);
    void job_done(Tracker &tr, int track_id, bool ok);   // arm finished; failed picks fall through to recheck
    // Gripper closing direction and opening for track t, checked against every other part on the belt.
    // Returns nullptr when a collision-free grip exists, else the reason ("too wide", "crowded").
    const char *plan_grip(const Tracker &tr, const Track &t, double belt_mm, float &close_axis, float &open_mm) const;
    // Intercept possible for t (round, within flying_r0..r1 of the arm base at the pick line, fingers wide open across
    // the belt clear of every other part, nothing within flying_gap along the belt)? open_mm = the opening to use.
    bool flying_ok(const Tracker &tr, const Track &t, double belt_mm, float &open_mm) const;
    const PickerConfig &config() const { return cfg_; }
    struct Stats {
        uint64_t jobs = 0, picked = 0, failed = 0, crowded = 0, missed = 0, out_of_reach = 0, too_wide = 0;
        uint64_t flap_conflicts = 0, flying = 0;
        // gate: -> recheck (out of reach), -> recheck (decided too late), cycles an early opening could not keep a
        // later part away (it will meet the paddle)
        uint64_t gate_unreachable = 0, gate_late = 0, gate_squeezed = 0;
    } stats;
private:
    void to_recheck(Track &t, const char *why);
    bool fingers_clear(const Tracker &tr, const Track &t, double belt_mm, float axis, float open_mm) const;
    int gate_for(int outlet) const;
    // gate event for t (closing / release travel); false = the gate cannot serve it (why set; strict only)
    bool gate_event(const Track &t, int gate, double move_mm, FlapEvent &e, const char *&why, bool strict = true) const;
    bool committed(int track_id) const;   // its gate may already be moving: never re-routed
    // belt travel range over which o's disc overlaps gate g's swept sector (o stops at the pick line if it is picked);
    // false = never
    bool sweep_window(const Track &o, int g, double &b0, double &b1) const;
    PickerConfig cfg_;
    int committed_id_ = 0;       // target the belt has already been told to stop for
    std::vector<Track *> flap_tracks_;   // track of each pending FlapEvent (same order before the final sort)
    std::vector<int> committed_;         // tracks whose gate event passed its close_b
    // plan_grip is a pure function of the track set + belt travel: remember the last answer for the next tick
    mutable const void *grip_tr_ = nullptr;
    mutable int grip_id_ = -1;
    mutable uint32_t grip_rev_ = 0;
    mutable double grip_belt_ = 0;
    mutable const char *grip_why_ = nullptr;
    mutable float grip_axis_ = 0, grip_open_ = 0;
};
struct FlapConfig {
    bool enabled = false;          // ⚠ on once the gates are mounted and their pulse widths measured
    struct Gate {
        std::string outlet_name = "scrap";
        int outlet = 1;            // index into AppConfig::outlets (resolved from outlet_name in load_config)
        GateGeom geom;             // pivot_xy / len_mm / closed_deg / open_deg in des_app.json
        float hold_mm = 0;         // closed this much belt travel after the part meets it (0 = estimate, gate.hpp)
        int open_us = 1000;        // paddle turned away
        int closed_us = 1900;      // paddle across the lane
    };
    std::vector<Gate> gates;       // des_pwm channel = index; at most 2
    float move_ms = 300;           // pulse ramp open <-> closed (smoothstep: soft start and stop, no bounce)
    float stagger_ms = 120;        // a gate does not start moving within this long after the other one started
    float margin_mm = 10;          // clearance in the timing / conflict checks (PickerConfig::gate_margin)
    // command -> paddle in place, for the picker's timing: ramp + stagger + one cycle + one PWM period
    float move_s() const { return (move_ms + stagger_ms) / 1000.f + 0.04f; }
};
class FlapSequencer {
public:
    explicit FlapSequencer(FlapConfig cfg) : cfg_(std::move(cfg)) {}
    struct State {
        int us[2] = {0, 0};           // pulse width per gate now (0 = no such gate)
        int track_id[2] = {-1, -1};   // part a gate is closed for (-1 = open)
        bool moving[2] = {false, false};
    };
    // events: the picker's pending flap events (empty when the line is not running: closed gates finish their
    // hold, then open). The first call puts every gate at open_us at once (its position is unknown: no ramp).
    const State &update(const std::vector<FlapEvent> &events, double belt_mm, double now_s);
    const State &state() const { return st_; }
    uint64_t served = 0;           // parts a gate was closed for
private:
    FlapConfig cfg_;
    State st_;
    bool started_ = false;
    double release_[2] = {-1e18, -1e18};   // belt travel until which the gate stays closed
    struct Ramp { int from = 0, to = 0; double t0 = -1e18; } ramp_[2];
    double last_start_ = -1e18;            // time the last ramp of either gate started
    int us_at(int g, double now) const;
};
struct SpeedAdaptConfig {
    bool enabled = false;
    double min_mm_s = 15;        // never slower than this while adapting (stops still come from the picker)
    double change_frac = 0.05;   // apply only changes larger than this fraction of the base speed (no hunting)
};
// base: operator / PLC speed (the maximum); dist_mm: next pickable part to the pick line (< 0 = none ahead);
// arm_ready_s: seconds until the arm is back at its ready pose (0 = idle). Returns the cruise speed to use.
inline double adapt_speed(const SpeedAdaptConfig &c, double base, double dist_mm, double arm_ready_s)
{
    if (!c.enabled || dist_mm <= 0 || arm_ready_s <= 0) return base;
    const double v = dist_mm / arm_ready_s;   // arrive when the arm is ready
    return std::max(std::min(c.min_mm_s, base), std::min(base, v));
}
}  // namespace des
