// arm_exec.hpp — drives the 6 bus servos through the PL servo_bus master (common/des_hw) from waypoint lists
// planned by scheduler/arm (plan_pick), plus the bring-up helpers (scan, read back, torque, teach).
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "arm.hpp"
#include "des_hw.h"

namespace des {

struct ServoInfo {
    bool alive = false;        // answered PING
    uint8_t status = 0;        // status byte: b5 overload, b2 over-temp, b0 voltage
    int pos = -1;              // 0x38 current position (counts), -1 unread
    int current_ma = -1;       // 0x2E
    int temp_c = -1;           // 0x3F
};

class ArmExec {
public:
    ArmExec(des_hw_t *hw, const ArmConfig &cfg) : hw_(hw), cfg_(cfg) {}

    ServoInfo probe(int joint);                          // joint 0..5 -> id from config
    bool read_positions(std::array<uint16_t, 6> &pos);   // false if any servo did not answer
    int torque(bool on);                                 // broadcast to all 6
    int send(const std::array<uint16_t, 6> &pos, uint16_t ms);   // one SYNC WRITE frame

    struct Result {
        bool ok = false;
        bool grasped = false;
        int grip_pos = -1, grip_ma = -1;   // gripper read back after closing
        std::string why;
        double ms = 0;
    };
    // Executes the waypoints in order (waits each move time + settle). on_belt_clear runs right after the frame
    // flagged belt_clear (the belt may move again). dry = print instead of sending.
    Result run(const std::vector<ServoFrame> &wp, bool dry, const std::function<void()> &on_belt_clear = nullptr);

    const ArmConfig &config() const { return cfg_; }
    uint8_t id(int joint) const { return cfg_.joint[joint].id; }

private:
    des_hw_t *hw_;
    ArmConfig cfg_;
};

}  // namespace des
