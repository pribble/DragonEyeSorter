// geom.hpp — small 2-D helpers shared by tracker / picker / app (no dependencies).
// Frames: image px (u, v) 1920x1080 origin top-left; table mm (x along belt travel downstream +, y across the belt,
// 90 deg counter-clockwise from x seen from above, right-handed z up — see measure/calib.hpp); belt mm s = x - B
// (constant while a point rides).
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
namespace des {
constexpr float kPi = 3.14159265358979f;
struct Vec2 { float x = 0, y = 0; };
inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, float k) { return {a.x * k, a.y * k}; }
inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float norm(Vec2 a) { return std::sqrt(dot(a, a)); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float deg2rad(float deg) { return deg * kPi / 180.f; }
inline float rad2deg(float rad) { return rad * 180.f / kPi; }
inline Vec2 dir(float a) { return {std::cos(a), std::sin(a)}; }   // unit vector, table frame (0 = +x, CCW)
inline float dist(Vec2 a, Vec2 b) { return norm(a - b); }
inline float circ_mean2(float s, float c) { return 0.5f * std::atan2(s, c); }   // mean of an axis angle from 2a sums
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
// Planar homography, row-major 3x3, mapping image px -> table mm (calibration result).
struct Homography {
    std::array<double, 9> h{{1, 0, 0, 0, 1, 0, 0, 0, 1}};
    Vec2 map(Vec2 p) const
    {
        double w = h[6] * p.x + h[7] * p.y + h[8];
        return {float((h[0] * p.x + h[1] * p.y + h[2]) / w), float((h[3] * p.x + h[4] * p.y + h[5]) / w)};
    }
    Homography inverse() const   // table mm -> image px (drawing tracks back onto the frame)
    {
        double a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7], i = h[8];
        double A = e * i - f * k, B = f * g - d * i, C = d * k - e * g, D = c * k - b * i, E = a * i - c * g,
               F = b * g - a * k, G = b * f - c * e, H = c * d - a * f, I = a * e - b * d;
        const double det = a * A + b * B + c * C;
        Homography r;
        r.h = {A / det, D / det, G / det, B / det, E / det, H / det, C / det, F / det, I / det};
        return r;
    }
    Homography normalized() const   // divide by h[8] (calibration results are stored normalized; = calib.cpp)
    {
        Homography r;
        for (int i = 0; i < 9; i++) r.h[i] = h[i] / h[8];
        return r;
    }
    Homography translated(double dx, double dy) const   // pre-translate the source frame (track_measure shifted())
    {
        Homography r = *this;
        r.h[2] = h[0] * dx + h[1] * dy + h[2]; r.h[5] = h[3] * dx + h[4] * dy + h[5]; r.h[8] = h[6] * dx + h[7] * dy + h[8];
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
        const Vec2 a{std::cos(angle), std::sin(angle)}, b{-a.y, a.x}, da = a * half_len, db = b * half_wid;
        return {{c + da + db, c - da + db, c - da - db, c + da - db}};
    }
};
// Separating-axis test: both own axes, cached centre / half extents, no corner reconstruction. Touching = overlap.
inline bool overlap(const ORect &p, const ORect &q)
{
    const Vec2 d = q.c - p.c;
    const float as[4] = {p.angle, p.angle + kPi / 2, q.angle, q.angle + kPi / 2};
    for (float ang : as) {
        const Vec2 ax{std::cos(ang), std::sin(ang)};
        const float dp = p.half_len * std::fabs(std::cos(ang - p.angle)) + p.half_wid * std::fabs(std::sin(ang - p.angle));
        const float dq = q.half_len * std::fabs(std::cos(ang - q.angle)) + q.half_wid * std::fabs(std::sin(ang - q.angle));
        if (std::fabs(dot(d, ax)) > dp + dq) return false;
    }
    return true;
}
// Row-major 3x3 product: (a * b).map(p) == a.map(b.map(p)) — the convention of calib.cpp / roi_crop.cpp
inline Homography operator*(const Homography &a, const Homography &b)
{
    Homography r;
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++)
        r.h[3 * i + j] = a.h[3 * i] * b.h[j] + a.h[3 * i + 1] * b.h[3 + j] + a.h[3 * i + 2] * b.h[6 + j];
    return r;
}
// Axis-aligned float box; empty until a point is added (valid() is false while empty).
struct Box {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    void add(Vec2 p)
    {
        x0 = std::fmin(x0, p.x); x1 = std::fmax(x1, p.x); y0 = std::fmin(y0, p.y); y1 = std::fmax(y1, p.y);
    }
    bool valid() const { return x0 <= x1 && y0 <= y1; }
};
// Bounding box of an oriented rectangle in the target frame (to_px = table mm -> image px), grown by pad px.
inline Box image_box(const Homography &to_px, const ORect &r, int pad = 0)
{
    Box b;
    for (const Vec2 &v : r.corners()) b.add(to_px.map(v));
    b.x0 -= float(pad); b.y0 -= float(pad); b.x1 += float(pad); b.y1 += float(pad);
    return b;
}
// Algebraic (Kasa) circle fit on centered coordinates: the exact algebra of measure.cpp fit_circle.
inline bool fit_circle(const Vec2 *pts, size_t n, Vec2 &c, float &r, float *rms = nullptr)
{
    if (n < 3) return false;
    double mx = 0, my = 0;
    for (size_t i = 0; i < n; i++) { mx += pts[i].x; my += pts[i].y; }
    mx /= n; my /= n;
    double suu = 0, svv = 0, suv = 0, suuu = 0, svvv = 0, suvv = 0, svuu = 0;
    for (size_t i = 0; i < n; i++) {
        double u = pts[i].x - mx, v = pts[i].y - my;
        suu += u * u; svv += v * v; suv += u * v;
        suuu += u * u * u; svvv += v * v * v; suvv += u * v * v; svuu += v * u * u;
    }
    double det = suu * svv - suv * suv;
    if (std::fabs(det) < 1e-12) return false;
    double b1 = 0.5 * (suuu + suvv), b2 = 0.5 * (svvv + svuu);
    double uc = (b1 * svv - b2 * suv) / det, vc = (b2 * suu - b1 * suv) / det;
    c = {float(mx + uc), float(my + vc)};
    r = float(std::sqrt(uc * uc + vc * vc + (suu + svv) / n));
    if (rms) {
        double se = 0;
        for (size_t i = 0; i < n; i++) { double d = norm(pts[i] - c) - r; se += d * d; }
        *rms = float(std::sqrt(se / n));
    }
    return true;
}
// Rigid 2-D fit dst = t + R(yaw) * src (least squares) — the exact math / ordering of arm.cpp fit_arm_base: same
// centroids, sxx / sxy, 20 mm spread gate, float t in the residuals, rms = sqrt(ss / n), max = largest |residual|,
// resid (optional) = per-point residual (mm). False with < 3 points or spread < 20 mm.
inline bool fit_rigid_2d(const Vec2 *src, const Vec2 *dst, size_t n, Vec2 &t, float &yaw, float *rms = nullptr,
                         float *max = nullptr, std::vector<float> *resid = nullptr)
{
    if (n < 3) return false;
    double ax = 0, ay = 0, tx = 0, ty = 0;
    for (size_t i = 0; i < n; i++) { ax += src[i].x; ay += src[i].y; tx += dst[i].x; ty += dst[i].y; }
    ax /= n; ay /= n; tx /= n; ty /= n;
    double sxx = 0, sxy = 0, spread = 0;
    for (size_t i = 0; i < n; i++) {
        const double px = src[i].x - ax, py = src[i].y - ay, qx = dst[i].x - tx, qy = dst[i].y - ty;
        sxx += px * qx + py * qy; sxy += px * qy - py * qx;
        spread = std::max(spread, std::sqrt(qx * qx + qy * qy));
    }
    if (spread < 20) return false;
    const double a = std::atan2(sxy, sxx), c = std::cos(a), s = std::sin(a);
    yaw = float(a);
    t = {float(tx - (c * ax - s * ay)), float(ty - (s * ax + c * ay))};
    double ss = 0;
    float emax = 0;
    if (resid) resid->reserve(resid->size() + n);
    for (size_t i = 0; i < n; i++) {
        const double ex = t.x + c * src[i].x - s * src[i].y - dst[i].x;
        const double ey = t.y + s * src[i].x + c * src[i].y - dst[i].y;
        const float e = float(std::sqrt(ex * ex + ey * ey));
        if (resid) resid->push_back(e);
        emax = std::max(emax, e);
        ss += ex * ex + ey * ey;
    }
    if (rms) *rms = float(std::sqrt(ss / n));
    if (max) *max = emax;
    return true;
}
}  // namespace des
