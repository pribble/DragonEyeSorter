// flap_manual.cpp — see flap_manual.hpp.
#include "flap_manual.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace des {

FlapManual::FlapManual(des_hw_t *hw) : hw_(hw)
{
    v_.hw = hw_ && hw_->version >= DES_VERSION_PWM;
}
void FlapManual::say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(v_.msg, sizeof v_.msg, fmt, ap);
    va_end(ap);
}
void FlapManual::read_hw()
{
    if (!v_.hw) return;
    const uint32_t c = des_rd(hw_, DES_PWM_CTRL);
    v_.on[0] = c & 1u;
    v_.on[1] = c & 2u;
    v_.us[0] = int(des_rd(hw_, DES_PWM_CH0) & 0xFFFu);
    v_.us[1] = int(des_rd(hw_, DES_PWM_CH1) & 0xFFFu);
    for (int ch = 0; ch < 2; ch++) {
        v_.target[ch] = v_.us[ch];
        pos_[ch] = v_.us[ch];
        sweep_[ch] = 0;
    }
}
void FlapManual::write(int ch, int us)
{
    des_pwm_set(hw_, ch, us);   // the PL applies it at the next 20 ms period start
    v_.us[ch] = us;
}
void FlapManual::context(bool shown, bool allowed, const char *why)
{
    if (shown && !v_.shown) read_hw();   // entering the page: show what the PWM block holds now
    if (v_.unlocked && (!shown || !allowed)) {
        v_.unlocked = false;
        for (int ch = 0; ch < 2; ch++) {   // a move in progress stops where it is
            v_.target[ch] = v_.us[ch];
            sweep_[ch] = 0;
        }
        if (!shown) say("离开页面，已上锁");
        else say("已上锁：%s", why ? why : "");
    }
    v_.shown = shown;
    v_.allowed = allowed;
    v_.why_not = why ? why : "";
}
void FlapManual::request(const FlapRequest &r)
{
    if (!v_.shown) return;
    if (!v_.hw) { say("比特流里没有挡板 PWM 模块（v7 起才有）"); return; }
    if (r.kind == FlapRequest::Unlock || r.kind == FlapRequest::Lock) {
        if (r.kind == FlapRequest::Unlock && !v_.allowed) return;
        v_.unlocked = r.kind == FlapRequest::Unlock;
        if (v_.unlocked) {
            read_hw();
            say("已解锁：动作前确认挡板周围没有手和零件");
        } else {
            say("已上锁（输出保持现状）");
        }
        return;
    }
    if (r.kind == FlapRequest::Speed) {
        v_.speed = std::clamp(r.value, 0, 2);
        return;
    }
    if (!active() || !v_.allowed) return;
    const int ch = r.ch & 1;
    const bool idle = v_.us[ch] == v_.target[ch];   // not moving: a new move starts from the width now
    switch (r.kind) {
    case FlapRequest::Output: {
        const uint32_t c = des_rd(hw_, DES_PWM_CTRL) & 3u;
        if (r.value) {
            int w = v_.us[ch];
            if (w < kFlapMinUs || w > kFlapMaxUs) w = kFlapCenterUs;   // never set since the board started: centre
            write(ch, w);
            v_.target[ch] = w;
            pos_[ch] = w;
            des_pwm_enable(hw_, c | (1u << ch));
            v_.on[ch] = true;
            say("挡板 %d 输出已打开：%d µs（舵机会直接转到这里）", ch + 1, w);
        } else {
            des_pwm_enable(hw_, c & ~(1u << ch));
            v_.on[ch] = false;
            v_.target[ch] = v_.us[ch];
            sweep_[ch] = 0;
            say("挡板 %d 输出已关闭：舵机没信号、不出力", ch + 1);
        }
        break;
    }
    case FlapRequest::Step:
    case FlapRequest::Center:
    case FlapRequest::Sweep:
        if (!v_.on[ch]) { say("先点「开」打开挡板 %d 的输出", ch + 1); break; }
        if (idle) pos_[ch] = v_.us[ch];
        if (r.kind == FlapRequest::Step) {
            sweep_[ch] = 0;
            v_.target[ch] = std::clamp(v_.target[ch] + r.value, kFlapMinUs, kFlapMaxUs);
        } else if (r.kind == FlapRequest::Center) {
            sweep_[ch] = 0;
            v_.target[ch] = kFlapCenterUs;
        } else {
            sweep_base_[ch] = v_.us[ch];
            v_.target[ch] = std::min(v_.us[ch] + kFlapSweepUs, kFlapMaxUs);
            sweep_[ch] = 2;
            say("挡板 %d 试转：往脉宽加大方向约 %.0f°，再回来", ch + 1, kFlapSweepUs * kFlapDegPerUs);
        }
        break;
    default:
        break;
    }
}
void FlapManual::tick(double now)
{
    const double dt = last_ < 0 ? 0 : std::min(now - last_, 0.2);
    last_ = now;
    if (!v_.hw || !v_.shown) return;
    if (!active()) {   // not ours: keep showing what the PWM block holds (runner / another tool may change it)
        if (last_read_ < 0 || now - last_read_ > 0.5) { read_hw(); last_read_ = now; }
        return;
    }
    for (int ch = 0; ch < 2; ch++) {
        if (!v_.on[ch]) continue;
        if (v_.us[ch] == v_.target[ch]) {
            if (sweep_[ch] == 2) { sweep_[ch] = 1; v_.target[ch] = sweep_base_[ch]; }
            else if (sweep_[ch] == 1) { sweep_[ch] = 0; say("挡板 %d 试转完成", ch + 1); }
            continue;
        }
        const int rate = kFlapSpeedUs[v_.speed];
        if (rate == 0) { write(ch, v_.target[ch]); pos_[ch] = v_.target[ch]; continue; }
        const double step = rate * dt, d = v_.target[ch] - pos_[ch];
        pos_[ch] = std::fabs(d) <= step ? double(v_.target[ch]) : pos_[ch] + (d > 0 ? step : -step);
        const int next = int(std::lround(pos_[ch]));
        // write in steps of at least kFlapMinStepUs (the servo ignores changes inside its 5 us dead band)
        if (next == v_.target[ch] || std::abs(next - v_.us[ch]) >= kFlapMinStepUs) write(ch, next);
    }
}

}  // namespace des
