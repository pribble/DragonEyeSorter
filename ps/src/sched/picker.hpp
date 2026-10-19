// picker.hpp — pick scheduling, belt stop decisions and tail-flap assignment (docs/运动控制与分拣.md).
//
// The belt only stops for OK parts the arm will pick. For the most downstream pickable part the belt is
// told to stop at B = pick_x - s (stepper position mode decelerates on its own, see stepper_ctrl), so the
// part halts exactly on the pick line. Everything else rides to the belt end where the flap sorts it.
#pragma once

#include <vector>

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
    // side diverter gates (flap.hpp): contact point = table x where a part's centre meets the closed paddle
    struct Gate {
        float x = 0;
        int outlet = 1;
    };
    std::vector<Gate> gates;     // empty = no gates: every NG / recheck part rides to the belt end
    float gate_prepare = 110;    // a gate closes this far before the part reaches its contact point ...
    float gate_clear = 190;      // ... and reopens this far after it
    float gate_gap = 40;         // another part meeting the gate in [b - prepare - gap, b + clear]: conflict
    // Intercept ("flying") picks: a round OK part whose pick point lies within the arm's vertical reach is met by a
    // gripper already down on the pick line, fingers wide open across the belt; the part rides in between and the
    // arm thread closes them when its centre arrives (PickJob::arrive_belt_mm). The belt does not stop. Parts that
    // do not qualify, or come too late / while the arm is busy, are stopped and picked as before.
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
    double belt_mm = 0;          // the part's centre reaches its gate's contact point (or tail_x) at this belt travel
    int gate = -1;               // index into PickerConfig::gates, -1 = belt end (recheck box)
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
    explicit Picker(PickerConfig cfg) : cfg_(cfg) {}

    struct Input {
        double belt_mm = 0;      // current belt travel B
        double brake_mm = 0;     // distance the belt needs to stop from its current speed
        bool belt_stopped = true;
        bool arm_idle = true;    // arm ready for a new job
        bool arm_clear = true;   // gripper is above the belt (lifted or away): belt may move
        double speed_mm_s = 0;   // current belt speed (intercept dispatch distance)
    };
    struct Output {
        BeltPlan belt;
        bool have_job = false;
        PickJob job;
        std::vector<FlapEvent> flaps;   // pending, ordered by belt_mm
    };

    Output tick(Tracker &tr, const Input &in);
    // Arm finished (ok = part confirmed in the gripper). Failed picks fall through to recheck.
    void job_done(Tracker &tr, int track_id, bool ok);

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
    } stats;

private:
    void to_recheck(Track &t, const char *why);
    bool fingers_clear(const Tracker &tr, const Track &t, double belt_mm, float axis, float open_mm) const;
    int gate_for(int outlet) const;
    PickerConfig cfg_;
    int committed_id_ = 0;       // target the belt has already been told to stop for
};

}  // namespace des
