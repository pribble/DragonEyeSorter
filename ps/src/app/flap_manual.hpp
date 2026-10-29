// flap_manual.hpp — the 挡板 page of the HDMI interface: drive the two side-diverter servos (des_pwm, J12 PIN18 /
// PIN20, MG996R) by hand, to check that a servo works and to find its open / closed pulse widths (2026-10-30, user).
//
// It lives in the worker thread and has no thread of its own: Hmi::handle turns clicks into requests, tick() moves the
// pulse widths every cycle. A hobby servo has no speed setting, so "slow" means stepping the width; each step is at
// least kFlapMinStepUs, above the servo's 5 us dead band (product page) — 2 us steps every 20 ms only made it twitch.
// While the page is shown and unlocked the Runner leaves the gates alone (Runner::set_flap_manual); outputs stay as
// they are when the page is locked or left.
#pragma once

#include <array>
#include <cstdint>

#include "des_hw.h"

namespace des {

struct FlapRequest {
    enum Kind { Unlock, Lock, Output, Step, Center, Sweep, Speed } kind = Lock;
    int ch = 0;      // 0 = PIN18 (挡板 1), 1 = PIN20 (挡板 2)
    int value = 0;   // Output: 1 on / 0 off; Step: microseconds (+ / -); Speed: index into kFlapSpeedUs
};

inline constexpr int kFlapSpeedUs[3] = {100, 300, 0};   // us per second; 0 = jump at once
inline constexpr int kFlapMinStepUs = 10;                // never a smaller step than this (servo dead band 5 us)
inline constexpr int kFlapCenterUs = 1500, kFlapMinUs = 500, kFlapMaxUs = 2500;
inline constexpr int kFlapSweepUs = 150;                 // 试转: about 13 deg out and back
inline constexpr double kFlapDegPerUs = 0.09;            // MG996R: 0.5 .. 2.5 ms = 0 .. 180 deg (product page)

struct FlapManualView {
    bool shown = false, allowed = false, unlocked = false;
    bool hw = false;                     // bitstream has des_pwm
    std::array<bool, 2> on{};            // output enabled (read back from the PWM block)
    std::array<int, 2> us{}, target{};   // width now / where a move goes
    int speed = 0;                       // index into kFlapSpeedUs
    const char *why_not = "";            // why the page is not available (static string)
    char msg[160] = "";
};

class FlapManual {
public:
    explicit FlapManual(des_hw_t *hw);
    void context(bool shown, bool allowed, const char *why);   // every cycle, like ArmManual::context
    void request(const FlapRequest &r);
    void tick(double now);                                     // moves the widths, refreshes the read-back
    const FlapManualView &view() const { return v_; }
    bool active() const { return v_.shown && v_.unlocked; }   // the Runner must not write the gates now

private:
    void read_hw();
    void write(int ch, int us);
    void say(const char *fmt, ...);
    des_hw_t *hw_;
    FlapManualView v_;
    double last_ = -1, last_read_ = -1;
    std::array<double, 2> pos_{};    // fractional width during a slow move
    std::array<int, 2> sweep_{};     // 试转 phase: 2 = going out, 1 = coming back, 0 = none
    std::array<int, 2> sweep_base_{};
};

}  // namespace des
