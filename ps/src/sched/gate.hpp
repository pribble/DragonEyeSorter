// gate.hpp — geometry of a side diverter gate: a swing paddle on a vertical pivot at one belt edge
// (10_项目文档/侧边导流挡板方案.md §2c, docs/运动控制与分拣.md "NG 分流").
//
// Table frame (x downstream, y lateral). Paddle angles are measured from the upstream direction (-x) toward the belt
// centre: closed = across the part lane, slanting upstream, so a part that meets it is pushed along it to the pivot
// and off the belt; open = turned away (along the edge outside the belt, or lying in the far quarter of the belt).
// Moving between the two the paddle sweeps a circular sector; |closed - open| < 180 deg keeps that sector convex, so
// on a line y the centres of a disc of radius r that touch it form one interval (found by a convex search below).
#pragma once
#include <cmath>
#include "geom.hpp"
namespace des {
struct GateGeom {
    Vec2 pivot{0, -150};   // table mm: on the belt edge (a gate on the -y edge points toward +y)
    float len = 230;       // paddle length (mm)
    float closed = 55 * kPi / 180, open = -20 * kPi / 180;   // rad, see above
    float into() const { return pivot.y < 0 ? 1.f : -1.f; }  // +1: the belt centre is toward +y
    Vec2 ray(float a) const { return {-std::cos(a), into() * std::sin(a)}; }
    Vec2 tip(float a) const { return pivot + ray(a) * len; }
};
// distance from c to the paddle segment at angle a
inline float gate_seg_dist(const GateGeom &g, float a, Vec2 c)
{
    const Vec2 u = g.ray(a), v = c - g.pivot;
    const float t = clampf(dot(v, u), 0.f, g.len);
    return norm(v - u * t);
}
// distance from c to the swept sector (0 inside)
inline float gate_sweep_dist(const GateGeom &g, Vec2 c)
{
    const float a0 = std::fmin(g.open, g.closed), a1 = std::fmax(g.open, g.closed);
    const Vec2 v = c - g.pivot;
    const float rho = norm(v), phi = std::atan2(g.into() * v.y, -v.x);
    if (phi >= a0 && phi <= a1) return std::fmax(0.f, rho - g.len);
    return std::fmin(gate_seg_dist(g, a0, c), gate_seg_dist(g, a1, c));
}
struct XRange {
    bool hit = false;
    float x0 = 0, x1 = 0;
};
// centres x on the line y whose disc of radius r touches the region (f = distance - r is convex in x)
template <class Dist>
inline XRange gate_x_range(const GateGeom &g, float y, float r, Dist dist_fn)
{
    float lo = g.pivot.x - g.len - r - 2, hi = g.pivot.x + g.len + r + 2;
    const float eps = 1e-3f;   // touching within this counts (r = 0: a single point)
    auto f = [&](float x) { return dist_fn(Vec2{x, y}) - r - eps; };
    float a = lo, b = hi;   // golden-section search for the minimum
    const float k = 0.381966f;
    for (int i = 0; i < 60; i++) {
        const float m1 = a + k * (b - a), m2 = b - k * (b - a);
        if (f(m1) < f(m2)) b = m2; else a = m1;
    }
    const float xm = 0.5f * (a + b);
    XRange out;
    if (f(xm) > 0) return out;
    out.hit = true;
    float l = lo, m = xm;   // f(l) > 0 >= f(m)
    for (int i = 0; i < 40; i++) { const float c = 0.5f * (l + m); if (f(c) > 0) l = c; else m = c; }
    out.x0 = m;
    float n = xm, h = hi;   // f(n) <= 0 < f(h)
    for (int i = 0; i < 40; i++) { const float c = 0.5f * (n + h); if (f(c) > 0) h = c; else n = c; }
    out.x1 = n;
    return out;
}
inline XRange gate_closed_range(const GateGeom &g, float y, float r)
{
    return gate_x_range(g, y, r, [&](Vec2 c) { return gate_seg_dist(g, g.closed, c); });
}
inline XRange gate_sweep_range(const GateGeom &g, float y, float r)
{
    return gate_x_range(g, y, r, [&](Vec2 c) { return gate_sweep_dist(g, c); });
}
// the closed paddle crosses the lateral position y with at least margin of paddle beyond it (else a part there is
// only grazed by the tip, or missed)
inline bool gate_reaches(const GateGeom &g, float y, float margin)
{
    const float d = (y - g.pivot.y) * g.into();   // lateral distance from the pivot edge toward the belt centre
    return d > 0 && d + margin <= g.len * std::sin(g.closed);
}
// belt travel from meeting the closed paddle until a part at lateral y is past the pivot: it slides along the paddle
// at the belt speed's component along it (no friction on the paddle): estimate for a gate's hold distance
inline float gate_slide_mm(const GateGeom &g, float y)
{
    const float d = (y - g.pivot.y) * g.into(), s = std::sin(g.closed), c = std::cos(g.closed);
    if (d <= 0 || s <= 0) return 0;
    return (d / s) / std::fmax(c, 0.1f);
}
}  // namespace des
