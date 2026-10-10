// runner.hpp — production control for `des_app run`: line state machine, belt (BeltDriver), arm (ArmExec on its own
// thread), CAN link to the "PLC" (bus/can_proto.h). Driven from the live worker thread once per processed frame.
//
// States: IDLE (belt stopped, nothing dispatched) / RUN / PAUSED (PLC interlock or operator; current pick finishes) /
// ESTOP (PL e-stop latched the stepper; needs RESET_ALARM then START). Without CAN the line starts in RUN.
#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "arm_exec.hpp"
#include "belt_ctl.hpp"
#include "can_proto.h"
#include "config.hpp"
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

private:
    void arm_loop();
    void handle_can(double now);
    void set_state(uint8_t s, const char *why);
    void send(const canp_frame_t &f);

    des_hw_t *hw_;
    const AppConfig &cfg_;
    RunOptions opt_;
    BeltDriver belt_;
    ArmExec arm_;
    int can_fd_ = -1;
    std::atomic<uint8_t> state_{CANP_ST_IDLE};
    uint8_t alarm_ = 0;
    uint32_t prev_status_ = 0;
    double t0_ = 0, last_status_ = 0, last_counts_ = 0, last_plc_hb_ = -1e9;
    uint8_t hb_ = 0;
    canp_counts_t counts_{0, 0, 0, 0};
    std::set<int> reported_;     // track ids already sent as PART
    std::string last_event_;

    // arm thread hand-off
    std::thread arm_thread_;
    std::mutex m_;
    std::condition_variable cv_;
    bool quit_ = false, job_pending_ = false;
    PickJob job_;
    struct Done { int id; bool ok; std::string why; };
    std::vector<Done> done_;
    std::atomic<bool> arm_busy_{false}, arm_clear_{true};
    std::array<uint16_t, 6> arm_counts_{};   // last commanded pose (arm thread only)
};

}  // namespace des
