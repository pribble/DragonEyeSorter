// runner.hpp — production control for `des_app run`: line state machine, belt (BeltDriver), arm (ArmExec on its own
// thread), CAN link to the "PLC" (hw/can_proto.h), Modbus-TCP slave (hw/modbus_tcp.hpp, same rules). Driven from the
// live worker thread once per processed frame.
//
// States: IDLE (belt stopped, nothing dispatched) / RUN / PAUSED (PLC interlock or operator; current pick finishes) /
// ESTOP (PL e-stop latched the stepper; needs RESET_ALARM then START). Without CAN the line starts in RUN.
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include "arm_io.hpp"
#include "belt_ctl.hpp"
#include "bg_queue.hpp"
#include "can_proto.h"
#include "config.hpp"
#include "modbus_tcp.hpp"
#include "picker.hpp"
#include "tracker.hpp"
namespace des {
struct RunOptions {
    std::string can_if;      // "" = no CAN (start in RUN)
    bool arm_go = false;     // false: simulate arm timing without bus frames
    double speed_mm_s = 0;   // 0 = config belt.run_speed
};
class Runner {
public:
    Runner(des_hw_t *hw, const AppConfig &cfg, const RunOptions &opt);
    ~Runner();
    std::string start();
    void cycle(Tracker &tr, Picker &pk);   // worker thread, ~60 Hz
    std::string status_line() const;      // for the HDMI overlay (only called on the 10 Hz panel tick)
    uint8_t command(uint8_t cmd, const char *who);   // operator / PLC commands (same rules; worker). CANP_ACK_*
    uint8_t set_speed(double mm_s, const char *who);
    // live worker: encoder count this cycle (des_io ENC_CNT) for the optional belt PI
    void set_encoder(int32_t cnt, bool ok) { enc_cnt_ = cnt; enc_ok_ = ok; }
    // HMI parameter page: runtime belt-PI switch (default from cfg.belt.pid.enabled; not persisted)
    void set_pid_enabled(bool on);
    // manual belt jog (HMI): +1 / -1 / 0; only while the line is stopped (IDLE) and the e-stop is not latched
    uint8_t jog(int dir, const char *who);
    int jog_dir() const { return jog_dir_; }
    // recipe changeover: known id and the line not running; the worker picks it up with take_recipe()
    uint8_t select_recipe(int id, const char *who);
    int take_recipe();                      // id accepted since the last call, -1 = none
    uint8_t state() const { return state_; }
    const BeltPlan &belt_plan() const { return plan_; }   // the picker's last belt plan (valid while RUN)
    // speed adaptation now (worker thread, like belt_plan): mode and the cruise speed it set
    AdaptMode adapt_mode() const { return adapt_mode_; }
    double adapt_speed_set() const { return adapt_v_; }
    bool arm_busy() const { return arm_busy_; }   // a pick is being executed (the manual arm page waits)
    void set_home(const std::array<int, 6> &counts);   // HMI arm page: ready pose of every following pick
    // HMI flap page unlocked: the gates are driven by hand, leave the PWM alone; handing them back rewrites them
    void set_flap_manual(bool on);
    double speed_mm_s() const { return speed_mm_s_; }
    void set_active_recipe(int id) { active_recipe_ = id; }   // for the Modbus read-back (the worker applies recipes)
    // tower light on RS485 (CAN LAMP / Modbus HR 7): mode 1 = these lamps (LAMP_*), 0 = follow the line state
    uint8_t set_lamp(uint8_t mode, uint8_t lamps, const char *who);
    // work orders (app/ops.hpp, fault detection in the live worker): a stop-level fault holds the line like an alarm
    // (FAULT: belt held, red lamp + buzzer, 复位报警 to leave); every new order goes to the PLC as an ALARM frame
    void ops_fault(uint8_t code, const char *why);
    void ops_alarm(uint8_t code, uint8_t level, uint16_t id);
    struct OpsRegs {   // summary for Modbus IR 32..37 / STATUS flag / yellow lamp
        uint16_t active = 0, unacked = 0, level = 0, last_id = 0, last_code = 0, seq = 0;
        bool warn = false;   // a warning nobody acknowledged: yellow joins the line state
    };
    void set_ops(const OpsRegs &r) { ops_ = r; }
    bool take_ops_ack(uint16_t &id);    // Modbus HR 8 writes (0xFFFF = all), oldest first
    bool plc_lost() const { return plc_lost_; }   // a PLC that was there went quiet (PlcWatch)
    int rs485_link() const { return rs485_ ? (rs485_->link_ok() ? 1 : 0) : -1; }   // -1 bus not running
    int env_link() const { return rs485_ ? rs485_->th_link() : -1; }               // -1 no transmitter configured
    bool env_reading(float &temp_c, float &hum_rh) const                           // newest temperature / humidity
    {
        if (!rs485_) return false;
        const Rs485Io::Stats s = rs485_->stats();
        temp_c = s.temp_c; hum_rh = s.hum_rh;
        return s.th_valid;
    }
    struct PickStats { uint32_t ok = 0, failed = 0, servo = 0; std::string last_why; };
    const PickStats &pick_stats() const { return picks_; }   // worker thread (cycle drains the arm results)
private:
    // arm thread hand-off: the job carries the ready pose it was planned with, so the config is only ever touched
    // by the worker thread (no lock around it).
    struct ArmJob { PickJob p; ArmConfig ac; };
    struct Done { int id; bool ok; std::string why; };
    void arm_loop();
    void handle_can(double now); void handle_modbus(double now); void update_modbus(double now);
    uint8_t status_flags(double now) const;   // CANP_FLAG_* of both the STATUS frame and the Modbus register
    void set_state(uint8_t s, const char *why);
    void send(const canp_frame_t &f);        // queued: at most one frame per cycle (see flush_tx)
    void send_ack(const canp_frame_t &f);    // ACKs: own queue, sent before txq_ (still one frame per cycle)
    void flush_tx();
    des_hw_t *hw_;
    const AppConfig &cfg_;
    RunOptions opt_;
    BeltDriver belt_;
    ArmExec arm_;
    ArmConfig arm_cfg_;          // worker thread only: copied into every ArmJob
    bool wrist_dirty_ = false;   // set_home changed #5: the picker's wrist line follows in the next cycle
    int can_fd_ = -1;
    std::deque<canp_frame_t> txq_, ackq_;
    uint64_t tx_dropped_ = 0;
    std::atomic<uint8_t> state_{CANP_ST_IDLE};
    uint8_t alarm_ = 0, hb_ = 0;
    uint32_t prev_status_ = 0;
    double t0_ = 0, last_status_ = 0, last_counts_ = 0, last_plc_hb_ = -1e9;
    struct { uint32_t total = 0, ok = 0, ng = 0, recheck = 0; } cnt_;   // 32 bit (Modbus IR 2..9 reads hi/lo); CAN COUNTS truncates to u16 per protocol v1
    std::set<int> reported_;     // track ids already sent as PART
    std::set<int> recheck_log_;  // track ids whose re-check was logged (diagnostic: why a pickable part was dropped)
    std::string last_event_;
    double speed_mm_s_ = 0;      // last set belt speed = the idle speed (0 = idle_default: fast_mm_s, or run_speed)
    int jog_dir_ = 0;            // manual belt jog (IDLE only)
    int pending_recipe_ = -1;    // accepted by select_recipe, not yet applied by the worker
    std::unique_ptr<ModbusTcp> mb_;  // Modbus-TCP slave (cfg.plc.modbus_port > 0)
    std::unique_ptr<Rs485Io> rs485_; // RS485 relay module / tower light (cfg.rs485.enabled and bitstream v16)
    PlcWatch plc_watch_;             // yellow lamp "PLC link lost" (rs485_io.hpp)
    uint64_t mb_requests_ = 0;
    double last_mb_ = -1e9;          // last Modbus request seen (PLC_LINK)
    uint16_t mb_seq_ = 0, mb_time_hi_ = 0, mb_hb_ = 0, part_seq_ = 0;
    canp_part_t last_part_{0, 0xFF, 0, 0, 0, 0};
    int active_recipe_ = 0;
    std::vector<uint16_t> ir_;   // Modbus input-register image, reused every cycle
    FlapSequencer flaps_;
    std::vector<FlapEvent> flap_events_;   // the picker's last flap events (empty when the line is not running)
    BeltPlan plan_;              // the picker's last belt plan (HMI panel; worker thread only)
    int flap_us_[2] = {-1, -1};  // last written pulse widths
    bool pwm_enabled_ = false;
    bool flap_manual_ = false;   // set_flap_manual
    std::thread arm_thread_;
    BgQueue<ArmJob> jobs_{1};    // one pick in flight; the worker only dispatches while arm_busy_ is clear
    BgQueue<Done> done_;
    std::vector<Done> done_buf_; // reused by cycle() to drain done_
    std::atomic<bool> arm_busy_{false}, arm_clear_{true};
    std::atomic<double> arm_eta_{0};   // monotonic s when the running pick will be back at the ready pose
    double adapt_v_ = 0;               // last cruise speed set by the adaptation
    AdaptMode adapt_mode_ = AdaptMode::Base;   // which way that speed went (HMI panel)
    uint64_t adapt_changes_ = 0;
    SpeedPid pid_;                     // optional belt-speed PI (cfg.belt.pid, default off)
    std::atomic<bool> pid_switch_{false};   // HMI sets this; the worker applies it (no cross-thread pid_ access)
    bool pid_on_ = false;              // worker's copy of the runtime switch
    bool pid_off_restore_ = false;     // switched off while cruising: put the commanded speed back to base
    int32_t enc_cnt_ = 0, enc_last_ = 0;   // des_io ENC_CNT, sampled over >=100 ms for a 0.5 mm/s actual speed
    double enc_t_ = -1, enc_actual_ = 0;   // last encoder sample time / measured actual speed (mm/s)
    bool enc_ok_ = false;              // des_io_read succeeded this cycle
    double pid_log_t_ = -1e9;          // 1 Hz [pid] line while the loop is enabled (tuning / demo)
    // PL belt-speed PI (des_bpi, bitstream v17+, cfg.belt.pid.hw): the same switch then drives the PL loop and the
    // PS loop above stays idle. The worker only keeps the set-point current; the PL freezes itself outside cruise.
    bool pl_pid_ = false;
    double pl_set_ = -1;               // set-point last written (mm/s), -1 = none
    std::atomic<double> pl_scale_{0};  // PL output read back at 1 Hz (fraction), for the panel line
    double cruise_base() const         // the cruise speed the belt is meant to hold: adaptation, operator / PLC, config
    {
        return (cfg_.belt.adapt.enabled && adapt_v_ > 0) ? adapt_v_ : handling_mm_s();
    }
    // set speed = idle speed (operator / PLC / Modbus); parts are handled at its handling_speed (picker.hpp)
    double set_mm_s() const { return speed_mm_s_ > 0 ? speed_mm_s_ : idle_default(cfg_.belt.adapt, cfg_.belt.run_speed); }
    double handling_mm_s() const { return handling_speed(cfg_.belt.adapt, cfg_.belt.run_speed, set_mm_s()); }
    void pl_pid_enable(bool on);
    std::array<uint16_t, 6> arm_counts_{};   // last commanded pose (arm thread only)
    OpsRegs ops_;
    std::deque<uint16_t> ops_acks_;
    bool plc_lost_ = false;
    PickStats picks_;
};
}  // namespace des
