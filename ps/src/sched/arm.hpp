// arm.hpp — 6-servo arm kinematics and pick / place waypoint planning (docs/运动控制与分拣.md).
//
// Joints (servo bus ids 1..6): #1 base yaw, #2 shoulder pitch, #3 elbow pitch, #4 wrist pitch,
// #5 wrist rotate, #6 gripper. Picking keeps the gripper pointing straight down, so the pitch chain is
// a planar 2-link problem to the wrist-pitch axis plus q4 = -90 - (q2 + q3).
// Tilted grasp (round parts out of the vertical reach): the tool leans away from the base by `tilt` in the arm's
// vertical plane, q4 = -90 + tilt - (q2 + q3); the fingers must then close along the tangential direction (the one
// horizontal axis the lean does not tip), so both fingertips stay at the same height. Vertical reach is ~150 mm
// from the base axis; 30 deg of tilt adds ~60 mm (docs/运动控制与分拣.md).
//
// Angle conventions (deg): q1 0 = arm frame +x, CCW +; q2 from horizontal, up +; q3 forearm relative to
// upper arm (0 = straight, negative = bent down); q4 relative to forearm; q5 0 = fingers close along the
// radial direction (rotated by grip_axis_at_zero, see ArmConfig).
// Servo count = zero_count + counts_per_deg * angle. All geometry marked ⚠ must be measured on the arm.
//
// Gripper offset: the finger plane does not contain the wrist-rotate (#5) axis. The grasp point sits tool_offset
// from that axis along s, the direction from the axis to the #6 servo side, which is the closing direction turned
// by servo_side about the tool axis (measured 2026-10-20: 9 mm, servo perpendicular to the closing direction).
// In the arm plane frame (e_r radial, e_t tangential, e_z up) with the tool pointing along a3 = q2 + q3 + q4:
//   s = cos(phi) * (-sin a3, 0, cos a3) + sin(phi) * e_t,   phi = q5 + grip_axis_at_zero + servo_side
// (vertical tool: phi = 0 puts the servo side radially outward). Tilted grasps keep phi = 0: the servo then sits
// on the outer side, which the lean lifts, instead of the inner side that it would push down toward the belt.
#pragma once

#include <array>
#include <cstdint>
#include <string>
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
    float tool_offset = 0;       // grasp point from the #5 axis toward the #6 servo side (mm) ⚠ measure
    float servo_side = 0;        // direction of that side: the closing direction turned about the tool axis
                                 // (rad, +-pi/2 when the servo sits beside the finger plane) ⚠ check sign
    float max_tilt_deg = 0;      // tilted grasp: lean the tool away from the base up to this (0 = vertical only)
    float tilt_step_deg = 5;     // tilts tried: 0, step, 2 step, ... (the smallest that reaches wins)

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
    float home_grip_count = -1;  // gripper count at the ready pose (taught on the HMI arm page; -1 = open)
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

// Inverse kinematics for a grasp point p (arm frame, mm) with the gripper pointing down (tilted away from the base
// by tilt, rad) and the fingers closing along close_axis (arm frame, rad; with tilt != 0 it must be tangential
// within 10 deg). Returns nullptr on success, else the reason.
const char *arm_ik(const ArmConfig &c, Vec3 p, float close_axis, JointAngles &out, float tilt = 0);
// Closing axis (table frame, rad) tangential to the arm base at table point p: the one a tilted grasp can use.
float arm_tangential_axis(const ArmConfig &c, Vec2 p_table);
// Forward kinematics of the grasp point (pitch chain + yaw), for tests and the UI.
Vec3 arm_fk(const ArmConfig &c, const JointAngles &j);

// table frame -> arm frame
Vec3 table_to_arm(const ArmConfig &c, Vec2 p_table, float z_above_belt);
float table_axis_to_arm(const ArmConfig &c, float axis_table);

// joint angles (+ gripper opening mm) -> servo counts; nullptr on success, else the joint that is out of range
const char *arm_to_counts(const ArmConfig &c, const JointAngles &j, float grip_mm, std::array<uint16_t, 6> &pos);
// servo counts -> joint angles + gripper opening (mm), the inverse of arm_to_counts (teaching by hand)
JointAngles arm_from_counts(const ArmConfig &c, const std::array<uint16_t, 6> &pos, float &grip_mm);

// Waypoints for one pick: above -> descend -> close -> lift (belt clear) -> bin -> open -> home.
// prev = counts the arm is at now (sizes the first move time). Vertical first; out of reach and the closing axis
// tangential -> the smallest tilt up to max_tilt_deg that reaches (same tilt above and down: the descent stays
// vertical). tilt_deg (optional) = the tilt used.
const char *plan_pick(const ArmConfig &c, Vec2 p_table, float close_axis_table, float open_mm, float part_wid,
                      int bin, const std::array<uint16_t, 6> &prev, std::vector<ServoFrame> &out,
                      float *tilt_deg = nullptr);

// Hand-eye calibration (des_tool calib-arm): the grasp point of taught poses (forward kinematics, arm frame) and the
// same marker positions seen by the camera (table frame). Least-squares 2D rigid fit table = base + R(yaw) * arm
// (the convention of table_to_arm), belt height from the touched marker tops. Needs >= 3 points spread >= 40 mm.
struct ArmBaseFit {
    bool ok = false;
    std::string err;
    Vec2 base_xy;
    float base_yaw = 0;          // rad
    float belt_z = 0;            // mean(arm z of the touches) - marker_h
    float rms = 0, max = 0;      // residual in the table plane (mm)
    std::vector<float> resid;    // per point (mm)
};
ArmBaseFit fit_arm_base(const std::vector<Vec3> &arm, const std::vector<Vec2> &table, float marker_h);

}  // namespace des
