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
#include <mutex>
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
    // manual jog while the line is stopped (HMI): +1 = forward (parts toward the arm), -1 = reverse, at the cruise
    // speed; 0 = ramp down. A change of direction ramps down first and starts on a later call. Call every cycle.
    void jog(int dir);
    // accumulated belt travel [mm]: the PL POS is a 32-bit step counter that wraps (2^31 steps ≈ 44 km ≈ 10 days at
    // 50 mm/s), so the PS chains wrap-safe deltas into an int64 step count. Anchored at the PL counter zero (a
    // restart without an FPGA reconfiguration keeps the raw POS): the same origin as the camera side's frame-latch
    // chains in live.cpp — tracker and picker must stay in one frame.
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
    int jog_dir_ = 0;           // direction of the running jog move, 0 = none
    // belt_mm accumulator: the cycle thread and the arm thread's intercept wait both read it
    mutable std::mutex pos_m_;
    mutable bool pos_init_ = false;
    mutable int32_t last_pos_ = 0;
    mutable int64_t steps_ = 0;
};
// Picker::Input for one control cycle. belt != nullptr = production (run): brake distance from the PL's
// des_step_brake_steps. belt == nullptr = dry mode (live without Runner, `live --belt`): brake distance from
// v^2 / (2 a) and no speed (that mode only displays picker plans). Both formulas are kept on purpose.
Picker::Input picker_input(const BeltDriver *belt, des_hw_t *hw, const BeltConfig &cfg);
}  // namespace des
