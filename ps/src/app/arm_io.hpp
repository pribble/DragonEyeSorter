// arm_io.hpp — the six bus servos through the PL servo_bus master, in one place:
//   ArmExec    production pick execution (waypoint lists from sched/arm plan_pick) + bring-up helpers.
//   ArmManual  manual jogging for the HMI page 「机械臂」: own thread, own ArmExec.
// The arm_*() helpers are the one SYNC WRITE frame builder / read-back both use.
//
// Manual page rules (unchanged): commands only while the line is not running and the production arm is idle (the bus
// itself is serialised in hw/des_hw), and only after the operator unlocks the page; leaving the page locks it again.
// While shown and allowed it polls the six servos (~4 Hz positions, current / temperature every 4th poll). Jog = one
// SYNC WRITE to one servo (no reply), clamped to the joint's soft limits, stepping from the last jog target (the servo
// dead band does not add up). Teach appends the pose to <config>/data/arm_teach.csv; Set home stores it in
// <config>/arm_home.json (the ready pose every pick starts and ends in); Reset moves all six there, >= 2 s.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "arm.hpp"
#include "bg_queue.hpp"
#include "des_hw.h"
namespace des {
// One SYNC WRITE frame (0x83 -> 0x2A) to joints 0..n-1 (ids from cfg.joint[]), and the 0x38 read-back of all six.
int sync_write(des_hw_t *hw, const ArmConfig &cfg, const uint16_t *pos, const uint16_t *ms, int n);
int sync_write_all(des_hw_t *hw, const ArmConfig &cfg, const std::array<uint16_t, 6> &pos, uint16_t ms);
int sync_write_one(des_hw_t *hw, const ArmConfig &cfg, int joint, uint16_t pos, uint16_t ms);
bool read_counts(des_hw_t *hw, const ArmConfig &cfg, std::array<uint16_t, 6> &pos);
struct ServoInfo {
    bool alive = false;        // answered PING
    uint8_t status = 0;        // b5 overload, b2 over-temp, b0 voltage
    int pos = -1;              // 0x38 counts, -1 unread
    int current_ma = -1;       // 0x2E
    int temp_c = -1;           // 0x3F
};
class ArmExec {
public:
    ArmExec(des_hw_t *hw, const ArmConfig &cfg) : hw_(hw), cfg_(cfg)
    {
        static const bool once = [] { des_sb_timings(0); return true; }();   // no reply timestamps: nobody reads them
        (void)once;
    }
    ServoInfo probe(int joint);                          // joint 0..5 -> id from config
    bool read_positions(std::array<uint16_t, 6> &pos) { return read_counts(hw_, cfg_, pos); }
    int torque(bool on);                                 // broadcast to all 6
    int send(const std::array<uint16_t, 6> &pos, uint16_t ms) { return sync_write_all(hw_, cfg_, pos, ms); }
    struct Result {
        bool ok = false, grasped = false;
        int grip_pos = -1, grip_ma = -1;   // gripper read back after closing
        std::string why;
        double ms = 0;
    };
    // Executes the waypoints (waits each move time + 80 ms settle). on_belt_clear runs right after the belt_clear
    // frame. dry = print instead of sending. wait_belt frames (intercept close) send only when wait_belt(belt_mm);
    // false aborts: back to the first frame's pose, opened, result not ok.
    using WaitBelt = std::function<bool(double)>;
    Result run(const std::vector<ServoFrame> &wp, bool dry, const std::function<void()> &on_belt_clear = nullptr,
               const WaitBelt &wait_belt = nullptr);
    Result simulate(const std::vector<ServoFrame> &wp, const std::function<void()> &on_belt_clear = nullptr,
                    const WaitBelt &wait_belt = nullptr);   // same timing without touching the bus; always "grasped"
    const ArmConfig &config() const { return cfg_; }
    uint8_t id(int joint) const { return cfg_.joint[joint].id; }
private:
    des_hw_t *hw_;
    ArmConfig cfg_;
};
struct ArmJointView {
    bool alive = false;
    int pos = -1;              // counts 0..4095, -1 never read
    int target = -1;           // last jog target
    int current_ma = -1, temp_c = -1;
    uint8_t status = 0;        // b5 overload, b2 over-temperature, b0 voltage
};
struct ArmManualView {
    bool shown = false, allowed = false, unlocked = false;
    int torque = -1;           // last torque command: -1 none, 0 off, 1 on
    std::array<ArmJointView, 6> j;
    char msg[160] = {0};       // result of the last command (C string for the HMI, whole UTF-8 characters only)
    int taught = 0;            // poses written to the teach file in this session
    std::array<int, 6> home{{-1, -1, -1, -1, -1, -1}};   // ready pose (counts)
    bool home_taught = false;  // from arm_home.json (else the config default, which Reset will not go to)
};
struct ArmRequest {
    enum Kind { Unlock, Lock, Torque, Jog, Teach, SetHome, Reset } kind = Lock;
    int joint = 0, dir = 0;    // Jog: joint 0..5, dir +1 / -1
    int on = 0;                // Torque
    double step_deg = 1;       // Jog
    int ms = 1000;             // Jog / Reset: move time
};
class ArmManual {
public:
    ArmManual(des_hw_t *hw, const ArmConfig &cfg, bool home_taught, std::string teach_path, std::string home_path);
    ~ArmManual();
    void context(bool shown, bool allowed, const char *why_not);   // worker, every cycle
    void request(const ArmRequest &r);                             // worker
    ArmManualView view() const;
    bool take_home(std::array<int, 6> &counts);   // worker: a new ready pose was set since the last call
private:
    void loop();
    void poll(bool full);
    void handle(const ArmRequest &r);
    void say(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
    void say_locked(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
    ArmExec arm_;
    des_hw_t *hw_;
    std::string teach_path_, home_path_;
    bool home_changed_ = false;
    mutable std::mutex m_;          // v_ / why_not_ / home_changed_ (worker reads the view, the thread writes it)
    BgQueue<ArmRequest> q_{16};     // commands, dropped when the page floods the thread
    ArmManualView v_;
    std::string why_not_;
    int polls_ = 0;
    std::thread th_;
};
}  // namespace des
