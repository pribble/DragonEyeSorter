// arm.hpp — 6-servo arm kinematics and pick / place waypoint planning (docs/运动控制与分拣.md).
//
// Joints (servo bus ids 1..6): #1 base yaw, #2 shoulder pitch, #3 elbow pitch, #4 wrist pitch, #5 wrist rotate,
// #6 gripper. Picking keeps the gripper straight down, so the pitch chain is a planar 2-link problem to the
// wrist-pitch axis plus q4 = -90 - (q2 + q3). Tilted grasp (round parts out of the vertical reach): the tool leans
// away from the base by `tilt` in the arm's vertical plane, q4 = -90 + tilt - (q2 + q3); the fingers must then close
// along the tangential direction (the one horizontal axis the lean does not tip). Vertical reach is ~150 mm from the
// base axis; 30 deg of tilt adds ~60 mm (docs). All geometry marked ⚠ must be measured on the arm.
//
// Angle conventions (deg): q1 0 = arm frame +x, CCW +; q2 from horizontal, up +; q3 forearm relative to upper arm
// (0 = straight, negative = bent down); q4 relative to forearm; q5 0 = fingers close along the radial direction
// (rotated by grip_axis_at_zero). Servo count = zero_count + counts_per_deg * angle.
//
// Gripper offset: the grasp point sits tool_offset from the #5 axis along s, the direction from that axis to the #6
// servo side = closing direction turned by servo_side about the tool axis (measured 9 mm, servo perpendicular to the
// closing direction). In the arm plane frame (e_r radial, e_t tangential, e_z up) with tool along a3 = q2+q3+q4:
//   s = cos(phi) * (-sin a3, 0, cos a3) + sin(phi) * e_t,   phi = q5 + grip_axis_at_zero + servo_side
// (vertical tool: phi = 0 puts the servo side radially outward; tilted grasps keep phi = 0 so the lean lifts it).
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
    float counts_per_deg = 4096.f / 300.f;   // signed; DS-SY15A: 0..4095 over 300 deg ⚠ verify
    float min_deg = -135, max_deg = 135;     // soft limits
};
struct ArmConfig {
    // link lengths (mm), product page values ⚠ verify which length is which on the real arm
    float base_h = 107.5f;       // base plate to shoulder axis
    float l_upper = 82.85f;      // shoulder axis to elbow axis
    float l_fore = 82.85f;       // elbow axis to wrist pitch axis
    float l_wrist = 79.05f;      // wrist pitch axis to gripper flange
    float l_tool = 50.f;         // flange to grasp point between the fingertips ⚠ measure
    Vec2 base_xy{450.f, -170.f}; // arm base in the table frame ⚠ set after mounting
    float base_yaw = kPi / 2;    // direction of arm +x in the table frame (rad)
    float belt_z = 0.f;          // belt surface height above the arm base plate (mm) ⚠
    float grip_axis_at_zero = 0; // closing direction at q5 = 0 relative to the radial direction (rad)
    float tool_offset = 0;       // grasp point from the #5 axis toward the #6 servo side (mm) ⚠ measure
    float servo_side = 0;        // direction of that side: the closing direction turned about the tool axis
                                 // (rad, +-pi/2 when the servo sits beside the finger plane) ⚠ check sign
    float servo_in_min_r = 120;  // closer to the base axis than this (mm) a vertical grasp must not turn the #6 servo
                                 // toward the base: it sticks out ~40 mm and hit the base turret at 83 mm (10-21)
    float max_tilt_deg = 0;      // tilted grasp: lean the tool away from the base up to this (0 = vertical only)
    float tilt_step_deg = 5;     // tilts tried: 0, step, 2 step, ... (the smallest that reaches wins)
    float tilt_comp_mm = 0;      // tilted grasps aim this much farther from the base axis (radially, table plane):
                                 // stretched out of the vertical reach the gripper lands short (11-15: slow, 40 deg/s:
                                 // ~8 mm at 167 mm from the base axis; full speed 8 overshot, 4 picked 168..181 mm 4/4)
    // vertical grasps close to the base land short too (11-15: ~6 mm at 109 mm, part centre on the belt side of the
    // fingertip line; 120..131 mm fine): [[radius mm, mm farther out], ...] sorted by radius, linear in between, the
    // end values beyond the ends; empty = none (arm_near_comp)
    std::vector<std::array<float, 2>> near_comp;
    std::array<JointCal, 6> joint{};
    // gripper (#6): counts at fully open (max_open mm) and fully closed (0 mm), linear in between, unless grip_table
    // gives measured (count, opening mm) points: the finger linkage is far from linear (10-22: 32 mm at 2318 where
    // the straight line between closed 2919 and open 1195 says 21 mm). The table is sorted by opening on first use
    // (mutable: it is a cache — set grip_sorted = false when the table is refilled).
    float grip_open_count = 2600, grip_closed_count = 1500, grip_max_open = 60;
    mutable std::vector<std::array<float, 2>> grip_table;
    mutable bool grip_sorted = false;
    float grip_squeeze = 2;      // close this much narrower than the part
    // motion
    float z_safe = 60;           // travel height of the grasp point above the belt
    float z_grasp = 6;           // grasp point height above the belt when closing
    float joint_speed = 120;     // deg/s used to size move times
    float yaw_speed = 0;         // deg/s for the base (#1) only; 0 = same as joint_speed (10-30: the 113 deg swing to
                                 // the bin felt too fast at 220 deg/s while the other joints were fine)
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
    bool wait_belt = false;          // intercept: send only once the belt travel reaches belt_mm
    double belt_mm = 0;
    const char *what = "";
};
// Inverse kinematics for a grasp point p (arm frame, mm), gripper pointing down (tilted away from the base by tilt,
// rad), fingers closing along close_axis (arm frame, rad; with tilt != 0 it must be tangential within 10 deg).
// Returns nullptr on success, else the reason.
// One closing line has two wrist angles 180 deg apart (the #6 servo on one side or the other). q5_pref_deg (NaN =
// none) picks the one nearer to it, e.g. the ready pose's #5: no half turn of the wrist on the way down (10-26:
// closing across the belt sits at the -90 / +90 fold, so the plain fold flipped the wrist by a hair). Joint 5 limits
// and the servo-toward-the-base rule near the base still choose the other one when they must.
const char *arm_ik(const ArmConfig &c, Vec3 p, float close_axis, JointAngles &out, float tilt = 0,
                   float q5_pref_deg = NAN);
