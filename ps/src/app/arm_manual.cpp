// arm_manual.cpp — see arm_manual.hpp
#include "arm_manual.hpp"

#include <time.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace des {

namespace {

double now_s()
{
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

const char *kJointName[6] = {"#1 底座", "#2 肩", "#3 肘", "#4 腕俯仰", "#5 腕旋", "#6 夹爪"};

}  // namespace

ArmManual::ArmManual(des_hw_t *hw, const ArmConfig &cfg, std::string teach_path)
    : arm_(hw, cfg), hw_(hw), teach_path_(std::move(teach_path))
{
    th_ = std::thread(&ArmManual::loop, this);
}

ArmManual::~ArmManual()
{
    {
        std::lock_guard<std::mutex> lk(m_);
        quit_ = true;
    }
    cv_.notify_all();
    if (th_.joinable()) th_.join();
}

void ArmManual::context(bool shown, bool allowed, const char *why_not)
{
    std::lock_guard<std::mutex> lk(m_);
    if ((!shown || !allowed) && v_.unlocked) {   // leaving the page or the line starting locks it again
        v_.unlocked = false;
        v_.msg = shown ? std::string("已上锁：") + (why_not ? why_not : "") : "离开页面，已上锁";
    }
    const bool wake = shown && allowed && !(v_.shown && v_.allowed);
    v_.shown = shown;
    v_.allowed = allowed;
    why_not_ = why_not ? why_not : "";
    if (wake) cv_.notify_all();
}

void ArmManual::request(const ArmRequest &r)
{
    {
        std::lock_guard<std::mutex> lk(m_);
        if (q_.size() < 16) q_.push_back(r);
    }
    cv_.notify_all();
}

ArmManualView ArmManual::view() const
{
    std::lock_guard<std::mutex> lk(m_);
    return v_;
}

void ArmManual::poll(bool full)
{
    std::array<ArmJointView, 6> j;
    {
        std::lock_guard<std::mutex> lk(m_);
        j = v_.j;
    }
    for (int k = 0; k < 6; k++) {   // positions first: frames to different servos need no quiet time in between
        uint8_t d[2];
        des_sb_result_t r;
        j[k].alive = des_servo_read(hw_, arm_.id(k), 0x38, 2, d, &r) == DES_OK;
        if (j[k].alive) {
            j[k].pos = (d[0] << 8) | d[1];
            j[k].status = r.status;
        }
    }
    if (full)
        for (int k = 0; k < 6; k++) {
            if (!j[k].alive) continue;
            uint16_t ma;
            uint8_t t;
            j[k].current_ma = des_servo_read_u16(hw_, arm_.id(k), 0x2E, &ma) == DES_OK ? ma : -1;
            j[k].temp_c = des_servo_read(hw_, arm_.id(k), 0x3F, 1, &t, nullptr) == DES_OK ? t : -1;
        }
    std::lock_guard<std::mutex> lk(m_);
    for (int k = 0; k < 6; k++) {
        const int target = v_.j[k].target;
        v_.j[k] = j[k];
        v_.j[k].target = target;
    }
}

void ArmManual::handle(const ArmRequest &r)
{
    ArmManualView v = view();
    auto say = [&](const std::string &s) {
        std::lock_guard<std::mutex> lk(m_);
        v_.msg = s;
    };
    if (!v.shown) return;
    if (!v.allowed) {
        say("不可用：" + why_not_);
        return;
    }
    char b[160];
    switch (r.kind) {
    case ArmRequest::Unlock:
    case ArmRequest::Lock: {
        std::lock_guard<std::mutex> lk(m_);
        v_.unlocked = r.kind == ArmRequest::Unlock;
        v_.msg = v_.unlocked ? "已解锁：可以微调关节（动作前确认机械臂周围无障碍）" : "已上锁";
        return;
    }
    case ArmRequest::Teach: {
        FILE *f = fopen(teach_path_.c_str(), "a");
        if (!f) {
            say("记录失败：打不开 " + teach_path_);
            return;
        }
        const ArmConfig &c = arm_.config();
        fprintf(f, "%.0f", time(nullptr) * 1.0);
        for (int k = 0; k < 6; k++) fprintf(f, ",%d", v.j[k].pos);
        for (int k = 0; k < 6; k++)
            fprintf(f, ",%.1f", v.j[k].pos < 0 ? NAN : (v.j[k].pos - c.joint[k].zero_count) / c.joint[k].counts_per_deg);
        fprintf(f, "\n");
        fclose(f);
        std::lock_guard<std::mutex> lk(m_);
        v_.taught++;
        snprintf(b, sizeof b, "已记录第 %d 个姿态 → %s", v_.taught, teach_path_.c_str());
        v_.msg = b;
        return;
    }
    default:
        break;
    }
    if (!v.unlocked) {
        say("先点「解锁」");
        return;
    }
    if (r.kind == ArmRequest::Torque) {
        const int rc = arm_.torque(r.on != 0);
        std::lock_guard<std::mutex> lk(m_);
        v_.torque = r.on != 0;
        v_.msg = rc == DES_OK ? (r.on ? "扭矩已开（6 个舵机保持当前位置）" : "扭矩已关：机械臂可以用手扳动")
                              : std::string("扭矩命令失败：") + des_strerror(rc);
        return;
    }
    // Jog: from the last read position (or the last target if the servo did not answer), clamped to the soft limits
    const int k = std::clamp(r.joint, 0, 5);
    const JointCal &jc = arm_.config().joint[k];
    const int base = v.j[k].pos >= 0 ? v.j[k].pos : v.j[k].target;
    if (base < 0) {
        say(std::string(kJointName[k]) + " 还没读到位置，不能微调");
        return;
    }
    double deg = (base - jc.zero_count) / jc.counts_per_deg + r.dir * r.step_deg;
    deg = std::clamp(deg, double(jc.min_deg), double(jc.max_deg));
    const int target = std::clamp(int(std::lround(jc.zero_count + deg * jc.counts_per_deg)), 0, 4095);
    const uint8_t id = arm_.id(k);
    const uint16_t pos = uint16_t(target), ms = uint16_t(std::clamp(r.ms, 100, 5000));
    const int rc = des_servo_sync_pos(hw_, &id, &pos, &ms, 1);
    std::lock_guard<std::mutex> lk(m_);
    v_.j[k].target = target;
    snprintf(b, sizeof b, "%s → %d（%+.0f°，%.1f s）%s", kJointName[k], target, r.dir * r.step_deg, ms / 1000.0,
             rc == DES_OK ? "" : des_strerror(rc));
    v_.msg = b;
}

void ArmManual::loop()
{
    double last_poll = 0;
    for (;;) {
        ArmRequest r;
        bool have = false, active;
        {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait_for(lk, std::chrono::milliseconds(250), [&] { return quit_ || !q_.empty(); });
            if (quit_) return;
            if (!q_.empty()) {
                r = q_.front();
                q_.pop_front();
                have = true;
            }
            active = v_.shown && v_.allowed;
        }
        if (have) handle(r);
        if (active && now_s() - last_poll >= 0.25) {
            poll(polls_++ % 4 == 0);
            last_poll = now_s();
        }
    }
}

}  // namespace des
