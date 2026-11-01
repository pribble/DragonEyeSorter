// speed_pid.cpp — see speed_pid.hpp
#include "speed_pid.hpp"
#include <algorithm>
#include <cmath>
namespace des {
void SpeedPid::reset()
{
    integral_ = 0;
    trim_ = 0;
    last_t_ = -1;
    n_ = 0;
}
double SpeedPid::update(double target_mm_s, double actual_mm_s, double now_s)
{
    if (!on_ || target_mm_s <= 0 || cfg_.period_ms <= 0) return 0;
    if (last_t_ < 0) {   // first sample: establish the period
        last_t_ = now_s;
        return 0;
    }
    const double period = cfg_.period_ms / 1000.0;
    const double elapsed = now_s - last_t_;
    if (elapsed + 1e-9 < period) return trim_;
    last_t_ = now_s;
    if (elapsed > 1.0) {   // the worker stalled: the old integral is meaningless
        integral_ = 0;
        trim_ = 0;
        return 0;
    }
    const double err = target_mm_s - actual_mm_s;
    if (std::fabs(err) <= cfg_.deadband_mm_s) {   // hold, then bleed the correction out so it cannot park off-target
        trim_ *= 0.5;
        integral_ *= 0.5;
        if (std::fabs(trim_) < 0.01) trim_ = 0;
        n_++;
        return trim_;
    }
    integral_ += err * elapsed;
    if (cfg_.ki > 1e-9) {
        const double i_max = cfg_.trim_mm_s / cfg_.ki;   // anti-windup: the integral alone cannot exceed the clamp
        integral_ = std::clamp(integral_, -i_max, i_max);
    } else {
        integral_ = 0;   // P-only
    }
    double u = cfg_.kp * err + cfg_.ki * integral_;
    u = std::clamp(u, -cfg_.trim_mm_s, cfg_.trim_mm_s);
    trim_ = u;
    n_++;
    return trim_;
}
}  // namespace des
