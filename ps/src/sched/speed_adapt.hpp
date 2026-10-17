// speed_adapt.hpp — belt speed adaptation (赛题领域 3 高级「流水线速度自适应调节」).
//
// The belt stops for every OK part on the pick line. When the arm is still busy with the previous part, the next one
// would arrive early and the belt would brake to a stop and wait. Instead the belt slows down so that the next part
// reaches the pick line about when the arm is ready again: fewer full stops, the same or better throughput, and the
// stop on the line stays exact (the picker's StopAt is unchanged). Pure function; the Runner applies the result.
#pragma once

#include <algorithm>

namespace des {

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
