// picker.hpp — pick scheduling, belt stop decisions and tail-flap assignment (docs/04).
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
    float neighbor_gap = 2;      // keep-out grown around other parts
    float tail_x = 600;          // belt end, where the flap acts
    float flap_lead = 0;         // fire the flap this far before tail_x (mechanical latency)
    float flap_conflict = 30;    // flap parts with different outlets closer than this both go to recheck
    int recheck_outlet = 0;      // outlet index for Disposition::Recheck
};

struct PickJob {
    int track_id = 0;
    Vec2 p;                      // table mm (x == pick line once stopped)
    float close_axis = 0;        // direction the fingers close along (rad, table frame)
    float open_mm = 0;           // opening before descending
    float part_wid = 0;          // expected width between fingers when closed (grasp check)
    int bin = 0;
    int cls = -1;
};

struct FlapEvent {
    int track_id = 0;
    int outlet = 0;
    double belt_mm = 0;          // the part reaches tail_x - flap_lead when the belt travel equals this
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

    const PickerConfig &config() const { return cfg_; }

    struct Stats {
        uint64_t jobs = 0, picked = 0, failed = 0, crowded = 0, missed = 0, out_of_reach = 0, too_wide = 0;
        uint64_t flap_conflicts = 0;
    } stats;

private:
    void to_recheck(Track &t, const char *why);
    PickerConfig cfg_;
    int committed_id_ = 0;       // target the belt has already been told to stop for
};

}  // namespace des
