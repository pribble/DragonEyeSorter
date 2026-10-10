// arm.hpp — 6-servo arm kinematics and pick / place waypoint planning (docs/04).
//
// Joints (servo bus ids 1..6): #1 base yaw, #2 shoulder pitch, #3 elbow pitch, #4 wrist pitch,
// #5 wrist rotate, #6 gripper. Picking keeps the gripper pointing straight down, so the pitch chain is
// a planar 2-link problem to the wrist-pitch axis plus q4 = -90 - (q2 + q3).
//
// Angle conventions (deg): q1 0 = arm frame +x, CCW +; q2 from horizontal, up +; q3 forearm relative to
// upper arm (0 = straight, negative = bent down); q4 relative to forearm; q5 0 = fingers close along the
// radial direction (rotated by grip_axis_at_zero, see ArmConfig).
// Servo count = zero_count + counts_per_deg * angle. All geometry marked ⚠ must be measured on the arm.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "geom.hpp"

namespace des {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

struct JointCal {
    uint8_t id = 0;              // servo bus id
    float zero_count = 2048;     // servo count at angle 0 ⚠ calibrate
    float counts_per_deg = 4096.f / 300.f;   // signed (direction); DS-SY15A: 0..4095 over 300 deg ⚠ verify
    float min_deg = -135, max_deg = 135;     // soft limits
};

struct ArmConfig {
    // link lengths (mm), product page values ⚠ verify which length is which on the real arm
    float base_h = 107.5f;       // base plate to shoulder axis
    float l_upper = 82.85f;      // shoulder axis to elbow axis
    float l_fore = 82.85f;       // elbow axis to wrist pitch axis
    float l_wrist = 79.05f;      // wrist pitch axis to gripper flange
    float l_tool = 50.f;         // flange to grasp point between the fingertips ⚠ measure
    // arm base in the table frame
    Vec2 base_xy{450.f, -170.f}; // ⚠ set after mounting
    float base_yaw = kPi / 2;    // direction of arm +x in the table frame (rad)
    float belt_z = 0.f;          // belt surface height above the arm base plate (mm) ⚠
    float grip_axis_at_zero = 0; // closing direction at q5 = 0 relative to the radial direction (rad)

    std::array<JointCal, 6> joint{};
    // gripper (#6): count at fully open (max_open mm) and fully closed (0 mm), linear in between ⚠
    float grip_open_count = 2600, grip_closed_count = 1500, grip_max_open = 60;
    float grip_squeeze = 2;      // close this much narrower than the part

    // motion
    float z_safe = 60;           // travel height of the grasp point above the belt
    float z_grasp = 6;           // grasp point height above the belt when closing
    float joint_speed = 120;     // deg/s used to size move times
    uint16_t min_ms = 150, descend_ms = 400, close_ms = 300, open_ms = 250;
    std::array<float, 5> home_deg{{0, 60, -120, -30, 0}};   // ready pose, outside the camera view ⚠ teach
    std::vector<std::array<float, 5>> bins;                  // drop poses per bin (joint angles) ⚠ teach
};

struct JointAngles {
    std::array<float, 5> q{};    // q1..q5 in deg
};

struct ServoFrame {
    std::array<uint16_t, 6> pos{};   // servo counts, index = joint (#1..#6)
    uint16_t ms = 0;                 // move time
    bool belt_clear = false;         // after this frame the gripper is off the belt (belt may move)
    bool check_grasp = false;        // after this frame verify the part is held (P1)
    const char *what = "";
};

// Inverse kinematics for a grasp point p (arm frame, mm) with the gripper pointing down and the fingers
// closing along close_axis (arm frame, rad). Returns nullptr on success, else the reason.
const char *arm_ik(const ArmConfig &c, Vec3 p, float close_axis, JointAngles &out);
// Forward kinematics of the grasp point (pitch chain + yaw), for tests and the UI.
Vec3 arm_fk(const ArmConfig &c, const JointAngles &j);

// table frame -> arm frame
Vec3 table_to_arm(const ArmConfig &c, Vec2 p_table, float z_above_belt);
float table_axis_to_arm(const ArmConfig &c, float axis_table);

// joint angles (+ gripper opening mm) -> servo counts; nullptr on success, else the joint that is out of range
const char *arm_to_counts(const ArmConfig &c, const JointAngles &j, float grip_mm, std::array<uint16_t, 6> &pos);

// Waypoints for one pick: above -> descend -> close -> lift (belt clear) -> bin -> open -> home.
// prev = counts the arm is at now (sizes the first move time).
const char *plan_pick(const ArmConfig &c, Vec2 p_table, float close_axis_table, float open_mm, float part_wid,
                      int bin, const std::array<uint16_t, 6> &prev, std::vector<ServoFrame> &out);

}  // namespace des
