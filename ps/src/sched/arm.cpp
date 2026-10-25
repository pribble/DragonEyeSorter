// arm.cpp — see arm.hpp
#include "arm.hpp"

#include <algorithm>
#include <cmath>

namespace des {

const char *arm_ik(const ArmConfig &c, Vec3 p, float close_axis, JointAngles &out, float tilt)
{
    const float az = std::atan2(p.y, p.x), R = std::hypot(p.x, p.y);
    if (tilt != 0 && std::fabs(wrap_axis(close_axis - az - kPi / 2)) > deg2rad(10.f))
        return "tilted grasp needs a tangential closing axis";
    const float lt = c.l_wrist + c.l_tool, D = c.tool_offset, a3 = -kPi / 2 + tilt;
    const bool beside = std::fabs(std::fabs(c.servo_side) - kPi / 2) < 0.1f;   // servo beside the finger plane
    // the offset moves the grasp point out of the arm plane: q1 aims the plane at p, q5 / q1 coupled (fixed-point)
    float q1 = az, q5 = 0, phi = 0, rho = R;
    auto aim = [&](float flip) -> const char * {   // flip: q5 + 180 deg = same closing line, servo on the other side
        for (int it = 0; it < 8; it++) {
            q5 = wrap_axis(close_axis - q1 - c.grip_axis_at_zero) + flip;
            if (tilt != 0 && beside) q5 = wrap_pi(-c.grip_axis_at_zero - c.servo_side);   // servo on the upper side
            phi = q5 + c.grip_axis_at_zero + c.servo_side;
            const float tau = D * std::sin(phi);
            if (R <= std::fabs(tau)) return "too close to the base axis";
            rho = std::sqrt(R * R - tau * tau);
            q1 = az - std::atan2(tau, rho);
        }
        return nullptr;
    };
    if (const char *w = aim(0)) return w;
    // vertical tool: more than 60 deg toward the base near it -> the other q5 for the same closing line (if allowed)
    if (tilt == 0 && beside && std::cos(phi) < -0.5f && R < c.servo_in_min_r) {
        const float flip = q5 <= 0 ? kPi : -kPi, f5 = rad2deg(q5 + flip);
        if (f5 < c.joint[4].min_deg || f5 > c.joint[4].max_deg) return "servo would face the base (joint 5 limit)";
        q1 = az;
        if (const char *w = aim(flip)) return w;
    }
    // wrist pitch axis: back along the tool and the offset (arm plane: radial, up)
    const float dx = rho - lt * std::cos(a3) + D * std::cos(phi) * std::sin(a3);
    const float dz = p.z - lt * std::sin(a3) - D * std::cos(phi) * std::cos(a3) - c.base_h;
    if (dx < 20) return "too close for this tilt";
    const float L1 = c.l_upper, L2 = c.l_fore;
    const float c3 = (dx * dx + dz * dz - L1 * L1 - L2 * L2) / (2 * L1 * L2);
    if (c3 > 1.f || c3 < -1.f) return "out of reach";
    const float q3 = -std::acos(c3);   // elbow up
    const float q2 = std::atan2(dz, dx) - std::atan2(L2 * std::sin(q3), L1 + L2 * std::cos(q3));
    const float q4 = a3 - (q2 + q3);
    out.q = {rad2deg(q1), rad2deg(q2), rad2deg(q3), rad2deg(q4), rad2deg(q5)};
    static const char *lim[5] = {"joint 1 limit", "joint 2 limit", "joint 3 limit", "joint 4 limit", "joint 5 limit"};
    for (int i = 0; i < 5; i++)
        if (out.q[i] < c.joint[i].min_deg || out.q[i] > c.joint[i].max_deg) return lim[i];
    return nullptr;
}
Vec3 arm_fk(const ArmConfig &c, const JointAngles &j)
{
    const float q1 = deg2rad(j.q[0]), a1 = deg2rad(j.q[1]), a2 = a1 + deg2rad(j.q[2]), a3 = a2 + deg2rad(j.q[3]);
    const float lt = c.l_wrist + c.l_tool, D = c.tool_offset;
    const float phi = deg2rad(j.q[4]) + c.grip_axis_at_zero + c.servo_side;
    const float r = c.l_upper * std::cos(a1) + c.l_fore * std::cos(a2) + lt * std::cos(a3) - D * std::cos(phi) * std::sin(a3);
    const float t = D * std::sin(phi);
    const float z = c.base_h + c.l_upper * std::sin(a1) + c.l_fore * std::sin(a2) + lt * std::sin(a3) +
                    D * std::cos(phi) * std::cos(a3);
    return {r * std::cos(q1) - t * std::sin(q1), r * std::sin(q1) + t * std::cos(q1), z};
}
Vec3 table_to_arm(const ArmConfig &c, Vec2 p, float z_above_belt)
{
    const float dx = p.x - c.base_xy.x, dy = p.y - c.base_xy.y;
    const float cy = std::cos(c.base_yaw), sy = std::sin(c.base_yaw);
    return {cy * dx + sy * dy, -sy * dx + cy * dy, c.belt_z + z_above_belt};
}
Vec2 arm_to_table(const ArmConfig &c, Vec3 p)
{
    const float cy = std::cos(c.base_yaw), sy = std::sin(c.base_yaw);
    return {c.base_xy.x + cy * p.x - sy * p.y, c.base_xy.y + sy * p.x + cy * p.y};
}
float table_axis_to_arm(const ArmConfig &c, float axis_table) { return wrap_axis(axis_table - c.base_yaw); }
float arm_tangential_axis(const ArmConfig &c, Vec2 p) { return arm_tangential_axis(c.base_xy, p); }

// grip_table sorted by opening once (it only changes with the configuration); piecewise linear, extrapolated from
// the end segments. Mutates the cache in ArmConfig: nullptr = no measured table = use the straight line.
static const std::vector<std::array<float, 2>> *grip_points(const ArmConfig &c)
{
    if (c.grip_table.size() < 2) return nullptr;
    if (!c.grip_sorted) {
        std::sort(c.grip_table.begin(), c.grip_table.end(),
                  [](const std::array<float, 2> &a, const std::array<float, 2> &b) { return a[1] < b[1]; });
        c.grip_sorted = true;
    }
    return &c.grip_table;
}
float grip_count(const ArmConfig &c, float mm)
{
    mm = clampf(mm, 0.f, c.grip_max_open);
    const std::vector<std::array<float, 2>> *t = grip_points(c);
    if (!t) return c.grip_closed_count + (c.grip_open_count - c.grip_closed_count) * mm / c.grip_max_open;
    size_t i = 1;
    while (i + 1 < t->size() && mm > (*t)[i][1]) i++;
    const float d = (*t)[i][1] - (*t)[i - 1][1];
    return d != 0 ? (*t)[i - 1][0] + ((*t)[i][0] - (*t)[i - 1][0]) * (mm - (*t)[i - 1][1]) / d : (*t)[i][0];
}
float grip_mm(const ArmConfig &c, float count)
{
    const std::vector<std::array<float, 2>> *t = grip_points(c);
    if (!t) {
        const float span = c.grip_open_count - c.grip_closed_count;
        return span != 0 ? (count - c.grip_closed_count) / span * c.grip_max_open : 0.f;
    }
    size_t i = 1;   // counts are monotonic in the opening: find the segment whose count range holds `count`
    const bool up = t->back()[0] > t->front()[0];
    while (i + 1 < t->size() && (up ? count > (*t)[i][0] : count < (*t)[i][0])) i++;
    const float d = (*t)[i][0] - (*t)[i - 1][0];
    return d != 0 ? (*t)[i - 1][1] + ((*t)[i][1] - (*t)[i - 1][1]) * (count - (*t)[i - 1][0]) / d : (*t)[i][1];
}
float grip_counts_per_mm(const ArmConfig &c, float mm)
{
    const float a = std::max(mm - 0.5f, 0.f), b = std::min(mm + 0.5f, c.grip_max_open);
    return b > a ? std::fabs(grip_count(c, b) - grip_count(c, a)) / (b - a) : 1.f;
}
const char *arm_to_counts(const ArmConfig &c, const JointAngles &j, float grip_mm, std::array<uint16_t, 6> &pos)
{
    static const char *rng[5] = {"servo 1 count range", "servo 2 count range", "servo 3 count range",
                                 "servo 4 count range", "servo 5 count range"};
    for (int i = 0; i < 5; i++) {
        const float v = c.joint[i].zero_count + c.joint[i].counts_per_deg * j.q[i];
        if (v < 0 || v > 4095) return rng[i];
        pos[i] = uint16_t(std::lround(v));
    }
    const float g = grip_count(c, grip_mm);
    if (g < 0 || g > 4095) return "servo 6 count range";
    pos[5] = uint16_t(std::lround(g));
    return nullptr;
}
JointAngles arm_from_counts(const ArmConfig &c, const std::array<uint16_t, 6> &pos, float &grip_mm)
{
    JointAngles j;
    for (int i = 0; i < 5; i++) {
        const float cpd = c.joint[i].counts_per_deg;
        j.q[i] = cpd != 0 ? (pos[i] - c.joint[i].zero_count) / cpd : 0.f;
    }
    grip_mm = des::grip_mm(c, pos[5]);
    return j;
}
const char *arm_raise(const ArmConfig &c, const JointAngles &j, float dz, JointAngles &out)
{
    // the tool offset terms of arm_fk depend on q5 and the pitch only: raising the grasp point = raising the wrist
    // pitch axis, a planar two-link solve from the shoulder
    const float a1 = deg2rad(j.q[1]), a2 = a1 + deg2rad(j.q[2]), a3 = a2 + deg2rad(j.q[3]);
    const float L1 = c.l_upper, L2 = c.l_fore;
    const float wx = L1 * std::cos(a1) + L2 * std::cos(a2), wz = L1 * std::sin(a1) + L2 * std::sin(a2) + dz;
    const float c3 = (wx * wx + wz * wz - L1 * L1 - L2 * L2) / (2 * L1 * L2);
    if (c3 > 1.f || c3 < -1.f) return "out of reach";
    const float q3 = j.q[2] <= 0 ? -std::acos(c3) : std::acos(c3);   // keep the taught elbow branch
    const float q2 = std::atan2(wz, wx) - std::atan2(L2 * std::sin(q3), L1 + L2 * std::cos(q3));
    out.q = {j.q[0], rad2deg(q2), rad2deg(q3), rad2deg(a3 - q2 - q3), j.q[4]};
    static const char *lim[5] = {"joint 1 limit", "joint 2 limit", "joint 3 limit", "joint 4 limit", "joint 5 limit"};
    for (int i = 0; i < 5; i++)
        if (out.q[i] < c.joint[i].min_deg || out.q[i] > c.joint[i].max_deg) return lim[i];
    return nullptr;
}
static uint16_t move_ms(const ArmConfig &c, const std::array<uint16_t, 6> &a, const std::array<uint16_t, 6> &b)
{
    float worst = 0;
    for (int i = 0; i < 5; i++) {
        const float cpd = std::fabs(c.joint[i].counts_per_deg);
        if (cpd > 0) worst = std::max(worst, std::abs(int(a[i]) - int(b[i])) / cpd);
    }
    return uint16_t(std::min(std::max(worst / c.joint_speed * 1000.f, float(c.min_ms)), 5000.f));
}
// gripper below the travel height (taught bin, an interrupted pick): straight up to it first, so the next joint
// move cannot drag the gripper / #6 servo sideways below the belt surface along the belt edge or the box (10-26:
// bin -> ready pose directly dipped ~45 mm below the belt next to the side frame). The gripper count is kept.
static const char *rise_first(const ArmConfig &c, const std::array<uint16_t, 6> &prev, std::vector<ServoFrame> &out,
                              uint16_t min_ms)
{
    float g;
    const JointAngles now = arm_from_counts(c, prev, g);
    const float dz = (c.belt_z + c.z_safe) - arm_fk(c, now).z;
    if (dz <= 1.f) return nullptr;
    JointAngles up;
    if (arm_raise(c, now, dz, up)) return "gripper is low and cannot go straight up from here: move the arm up by hand";
    ServoFrame f;
    if (const char *w = arm_to_counts(c, up, g, f.pos)) return w;
    f.pos[5] = prev[5];
    f.ms = std::max(move_ms(c, prev, f.pos), min_ms);
    f.what = "rise first";
    out.push_back(f);
    return nullptr;
}
const char *plan_move(const ArmConfig &c, const std::array<uint16_t, 6> &prev, const JointAngles &to, float grip_mm,
                      uint16_t min_ms, const char *what, std::vector<ServoFrame> &out)
{
    out.clear();
    float g;
    const float pn = tool_pitch_deg(arm_from_counts(c, prev, g)), pt = tool_pitch_deg(to);
    if (std::fabs(pn - pt) > 60.f)   // one joint move would swing the 190 mm gripper through horizontal (10-22)
        return "gripper direction differs more than 60 deg from the target: move the arm by hand first";
    if (const char *w = rise_first(c, prev, out, min_ms)) return w;
    ServoFrame f;
    auto add = [&](const JointAngles &j, const char *name) -> const char * {
        f = ServoFrame{};
        if (const char *w = arm_to_counts(c, j, grip_mm, f.pos)) return w;
        f.ms = std::max(move_ms(c, out.empty() ? prev : out.back().pos, f.pos), min_ms);
        f.what = name;
        out.push_back(f);
        return nullptr;
    };
    const float dz = (c.belt_z + c.z_safe) - arm_fk(c, to).z;
    if (dz > 1.f && pt <= kToolDownMaxDeg) {   // a low target (a bin): from straight above it
        JointAngles over;
        if (arm_raise(c, to, dz, over)) return "target is low and cannot be reached from straight above";
        if (const char *w = add(over, "over target")) return w;
    }
    return add(to, what);
}
const char *plan_pick(const ArmConfig &c, Vec2 p_table, float close_axis_table, float open_mm, float part_wid,
                      int bin, const std::array<uint16_t, 6> &prev, std::vector<ServoFrame> &out, float *tilt_deg)
{
    out.clear();
    const float ax = table_axis_to_arm(c, close_axis_table);
    JointAngles above, down, home, drop;
    const char *vertical_why = nullptr;
    for (float t = 0;; t += c.tilt_step_deg) {
        const float tr = deg2rad(t);
        const char *why = arm_ik(c, table_to_arm(c, p_table, c.z_safe), ax, above, tr);
        if (!why) why = arm_ik(c, table_to_arm(c, p_table, c.z_grasp), ax, down, tr);
        if (!why) {
            if (tilt_deg) *tilt_deg = t;
            break;
        }
        if (t == 0) vertical_why = why;
        if (c.tilt_step_deg <= 0 || t + c.tilt_step_deg > c.max_tilt_deg + 1e-3f) return vertical_why;
    }
    if (bin < 0 || size_t(bin) >= c.bins.size()) return "no drop pose for bin";
    drop.q = c.bins[bin];
    home.q = c.home_deg;
    for (int i = 0; i < 5; i++) {   // taught poses bypass the IK limit check: check them here
        if (drop.q[i] < c.joint[i].min_deg || drop.q[i] > c.joint[i].max_deg) return "bin pose outside joint limits";
        if (home.q[i] < c.joint[i].min_deg || home.q[i] > c.joint[i].max_deg) return "home pose outside joint limits";
    }
    // Every frame below has the gripper pointing down, and the pitch q2 + q3 + q4 is linear in the joints, so the
    // joint-space moves keep it down all the way. From a gripper-up pose the first move would swing the 190 mm
    // gripper forward across the belt into the opposite light bar (10-22): refuse instead.
    {
        float g;
        if (tool_pitch_deg(arm_from_counts(c, prev, g)) > kToolDownMaxDeg)
            return "gripper not pointing down: reset the arm to the ready pose first";
    }
    if (tool_pitch_deg(home) > kToolDownMaxDeg) return "ready pose is not gripper-down: teach it again";
    if (tool_pitch_deg(drop) > kToolDownMaxDeg) return "bin pose is not gripper-down: teach it again";
    // a bin below the travel height: swing over at the travel height, then straight down to it (and back up)
    JointAngles over;
    const float over_dz = (c.belt_z + c.z_safe) - arm_fk(c, drop).z;
    const bool use_over = over_dz > 1.f;
    if (use_over && arm_raise(c, drop, over_dz, over))
        return "bin pose: cannot raise it to the travel height (z_safe): teach the bin closer to the base";
    const float held = std::max(part_wid - c.grip_squeeze, 0.f);
    ServoFrame f;
    auto add = [&](const JointAngles &j, float grip, uint16_t ms, const char *what) -> const char * {
        f = ServoFrame{};
        if (const char *why = arm_to_counts(c, j, grip, f.pos)) return why;
        f.ms = ms ? ms : move_ms(c, out.empty() ? prev : out.back().pos, f.pos);
        f.what = what;
        out.push_back(f);
        return nullptr;
    };
    const char *why;
    if ((why = rise_first(c, prev, out, 0))) return why;   // starting low (e.g. stopped at the bin): up first
    if ((why = add(above, open_mm, 0, "above"))) return why;
    if ((why = add(down, open_mm, c.descend_ms, "descend"))) return why;
    if ((why = add(down, held, c.close_ms, "close"))) return why;
    out.back().check_grasp = true;
    if ((why = add(above, held, c.descend_ms, "lift"))) return why;
    out.back().belt_clear = true;
    if (use_over && (why = add(over, held, 0, "over bin"))) return why;
    if ((why = add(drop, held, 0, "to bin"))) return why;
    if ((why = add(drop, open_mm, c.open_ms, "release"))) return why;
    if (use_over && (why = add(over, open_mm, 0, "rise"))) return why;
    // back at the ready pose with the taught opening, not the part's: at posture B the fingers close radially and
    // the inner one points at the black board under the base, ~2 cm away at 40 mm open (10-26)
    if ((why = add(home, c.home_grip_count >= 0 ? grip_mm(c, c.home_grip_count) : open_mm, 0, "home"))) return why;
    return nullptr;
}
ArmBaseFit fit_arm_base(const std::vector<Vec3> &arm, const std::vector<Vec2> &table, float marker_h)
{
    ArmBaseFit f;
    const size_t n = arm.size();
    if (n != table.size()) { f.err = "point lists differ in length"; return f; }
    if (n < 3) { f.err = "need at least 3 points (2 fix the transform exactly, a third checks it)"; return f; }
    std::vector<Vec2> src;   // planar part of the arm-frame points
    src.reserve(n);
    double z = 0;
    for (size_t i = 0; i < n; i++) { src.push_back({arm[i].x, arm[i].y}); z += arm[i].z; }
    // table = base + R(yaw) * arm, the convention of table_to_arm; residual = per-point hand-eye error (mm)
    if (!fit_rigid_2d(src.data(), table.data(), n, f.base_xy, f.base_yaw, &f.rms, &f.max, &f.resid)) {
        f.err = "points too close together: spread the marker over >= 40 mm of the reachable view";
        return f;
    }
    f.belt_z = float(z / n - marker_h);
    f.ok = true;
    return f;
}

}  // namespace des
