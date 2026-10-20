// arm_exec.cpp — see arm_exec.hpp
#include "arm_exec.hpp"

#include <time.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace des {

static double now_ms()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static void sleep_ms(double ms)
{
    if (ms <= 0) return;
    timespec ts{time_t(ms / 1000), long(std::fmod(ms, 1000.0) * 1e6)};
    nanosleep(&ts, nullptr);
}

ServoInfo ArmExec::probe(int joint)
{
    ServoInfo s;
    des_sb_result_t r;
    if (des_servo_ping(hw_, id(joint), &r) != DES_OK) return s;
    s.alive = true;
    s.status = r.status;
    uint16_t v;
    if (des_servo_read_u16(hw_, id(joint), 0x38, &v) == DES_OK) s.pos = v;
    if (des_servo_read_u16(hw_, id(joint), 0x2E, &v) == DES_OK) s.current_ma = v;
    uint8_t t;
    if (des_servo_read(hw_, id(joint), 0x3F, 1, &t, nullptr) == DES_OK) s.temp_c = t;
    return s;
}

bool ArmExec::read_positions(std::array<uint16_t, 6> &pos)
{
    bool all = true;
    for (int j = 0; j < 6; j++) {
        uint16_t v;
        if (des_servo_read_u16(hw_, id(j), 0x38, &v) == DES_OK) pos[j] = v;
        else all = false;
    }
    return all;
}

int ArmExec::torque(bool on) { return des_servo_torque(hw_, DES_SERVO_BROADCAST, on); }

int ArmExec::send(const std::array<uint16_t, 6> &pos, uint16_t ms)
{
    uint8_t ids[6];
    uint16_t t[6];
    for (int j = 0; j < 6; j++) {
        ids[j] = id(j);
        t[j] = ms;
    }
    return des_servo_sync_pos(hw_, ids, pos.data(), t, 6);
}

ArmExec::Result ArmExec::simulate(const std::vector<ServoFrame> &wp, const std::function<void()> &on_belt_clear,
                                  const WaitBelt &wait_belt)
{
    Result res;
    const double t0 = now_ms();
    for (const ServoFrame &f : wp) {
        if (f.wait_belt && wait_belt && !wait_belt(f.belt_mm)) {
            res.why = "intercept: the part did not arrive";
            res.ms = now_ms() - t0;
            return res;
        }
        sleep_ms(f.ms + 80);
        if (f.belt_clear && on_belt_clear) on_belt_clear();
    }
    res.ok = res.grasped = true;
    res.ms = now_ms() - t0;
    return res;
}

ArmExec::Result ArmExec::run(const std::vector<ServoFrame> &wp, bool dry, const std::function<void()> &on_belt_clear,
                             const WaitBelt &wait_belt)
{
    Result res;
    const double t0 = now_ms();
    const double settle_ms = 80;   // after the commanded move time, before the next frame ⚠ tune on the arm
    for (const ServoFrame &f : wp) {
        if (f.wait_belt && wait_belt && !wait_belt(f.belt_mm)) {   // intercept: the part never came between the fingers
            if (!dry && !wp.empty()) {
                send(wp.front().pos, 400);   // straight up (first frame = above the pick point, open)
                sleep_ms(400 + settle_ms);
                if (on_belt_clear) on_belt_clear();
            }
            res.why = "intercept: the part did not arrive";
            res.ms = now_ms() - t0;
            return res;
        }
        if (dry) {
            printf("  %-8s %4u ms  counts %4u %4u %4u %4u %4u | grip %4u%s%s\n", f.what, f.ms, f.pos[0], f.pos[1],
                   f.pos[2], f.pos[3], f.pos[4], f.pos[5], f.check_grasp ? "  [grasp check]" : "",
                   f.belt_clear ? "  [belt clear]" : "");
        } else {
            int rc = send(f.pos, f.ms);
            if (rc != DES_OK) {
                res.why = std::string("SYNC WRITE failed: ") + des_strerror(rc);
                res.ms = now_ms() - t0;
                return res;
            }
            sleep_ms(f.ms + settle_ms);
            if (f.check_grasp) {
                // gripper stopped on the part: position short of the closed command and a holding current.
                // Fully closed (within ~1.5 mm of the command) = nothing in the fingers. ⚠ thresholds from the arm
                uint16_t p = 0, ma = 0;
                bool rp = des_servo_read_u16(hw_, id(5), 0x38, &p) == DES_OK;
                bool rc2 = des_servo_read_u16(hw_, id(5), 0x2E, &ma) == DES_OK;
                res.grip_pos = rp ? p : -1;
                res.grip_ma = rc2 ? ma : -1;
                const float per_mm = grip_counts_per_mm(cfg_, grip_mm(cfg_, f.pos[5]));
                res.grasped = rp && std::fabs(float(p) - float(f.pos[5])) > 1.5f * per_mm;
                if (!rp) {
                    res.why = "gripper did not answer (grasp unknown)";
                } else if (!res.grasped) {
                    res.why = "gripper closed fully: nothing picked";
                }
            }
        }
        if (f.belt_clear && on_belt_clear) on_belt_clear();
    }
    res.ok = dry || res.why.empty() || res.grasped;
    res.ms = now_ms() - t0;
    return res;
}

}  // namespace des
