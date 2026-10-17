// runner.hpp — production control for `des_app run`: line state machine, belt (BeltDriver), arm (ArmExec on its own
// thread), CAN link to the "PLC" (hw/can_proto.h), Modbus-TCP slave (hw/modbus_map.h, same rules). Driven from the
// live worker thread once per processed frame.
//
// States: IDLE (belt stopped, nothing dispatched) / RUN / PAUSED (PLC interlock or operator; current pick finishes) /
// ESTOP (PL e-stop latched the stepper; needs RESET_ALARM then START). Without CAN the line starts in RUN.
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "arm_exec.hpp"
#include "belt_ctl.hpp"
#include "can_proto.h"
#include "config.hpp"
#include "flap.hpp"
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
    std::string status_line() const;      // for the HDMI overlay
    // operator / PLC commands (same rules for both; worker thread). Return a CANP_ACK_* result.
    uint8_t command(uint8_t cmd, const char *who);
    uint8_t set_speed(double mm_s, const char *who);
    // recipe changeover: id must be in cfg.recipes and the line not running (stop first). The worker picks the
    // accepted id up with take_recipe() and applies it (tracker size rule + HMI).
    uint8_t select_recipe(int id, const char *who);
    int take_recipe();                      // id accepted since the last call, -1 = none
    uint8_t state() const { return state_; }
    bool arm_busy() const { return arm_busy_; }   // a pick is being executed (the manual arm page waits)
    // new ready pose (HMI arm page 「设为初始姿态」): every following pick starts and ends there
    void set_home(const std::array<int, 6> &counts);
    double speed_mm_s() const { return speed_mm_s_; }
    void set_active_recipe(int id) { active_recipe_ = id; }   // for the Modbus read-back (the worker applies recipes)

private:
    void arm_loop();
    void handle_can(double now);
    void handle_modbus(double now);
    void update_modbus(double now);
    void set_state(uint8_t s, const char *why);
    void send(const canp_frame_t &f);        // queued: at most one frame per cycle (see flush_tx)
    void send_now(const canp_frame_t &f);    // ACKs: right away
    void flush_tx();

    des_hw_t *hw_;
    const AppConfig &cfg_;
    RunOptions opt_;
    BeltDriver belt_;
    ArmExec arm_;
    ArmConfig arm_cfg_;          // cfg.arm + the ready pose taught at run time (guarded by m_)
    int can_fd_ = -1;
    std::deque<canp_frame_t> txq_;
    uint64_t tx_dropped_ = 0;
    std::atomic<uint8_t> state_{CANP_ST_IDLE};
    uint8_t alarm_ = 0;
    uint32_t prev_status_ = 0;
    double t0_ = 0, last_status_ = 0, last_counts_ = 0, last_plc_hb_ = -1e9;
    uint8_t hb_ = 0;
    canp_counts_t counts_{0, 0, 0, 0};
    std::set<int> reported_;     // track ids already sent as PART
    std::string last_event_;
    double speed_mm_s_ = 0;      // last set belt speed (0 = config run_speed)
    int pending_recipe_ = -1;    // accepted by select_recipe, not yet applied by the worker
    std::unique_ptr<ModbusTcp> mb_;  // Modbus-TCP slave (cfg.plc.modbus_port > 0)
    uint64_t mb_requests_ = 0;
    double last_mb_ = -1e9;          // last Modbus request seen (PLC_LINK)
    uint16_t mb_seq_ = 0, mb_time_hi_ = 0, mb_hb_ = 0, part_seq_ = 0;
    canp_part_t last_part_{0, 0xFF, 0, 0, 0, 0};
    int active_recipe_ = 0;
    FlapSequencer flaps_;
    int flap_us_[2] = {-1, -1};  // last written pulse widths
    bool pwm_enabled_ = false;

    // arm thread hand-off
    std::thread arm_thread_;
    std::mutex m_;
    std::condition_variable cv_;
    bool quit_ = false, job_pending_ = false;
    PickJob job_;
    struct Done { int id; bool ok; std::string why; };
    std::vector<Done> done_;
    std::atomic<bool> arm_busy_{false}, arm_clear_{true};
    std::atomic<double> arm_eta_{0};   // monotonic s when the running pick will be back at the ready pose (speed adaptation)
    double adapt_v_ = 0;               // last cruise speed set by the adaptation
    uint64_t adapt_changes_ = 0;
    std::array<uint16_t, 6> arm_counts_{};   // last commanded pose (arm thread only)
};

}  // namespace des