// Closing axis (table frame, rad) tangential to the arm base at table point p: the one a tilted grasp can use.
float arm_tangential_axis(const ArmConfig &c, Vec2 p_table);
// ArmConfig::near_comp at r mm from the base axis (0 without a table)
float arm_near_comp(const ArmConfig &c, float r);
// The same for an arm base given directly (the picker only keeps PickerConfig::arm_base).
inline float arm_tangential_axis(Vec2 base_xy, Vec2 p_table)
{
    return wrap_axis(std::atan2(p_table.y - base_xy.y, p_table.x - base_xy.x) + kPi / 2);
}
// Closing axis (table frame, rad) of a vertical grasp at table point p with the wrist (#5) at q5_deg: a round part
// picked along it needs no wrist turn from a pose with that #5 (the ready pose). Only q1 changes the axis then.
float arm_wrist_axis(const ArmConfig &c, Vec2 p_table, float q5_deg);
// The same from the two numbers the picker keeps: rel = q5 + grip_axis_at_zero (rad), tau = the sideways part of the
// tool offset at that q5 (tool_offset * sin(rel + servo_side), mm), base_xy = the arm base in the table frame.
inline float arm_wrist_axis(Vec2 base_xy, float rel, float tau, Vec2 p_table)
{
    const float dx = p_table.x - base_xy.x, dy = p_table.y - base_xy.y, R = std::hypot(dx, dy);
    const float rho = std::sqrt(std::max(R * R - tau * tau, 1.f));
    return wrap_axis(std::atan2(dy, dx) - std::atan2(tau, rho) + rel);
}
// rel / tau of the ready pose's wrist (home_deg[4]) for the picker (PickerConfig::wrist_rel / wrist_tau)
inline void arm_home_wrist(const ArmConfig &c, float &rel, float &tau)
{
    rel = deg2rad(c.home_deg[4]) + c.grip_axis_at_zero;
    tau = c.tool_offset * std::sin(rel + c.servo_side);
}
Vec3 arm_fk(const ArmConfig &c, const JointAngles &j);   // forward kinematics of the grasp point (tests / UI)
Vec3 table_to_arm(const ArmConfig &c, Vec2 p_table, float z_above_belt);
Vec2 arm_to_table(const ArmConfig &c, Vec3 p_arm);   // inverse of table_to_arm; z ignored (table plane of the belt)
float table_axis_to_arm(const ArmConfig &c, float axis_table);
// gripper opening <-> servo count (grip_table or the open / closed line), clamped to 0..grip_max_open
float grip_count(const ArmConfig &c, float mm);
float grip_mm(const ArmConfig &c, float count);
float grip_counts_per_mm(const ArmConfig &c, float mm);   // local slope (counts per mm, > 0)
// gripper direction in the arm's vertical plane (deg): -90 = straight down, +90 = up (q2 + q3 + q4)
inline float tool_pitch_deg(const JointAngles &j) { return j.q[1] + j.q[2] + j.q[3]; }
constexpr float kToolDownMaxDeg = -40.f;   // a pick only starts from / passes through gripper-down poses
constexpr float kWristTurnDeg = 30.f;      // plan_pick: a grasp wrist this far from the ready pose's turns above the part
// joint angles (+ gripper opening mm) -> servo counts; nullptr on success, else the joint that is out of range
const char *arm_to_counts(const ArmConfig &c, const JointAngles &j, float grip_mm, std::array<uint16_t, 6> &pos);
// servo counts -> joint angles + gripper opening (mm), the inverse of arm_to_counts (teaching by hand)
JointAngles arm_from_counts(const ArmConfig &c, const std::array<uint16_t, 6> &pos, float &grip_mm);
// The same pose with the grasp point dz higher (dz < 0: lower): q1, q5 and the gripper pitch kept, the shoulder /
// elbow re-solved on the same elbow branch. nullptr on success, else why (out of reach, joint limit).
const char *arm_raise(const ArmConfig &c, const JointAngles &j, float dz, JointAngles &out);
// Waypoints for one pick: above -> descend -> close -> lift (belt clear) -> [over bin] -> bin -> open -> [rise] ->
// home. A bin taught below the travel height (z_safe above the belt, e.g. a box on the table beside the belt) is
// reached through "over bin" = the bin pose raised to that height: the swing stays at the travel height and only the
// last move goes down (10-26: the direct joint move dipped the held gear ~50 mm below the belt next to the side
// frame). prev = counts the arm is at now (sizes the first move time). Vertical first; out of reach and the closing
// axis tangential -> the smallest tilt up to max_tilt_deg that reaches (same tilt above and down).
// q5_pref_deg (NaN = the ready pose's #5): which of the two wrist angles of the closing line to grasp with, e.g. the
// inner picks' #5 (PickJob::wrist_deg). An asked-for wrist more than kWristTurnDeg from the ready pose's is turned
// above the part ("above (ready wrist)" -> "turn wrist") and back after the lift ("turn back"); NaN keeps the frames.
const char *plan_pick(const ArmConfig &c, Vec2 p_table, float close_axis_table, float open_mm, float part_wid,
                      int bin, const std::array<uint16_t, 6> &prev, std::vector<ServoFrame> &out,
                      float *tilt_deg = nullptr, float q5_pref_deg = NAN);
// A manual move (HMI reset, des_tool home / goto) from the counts `prev` to the joint angles `to`, the gripper at
// grip_mm: refused if the gripper direction changes > 60 deg (swing through horizontal); a gripper below the travel
// height goes straight up first ("rise first"), a low target is reached from straight above ("over target"). Every
// frame takes at least min_ms. A pick (plan_pick) starts with the same "rise first" when the arm is low.
const char *plan_move(const ArmConfig &c, const std::array<uint16_t, 6> &prev, const JointAngles &to, float grip_mm,
                      uint16_t min_ms, const char *what, std::vector<ServoFrame> &out);
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
