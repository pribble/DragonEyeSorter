// belt_ctl.hpp — applies the picker's BeltPlan to the belt stepper (docs/运动控制与分拣.md "传送带什么时候停").
//
// The stepper always runs in PL position mode (stepper_ctrl):
//   Run     target far ahead (re-extended before it is reached) -> the belt keeps cruising
//   StopAt  rewrite TARGET while moving; PL recomputes the deceleration point and stops on that exact step.
//           From standstill, a move to the target is started. Never reverses to reach a target behind.
//   Hold    arm on the belt: if (unexpectedly) moving, ramp down; otherwise do nothing
// Belt travel B [mm] = POS * mm_per_step * dir (same as the camera / tracker side).
#pragma once

#include <cstdint>

#include "config.hpp"
#include "des_hw.h"
#include "picker.hpp"

namespace des {

class BeltDriver {
public:
    BeltDriver(des_hw_t *hw, const BeltConfig &cfg);

    void enable(bool on);
    void apply(const BeltPlan &plan);   // once per control cycle
    void set_speed(double mm_s);        // cruise speed used from the next move / retarget (CAN / UI)

    double belt_mm() const;
    double speed_mm_s() const;
    double brake_mm() const;            // distance needed to stop from the current speed
    bool stopped() const;
    int32_t steps_for(double belt_mm) const;

    struct Stats {
        uint64_t runs = 0;        // Run plans that (re)started or re-extended a move
        uint64_t stops = 0;       // StopAt targets written
        uint64_t late = 0;        // StopAt targets already behind the belt (PL ramps down past them / no reverse)
        uint64_t holds = 0;       // Hold while moving (should stay 0)
    } stats;

private:
    void move_to(int32_t target);
    des_hw_t *hw_;
    BeltConfig cfg_;
    int sign_;                  // +1: forward travel = increasing POS
    double sps_, acc_;          // cruise steps/s, steps/s^2
    BeltPlan::Kind last_kind_ = BeltPlan::Hold;
    int32_t last_target_ = 0;
};

}  // namespace des
