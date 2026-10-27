// arm_io.cpp — see arm_io.hpp
#include "arm_io.hpp"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include "util.hpp"
namespace des {
namespace {
// The HMI reads ArmManualView::msg as a UTF-8 C string: a message that does not fit in msg[] is cut at a character
// boundary, never in the middle of a multi-byte sequence.
void copy_utf8(const char *s, char *out, size_t cap)
{
    size_t n = 0;
    while (n + 1 < cap && s[n]) {
        const uint8_t c = uint8_t(s[n]); const size_t len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
        bool whole = n + len <= cap - 1; for (size_t i = 1; whole && i < len; i++) whole = s[n + i] != 0;
        if (!whole) { break; } n += len;
    }
    memcpy(out, s, n);
    out[n] = 0;
}
void sleep_ms(double ms)
{
    if (ms <= 0) return;
    timespec ts{time_t(ms / 1000), long(std::fmod(ms, 1000.0) * 1e6)};
    nanosleep(&ts, nullptr);
}
// 2026-10-26: two real picks both reported "nothing picked" although the part was held. POM + the soft-tape
// fingertips compress, so the gripper stops within ~1.5 mm of the close command; the old 1.5 mm window sat exactly
// on that boundary. 0.8 mm still separates a real grasp from a fully closed, empty gripper (which stops about one
// part width away). Re-check with a true pick after changing fingertips / part material.
constexpr float kGraspCloseTolMm = 0.8f;
}  // namespace
// Every move goes out read-back checked: the belt's stepper driver corrupts a few % of the bus frames while it is
// energised and a SYNC WRITE has no reply, so a lost one would otherwise go unnoticed (2026-10-26).
int sync_write(des_hw_t *hw, const ArmConfig &cfg, const uint16_t *pos, const uint16_t *ms, int n)
{
    uint8_t ids[6];
    for (int j = 0; j < n && j < 6; j++) ids[j] = cfg.joint[j].id;
    return des_servo_sync_pos_checked(hw, ids, pos, ms, n, 0);
}
int sync_write_all(des_hw_t *hw, const ArmConfig &cfg, const std::array<uint16_t, 6> &pos, uint16_t ms)
{
    const uint16_t t[6] = {ms, ms, ms, ms, ms, ms};
    return sync_write(hw, cfg, pos.data(), t, 6);
}
int sync_write_one(des_hw_t *hw, const ArmConfig &cfg, int joint, uint16_t pos, uint16_t ms)
{
    const uint8_t id = cfg.joint[joint].id;
    return des_servo_sync_pos_checked(hw, &id, &pos, &ms, 1, 0);
}
bool read_counts(des_hw_t *hw, const ArmConfig &cfg, std::array<uint16_t, 6> &pos)
{
    bool all = true;
    for (int j = 0; j < 6; j++) {
        uint16_t v; if (des_servo_read_u16(hw, cfg.joint[j].id, 0x38, &v) == DES_OK) pos[j] = v;
        else all = false;
    }
    return all;
}
// ------------------------------------------------------------------ ArmExec
ServoInfo ArmExec::probe(int joint)
{
    ServoInfo s;
    des_sb_result_t r;
    if (des_servo_ping(hw_, id(joint), &r) != DES_OK) return s;
    s.alive = true;
    s.status = r.status;
    uint16_t v;
    if (des_servo_read_u16(hw_, id(joint), 0x38, &v) == DES_OK) s.pos = v;
    if (des_servo_read_u16(hw_, id(joint), 0x2E, &v) == DES_OK) s.raw_2e = v;
    uint8_t t;
    if (des_servo_read(hw_, id(joint), 0x3F, 1, &t, nullptr) == DES_OK) s.raw_3f = t;
    return s;
}
int ArmExec::torque(bool on)
{
    uint8_t ids[6];
    for (int j = 0; j < 6; j++) ids[j] = id(j);
    return des_servo_torque_checked(hw_, ids, 6, on);   // broadcast + 0x28 read-back from each, re-sent if needed
}
ArmExec::Result ArmExec::simulate(const std::vector<ServoFrame> &wp, const std::function<void()> &on_belt_clear,
                                  const WaitBelt &wait_belt)
{
    Result res;
    const uint64_t t0 = mono_us();
    for (const ServoFrame &f : wp) {
        if (f.wait_belt && wait_belt && !wait_belt(f.belt_mm)) {
            res.why = "intercept: the part did not arrive"; res.ms = (mono_us() - t0) * 1e-3;
            return res;
        }
        sleep_ms(f.ms + 80); if (f.belt_clear && on_belt_clear) on_belt_clear();
    }
    res.ok = res.grasped = true;
    res.ms = (mono_us() - t0) * 1e-3;
    return res;
}
ArmExec::Result ArmExec::run(const std::vector<ServoFrame> &wp, bool dry, const std::function<void()> &on_belt_clear,
                             const WaitBelt &wait_belt)
{
    Result res;
    const uint64_t t0 = mono_us();
    const double settle_ms = 80;   // after the commanded move time, before the next frame ⚠ tune on the arm
    for (const ServoFrame &f : wp) {
        if (f.wait_belt && wait_belt && !wait_belt(f.belt_mm)) {   // intercept: the part never came between the fingers
            if (!dry && !wp.empty()) {
                send(wp.front().pos, 400);   // straight up (first frame = above the pick point, open)
                sleep_ms(400 + settle_ms); if (on_belt_clear) on_belt_clear();
            }
            res.why = "intercept: the part did not arrive"; res.ms = (mono_us() - t0) * 1e-3;
            return res;
        }
        if (dry) {
            printf("  %-8s %4u ms  counts %4u %4u %4u %4u %4u | grip %4u%s%s\n", f.what, f.ms, f.pos[0], f.pos[1],
                   f.pos[2], f.pos[3], f.pos[4], f.pos[5], f.check_grasp ? "  [grasp check]" : "",
                   f.belt_clear ? "  [belt clear]" : "");
        } else {
            const int rc = send(f.pos, f.ms); if (rc != DES_OK) {
                res.why = std::string("SYNC WRITE failed: ") + des_strerror(rc); res.ms = (mono_us() - t0) * 1e-3;
                return res;  }
            sleep_ms(f.ms + settle_ms); if (f.check_grasp) {
                // gripper stopped on the part: position short of the closed command (these servos report no current).
                // Fully closed (within kGraspCloseTolMm of the command) = nothing in the fingers. ⚠ thresholds from the arm
                uint16_t p = 0; const bool rp = des_servo_read_u16(hw_, id(5), 0x38, &p) == DES_OK;
                res.grip_pos = rp ? p : -1; const float per_mm = grip_counts_per_mm(cfg_, grip_mm(cfg_, f.pos[5]));
                const float diff_mm = rp && per_mm > 0 ? std::fabs(float(p) - float(f.pos[5])) / per_mm : 0.f;
                res.grasped = rp && diff_mm > kGraspCloseTolMm;
                if (!rp) res.why = "gripper did not answer (grasp unknown)";
                else if (!res.grasped) {
                    char b[160];
                    snprintf(b, sizeof b, "gripper closed fully: nothing picked (pos %u, cmd %u, %.2f mm < %.2f mm)",
                             unsigned(p), unsigned(f.pos[5]), double(diff_mm), double(kGraspCloseTolMm));
                    res.why = b;
                }  }
        }
        if (f.belt_clear && on_belt_clear) on_belt_clear();
    }
    res.ok = dry || res.why.empty() || res.grasped;
    res.ms = (mono_us() - t0) * 1e-3;
    return res;
}
// ------------------------------------------------------------------ ArmManual
ArmManual::ArmManual(des_hw_t *hw, const ArmConfig &cfg, bool home_taught, std::string teach_path,
                     std::string home_path)
    : arm_(hw, cfg), hw_(hw), teach_path_(std::move(teach_path)), home_path_(std::move(home_path))
{
    JointAngles h;
    h.q = cfg.home_deg;
    std::array<uint16_t, 6> c{};
    if (!arm_to_counts(cfg, h, cfg.grip_max_open, c)) {
        for (int k = 0; k < 6; k++) v_.home[k] = c[k];
        if (cfg.home_grip_count >= 0) v_.home[5] = int(cfg.home_grip_count);
    }
    v_.home_taught = home_taught;
    th_ = std::thread(&ArmManual::loop, this);
}
ArmManual::~ArmManual()
{
    q_.close_and_clear();   // drop pending commands: nothing may move while the page is being destroyed
    if (th_.joinable()) th_.join();
}
void ArmManual::say(const char *fmt, ...)
{
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    std::lock_guard<std::mutex> lk(m_);
    copy_utf8(b, v_.msg, sizeof v_.msg);
}
void ArmManual::say_locked(const char *fmt, ...)
{
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    copy_utf8(b, v_.msg, sizeof v_.msg);
}
void ArmManual::context(bool shown, bool allowed, const char *why_not)
{
    std::lock_guard<std::mutex> lk(m_);
    if ((!shown || !allowed) && v_.unlocked) {   // leaving the page or the line starting locks it again
        v_.unlocked = false; if (shown) say_locked("已上锁：%s", why_not ? why_not : "");
        else say_locked("离开页面，已上锁");
    }
    v_.shown = shown;
    v_.allowed = allowed;
    why_not_ = why_not ? why_not : "";
}
void ArmManual::request(const ArmRequest &r) { q_.push(r); }
ArmManualView ArmManual::view() const
{
    std::lock_guard<std::mutex> lk(m_);
    return v_;
}
bool ArmManual::take_home(std::array<int, 6> &counts)
{
    std::lock_guard<std::mutex> lk(m_);
    if (!home_changed_) return false;
    home_changed_ = false;
    counts = v_.home;
    return true;
}
void ArmManual::poll()
{
    std::array<ArmJointView, 6> j;
    {
        std::lock_guard<std::mutex> lk(m_); j = v_.j;
    }
    for (int k = 0; k < 6; k++) {   // positions first: frames to different servos need no quiet time in between
        uint8_t d[2]; des_sb_result_t r;
        const bool ok = des_servo_read(hw_, arm_.id(k), 0x38, 2, d, &r) == DES_OK;   // already retried in des_hw
        if (ok) {
            j[k].pos = (d[0] << 8) | d[1]; j[k].status = r.status; misses_[k] = 0;
        } else misses_[k]++;
        // one failed poll keeps the last reading: only two in a row show the servo as not answering
        j[k].alive = ok || (misses_[k] < 2 && j[k].pos >= 0);
    }
    // no current / temperature reads: these servos do not report them (0x2E follows the supply, 0x3F = 255; 10-26)
    std::lock_guard<std::mutex> lk(m_);
    for (int k = 0; k < 6; k++) {
        const int target = v_.j[k].target; v_.j[k] = j[k];
        v_.j[k].target = target;
    }
}
void ArmManual::handle(const ArmRequest &r)
{
    ArmManualView v = view();
    if (!v.shown) return;
    if (!v.allowed) {
        std::string why; {
            std::lock_guard<std::mutex> lk(m_); why = why_not_;
        }
        say("不可用：%s", why.c_str()); return;
    }
    const ArmConfig &ac = arm_.config();
    switch (r.kind) {
    case ArmRequest::Unlock:
    case ArmRequest::Lock: {
        std::lock_guard<std::mutex> lk(m_); v_.unlocked = r.kind == ArmRequest::Unlock;
        if (v_.unlocked)
            for (auto &j : v_.j) j.target = -1;   // the arm may have moved since: start from where it is
        say_locked(v_.unlocked ? "已解锁：可以微调关节（动作前确认机械臂周围无障碍）" : "已上锁"); return;
    }
    case ArmRequest::Teach: {
        FILE *f = fopen(teach_path_.c_str(), "a"); if (!f) {
            say("记录失败：打不开 %s", teach_path_.c_str()); return;
        }
        fprintf(f, "%.0f", time(nullptr) * 1.0); for (int k = 0; k < 6; k++) fprintf(f, ",%d", v.j[k].pos);
        for (int k = 0; k < 6; k++)
            fprintf(f, ",%.1f", v.j[k].pos < 0 ? NAN : (v.j[k].pos - ac.joint[k].zero_count) / ac.joint[k].counts_per_deg);
        fprintf(f, "\n");
        fclose(f); std::lock_guard<std::mutex> lk(m_);
        v_.taught++; say_locked("已记录第 %d 个姿态 → %s", v_.taught, teach_path_.c_str());
        return;
    }
    default: break;
    }
    if (!v.unlocked) {
        say("先点「解锁」"); return;
    }
    if (r.kind == ArmRequest::Torque) {
        const int rc = arm_.torque(r.on != 0); std::lock_guard<std::mutex> lk(m_);
        v_.torque = r.on != 0; if (!r.on)
            for (auto &j : v_.j) j.target = -1;   // moved by hand from here on: jog from the read-back position
        if (rc == DES_OK) say_locked(r.on ? "扭矩已开（6 个舵机保持当前位置）" : "扭矩已关：机械臂可以用手扳动");
        else { say_locked("扭矩命令失败：%s", des_strerror(rc)); } return;
    }
    if (r.kind == ArmRequest::SetHome) {
        std::array<int, 6> c; for (int k = 0; k < 6; k++) {
            if (!v.j[k].alive || v.j[k].pos < 0) {
                say("%s 没有应答，不能设初始姿态", kArmJointName[k]); return;
            }
            c[k] = v.j[k].pos;
        }
        FILE *f = fopen(home_path_.c_str(), "w"); if (!f) {
            say("保存失败：打不开 %s", home_path_.c_str()); return;
        }
        fprintf(f, "{\"comment\": \"arm ready pose taught on the HMI arm page (servo counts, joints 1..6)\",\n"
                   " \"counts\": [%d, %d, %d, %d, %d, %d]}\n", c[0], c[1], c[2], c[3], c[4], c[5]); fclose(f);
        std::lock_guard<std::mutex> lk(m_); v_.home = c;
        v_.home_taught = true; home_changed_ = true;
        say_locked("已设为初始姿态（生产流程每次抓取从这里开始、回到这里）"); return;
    }
    if (r.kind == ArmRequest::Reset) {
        if (!v.home_taught) {
            say("还没有设定初始姿态：先把机械臂调到位，点「设为初始姿态」"); return;
        }
        {   // one joint-space move: if the gripper pitch changes a lot (up <-> down) it swings the 190 mm gripper
            // through horizontal, forward across the belt (opposite light bar, 10-22) -> do that by hand first
            bool known = true; std::array<uint16_t, 6> now{}, home{};
            for (int k = 0; k < 6; k++) {
                if (v.j[k].pos < 0) { known = false; } now[k] = uint16_t(std::clamp(v.j[k].pos, 0, 4095));
                home[k] = uint16_t(std::clamp(v.home[k], 0, 4095));  }
            float g; const float pn = tool_pitch_deg(arm_from_counts(ac, now, g)), ph = tool_pitch_deg(arm_from_counts(ac, home, g));
            if (!known || std::fabs(pn - ph) > 60.f) {
                if (known)
                    say("不复位：夹爪方向与初始姿态差 %d°，直接转会把夹爪甩过皮带（可能碰对面灯条）。"
                        "先关扭矩用手把夹爪摆成和初始姿态差不多（朝下），再点复位",
                        int(std::lround(std::fabs(pn - ph))));  else
                    say("不复位：夹爪方向未读到，直接转会把夹爪甩过皮带（可能碰对面灯条）。"
                        "先关扭矩用手把夹爪摆成和初始姿态差不多（朝下），再点复位"); return;
            }
        }
        std::array<uint16_t, 6> target{}; const uint16_t t = uint16_t(std::clamp(r.ms, 2000, 5000));   // all six together: never faster than 2 s
        std::array<uint16_t, 6> now{};
        for (int k = 0; k < 6; k++) {
            target[k] = uint16_t(std::clamp(v.home[k], 0, 4095)); now[k] = uint16_t(std::clamp(v.j[k].pos, 0, 4095));
        }
        // from a low pose (a taught bin) straight up first, then home: one joint move from there drags the gripper
        // and the #6 servo below the belt surface along the belt edge (10-26)
        float g; std::vector<ServoFrame> fr;
        if (const char *why = plan_move(ac, now, arm_from_counts(ac, target, g), g, t, "home", fr)) {
            say("不复位：%s", why); return;
        }
        fr.back().pos = target;   // exactly the taught counts
        int rc = DES_OK; double total = 0;
        for (size_t i = 0; i < fr.size() && rc == DES_OK; i++) {
            rc = sync_write_all(hw_, ac, fr[i].pos, fr[i].ms); total += fr[i].ms;
            if (rc == DES_OK && i + 1 < fr.size()) {
                say("复位：先竖直抬到皮带上方（%.1f s）", fr[i].ms / 1000.0); sleep_ms(fr[i].ms + 150);
            }
        }
        std::lock_guard<std::mutex> lk(m_);
        for (int k = 0; k < 6; k++) v_.j[k].target = target[k];
        if (rc == DES_OK) say_locked(fr.size() > 1 ? "复位：先竖直抬起，再回初始姿态（%.1f s）" : "复位：6 个关节回初始姿态（%.1f s）", total / 1000.0);
        else { say_locked("复位：%s", des_strerror(rc)); } return;
    }
    // Jog: from the last target (exact steps; the servo dead band does not add up), else from the read-back
    // position, but only while the servo is near it: after a pick, a hand move or a blocked move the target is
    // stale and stepping from it would jump the joint back. Clamped to the joint's soft limits.
    const int k = std::clamp(r.joint, 0, 5);
    const JointCal &jc = ac.joint[k];
    const int tgt = v.j[k].target, rb = v.j[k].pos;
    const int base = tgt >= 0 && (rb < 0 || std::abs(tgt - rb) <= std::abs(int(3 * jc.counts_per_deg))) ? tgt : rb;
    if (base < 0) {
        say("%s 还没读到位置，不能微调", kArmJointName[k]); return;
    }
    double deg = (base - jc.zero_count) / jc.counts_per_deg + r.dir * r.step_deg;
    deg = std::clamp(deg, double(jc.min_deg), double(jc.max_deg));
    const uint16_t ms = uint16_t(std::clamp(r.ms, 100, 5000));
    const int target = std::clamp(int(std::lround(jc.zero_count + deg * jc.counts_per_deg)), 0, 4095);
    const int rc = sync_write_one(hw_, ac, k, uint16_t(target), ms);
    std::lock_guard<std::mutex> lk(m_);
    v_.j[k].target = target;
    say_locked("%s → %d（%+.0f°，%.1f s）%s", kArmJointName[k], target, r.dir * r.step_deg, ms / 1000.0,
               rc == DES_OK ? "" : des_strerror(rc));
}
void ArmManual::loop()
{
    double last_poll = 0;
    for (;;) {
        ArmRequest r; const bool have = q_.pop_for(r, 250);
        if (!have && q_.closed()) return;   // the destructor closed the command queue
        bool active; {
            std::lock_guard<std::mutex> lk(m_); active = v_.shown && v_.allowed;
        }
        if (have) { handle(r); } const double now = mono_s();
        if (active && now - last_poll >= 0.25) {
            poll(); last_poll = now;
        }
    }
}
}  // namespace des
