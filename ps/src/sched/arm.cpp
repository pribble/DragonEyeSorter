// arm.cpp — see arm.hpp
#include "arm.hpp"

#include <algorithm>
#include <cmath>

namespace des {

static constexpr float kDeg = 180.f / kPi;

const char *arm_ik(const ArmConfig &c, Vec3 p, float close_axis, JointAngles &out, float tilt)
{
    const float az = std::atan2(p.y, p.x), R = std::hypot(p.x, p.y);
    if (tilt != 0 && std::fabs(wrap_axis(close_axis - az - kPi / 2)) > 10.f / kDeg)
        return "tilted grasp needs a tangential closing axis";
    const float lt = c.l_wrist + c.l_tool, D = c.tool_offset, a3 = -kPi / 2 + tilt;
    const bool beside = std::fabs(std::fabs(c.servo_side) - kPi / 2) < 0.1f;   // servo beside the finger plane
    // the offset's tangential part moves the grasp point out of the arm plane: q1 aims the plane so that the
    // grasp point lands on p; q5 (from the closing axis) and q1 depend on each other -> a few fixed-point passes
    float q1 = az, q5 = 0, phi = 0, rho = R;
    for (int it = 0; it < 8; it++) {
        q5 = wrap_axis(close_axis - q1 - c.grip_axis_at_zero);
        if (tilt != 0 && beside) q5 = wrap_pi(-c.grip_axis_at_zero - c.servo_side);   // servo on the upper side
        phi = q5 + c.grip_axis_at_zero + c.servo_side;
        const float tau = D * std::sin(phi);
        if (R <= std::fabs(tau)) return "too close to the base axis";
        rho = std::sqrt(R * R - tau * tau);
        q1 = az - std::atan2(tau, rho);
    }
    // wrist pitch axis: back along the tool and the offset (arm plane: radial, up)
    float dx = rho - lt * std::cos(a3) + D * std::cos(phi) * std::sin(a3);
    float dz = p.z - lt * std::sin(a3) - D * std::cos(phi) * std::cos(a3) - c.base_h;
    if (dx < 20) return "too close for this tilt";
    float L1 = c.l_upper, L2 = c.l_fore;
    float c3 = (dx * dx + dz * dz - L1 * L1 - L2 * L2) / (2 * L1 * L2);
    if (c3 > 1.f || c3 < -1.f) return "out of reach";
    float q3 = -std::acos(c3);                        // elbow up
    float q2 = std::atan2(dz, dx) - std::atan2(L2 * std::sin(q3), L1 + L2 * std::cos(q3));
    float q4 = a3 - (q2 + q3);

    out.q = {q1 * kDeg, q2 * kDeg, q3 * kDeg, q4 * kDeg, q5 * kDeg};
    static const char *lim[5] = {"joint 1 limit", "joint 2 limit", "joint 3 limit", "joint 4 limit", "joint 5 limit"};
    for (int i = 0; i < 5; i++)
        if (out.q[i] < c.joint[i].min_deg || out.q[i] > c.joint[i].max_deg) return lim[i];
    return nullptr;
}

Vec3 arm_fk(const ArmConfig &c, const JointAngles &j)
{
    float q1 = j.q[0] / kDeg, a1 = j.q[1] / kDeg, a2 = a1 + j.q[2] / kDeg, a3 = a2 + j.q[3] / kDeg;
    float lt = c.l_wrist + c.l_tool, D = c.tool_offset;
    float phi = j.q[4] / kDeg + c.grip_axis_at_zero + c.servo_side;
    float r = c.l_upper * std::cos(a1) + c.l_fore * std::cos(a2) + lt * std::cos(a3) - D * std::cos(phi) * std::sin(a3);
    float t = D * std::sin(phi);
    float z = c.base_h + c.l_upper * std::sin(a1) + c.l_fore * std::sin(a2) + lt * std::sin(a3) +
              D * std::cos(phi) * std::cos(a3);
    return {r * std::cos(q1) - t * std::sin(q1), r * std::sin(q1) + t * std::cos(q1), z};
}

Vec3 table_to_arm(const ArmConfig &c, Vec2 p, float z_above_belt)
{
    float dx = p.x - c.base_xy.x, dy = p.y - c.base_xy.y;
    float cy = std::cos(c.base_yaw), sy = std::sin(c.base_yaw);
    return {cy * dx + sy * dy, -sy * dx + cy * dy, c.belt_z + z_above_belt};
}

float table_axis_to_arm(const ArmConfig &c, float axis_table) { return wrap_axis(axis_table - c.base_yaw); }

float arm_tangential_axis(const ArmConfig &c, Vec2 p)
{
    return wrap_axis(std::atan2(p.y - c.base_xy.y, p.x - c.base_xy.x) + kPi / 2);
}

const char *arm_to_counts(const ArmConfig &c, const JointAngles &j, float grip_mm, std::array<uint16_t, 6> &pos)
{
    static const char *rng[5] = {"servo 1 count range", "servo 2 count range", "servo 3 count range",
                                 "servo 4 count range", "servo 5 count range"};
    for (int i = 0; i < 5; i++) {
        float v = c.joint[i].zero_count + c.joint[i].counts_per_deg * j.q[i];
        if (v < 0 || v > 4095) return rng[i];
        pos[i] = uint16_t(std::lround(v));
    }
    float g = std::min(std::max(grip_mm, 0.f), c.grip_max_open) / c.grip_max_open;
    pos[5] = uint16_t(std::lround(c.grip_closed_count + (c.grip_open_count - c.grip_closed_count) * g));
    return nullptr;
}

JointAngles arm_from_counts(const ArmConfig &c, const std::array<uint16_t, 6> &pos, float &grip_mm)
{
    JointAngles j;
    for (int i = 0; i < 5; i++) {
        float cpd = c.joint[i].counts_per_deg;
        j.q[i] = cpd != 0 ? (pos[i] - c.joint[i].zero_count) / cpd : 0.f;
    }
    float span = c.grip_open_count - c.grip_closed_count;
    grip_mm = span != 0 ? (pos[5] - c.grip_closed_count) / span * c.grip_max_open : 0.f;
    return j;
}

static uint16_t move_ms(const ArmConfig &c, const std::array<uint16_t, 6> &a, const std::array<uint16_t, 6> &b)
{
    float worst = 0;
    for (int i = 0; i < 5; i++) {
        float cpd = std::fabs(c.joint[i].counts_per_deg);
        if (cpd > 0) worst = std::max(worst, std::abs(int(a[i]) - int(b[i])) / cpd);
    }
    float ms = worst / c.joint_speed * 1000.f;
    return uint16_t(std::min(std::max(ms, float(c.min_ms)), 5000.f));
}

const char *plan_pick(const ArmConfig &c, Vec2 p_table, float close_axis_table, float open_mm, float part_wid,
                      int bin, const std::array<uint16_t, 6> &prev, std::vector<ServoFrame> &out, float *tilt_deg)
{
    out.clear();
    float ax = table_axis_to_arm(c, close_axis_table);
    JointAngles above, down, home, drop;
    const char *vertical_why = nullptr;
    for (float t = 0;; t += c.tilt_step_deg) {
        const float tr = t / kDeg;
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
    // taught poses bypass the IK limit check: check them here
    for (int i = 0; i < 5; i++) {
        if (drop.q[i] < c.joint[i].min_deg || drop.q[i] > c.joint[i].max_deg) return "bin pose outside joint limits";
        if (home.q[i] < c.joint[i].min_deg || home.q[i] > c.joint[i].max_deg) return "home pose outside joint limits";
    }
    float held = std::max(part_wid - c.grip_squeeze, 0.f);

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
    if ((why = add(above, open_mm, 0, "above"))) return why;
    if ((why = add(down, open_mm, c.descend_ms, "descend"))) return why;
    if ((why = add(down, held, c.close_ms, "close"))) return why;
    out.back().check_grasp = true;
    if ((why = add(above, held, c.descend_ms, "lift"))) return why;
    out.back().belt_clear = true;
    if ((why = add(drop, held, 0, "to bin"))) return why;
    if ((why = add(drop, open_mm, c.open_ms, "release"))) return why;
    if ((why = add(home, open_mm, 0, "home"))) return why;
    return nullptr;
}

ArmBaseFit fit_arm_base(const std::vector<Vec3> &arm, const std::vector<Vec2> &table, float marker_h)
{
    ArmBaseFit f;
    const size_t n = arm.size();
    if (n != table.size()) { f.err = "point lists differ in length"; return f; }
    if (n < 3) { f.err = "need at least 3 points (2 fix the transform exactly, a third checks it)"; return f; }
    double ax = 0, ay = 0, tx = 0, ty = 0, z = 0;
    for (size_t i = 0; i < n; i++) {
        ax += arm[i].x; ay += arm[i].y; tx += table[i].x; ty += table[i].y; z += arm[i].z;
    }
    ax /= n; ay /= n; tx /= n; ty /= n; z /= n;
    double sxx = 0, sxy = 0, spread = 0;
    for (size_t i = 0; i < n; i++) {
        const double px = arm[i].x - ax, py = arm[i].y - ay, qx = table[i].x - tx, qy = table[i].y - ty;
        sxx += px * qx + py * qy;
        sxy += px * qy - py * qx;
        spread = std::max(spread, std::sqrt(qx * qx + qy * qy));
    }
    if (spread < 20) { f.err = "points too close together: spread the marker over >= 40 mm of the reachable view"; return f; }
    const double yaw = std::atan2(sxy, sxx), c = std::cos(yaw), s = std::sin(yaw);
    f.base_yaw = float(yaw);
    f.base_xy = {float(tx - (c * ax - s * ay)), float(ty - (s * ax + c * ay))};
    f.belt_z = float(z - marker_h);
    double ss = 0;
    for (size_t i = 0; i < n; i++) {
        const double ex = f.base_xy.x + c * arm[i].x - s * arm[i].y - table[i].x;
        const double ey = f.base_xy.y + s * arm[i].x + c * arm[i].y - table[i].y;
        const float e = float(std::sqrt(ex * ex + ey * ey));
        f.resid.push_back(e);
        f.max = std::max(f.max, e);
        ss += ex * ex + ey * ey;
    }
    f.rms = float(std::sqrt(ss / n));
    f.ok = true;
    return f;
}

}  // namespace des
