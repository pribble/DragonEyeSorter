// runner.cpp — see runner.hpp
#include "runner.hpp"

#include <time.h>

#include <algorithm>
#include <cstdio>

#include "can_link.h"
#include "part_log.hpp"

namespace des {

static double now_s()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static const char *state_name(uint8_t s)
{
    static const char *n[] = {"IDLE", "RUN", "PAUSED", "ESTOP", "FAULT"};
    return s <= 4 ? n[s] : "?";
}

Runner::Runner(des_hw_t *hw, const AppConfig &cfg, const RunOptions &opt)
    : hw_(hw), cfg_(cfg), opt_(opt), belt_(hw, cfg.belt), arm_(hw, cfg.arm), flaps_(cfg.flaps)
{
    if (opt.speed_mm_s > 0) belt_.set_speed(opt.speed_mm_s);
    JointAngles home;
    home.q = cfg.arm.home_deg;
    arm_to_counts(cfg.arm, home, cfg.arm.grip_max_open, arm_counts_);
}

Runner::~Runner()
{
    {
        std::lock_guard<std::mutex> lk(m_);
        quit_ = true;
    }
    cv_.notify_all();
    if (arm_thread_.joinable()) arm_thread_.join();
    des_step_stop(hw_);
    if (can_fd_ >= 0) can_close(can_fd_);
}

std::string Runner::start()
{
    t0_ = now_s();
    if (!opt_.can_if.empty()) {
        can_fd_ = can_open(opt_.can_if.c_str());
        if (can_fd_ < 0) return "cannot open CAN " + opt_.can_if + " (bring it up with scripts/deploy/out/can_up.sh)";
    }
    prev_status_ = des_step_status(hw_);
    belt_.enable(true);
    arm_thread_ = std::thread(&Runner::arm_loop, this);
    set_state(can_fd_ >= 0 ? CANP_ST_IDLE : CANP_ST_RUN, can_fd_ >= 0 ? "waiting for PLC START" : "running (no CAN)");
    return "";
}

// Frames written back to back can be lost by the CAN controller (soak 10-11, loopback: 1000 of 108691, 79 of 82
// lost STATUS frames in seconds with bursts of PART frames; spaced sends at <= 2000 frames/s lose nothing). So
// everything but ACKs goes through a queue drained one frame per cycle (60 frames/s; normal load ~15 frames/s,
// a burst of parts is spread over a few hundred ms).
void Runner::send(const canp_frame_t &f)
{
    if (can_fd_ < 0) return;
    if (txq_.size() >= 256) {   // PLC away / bus off: keep the newest
        txq_.pop_front();
        tx_dropped_++;
    }
    txq_.push_back(f);
}

void Runner::send_now(const canp_frame_t &f)
{
    if (can_fd_ >= 0) can_send(can_fd_, &f);
}

void Runner::flush_tx()
{
    if (can_fd_ < 0 || txq_.empty()) return;
    const int r = can_try_send(can_fd_, &txq_.front());   // never blocks the worker (no PLC on the bus: queue full)
    if (r == 1) return;                                    // try again next cycle
    if (r < 0) tx_dropped_++;
    txq_.pop_front();
}

void Runner::set_state(uint8_t s, const char *why)
{
    last_event_ = why;
    if (state_ == s) return;
    state_ = s;
    printf("[run] %s: %s\n", state_name(s), why);
    fflush(stdout);
    last_status_ = 0;   // report the change on the next cycle
}

// commands from the PLC (CAN) and the operator (HDMI HMI) follow the same rules; returns a CANP_ACK_* result
uint8_t Runner::command(uint8_t cmd, const char *who)
{
    const uint8_t st = state_;
    std::string why = std::string(who) + " ";
    switch (cmd) {
    case CANP_CMD_START:
        if (st == CANP_ST_IDLE || st == CANP_ST_PAUSED) set_state(CANP_ST_RUN, (why + "start").c_str());
        else if (st != CANP_ST_RUN) return CANP_ACK_REJECTED;
        return CANP_ACK_OK;
    case CANP_CMD_STOP: set_state(CANP_ST_IDLE, (why + "stop").c_str()); return CANP_ACK_OK;
    case CANP_CMD_PAUSE:
        if (st == CANP_ST_RUN) set_state(CANP_ST_PAUSED, (why + "interlock pause").c_str());
        else if (st != CANP_ST_PAUSED) return CANP_ACK_REJECTED;
        return CANP_ACK_OK;
    case CANP_CMD_RESUME:
        if (st == CANP_ST_PAUSED) set_state(CANP_ST_RUN, (why + "resume").c_str());
        else return CANP_ACK_REJECTED;
        return CANP_ACK_OK;
    case CANP_CMD_RESET_ALARM:
        alarm_ = 0;
        if (st == CANP_ST_ESTOP || st == CANP_ST_FAULT) set_state(CANP_ST_IDLE, "alarm reset");
        return CANP_ACK_OK;
    default: return CANP_ACK_UNKNOWN;
    }
}

uint8_t Runner::set_speed(double mm_s, const char *who)
{
    if (mm_s <= 0 || mm_s > 200) return CANP_ACK_BAD_VALUE;
    belt_.set_speed(mm_s);
    speed_mm_s_ = mm_s;
    char e[64];
    snprintf(e, sizeof e, "%s speed %.1f mm/s", who, mm_s);
    last_event_ = e;
    return CANP_ACK_OK;
}

uint8_t Runner::select_recipe(int id, const char *who)
{
    const Recipe *r = find_recipe(cfg_.recipes, id);
    if (!r) return CANP_ACK_BAD_VALUE;
    if (state_ == CANP_ST_RUN) return CANP_ACK_REJECTED;   // changeover only with the line stopped / paused
    pending_recipe_ = id;
    last_event_ = std::string(who) + " recipe " + std::to_string(id) + " " + r->name;
    return CANP_ACK_OK;
}

int Runner::take_recipe()
{
    const int id = pending_recipe_;
    pending_recipe_ = -1;
    return id;
}

void Runner::handle_can(double now)
{
    if (can_fd_ < 0) return;
    canp_frame_t f;
    while (can_recv(can_fd_, &f, 0) == 1) {
        uint8_t result = CANP_ACK_OK;
        switch (f.id) {
        case CANP_ID_CMD:
            result = command(f.data[0], "PLC");
            send_now(canp_ack(f.id, f.data[1], result));
            break;
        case CANP_ID_SPEED:
            result = set_speed(canp_get16(f.data) / 10.0, "PLC");
            send_now(canp_ack(f.id, f.data[2], result));
            break;
        case CANP_ID_RECIPE:
            result = select_recipe(f.data[0], "PLC");
            send_now(canp_ack(f.id, f.data[1], result));
            break;
        case CANP_ID_PLC_HB: last_plc_hb_ = now; break;
        case CANP_ID_TIME: {   // the PLC's wall clock (no RTC here): records use our clock + this offset
            timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            const int64_t mine = int64_t(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
            const bool first = !wall_synced();
            set_wall_offset_ms(canp_time_ms(&f) - mine);
            if (first) last_event_ = "clock synced from PLC";
            break;
        }
        default: break;
        }
    }
}

void Runner::cycle(Tracker &tr, Picker &pk)
{
    const double now = now_s();
    handle_can(now);

    // e-stop: PL halts the stepper on ESTOP_IN and latches HALTED; react to the rising edge only
    const uint32_t st = des_step_status(hw_);
    if ((st & DES_ST_STATUS_HALTED) && !(prev_status_ & DES_ST_STATUS_HALTED) && state_ == CANP_ST_RUN) {
        alarm_ = 1;
        set_state(CANP_ST_ESTOP, "e-stop: stepper halted");
        send(canp_alarm(1, 2, 0));
    }
    prev_status_ = st;

    // arm results
    {
        std::lock_guard<std::mutex> lk(m_);
        for (const Done &d : done_) {
            pk.job_done(tr, d.id, d.ok);
            if (d.ok) {
                counts_.ok++;
            } else {
                counts_.recheck++;
                last_event_ = "pick failed: " + d.why;
            }
        }
        done_.clear();
    }

    if (state_ == CANP_ST_RUN) {
        Picker::Input in;
        in.belt_mm = belt_.belt_mm();
        in.brake_mm = belt_.brake_mm();
        in.belt_stopped = belt_.stopped();
        in.arm_idle = !arm_busy_;
        in.arm_clear = arm_clear_;
        Picker::Output out = pk.tick(tr, in);
        belt_.apply(out.belt);
        // belt-end flaps follow the picker's flap events (two stages, des_pwm; no-op before v7 or when disabled)
        if (cfg_.flaps.enabled) {
            const FlapSequencer::State &fs = flaps_.update(out.flaps, in.belt_mm);
            for (int ch = 0; ch < 2; ch++)
                if (fs.us[ch] != flap_us_[ch]) {
                    des_pwm_set(hw_, ch, fs.us[ch]);
                    flap_us_[ch] = fs.us[ch];
                }
            if (!pwm_enabled_) pwm_enabled_ = des_pwm_enable(hw_, 3u) == DES_OK;   // both flaps
        }
        if (out.have_job) {
            std::lock_guard<std::mutex> lk(m_);
            job_ = out.job;
            job_pending_ = true;
            arm_busy_ = true;
            arm_clear_ = false;
            cv_.notify_one();
        }
    } else {
        BeltPlan hold;   // not running: ramp the belt down if it moves; a pick in progress finishes on its own
        hold.kind = BeltPlan::Hold;
        belt_.apply(hold);
    }

    // newly decided parts -> PART frames + counts (decision time; a later failed pick is counted as recheck)
    for (const Track &t : tr.tracks()) {
        if (t.state == TrackState::New || t.state == TrackState::Tracking || t.disp == Disposition::None) continue;
        if (!reported_.insert(t.id).second) continue;
        counts_.total++;
        if (t.disp == Disposition::Flap) counts_.ng++;
        else if (t.disp == Disposition::Recheck) counts_.recheck++;
        canp_part_t p;
        p.id = uint16_t(t.id);
        p.cls = uint8_t(t.cls < 0 ? 0xFF : t.cls);
        p.disp = t.disp == Disposition::Pick ? CANP_DISP_PICK : t.disp == Disposition::Flap ? CANP_DISP_FLAP
                                                                                          : CANP_DISP_RECHECK;
        p.dest = uint8_t(t.dest);
        p.conf_pct = uint8_t(std::min(100.f, t.cls_conf * 100));
        p.size_mm = t.meas.valid ? t.meas.size : 0;   // gear OD / rack length; 0 = not measured (can_proto.h)
        send(canp_part(&p));
    }
    while (reported_.size() > 2000) reported_.erase(reported_.begin());

    if (now - last_status_ >= 0.1) {
        canp_status_t s;
        s.state = state_;
        s.flags = uint8_t((belt_.stopped() ? 0 : CANP_FLAG_BELT_MOVING) | (arm_busy_ ? CANP_FLAG_ARM_BUSY : 0) |
                          (alarm_ ? CANP_FLAG_ALARM : 0) | (now - last_plc_hb_ < 1.0 ? CANP_FLAG_PLC_LINK : 0) |
                          (cfg_.calib_loaded.empty() ? 0 : CANP_FLAG_CALIBRATED));
        s.speed_mm_s = belt_.speed_mm_s();
        s.alarm = alarm_;
        s.hb = hb_++;
        s.uptime_s = uint16_t(now - t0_);
        send(canp_status(&s));
        last_status_ = now;
    }
    if (now - last_counts_ >= 1.0) {
        send(canp_counts(&counts_));
        last_counts_ = now;
    }
    flush_tx();
}

std::string Runner::status_line() const
{
    char b[240], q[48] = "";
    if (txq_.size() > 4 || tx_dropped_)
        snprintf(q, sizeof q, "CAN txq %zu dropped %llu  ", txq_.size(), (unsigned long long)tx_dropped_);
    snprintf(b, sizeof b, "%s  belt %.1f mm/s  arm %s  total %u ok %u ng %u recheck %u  %s%s%s", state_name(state_),
             belt_.speed_mm_s(), arm_busy_ ? "busy" : "idle", counts_.total, counts_.ok, counts_.ng, counts_.recheck,
             can_fd_ >= 0 ? (now_s() - last_plc_hb_ < 1.0 ? "PLC ok  " : "PLC --  ") : "", q, last_event_.c_str());
    return b;
}

void Runner::arm_loop()
{
    for (;;) {
        PickJob j;
        {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait(lk, [&] { return job_pending_ || quit_; });
            if (quit_) return;
            j = job_;
            job_pending_ = false;
        }
        std::vector<ServoFrame> wp;
        Done d{j.track_id, false, ""};
        const char *why = plan_pick(cfg_.arm, j.p, j.close_axis, j.open_mm, j.part_wid, j.bin, arm_counts_, wp);
        if (why) {
            d.why = why;
        } else {
            auto clear = [this] { arm_clear_ = true; };
            ArmExec::Result r = opt_.arm_go ? arm_.run(wp, false, clear) : arm_.simulate(wp, clear);
            d.ok = r.ok && r.grasped;
            d.why = r.why;
            arm_counts_ = wp.back().pos;
        }
        {
            std::lock_guard<std::mutex> lk(m_);
            done_.push_back(d);
        }
        arm_clear_ = true;
        arm_busy_ = false;
    }
}

}  // namespace des
