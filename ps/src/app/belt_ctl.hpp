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
// 32-bit PL POS -> accumulated belt travel [mm]. Wrap-safe (the unsigned difference of a monotonic counter), mm_per_step
// signed with the direction. Anchored at the PL counter zero: a restart without an FPGA reconfiguration keeps the raw
// POS, so any other origin would leave the tracker and the picker in frames that differ by a constant — every chain in
// one process (BeltDriver::belt_mm, live.cpp's frame-latch and display-side chains) shares this type for that reason.
struct PosAccum {
    explicit PosAccum(double mm_per_step_signed) : mmps_(mm_per_step_signed) {}
    double feed(int32_t raw)   // advance the chain to a freshly latched / read position
    {
        if (!init_) { init_ = true; raw_ = raw; steps_ = raw; }   // anchored at the PL counter zero
        else steps_ += int32_t(uint32_t(raw) - uint32_t(raw_));   // wrap-safe: no UB on the unsigned difference
        raw_ = raw;
        return double(steps_) * mmps_;
    }
    double peek(int32_t raw) const { return double(steps_ + int32_t(uint32_t(raw) - uint32_t(raw_))) * mmps_; }
    double mm() const { return double(steps_) * mmps_; }
private:
    double mmps_;
    int64_t steps_ = 0;
    int32_t raw_ = 0;
    bool init_ = false;
};
class BeltDriver {
public:
    BeltDriver(des_hw_t *hw, const BeltConfig &cfg);
    void enable(bool on);
    // driver released after standing still (BeltConfig::release_idle): no holding current, a quiet servo bus
    bool released() const { return released_; }
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
        uint64_t releases = 0;    // driver released after standing still (release_idle)
    } stats;
private:
    void move_to(int32_t target);
    // before a start from standstill: a released driver is enabled first and the start waits enable_settle_ms
    // (returns false: try again on a later cycle)
    bool ready_to_start();
    void idle_release(bool busy);   // busy = moving or about to start
    des_hw_t *hw_;
    BeltConfig cfg_;
    int sign_;                  // +1: forward travel = increasing POS
    double sps_, acc_;          // cruise steps/s, steps/s^2
    BeltPlan::Kind last_kind_ = BeltPlan::Hold;
    int32_t last_target_ = 0;
    int jog_dir_ = 0;           // direction of the running jog move, 0 = none
    bool released_ = false;     // ENA off after standing still
    double enabled_at_ = 0;     // mono_s() of the last enable
    double still_since_ = -1;   // mono_s() since the belt stands still, -1 = moving / just enabled
    // belt_mm accumulator (PosAccum): the cycle thread and the arm thread's intercept wait both read it
    mutable std::mutex pos_m_;
    mutable PosAccum pos_;
};
// Picker::Input for one control cycle. belt != nullptr = production (run): brake distance from the PL's
// des_step_brake_steps. belt == nullptr = dry mode (live without Runner, `live --belt`): brake distance from
// v^2 / (2 a) and no speed (that mode only displays picker plans). Both formulas are kept on purpose.
Picker::Input picker_input(const BeltDriver *belt, des_hw_t *hw, const BeltConfig &cfg);
}  // namespace des
