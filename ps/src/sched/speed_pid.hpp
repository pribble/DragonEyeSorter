// speed_pid.hpp — optional PS-side PI trim for the belt cruise speed (encoder feedback, des_io ENC_SPEED).
//
// The stepper stays open-loop position: this only nudges the commanded cruise speed so the measuring wheel holds
// the operator's set speed. Default off; nothing changes unless des_app.json belt.pid.enabled is true. The loop is
// deliberately conservative: it acts only while cruising (RUN + BeltPlan::Run + belt moving), clamps the trim and
// freezes while the belt is still ramping up after a start, so stop-at-the-pick-line timing is not disturbed.
#pragma once
#include <cstdint>
namespace des {
struct SpeedPidConfig {
    bool enabled = false;
    double kp = 0.5;          // mm/s trim per mm/s speed error
    double ki = 0.05;         // per second
    int period_ms = 100;      // control period
    double trim_mm_s = 3.0;   // output clamp (also the anti-windup limit)
    double deadband_mm_s = 5.0;   // encoder / belt ripple at 80 mm/s is ~4.9 mm/s per 10 ms count; do not chase it
};
class SpeedPid {
public:
    explicit SpeedPid(const SpeedPidConfig &c = SpeedPidConfig()) : cfg_(c), on_(c.enabled) {}
    void configure(const SpeedPidConfig &c)
    {
        cfg_ = c;
        on_ = c.enabled;
        reset();
    }
    void set_enabled(bool on)
    {
        on_ = on;
        if (!on) reset();
    }
    bool enabled() const { return on_; }
    void reset();
    // target/actual in mm/s, now monotonic seconds. Returns the trim to add to the commanded cruise speed.
    double update(double target_mm_s, double actual_mm_s, double now_s);
    double last_trim() const { return trim_; }
    uint64_t updates() const { return n_; }
private:
    SpeedPidConfig cfg_;
    bool on_ = false;
    double integral_ = 0, trim_ = 0, last_t_ = -1;
    uint64_t n_ = 0;
};
}  // namespace des
