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
    // manual belt jog (HMI): +1 / -1 / 0; only while the line is stopped (IDLE) and the e-stop is not latched
    uint8_t jog(int dir, const char *who);
    int jog_dir() const { return jog_dir_; }
    // recipe changeover: known id and the line not running; the worker picks it up with take_recipe()
    uint8_t select_recipe(int id, const char *who);
    int take_recipe();                      // id accepted since the last call, -1 = none
    uint8_t state() const { return state_; }
    bool arm_busy() const { return arm_busy_; }   // a pick is being executed (the manual arm page waits)
    void set_home(const std::array<int, 6> &counts);   // HMI arm page: ready pose of every following pick
    double speed_mm_s() const { return speed_mm_s_; }
    void set_active_recipe(int id) { active_recipe_ = id; }   // for the Modbus read-back (the worker applies recipes)
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
    void send_now(const canp_frame_t &f);    // ACKs: right away
    void flush_tx();
    des_hw_t *hw_;
    const AppConfig &cfg_;
    RunOptions opt_;
    BeltDriver belt_;
    ArmExec arm_;
    ArmConfig arm_cfg_;          // worker thread only: copied into every ArmJob
    int can_fd_ = -1;
    std::deque<canp_frame_t> txq_;
    uint64_t tx_dropped_ = 0;
    std::atomic<uint8_t> state_{CANP_ST_IDLE};
    uint8_t alarm_ = 0, hb_ = 0;
    uint32_t prev_status_ = 0;
    double t0_ = 0, last_status_ = 0, last_counts_ = 0, last_plc_hb_ = -1e9;
    canp_counts_t counts_{0, 0, 0, 0};
    std::set<int> reported_;     // track ids already sent as PART
    std::string last_event_;
    double speed_mm_s_ = 0;      // last set belt speed (0 = config run_speed)
    int jog_dir_ = 0;            // manual belt jog (IDLE only)
    int pending_recipe_ = -1;    // accepted by select_recipe, not yet applied by the worker
    std::unique_ptr<ModbusTcp> mb_;  // Modbus-TCP slave (cfg.plc.modbus_port > 0)
    uint64_t mb_requests_ = 0;
    double last_mb_ = -1e9;          // last Modbus request seen (PLC_LINK)
    uint16_t mb_seq_ = 0, mb_time_hi_ = 0, mb_hb_ = 0, part_seq_ = 0;
    canp_part_t last_part_{0, 0xFF, 0, 0, 0, 0};
    int active_recipe_ = 0;
    std::vector<uint16_t> ir_;   // Modbus input-register image, reused every cycle
    FlapSequencer flaps_;
    int flap_us_[2] = {-1, -1};  // last written pulse widths
    bool pwm_enabled_ = false;
    std::thread arm_thread_;
    BgQueue<ArmJob> jobs_{1};    // one pick in flight; the worker only dispatches while arm_busy_ is clear
    BgQueue<Done> done_;
    std::vector<Done> done_buf_; // reused by cycle() to drain done_
    std::atomic<bool> arm_busy_{false}, arm_clear_{true};
    std::atomic<double> arm_eta_{0};   // monotonic s when the running pick will be back at the ready pose
    double adapt_v_ = 0;               // last cruise speed set by the adaptation
    uint64_t adapt_changes_ = 0;
    std::array<uint16_t, 6> arm_counts_{};   // last commanded pose (arm thread only)
};
}  // namespace des
