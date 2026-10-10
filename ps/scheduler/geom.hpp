// geom.hpp — small 2-D helpers shared by tracker / picker / app (no dependencies).
//
// Frames used across ps/:
//   image px      (u, v)  camera pixels, 1920x1080, origin top-left
//   table mm      (x, y)  fixed to the machine: x along belt travel (downstream +), y across the belt, 90 deg
//                         counter-clockwise from x seen from above (right-handed, z up; see app/calib.hpp)
//   belt mm       s       x of a point riding on the belt minus the belt travel B: s = x - B (constant while it rides)
#pragma once

#include <array>
#include <cmath>

namespace des {

constexpr float kPi = 3.14159265358979f;

struct Vec2 {
    float x = 0, y = 0;
};
inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, float k) { return {a.x * k, a.y * k}; }
inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float norm(Vec2 a) { return std::sqrt(dot(a, a)); }

// Fold an axis angle (a line direction, 180 deg symmetric) into [-pi/2, pi/2).
inline float wrap_axis(float a)
{
    while (a >= kPi / 2) a -= kPi;
    while (a < -kPi / 2) a += kPi;
    return a;
}
inline float wrap_pi(float a)
{
    while (a >= kPi) a -= 2 * kPi;
    while (a < -kPi) a += 2 * kPi;
    return a;
}

// Planar homography, row-major 3x3. Maps image px -> table mm (calibration result).
struct Homography {
    std::array<double, 9> h{{1, 0, 0, 0, 1, 0, 0, 0, 1}};
    Vec2 map(Vec2 p) const
    {
        double w = h[6] * p.x + h[7] * p.y + h[8];
        return {float((h[0] * p.x + h[1] * p.y + h[2]) / w), float((h[3] * p.x + h[4] * p.y + h[5]) / w)};
    }
    // table mm -> image px (for drawing tracks back onto the frame)
    Homography inverse() const
    {
        double a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7], i = h[8];
        double A = e * i - f * k, B = f * g - d * i, C = d * k - e * g;
        double D = c * k - b * i, E = a * i - c * g, F = b * g - a * k;
        double G = b * f - c * e, H = c * d - a * f, I = a * e - b * d;
        double det = a * A + b * B + c * C;
        Homography r;
        r.h = {A / det, D / det, G / det, B / det, E / det, H / det, C / det, F / det, I / det};
        return r;
    }
};

// Oriented rectangle: center, half extents along its own axes, axis angle of the "len" side.
struct ORect {
    Vec2 c;
    float half_len = 0, half_wid = 0;
    float angle = 0;   // direction of the len side, radians
    std::array<Vec2, 4> corners() const
    {
        Vec2 a{std::cos(angle), std::sin(angle)}, b{-a.y, a.x};
        Vec2 da = a * half_len, db = b * half_wid;
        return {{c + da + db, c - da + db, c - da - db, c + da - db}};
    }
};

// Separating-axis test for two oriented rectangles.
inline bool overlap(const ORect &p, const ORect &q)
{
    auto pc = p.corners(), qc = q.corners();
    const float angs[4] = {p.angle, p.angle + kPi / 2, q.angle, q.angle + kPi / 2};
    for (float ang : angs) {
        Vec2 ax{std::cos(ang), std::sin(ang)};
        float pmin = 1e30f, pmax = -1e30f, qmin = 1e30f, qmax = -1e30f;
        for (auto &v : pc) { float d = dot(v, ax); pmin = std::fmin(pmin, d); pmax = std::fmax(pmax, d); }
        for (auto &v : qc) { float d = dot(v, ax); qmin = std::fmin(qmin, d); qmax = std::fmax(qmax, d); }
        if (pmax < qmin || qmax < pmin) return false;
    }
    return true;
}

}  // namespace des
