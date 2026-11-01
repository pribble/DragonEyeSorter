// test_belt_pid: the optional PS-side belt-speed PI (sched/speed_pid.hpp) without hardware.
#include <cmath>
#include <cstdio>
#include "speed_pid.hpp"
#include "test_check.hpp"
using namespace des;

static void test_disabled()
{
    SpeedPidConfig c;
    c.enabled = false;
    SpeedPid p(c);
    CHECK(p.update(50, 40, 0.0) == 0, "disabled: no trim");
    CHECK(p.update(50, 40, 0.2) == 0, "disabled: stays zero");
}

static void test_switch()
{
    SpeedPidConfig c;
    c.enabled = false;   // config default off, like production
    c.kp = 0.5;
    c.ki = 0;
    c.period_ms = 100;
    c.trim_mm_s = 3.0;
    SpeedPid p(c);
    p.set_enabled(true);   // HMI toggle can turn it on at run time
    p.update(50, 40, 0.0);
    CHECK(p.update(50, 40, 0.1) > 0, "runtime switch on: trims");
    p.set_enabled(false);  // and off again
    CHECK(p.update(50, 40, 0.2) == 0, "runtime switch off: no trim");
}

static void test_p_only_and_clamp()
{
    SpeedPidConfig c;
    c.enabled = true;
    c.kp = 0.5;
    c.ki = 0;
    c.period_ms = 100;
    c.trim_mm_s = 3.0;
    c.deadband_mm_s = 0.0;
    SpeedPid p(c);
    CHECK(p.update(50, 40, 0.0) == 0, "first sample only starts the period");
    CHECK(std::fabs(p.update(50, 40, 0.05) - 0) < 1e-9, "within period: hold");
    const double u = p.update(50, 40, 0.10);
    CHECK(std::fabs(u - 3.0) < 1e-9, "10 mm/s error x 0.5 clamps to 3 (got %.3f)", u);
}

static void test_tracks_a_first_order_plant()
{
    SpeedPidConfig c;
    c.enabled = true;
    c.kp = 0.5;
    c.ki = 0.10;
    c.period_ms = 100;
    c.trim_mm_s = 3.0;
    c.deadband_mm_s = 0.2;
    SpeedPid p(c);
    const double target = 50.0;
    double actual = 40.0, cmd = target;
    double t = 0;
    for (int i = 0; i < 400; i++) {   // 40 s
        const double trim = p.update(target, actual, t);
        cmd = target + trim;
        actual += (cmd - actual) * 0.1;   // 100 ms first-order plant
        t += 0.1;
    }
    CHECK(std::fabs(actual - target) < 0.5, "PI brings actual to target (actual %.2f)", actual);
    CHECK(std::fabs(p.last_trim()) <= 3.0 + 1e-9, "trim stays clamped");
    CHECK(p.updates() > 300, "the loop ran");
}

static void test_deadband()
{
    SpeedPidConfig c;
    c.enabled = true;
    c.kp = 0.5;
    c.ki = 0.1;
    c.period_ms = 100;
    c.deadband_mm_s = 0.5;
    SpeedPid p(c);
    p.update(50, 50.1, 0.0);
    const double u = p.update(50, 50.1, 0.1);
    CHECK(std::fabs(u) < 1e-9, "inside deadband: no trim (got %.4f)", u);
}

static void test_reset()
{
    SpeedPidConfig c;
    c.enabled = true;
    c.kp = 0.5;
    c.ki = 0.1;
    c.period_ms = 100;
    SpeedPid p(c);
    p.update(50, 40, 0.0);
    p.update(50, 40, 0.1);
    CHECK(p.last_trim() > 0, "trim before reset");
    p.reset();
    CHECK(p.last_trim() == 0 && p.updates() == 0, "reset clears the state");
}

int main()
{
    test_disabled();
    test_switch();
    test_p_only_and_clamp();
    test_tracks_a_first_order_plant();
    test_deadband();
    test_reset();
    return test_summary("test_belt_pid");
}
