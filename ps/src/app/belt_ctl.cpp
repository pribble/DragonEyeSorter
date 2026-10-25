// belt_ctl.cpp — see belt_ctl.hpp
#include "belt_ctl.hpp"
#include <cmath>
#include "util.hpp"
namespace des {
static constexpr int32_t kFar = 10000000;       // steps ahead for "run" (~207 m at 0.0207 mm/step)
static constexpr int32_t kReextend = kFar / 2;  // re-extend the far target when this close
BeltDriver::BeltDriver(des_hw_t *hw, const BeltConfig &cfg)
    : hw_(hw), cfg_(cfg), pos_(cfg.mm_per_step * (cfg.dir >= 0 ? 1 : -1))
{
    sign_ = cfg.dir >= 0 ? 1 : -1;
    sps_ = cfg.run_speed / cfg.mm_per_step;
    acc_ = cfg.accel / cfg.mm_per_step;
}
void BeltDriver::enable(bool on)
{
    des_step_enable(hw_, on);
    released_ = !on;
    if (on) enabled_at_ = mono_s();
    still_since_ = -1;
}
bool BeltDriver::ready_to_start()
{
    if (released_) {
        enable(true);   // TB6600: coils energised again before the first pulse
        return false;
    }
    return (mono_s() - enabled_at_) * 1000.0 >= cfg_.enable_settle_ms;
}
void BeltDriver::idle_release(bool busy)
{
    if (!cfg_.release_idle || released_) return;
    if (busy) {
        still_since_ = -1;
        return;
    }
    const double now = mono_s();
    if (still_since_ < 0) still_since_ = now;
    else if ((now - still_since_) * 1000.0 >= cfg_.release_after_ms) {
        des_step_enable(hw_, 0);   // POS keeps counting from where it is: no pulses while released
        released_ = true;
        stats.releases++;
    }
}
void BeltDriver::set_speed(double mm_s)
{
    if (mm_s <= 0) return;
    sps_ = mm_s / cfg_.mm_per_step;
    if (!stopped()) des_step_set_vmax(hw_, sps_);   // takes effect now; PL ramps to the new cruise speed
}
int32_t BeltDriver::steps_for(double belt_mm) const
{
    return int32_t(std::lround(belt_mm / (cfg_.mm_per_step * sign_)));
}
double BeltDriver::belt_mm() const
{
    const int32_t pos = des_step_pos(hw_);
    std::lock_guard<std::mutex> lk(pos_m_);
    return pos_.feed(pos);   // the accumulator is anchored at the PL counter zero (PosAccum)
}
double BeltDriver::speed_mm_s() const
{
    return stopped() ? 0.0 : des_step_speed(hw_) * cfg_.mm_per_step;   // CUR_INC keeps VMIN after a stop
}
bool BeltDriver::stopped() const { return !(des_step_status(hw_) & DES_ST_STATUS_MOVING); }
double BeltDriver::brake_mm() const
{
    if (stopped()) return 0;
    int32_t b = des_step_brake_steps(hw_);
    if (b >= 0) return b * cfg_.mm_per_step;
    double v = speed_mm_s();
    return v * v / (2 * cfg_.accel);
}
void BeltDriver::move_to(int32_t target)
{
    des_step_move(hw_, target, sps_, acc_);   // START only acts from standstill
    last_target_ = target;
}
void BeltDriver::jog(int dir)
{
    const int32_t pos = des_step_pos(hw_);
    const bool moving = !stopped();
    if (dir == 0 || (moving && dir != jog_dir_)) {   // stop, or still running the other way: ramp down first
        if (moving) { des_step_stop(hw_); } jog_dir_ = 0;
    } else if (!moving) {
        if (!ready_to_start()) return;
        move_to(pos + dir * sign_ * kFar); jog_dir_ = dir;
    } else if (int64_t(last_target_ - pos) * dir * sign_ < kReextend) {
        last_target_ = pos + dir * sign_ * kFar; des_step_set_target(hw_, last_target_);
    }
    last_kind_ = BeltPlan::Hold;   // the next Run plan (re)targets from scratch
}
void BeltDriver::apply(const BeltPlan &plan)
{
    jog_dir_ = 0;
    const int32_t pos = des_step_pos(hw_);
    const bool moving = !stopped();
    bool start_wanted = false;   // stopped but this plan moves the belt: the driver must stay enabled
    switch (plan.kind) {
    case BeltPlan::Run: {
        int32_t remaining = (last_target_ - pos) * sign_; if (!moving) {
            start_wanted = true;
            if (!ready_to_start()) break;
            move_to(pos + sign_ * kFar); stats.runs++;
        } else if (last_kind_ != BeltPlan::Run || remaining < kReextend) {
            last_target_ = pos + sign_ * kFar;   // a stop that is no longer wanted, or the far target getting close
            des_step_set_target(hw_, last_target_); stats.runs++;
        }
        break;
    }
    case BeltPlan::StopAt: {
        // the plan lives in the accumulated frame (belt_mm()) which outlives the PL's 32-bit POS: express the target
        // relative to the raw position the PL knows
        const int32_t t = pos + steps_for(plan.stop_belt_mm - belt_mm());
        const bool behind = int64_t(t - pos) * sign_ < 0;
        if (moving) {
            if (last_kind_ != BeltPlan::StopAt || t != last_target_) {
                // Writing TARGET == POS mid-move latches MOVING forever on PL v8 and older;
                // skip the write — next frame pos is past the stop point and the late path
                // below stops it (the earliest physical stop from here either way).
                if (t != pos) {
                    if (behind) stats.late++;      // PL ramps down and stops past it
                    des_step_set_target(hw_, t); last_target_ = t;
                    stats.stops++;  }
            }
        } else if (t != pos) {
            if (behind) {
                if (last_kind_ != BeltPlan::StopAt || t != last_target_) stats.late++;   // never reverse
                last_target_ = t;  } else {
                start_wanted = true;
                if (!ready_to_start()) break;
                move_to(t); stats.stops++;
            }
        }
        break;
    }
    case BeltPlan::Hold:
        if (moving) {
            des_step_stop(hw_); stats.holds++;
        }
        break;
    }
    idle_release(moving || start_wanted);
    last_kind_ = plan.kind;
}
Picker::Input picker_input(const BeltDriver *belt, des_hw_t *hw, const BeltConfig &cfg)
{
    Picker::Input in;
    if (belt) {   // production: the PL knows the braking distance of the running position move
        in.belt_mm = belt->belt_mm(); in.brake_mm = belt->brake_mm();
        in.belt_stopped = belt->stopped(); in.speed_mm_s = belt->speed_mm_s();
        return in;
    }
    const double mmps = cfg.mm_per_step * cfg.dir;
    in.belt_mm = des_step_pos(hw) * mmps;
    const double v = des_step_speed(hw) * cfg.mm_per_step;
    in.brake_mm = v * v / (2 * cfg.accel);
    in.belt_stopped = !(des_step_status(hw) & DES_ST_STATUS_MOVING);
    return in;
}
}  // namespace des
