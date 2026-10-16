// arm_manual.hpp — manual arm jogging for the HMI page 「机械臂」 (bring-up and calibration from the screen).
//
// Own thread, own ArmExec. Commands are accepted only while the line is not running and the production arm thread
// is idle (the servo bus itself is serialised in hw/des_hw), and only after the operator unlocks the page; leaving
// the page locks it again. While the page is shown and allowed it polls the six servos (~4 Hz positions, current
// and temperature every 4th poll). Jog = one SYNC WRITE to one servo (no reply, no 3 ms timeout), clamped to the
// joint's soft limits. Teach appends the current pose to <config dir>/data/arm_teach.csv (zero counts, bin poses).
// No home / gripper open-close buttons yet: zero counts and gripper counts are still uncalibrated (⚠ arm config).
#pragma once

#include <array>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "arm_exec.hpp"

namespace des {

struct ArmJointView {
    bool alive = false;        // answered the last poll
    int pos = -1;              // counts 0..4095, -1 never read
    int target = -1;           // last jog target
    int current_ma = -1, temp_c = -1;
    uint8_t status = 0;        // reply status byte: b5 overload, b2 over-temperature, b0 voltage
};

struct ArmManualView {
    bool shown = false, allowed = false, unlocked = false;
    int torque = -1;           // last torque command: -1 none, 0 off, 1 on
    std::array<ArmJointView, 6> j;
    std::string msg;           // result of the last command (for the page)
    int taught = 0;            // poses written to the teach file in this session
};

struct ArmRequest {
    enum Kind { Unlock, Lock, Torque, Jog, Teach } kind = Lock;
    int joint = 0, dir = 0;    // Jog: joint 0..5, dir +1 / -1
    int on = 0;                // Torque
    double step_deg = 1;       // Jog
    int ms = 1000;             // Jog: move time
};

class ArmManual {
public:
    ArmManual(des_hw_t *hw, const ArmConfig &cfg, std::string teach_path);
    ~ArmManual();
    void context(bool shown, bool allowed, const char *why_not);   // worker, every cycle
    void request(const ArmRequest &r);                              // worker
    ArmManualView view() const;

private:
    void loop();
    void poll(bool full);
    void handle(const ArmRequest &r);

    ArmExec arm_;
    des_hw_t *hw_;
    std::string teach_path_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::deque<ArmRequest> q_;
    ArmManualView v_;
    std::string why_not_;
    bool quit_ = false;
    int polls_ = 0;
    std::thread th_;
};

}  // namespace des
