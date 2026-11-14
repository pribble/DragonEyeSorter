// runner.cpp — see runner.hpp
#include "runner.hpp"
#include <time.h>
#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "can_link.h"
#include "util.hpp"
namespace des {
static const char *state_name(uint8_t s)
{
    static const char *n[] = {"IDLE", "RUN", "PAUSED", "ESTOP", "FAULT"};
    return s <= 4 ? n[s] : "?";
}
Runner::Runner(des_hw_t *hw, const AppConfig &cfg, const RunOptions &opt)
    : hw_(hw), cfg_(cfg), opt_(opt), belt_(hw, cfg.belt), arm_(hw, cfg.arm), arm_cfg_(cfg.arm), ir_(MB_N_IR, 0),
      flaps_(cfg.flaps, cfg.picker.gates), pid_(cfg.belt.pid)
{
    pid_on_ = cfg.belt.pid.enabled;
    pid_switch_.store(pid_on_);
    if (cfg.belt.pid.hw && des_bpi_present(hw)) {   // bitstream v17+: the PL closes the loop
        const SpeedPidConfig &p = cfg.belt.pid;
        des_bpi_cfg_t c{p.hw_kp, p.hw_ki, p.hw_limit, p.hw_deadband, unsigned(p.hw_period_ms),
                        unsigned(std::min(200, std::max(100, 10 * p.hw_period_ms))), p.hw_settle};   // PL: 24-bit clock count
        pl_pid_ = des_bpi_config(hw, &c) == DES_OK;
        fprintf(stderr, "[run] belt speed PI: %s (kp %.2f ki %.3f limit %.0f %% window %d ms)\n",
                pl_pid_ ? "PL des_bpi" : "PL des_bpi rejected the settings, PS loop", p.hw_kp, p.hw_ki, p.hw_limit * 100,
                p.hw_period_ms);
    }
    pid_.set_enabled(pid_on_ && !pl_pid_);
    if (pl_pid_) pl_pid_enable(pid_on_);
    if (opt.speed_mm_s > 0) belt_.set_speed(opt.speed_mm_s);
    JointAngles home;
    home.q = cfg.arm.home_deg;
    arm_to_counts(cfg.arm, home, cfg.arm.grip_max_open, arm_counts_);
    if (cfg.arm.home_grip_count >= 0) arm_counts_[5] = uint16_t(cfg.arm.home_grip_count);
}
void Runner::set_pid_enabled(bool on)
{
    pid_switch_.store(on, std::memory_order_relaxed);   // the worker applies it at the next cycle
}
// PL loop on / off. Off clears the integrator and puts the output to 0 at once (the belt is back on the programmed
// speed within one clock); on starts from a clean integrator with the set-point written again by the next cycle.
void Runner::pl_pid_enable(bool on)
{
    des_bpi_enable(hw_, on ? 1 : 0);
    pl_set_ = -1;
    pl_scale_.store(0);
}
Runner::~Runner()
{
    if (pl_pid_) des_bpi_enable(hw_, 0);   // never leave the PL trimming the belt for the next program
    state_ = CANP_ST_IDLE;   // a pending intercept close gives up on its next 2 ms poll: the abort lifts the arm open
    BeltPlan hold; hold.kind = BeltPlan::Hold; belt_.apply(hold);   // the belt must not cruise while the arm winds down
    rs485_.reset();   // lamps off, bus thread joined
    jobs_.close_and_clear();
    if (arm_thread_.joinable()) arm_thread_.join();
    // stand still, then release the driver: nobody drives the belt after exit (no holding current, 10-26)
    if (des_step_stop_release(hw_, 3000) != DES_OK) fprintf(stderr, "runner: belt did not ramp down in 3 s, halted\n");
    if (can_fd_ >= 0) can_close(can_fd_);
}
std::string Runner::start()
{
    t0_ = mono_s();
    if (!opt_.can_if.empty()) {
        can_fd_ = can_open(opt_.can_if.c_str()); if (can_fd_ < 0) return "cannot open CAN " + opt_.can_if + " (bring it up with scripts/deploy/out/can_up.sh)";
    }
    active_recipe_ = cfg_.recipe;
    if (cfg_.plc.modbus_port > 0) {   // a busy port costs only the Modbus link, not the line
        mb_.reset(new ModbusTcp(MB_N_HR, MB_N_IR)); const std::string e = mb_->start(cfg_.plc.modbus_port);
        if (!e.empty()) {
            fprintf(stderr, "[run] Modbus-TCP off: %s\n", e.c_str()); mb_.reset();
        } else {
            printf("[run] Modbus-TCP slave on port %d\n", mb_->port());
        }
    }
    if (cfg_.rs485.enabled) {   // a missing driver / old bitstream costs only the tower light
        std::string e;
        RtuXfer x = rs485_hw_xfer(hw_, cfg_.rs485, e);
        if (!x) {
            fprintf(stderr, "[run] RS485 off: %s\n", e.c_str());
        } else {
            rs485_.reset(new Rs485Io(cfg_.rs485, std::move(x)));
            rs485_->start();
            printf("[run] RS485 %d 8%c1, relay module at address %d (tower light)\n", cfg_.rs485.baud, cfg_.rs485.parity,
                   cfg_.rs485.addr);
        }
    }
    // side gates: where a part of the spec gear OD on the belt centre line meets each one, and how early it must be decided
    const double v = speed_mm_s_ > 0 ? speed_mm_s_ : cfg_.belt.run_speed;
    for (size_t g = 0; g < cfg_.picker.gates.size(); g++) {
        const Gate &G = cfg_.picker.gates[g];
        const float r = 0.5f * cfg_.tracker.size.gear_od, m = cfg_.flaps.margin_mm;
        const XRange c = gate_closed_range(G.geom, 0, r), s = gate_sweep_range(G.geom, 0, r + m);
        const double latest = s.x0 - v * cfg_.flaps.move_s();
        std::string served;   // every outlet this paddle takes ("scrap+rework" when one gate serves two)
        for (size_t o = 1; o < cfg_.outlets.size(); o++)
            if (G.serves(int(o))) served += (served.empty() ? "" : "+") + cfg_.outlets[o];
        if (served.empty()) served = cfg_.outlets[size_t(G.outlet)];
        // release per outlet: slow sweep-out after contact (gate busy for that long), or hold while the part slides
        // off along the closed paddle (a gate with different releases has a drop point per outlet)
        auto release_txt = [&](const GateRelease &R) {
            if (R.ms <= 0) return fmt("hold %.0f mm", double(G.hold_mm > 0 ? G.hold_mm : gate_slide_mm(G.geom, 0)));
            return fmt("sweep-out %d ms (second half %.0f %%) from %.0f mm after contact (busy ~%.0f mm)", R.ms,
                       100.0 * R.tail, double(R.after_mm), R.after_mm + v * R.s);
        };
        std::string release;
        for (size_t o = 1; o < cfg_.outlets.size(); o++) {
            if (!G.serves(int(o))) continue;
            release += (release.empty() ? "" : ", ") + cfg_.outlets[o] + " " + release_txt(G.release(int(o)));
        }
        if (release.empty()) release = release_txt(G.release(G.outlet));
        printf("[run] gate %zu (%s%s): part at y 0 meets it at x %.0f, sweep x %.0f..%.0f, %s; "
               "decide by x %.0f at %.0f mm/s (decide_x %.0f)%s\n", g + 1, served.c_str(),
               cfg_.flaps.enabled ? "" : ", disabled", c.x0, s.x0, s.x1, release.c_str(), latest, v,
               cfg_.tracker.decide_x,
               c.hit && s.hit && latest > cfg_.tracker.decide_x ? "" : "  <- WARNING: parts reach it undecided");
    }
    const SpeedAdaptConfig &ad = cfg_.belt.adapt;
    if (ad.enabled) {
        // fast mode: a part is decided at the fast speed, then the belt slows to the base speed; its gate must still
        // be able to close ahead of it from there
        std::string warn;
        for (const Gate &G : cfg_.picker.gates) {
            const XRange s = gate_sweep_range(G.geom, 0, 0.5f * cfg_.tracker.size.gear_od + cfg_.flaps.margin_mm);
            if (ad.fast_mm_s > 0 && s.hit && s.x0 - ad.fast_mm_s * cfg_.flaps.move_s() <= cfg_.tracker.decide_x)
                warn = "  <- WARNING: too fast for the gates (parts decided at that speed reach a gate too late)";
        }
        printf("[run] belt adapt: slower (>= %.0f mm/s) while the arm is busy and the next OK part is on its way; %s%s\n",
               ad.min_mm_s, ad.fast_mm_s > 0 ? fmt("%.0f mm/s while nothing past the decision line is handled",
                                                   ad.fast_mm_s).c_str() : "no fast mode", warn.c_str());
    }
    if (cfg_.picker.inner_on())
        printf("[run] arm: round parts at y >= %.0f are picked with the wrist at %.0f deg (turned above the part), "
               "reach up to y %.0f\n", cfg_.picker.inner_y, cfg_.picker.inner_q5, cfg_.picker.reach_y1);
    if (cfg_.tracker.size.enabled && cfg_.tracker.size.unjudged_recheck && cfg_.tracker.size.unjudged_pick_conf > 0)
        printf("[run] parts too far off the camera axis to judge the teeth: picked on the class alone at confidence "
               ">= %.2f (no size / spec check there)\n", cfg_.tracker.size.unjudged_pick_conf);
    prev_status_ = des_step_status(hw_);
    belt_.enable(true);
    arm_thread_ = std::thread(&Runner::arm_loop, this);
    set_state(can_fd_ >= 0 ? CANP_ST_IDLE : CANP_ST_RUN, can_fd_ >= 0 ? "等待 PLC 启动" : "运行（没有 CAN）");
    return "";
}
// Frames written back to back can be lost by the CAN controller (soak 10-11, loopback: 1000 of 108691, 79 of 82 lost
// STATUS frames in seconds with bursts of PART frames; spaced sends at <= 2000 frames/s lose nothing). So every frame
// goes through a queue drained one frame per cycle (60 frames/s; normal load ~15 frames/s, a burst of parts is spread
// over a few hundred ms). ACKs have their own queue that goes first: they still leave at the end of the cycle that
// received the command. (10-23, real bus with the USB-CAN: ACKs written right away + the queued frame of the same
// cycle lost 34 of 1067 STATUS frames with a PLC command every 0.5 s, none without commands.)
void Runner::send(const canp_frame_t &f)
{
    if (can_fd_ < 0) return;
    if (txq_.size() >= 256) {   // PLC away / bus off: keep the newest
        txq_.pop_front(); tx_dropped_++;
    }
    txq_.push_back(f);
}
void Runner::send_ack(const canp_frame_t &f)
{
    if (can_fd_ < 0) return;
    if (ackq_.size() >= 32) {   // commands flooding faster than one frame per cycle: keep the newest
        ackq_.pop_front(); tx_dropped_++;
    }
    ackq_.push_back(f);
}
void Runner::flush_tx()
{
    std::deque<canp_frame_t> &q = !ackq_.empty() ? ackq_ : txq_;
    if (can_fd_ < 0 || q.empty()) return;
    const int r = can_try_send(can_fd_, &q.front());   // never blocks the worker (no PLC on the bus: queue full)
    if (r == 1) return;                                 // try again next cycle
    if (r < 0) tx_dropped_++;
    q.pop_front();
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
        if (st == CANP_ST_IDLE || st == CANP_ST_PAUSED) set_state(CANP_ST_RUN, (why + "启动").c_str());
        else if (st != CANP_ST_RUN) return CANP_ACK_REJECTED;
        return CANP_ACK_OK;
    case CANP_CMD_STOP:
        jog_dir_ = 0; set_state(CANP_ST_IDLE, (why + "停止").c_str());
        return CANP_ACK_OK;
    case CANP_CMD_PAUSE:
        if (st == CANP_ST_RUN) set_state(CANP_ST_PAUSED, (why + "联锁暂停").c_str());
        else if (st != CANP_ST_PAUSED) return CANP_ACK_REJECTED;
        return CANP_ACK_OK;
    case CANP_CMD_RESUME:
        if (st != CANP_ST_PAUSED) { return CANP_ACK_REJECTED; } set_state(CANP_ST_RUN, (why + "恢复").c_str());
        return CANP_ACK_OK;
    case CANP_CMD_RESET_ALARM:
        // v11: the PL keeps the e-stop latched (stepper held) until cleared; it refuses while the button is pressed
        // (older bitstreams: des_estop_clear answers DES_E_ARG, the alarm still clears)
        if (des_estop_clear(hw_) == 1) { return CANP_ACK_REJECTED; } alarm_ = 0;
        if (st == CANP_ST_ESTOP || st == CANP_ST_FAULT) { set_state(CANP_ST_IDLE, "报警已复位"); } return CANP_ACK_OK;
    default: return CANP_ACK_UNKNOWN;
    }
}
uint8_t Runner::set_speed(double mm_s, const char *who)
{
    if (mm_s <= 0 || mm_s > 200) return CANP_ACK_BAD_VALUE;
    belt_.set_speed(mm_s);
    speed_mm_s_ = mm_s;
    adapt_v_ = mm_s; adapt_mode_ = AdaptMode::Base;   // the adaptation starts over from the new set speed
    last_event_ = fmt("%s 设带速 %.1f mm/s", who, mm_s);
    return CANP_ACK_OK;
}
uint8_t Runner::jog(int dir, const char *who)
{
    if (dir == 0) { jog_dir_ = 0; return CANP_ACK_OK; }
    if (state_ != CANP_ST_IDLE) return CANP_ACK_REJECTED;   // running / paused / e-stop: no manual moves
    if (des_estop_latched(hw_) == 1) return CANP_ACK_REJECTED;   // < DES_VERSION_IO: DES_E_ARG != 1, same verdict
    jog_dir_ = dir > 0 ? 1 : -1;
    last_event_ = std::string(who) + (jog_dir_ > 0 ? " 皮带正转点动" : " 皮带反转点动");
    return CANP_ACK_OK;
}
uint8_t Runner::select_recipe(int id, const char *who)
{
    const Recipe *r = find_recipe(cfg_.recipes, id);
    if (!r) return CANP_ACK_BAD_VALUE;
    if (state_ == CANP_ST_RUN) return CANP_ACK_REJECTED;   // changeover only with the line stopped / paused
    pending_recipe_ = id;
    last_event_ = std::string(who) + " 换配方 " + std::to_string(id) + " " + r->name;
    return CANP_ACK_OK;
}
uint8_t Runner::set_lamp(uint8_t mode, uint8_t lamps, const char *who)
{
    if (mode > 1 || (lamps & 0xF0)) return CANP_ACK_BAD_VALUE;
    if (!rs485_) return CANP_ACK_REJECTED;   // no RS485 bus running
    rs485_->set_override(mode == 1, lamps);
    last_event_ = mode ? fmt("%s 三色灯手动 %X（RS485）", who, lamps) : std::string(who) + " 三色灯回到跟随线体状态";
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
        uint8_t result = CANP_ACK_OK; switch (f.id) {
        // short frames ACK BAD_VALUE with seq 0 (can_recv copies all 8 bytes: anything past f.len is stale bus
        // garbage, so the PLC could not match the echoed seq anyway)
        case CANP_ID_CMD: result = f.len < 2 ? uint8_t(CANP_ACK_BAD_VALUE) : command(f.data[0], "PLC");
            send_ack(canp_ack(f.id, f.len < 2 ? 0 : f.data[1], result)); break;
        case CANP_ID_SPEED: result = f.len < 3 ? uint8_t(CANP_ACK_BAD_VALUE) : set_speed(canp_get16(f.data) / 10.0, "PLC");
            send_ack(canp_ack(f.id, f.len < 3 ? 0 : f.data[2], result)); break;
        case CANP_ID_RECIPE: result = f.len < 2 ? uint8_t(CANP_ACK_BAD_VALUE) : select_recipe(f.data[0], "PLC");
            send_ack(canp_ack(f.id, f.len < 2 ? 0 : f.data[1], result)); break;
        case CANP_ID_LAMP:   // CAN -> RS485: the relay module gets the PLC's lamps
            result = f.len < 3 ? uint8_t(CANP_ACK_BAD_VALUE) : set_lamp(f.data[0], f.data[1], "PLC");
            send_ack(canp_ack(f.id, f.data[2], result)); break;
        case CANP_ID_PLC_HB: last_plc_hb_ = now; break;
        case CANP_ID_TIME: {   // the PLC's wall clock (no RTC here): records use our clock + this offset
            const int64_t ms = canp_time_ms(&f);
            if (f.len < 6 || ms < 1700000000000LL) { last_event_ = "PLC 对时帧无效，已忽略"; break; }   // as Modbus
            const bool first = !wall_synced(); wall_apply_ms(ms, "PLC");
            if (first) { last_event_ = "已从 PLC 对时"; } break;
        }
        default: break;
        }
    }
}
// Modbus-TCP writes (hw/modbus_tcp.hpp): the server thread only queues them; executed here with the CAN rules
void Runner::handle_modbus(double now)
{
    if (!mb_) return;
    const uint64_t n = mb_->requests();
    if (n != mb_requests_) { mb_requests_ = n; last_mb_ = now; }
    ModbusTcp::Write w[32];
    size_t got;
    while ((got = mb_->take_writes(w, sizeof w / sizeof w[0])) > 0) {
        for (size_t i = 0; i < got; i++) {
            uint8_t r = CANP_ACK_OK; const uint16_t v = w[i].value;
            switch (w[i].addr) {
            case MB_HR_CMD: r = v <= 0xFF ? command(uint8_t(v), "Modbus") : uint8_t(CANP_ACK_UNKNOWN); break;
            case MB_HR_SPEED: r = set_speed(v / 10.0, "Modbus"); break;
            case MB_HR_RECIPE: r = select_recipe(v, "Modbus"); break;
            case MB_HR_LAMP:   // Ethernet -> RS485
                r = (v & 0xFEF0) ? uint8_t(CANP_ACK_BAD_VALUE) : set_lamp(uint8_t(v >> 8), uint8_t(v & 0x0F), "Modbus"); break;
            case MB_HR_WO_ACK:   // acknowledge work order v (0xFFFF = all); the worker applies it (app/ops.hpp)
                if (v == 0) { r = CANP_ACK_BAD_VALUE; break; }
                if (ops_acks_.size() < 64) ops_acks_.push_back(v);
                r = CANP_ACK_OK;
                break;
            case MB_HR_TIME_HI: mb_time_hi_ = v; continue;   // no RESULT/SEQ here (documented: "一次 FC 16 写两个，写低字时生效") — one ACK per clock set, so a split hi/lo write completes only after the low word applies
            case MB_HR_TIME_LO: {
                const int64_t unix_s = (int64_t(mb_time_hi_) << 16) | v; if (unix_s < 1700000000) { r = CANP_ACK_BAD_VALUE; break; }   // before 2023: not a clock
                const bool first = !wall_synced(); wall_apply_ms(unix_s * 1000, "Modbus");
                if (first) { last_event_ = "已从 Modbus 对时"; } r = CANP_ACK_OK;
                break;  }
            default: r = CANP_ACK_REJECTED; break;   // RESULT / SEQ / spare: read only
            }
            mb_->set_holding(MB_HR_RESULT, r); mb_->set_holding(MB_HR_SEQ, ++mb_seq_);
        }
    }
}
uint8_t Runner::status_flags(double now) const
{
    return uint8_t((belt_.stopped() ? 0 : CANP_FLAG_BELT_MOVING) | (arm_busy_ ? CANP_FLAG_ARM_BUSY : 0) |
                   (alarm_ ? CANP_FLAG_ALARM : 0) |
                   (now - last_plc_hb_ < 1.0 || now - last_mb_ < 1.0 ? CANP_FLAG_PLC_LINK : 0) |
                   (cfg_.calib_loaded.empty() ? 0 : CANP_FLAG_CALIBRATED) |
                   (rs485_ && rs485_->link_ok() ? CANP_FLAG_RS485 : 0) | (ops_.active ? CANP_FLAG_OPS : 0));
}
// read-back values (holding) and status (input registers), every cycle
void Runner::update_modbus(double now)
{
    if (!mb_) return;
    const double set = speed_mm_s_ > 0 ? speed_mm_s_ : cfg_.belt.run_speed;
    mb_->set_holding(MB_HR_CMD, 0);
    mb_->set_holding(MB_HR_SPEED, canp_mm_s_x10(set));
    mb_->set_holding(MB_HR_RECIPE, uint16_t(active_recipe_));
    auto hi = [](uint32_t v) { return uint16_t(v >> 16); };
    auto lo = [](uint32_t v) { return uint16_t(v & 0xFFFF); };
    std::fill(ir_.begin(), ir_.end(), uint16_t(0));   // the image is reused: start from "all zero" like a fresh vector
    ir_[MB_IR_STATE] = state_.load();
    ir_[MB_IR_FLAGS] = status_flags(now);
    const uint32_t cnt[4] = {cnt_.total, cnt_.ok, cnt_.ng, cnt_.recheck};
    for (int k = 0; k < 4; k++) { ir_[MB_IR_TOTAL + 2 * k] = hi(cnt[k]); ir_[MB_IR_TOTAL + 2 * k + 1] = lo(cnt[k]); }
    ir_[MB_IR_PART_SEQ] = part_seq_;
    ir_[MB_IR_PART_ID] = last_part_.id;
    ir_[MB_IR_PART_CLS] = last_part_.cls == 0xFF ? 0xFFFF : last_part_.cls;
    ir_[MB_IR_PART_DISP] = last_part_.disp;
    ir_[MB_IR_PART_DEST] = last_part_.dest;
    ir_[MB_IR_PART_SIZE] = uint16_t(std::min<uint32_t>(60000u, last_part_.size_x100));
    ir_[MB_IR_PART_CONF] = last_part_.conf;
    ir_[MB_IR_SPEED] = canp_mm_s_x10(belt_.speed_mm_s());
    ir_[MB_IR_HB] = mb_hb_++;
    ir_[MB_IR_UPTIME] = uint16_t(uint32_t(now - t0_));
    ir_[MB_IR_ALARM] = alarm_;
    ir_[MB_IR_RECIPE] = uint16_t(active_recipe_);
    ir_[MB_IR_VERSION] = MB_MAP_VERSION;
    mb_->set_holding(MB_HR_LAMP, rs485_ && rs485_->override_on() ? uint16_t(0x100 | rs485_->override_lamps()) : 0);
    mb_->set_holding(MB_HR_WO_ACK, 0);
    ir_[MB_IR_WO_ACTIVE] = ops_.active;
    ir_[MB_IR_WO_UNACKED] = ops_.unacked;
    ir_[MB_IR_WO_LEVEL] = ops_.level;
    ir_[MB_IR_WO_LAST_ID] = ops_.last_id;
    ir_[MB_IR_WO_LAST_CODE] = ops_.last_code;
    ir_[MB_IR_WO_SEQ] = ops_.seq;
    if (rs485_) {
        const Rs485Io::Stats s = rs485_->stats();
        ir_[MB_IR_485_FLAGS] = uint16_t(1 | (rs485_->link_ok() ? 2 : 0) | (s.override_on ? 4 : 0));
        ir_[MB_IR_485_LAMPS] = coils_to_lamps(s.coils_rb, cfg_.rs485);
        ir_[MB_IR_485_INPUTS] = uint16_t(s.inputs);
        ir_[MB_IR_485_OK] = uint16_t(s.ok);
        ir_[MB_IR_485_ERR] = uint16_t(s.timeouts + s.crc + s.rx_err + s.exceptions + s.bad_reply + s.other);
        ir_[MB_IR_485_RESP] = uint16_t(s.resp_last > 0 ? std::min(65535.0, s.resp_last * 100) : 0);
    }
    mb_->set_inputs(0, ir_);
}
void Runner::cycle(Tracker &tr, Picker &pk)
{
    const double now = mono_s();
    handle_can(now);
    handle_modbus(now);
    // e-stop. v11: des_io latches it (debounced) and holds the stepper until RESET_ALARM clears it -> seen in any
    // state. Older bitstreams: only the stepper's HALTED edge while running (missed when idle or stopped).
    const uint32_t st = des_step_status(hw_);
    const bool estop = hw_->version >= DES_VERSION_IO
                           ? des_estop_latched(hw_) == 1
                           : (st & DES_ST_STATUS_HALTED) && !(prev_status_ & DES_ST_STATUS_HALTED) && state_ == CANP_ST_RUN;
    if (estop && state_ != CANP_ST_ESTOP) {
        alarm_ = 1; set_state(CANP_ST_ESTOP, hw_->version >= DES_VERSION_IO ? "急停按下（PL 已锁存）" : "急停：步进已停");
        send(canp_alarm(1, 2, 0));
    }
    prev_status_ = st;
    done_buf_.clear();   // arm results
    if (done_.drain(done_buf_))
        for (const Done &d : done_buf_) {
            pk.job_done(tr, d.id, d.ok); if (d.ok) { cnt_.ok++; picks_.ok++; }
            else {   // the reason in the service log too: bus (SYNC WRITE failed) vs grasp (nothing picked)
                cnt_.recheck++; last_event_ = "抓取失败：" + d.why;
                fprintf(stderr, "[run] track #%d pick failed: %s\n", d.id, d.why.empty() ? "?" : d.why.c_str());
                picks_.failed++; picks_.last_why = d.why;   // servo bus trouble, not a missed part (app/ops.hpp)
                if (d.why.rfind("SYNC WRITE failed", 0) == 0 || d.why.rfind("gripper did not answer", 0) == 0) picks_.servo++;
            }
        }
    // Newly decided part: counts + PART frame (decision time; a later failed pick is counted as recheck)
    auto report_part = [&](const Track &t) {
        if (t.state == TrackState::New || t.state == TrackState::Tracking || t.disp == Disposition::None) return;
        if (cfg_.picker.gates_active() && t.disp == Disposition::Flap && !t.flap_final) return;   // gate not final yet
        if (!reported_.insert(t.id).second) return;
        cnt_.total++; if (t.disp == Disposition::Flap) cnt_.ng++;
        else if (t.disp == Disposition::Recheck) cnt_.recheck++;
        canp_part_t p;
        p.id = uint16_t(t.id); p.cls = uint8_t(t.cls < 0 ? 0xFF : t.cls);
        p.disp = t.disp == Disposition::Pick ? CANP_DISP_PICK
                 : t.disp == Disposition::Flap ? CANP_DISP_FLAP
                                               : CANP_DISP_RECHECK; p.dest = uint8_t(t.dest);
        p.conf = uint8_t(std::min(100.f, t.cls_conf * 100)); p.size_x100 = canp_mm_x100(t.meas.valid ? t.meas.size : 0);   // gear OD / rack length; 0 = not measured
        send(canp_part(&p)); last_part_ = p;
        part_seq_++;
    };
    if (wrist_dirty_) {   // ready pose re-taught: round parts close along its wrist line from now on
        float rel, tau;
        arm_home_wrist(arm_cfg_, rel, tau);
        if (pk.config().keep_wrist) pk.set_wrist(rel, tau);
        wrist_dirty_ = false;
    }
    if (state_ == CANP_ST_RUN) {
        jog_dir_ = 0; Picker::Input in = picker_input(&belt_, hw_, cfg_.belt);
        in.cruise_mm_s = speed_mm_s_ > 0 ? speed_mm_s_ : cfg_.belt.run_speed;
        in.arm_idle = !arm_busy_; in.arm_clear = arm_clear_;
        const Picker::Output &out = pk.tick(tr, in); plan_ = out.belt;   // ref into the picker: no per-cycle flaps copy
        // one pass over the tracks: speed adaptation (nearest decided pick ahead) + newly decided parts
        double nearest_pick = -1; for (const Track &t : tr.tracks()) {
            if (t.state == TrackState::Decided && t.disp == Disposition::Pick) {
                const double dd = pk.config().pick_x - t.x_at(in.belt_mm); if (dd > 0 && (nearest_pick < 0 || dd < nearest_pick)) nearest_pick = dd;
            }
            report_part(t);
            // why a decided "pick" part is dropped (10-30: a part stayed "decided" and the belt never stopped for it)
            if (t.disp == Disposition::Recheck && recheck_log_.insert(t.id).second)
                fprintf(stderr, "[run] track #%d recheck: %s (x %.0f, y %.0f)\n", t.id, t.why, double(t.x_at(in.belt_mm)), double(t.y));
        }
        // speed adaptation: slower while the arm is busy (fewer full stops; the StopAt still comes from the picker),
        // faster while nothing past the decision line is being handled (fast mode)
        if (cfg_.belt.adapt.enabled) {
            const double base = speed_mm_s_ > 0 ? speed_mm_s_ : cfg_.belt.run_speed; const double eta = arm_eta_.load();
            const bool quiet = cfg_.belt.adapt.fast_mm_s > 0 && !arm_busy_ && arm_clear_ &&
                               belt_quiet(tr, out.flaps, in.belt_mm, flaps_.state());
            AdaptMode mode = AdaptMode::Base;
            const double v = adapt_speed(cfg_.belt.adapt, base, nearest_pick, arm_busy_ && eta > now ? eta - now : 0,
                                         quiet, &mode);
            if (std::fabs(v - adapt_v_) > cfg_.belt.adapt.change_frac * base) {
                belt_.set_speed(v); adapt_v_ = v; adapt_mode_ = mode;
                adapt_changes_++;
            }
        }
        belt_.apply(out.belt);
        const bool want_pid = pid_switch_.load(std::memory_order_relaxed);   // HMI 参数页「带速 PI」开关
        if (want_pid != pid_on_) {
            pid_on_ = want_pid;
            if (pl_pid_) pl_pid_enable(want_pid);
            else {
                pid_.set_enabled(want_pid);
                if (!want_pid) pid_off_restore_ = true;
            }
        }
        if (pl_pid_ && pid_on_) {
            // PL loop (des_bpi): keep its set-point on the cruise speed the belt is meant to hold. Ramps, stops at the
            // pick line and speed changes need nothing here: the PL only steps on windows that lay inside the cruise.
            const double base = cruise_base();
            if (std::fabs(base - pl_set_) > 1e-6) {
                des_bpi_set(hw_, base / cfg_.belt.enc_scale());
                pl_set_ = base;
            }
            if (now - pid_log_t_ >= 1.0) {   // 1 Hz: panel value + tuning / demo line
                des_bpi_t b;
                if (des_bpi_read(hw_, &b) == DES_OK) {
                    pl_scale_.store(b.scale);
                    fprintf(stderr, "[pid] PL set %.1f meas %.2f mm/s scale %+.2f %%%s%s\n", base,
                            b.meas_cps * cfg_.belt.enc_scale(), b.scale * 100, b.status & DES_BPI_ST_SAT ? " LIMIT" : "",
                            b.status & DES_BPI_ST_STALL ? " STALL" : "");
                }
                pid_log_t_ = now;
            }
        }
        // Optional PS-side belt-speed PI (des_app.json belt.pid, default off): trim the cruise speed from the
        // measuring-wheel encoder. Only while cruising: a stop-at-the-pick-line plan and the startup ramp are left
        // to the PL profile so the stop timing does not change.
        if (pl_pid_) {
            // nothing: the PS loop below is not used with the PL loop
        } else if (pid_on_ && state_ == CANP_ST_RUN && out.belt.kind == BeltPlan::Run && enc_ok_ &&
            !belt_.stopped()) {
            const double base = (cfg_.belt.adapt.enabled && adapt_v_ > 0) ? adapt_v_
                                : (speed_mm_s_ > 0 ? speed_mm_s_ : cfg_.belt.run_speed);
            if (belt_.speed_mm_s() < 0.95 * base) {
                pid_.reset();   // still ramping up after a start: let the PL profile finish first
                enc_t_ = -1;
            } else {
                if (enc_ok_) {   // 100 ms count window: 0.49 mm/s resolution instead of the 4.9 mm/s of ENC_SPEED
                    if (enc_t_ < 0) {
                        enc_last_ = enc_cnt_;
                        enc_t_ = now;
                    } else if (now - enc_t_ >= 0.1) {
                        const double dt = now - enc_t_;
                        enc_actual_ = std::fabs(double(enc_cnt_ - enc_last_)) * cfg_.belt.enc_scale() / dt;
                        enc_last_ = enc_cnt_;
                        enc_t_ = now;
                    }
                }
                const double actual = enc_actual_;
                const uint64_t before = pid_.updates();
                const double trim = pid_.update(base, actual, now);
                if (pid_.updates() != before) {
                    if (std::fabs(trim) > 0.005) belt_.set_speed(base + trim);
                    if (now - pid_log_t_ >= 1.0) {   // 1 Hz tuning / demo line (only when the loop is on)
                        fprintf(stderr, "[pid] base %.1f actual %.1f trim %+.2f mm/s\n", base, actual, trim);
                        pid_log_t_ = now;
                    }
                }
            }
        } else {
            pid_.reset();
            enc_t_ = -1;
            enc_actual_ = 0;
            if (pid_off_restore_) {   // switched off while cruising: drop the trim, back to the operator/adapt base
                if (out.belt.kind == BeltPlan::Run && !belt_.stopped()) {
                    const double base = (cfg_.belt.adapt.enabled && adapt_v_ > 0) ? adapt_v_
                                        : (speed_mm_s_ > 0 ? speed_mm_s_ : cfg_.belt.run_speed);
                    belt_.set_speed(base);
                }
                pid_off_restore_ = false;
            }
        }
        flap_events_ = out.flaps;   // the gates follow them below (capacity reused: no allocation per cycle)
        if (out.have_job) {
            ArmJob job{out.job, arm_cfg_}; arm_busy_ = true;
            arm_clear_ = out.job.flying;   // intercept: the belt keeps running while the gripper is down
            jobs_.push_force(std::move(job));
        }
    } else if (state_ == CANP_ST_IDLE && jog_dir_ != 0) {
        flap_events_.clear();
        belt_.jog(jog_dir_);   // manual jog from the HMI (Runner::jog)
        for (const Track &t : tr.tracks()) report_part(t);
    } else {
        if (state_ != CANP_ST_IDLE) { jog_dir_ = 0; } BeltPlan hold;   // not running: ramp the belt down if it moves; a pick in progress finishes on its own
        hold.kind = BeltPlan::Hold; belt_.apply(hold);
        for (const Track &t : tr.tracks()) report_part(t);
        flap_events_.clear();
    }
    // side diverter gates (des_pwm channel = gate), in every state: open from the start, a gate closed for a part
    // keeps it until the belt has carried that part past its release (no-op before v7 or when disabled)
    if (cfg_.picker.gates_active() && !flap_manual_) {
        const FlapSequencer::State &fs = flaps_.update(flap_events_, belt_.belt_mm(), now);
        const int n = int(std::min<size_t>(cfg_.picker.gates.size(), 2));
        for (int ch = 0; ch < n; ch++)
            if (fs.us[ch] != flap_us_[ch]) {
                des_pwm_set(hw_, ch, fs.us[ch]);
                flap_us_[ch] = fs.us[ch];
            }
        if (!pwm_enabled_) pwm_enabled_ = des_pwm_enable(hw_, n == 2 ? 3u : 1u) == DES_OK;
    }
    while (reported_.size() > 2000) reported_.erase(reported_.begin());
    if (now - last_status_ >= 0.1) {
        canp_status_t s; s.state = state_;
        s.flags = status_flags(now); s.speed_x10 = canp_mm_s_x10(belt_.speed_mm_s());
        s.alarm = alarm_; s.hb = hb_++;
        s.uptime_s = uint16_t(now - t0_); send(canp_status(&s));
        last_status_ = now;
    }
    if (now - last_counts_ >= 1.0) {
        const canp_counts_t c{uint16_t(cnt_.total), uint16_t(cnt_.ok), uint16_t(cnt_.ng), uint16_t(cnt_.recheck)};
        send(canp_counts(&c)); last_counts_ = now;
    }
    {   // the tower light follows the line; yellow joins green when a PLC that was there went quiet, or while a
        // warning work order is not acknowledged (app/ops.hpp)
        const bool link = now - last_plc_hb_ < 1.0 || now - last_mb_ < 1.0;   // (PlcWatch: >= 3 s of it = there)
        plc_lost_ = plc_watch_.update(now, link, state_ == CANP_ST_IDLE);
        if (rs485_) rs485_->set_line(state_, alarm_ != 0, plc_lost_ || ops_.warn);
    }
    flush_tx();
    update_modbus(now);
}
std::string Runner::status_line() const
{
    char q[48] = "";
    if (txq_.size() > 4 || tx_dropped_)
        snprintf(q, sizeof q, "CAN 发送排队 %zu 丢弃 %llu", txq_.size(), (unsigned long long)tx_dropped_);
    const double t = mono_s();
    // operator panel line (2026-10-31): connections / arm / last event only — state and belt speed live on the
    // panel's run-control chip, the counts on the panel and 总览; the rest was duplicated there word for word.
    // Items joined with " · " (absent subsystems leave no gap), so the panel wrap lands between items.
    std::string r = fmt("机械臂 %s", arm_busy_ ? "忙" : "空闲");
    if (can_fd_ >= 0) r += t - last_plc_hb_ < 1.0 ? " · PLC 在线" : " · PLC 未连";
    if (mb_) r += t - last_mb_ < 1.0 ? " · Modbus 在线" : " · Modbus 未连";
    if (rs485_) r += rs485_->link_ok() ? " · RS485 在线" : " · RS485 断开";
    if (pid_on_) r += pl_pid_ ? fmt(" · PL PI %+.1f%%", pl_scale_.load() * 100) : fmt(" · PI %+.1f mm/s", pid_.last_trim());
    if (q[0]) r += std::string(" · ") + q;
    if (!last_event_.empty()) r += " · " + last_event_;
    return r;
}
void Runner::ops_fault(uint8_t code, const char *why)
{
    if (state_ == CANP_ST_ESTOP || state_ == CANP_ST_FAULT) return;   // already held
    jog_dir_ = 0;
    alarm_ = code;
    set_state(CANP_ST_FAULT, why);   // cycle() holds the belt in every state but RUN; 复位报警 -> IDLE
}
void Runner::ops_alarm(uint8_t code, uint8_t level, uint16_t id)
{
    if (code == OPS_ESTOP) return;   // the e-stop already sends its ALARM (protocol v1: code 1, severity 2)
    send(canp_alarm(code, level, id));
}
bool Runner::take_ops_ack(uint16_t &id)
{
    if (ops_acks_.empty()) return false;
    id = ops_acks_.front();
    ops_acks_.pop_front();
    return true;
}
void Runner::set_flap_manual(bool on)
{
    if (flap_manual_ && !on) {   // handed back: the sequencer writes its widths and the enable again
        flap_us_[0] = flap_us_[1] = -1;
        pwm_enabled_ = false;
    }
    flap_manual_ = on;
}
void Runner::set_home(const std::array<int, 6> &counts)
{
    for (int k = 0; k < 5; k++) {
        const JointCal &jc = arm_cfg_.joint[k]; arm_cfg_.home_deg[k] = (counts[k] - jc.zero_count) / jc.counts_per_deg;
    }
    arm_cfg_.home_grip_count = float(counts[5]);
    wrist_dirty_ = true;
}
void Runner::arm_loop()
{
    for (;;) {
        ArmJob job; if (!jobs_.pop(job)) return;   // closed: Runner is going away
        const ArmConfig &ac = job.ac; std::vector<ServoFrame> wp;
        Done d{job.p.track_id, false, ""}; float tilt = 0;
        if (opt_.arm_go) {   // where the arm really is (moved by hand, reset, a failed pick) - plan_pick checks it
            std::array<uint16_t, 6> now; if (arm_.read_positions(now)) arm_counts_ = now;
        }
        const char *why = plan_pick(ac, job.p.p, job.p.close_axis, job.p.open_mm, job.p.part_wid, job.p.bin, arm_counts_,
                                    wp, &tilt, job.p.wrist_deg);   // inner picks: the wrist the user showed
        if (why && !job.p.flying && !std::isnan(job.p.alt_axis)) {   // round part out of the vertical reach along
            const char *why2 = plan_pick(ac, job.p.p, job.p.alt_axis, job.p.open_mm, job.p.part_wid, job.p.bin,
                                         arm_counts_, wp, &tilt);   // the wrist line: tangential, tilted
            if (!why2) why = nullptr;
        }
        if (!why && job.p.flying) {   // intercept: vertical only; the close frame waits for the part
            if (tilt != 0) { why = "intercept point needs a tilted grasp"; } for (ServoFrame &f : wp)
                if (!strcmp(f.what, "close")) { f.wait_belt = true; f.belt_mm = job.p.arrive_belt_mm; }
        }
        if (!why) {   // whole pick incl. the way back to the ready pose (speed adaptation)
            double ms = 0; for (const ServoFrame &f : wp) ms += f.ms;
            arm_eta_ = mono_s() + ms / 1000.0;
        }
        if (why) {
            d.why = why;
        } else {
            auto clear = [this] { arm_clear_ = true; };
            // intercept close: wait until the part's centre is lead_s x speed short of the pick line; give up when
            // the line leaves RUN / PAUSED or the part is overdue (belt stopped for good)
            const double lead_s = cfg_.picker.flying_close_lead_s; auto wait = [this, lead_s](double arrive) {
                const double t_start = mono_s(); for (;;) {
                    if (belt_.belt_mm() >= arrive - belt_.speed_mm_s() * lead_s) { return true; } const uint8_t st = state_;
                    if (st != CANP_ST_RUN && st != CANP_ST_PAUSED) { return false; } if (mono_s() - t_start > 10.0) return false;
                    usleep(2000);  }
            }; ArmExec::Result r = opt_.arm_go ? arm_.run(wp, false, clear, wait) : arm_.simulate(wp, clear, wait);
            d.ok = r.ok && r.grasped; d.why = r.why;
            arm_counts_ = wp.back().pos;
        }
        done_.push(std::move(d)); arm_clear_ = true;
        arm_eta_ = 0; arm_busy_ = false;
    }
}
}  // namespace des
