// measure_gear.cpp — gear branch of the geometric measurement (measure.hpp): outline circle fit -> r(theta) -> tooth
// count (spec mix-up) -> tip (OD) / root circle fits -> bore + eccentricity; per tooth short / missing, per gap flash.
#include "measure.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "fit.hpp"
namespace des {

GearMeasure measure_gear(const cv::Mat &gray, cv::Rect roi, const Homography &h, const MeasureConfig &cfg,
                         std::vector<float> *r_theta)
{
    GearMeasure g;
    meas::Scratch &s = meas::scratch();
    float thr;
    if (!meas::find_part(gray, roi, s.outer, s.hole, thr, s)) { g.err = "no part in ROI"; return g; }
    meas::edges(gray, s.outer, roi.tl(), thr, h, cfg, cfg.gear_h_mm, false, s.oe);
    if (s.oe.size() < 60) { g.err = "outline too small"; return g; }
    s.pts.clear(); s.pts.reserve(s.oe.size());
    for (const meas::Edge &e : s.oe) s.pts.push_back(e.p);
    Vec2 c0; float r0;
    meas::fit_circle(s.pts, c0, r0);
    g.smear_mm = meas::smear_at(cfg, cfg.gear_h_mm, c0);
    g.teeth_judged = g.smear_mm <= cfg.max_smear_mm;
    const int NB = 720;   // r(theta) bins, 0.5 deg
    s.prof.assign(NB, -1.f);
    s.bin_of.resize(s.pts.size()); s.rad.resize(s.pts.size());
    int filled = 0;
    for (size_t i = 0; i < s.pts.size(); i++) {
        const Vec2 d = s.pts[i] - c0;
        float th = std::atan2(d.y, d.x);
        if (th < 0) th += 2 * kPi;
        const int b = std::min(NB - 1, int(th / (2 * kPi) * NB));
        s.bin_of[i] = b;
        s.rad[i] = norm(d);
        if (s.prof[b] < 0) filled++;
        s.prof[b] = std::max(s.prof[b], s.rad[i]);
    }
    if (filled < NB / 4) { g.err = "no tooth structure"; return g; }   // too sparse to carry tooth structure
    meas::fill_empty_bins(s.prof);
    if (r_theta) *r_theta = s.prof;   // des_tool inset; the live path (nullptr) copies nothing
    const float tip_est = meas::percentile(s.prof, 0.95f), root_est = meas::percentile(s.prof, 0.05f);
    const float mid = 0.5f * (tip_est + root_est);
    meas::runs_above(s.prof, mid, true, s.runs);
    if (s.runs.size() < 6) { g.err = "no tooth structure"; return g; }
    float w_med = 0, tip_med = 0, pitch0 = 0;
    // Mean of the one-pitch spacings around the median estimate p0: the median sits on the half-bin grid of the
    // tooth centres, and the double spacings over missing teeth push it up a step -> 720 / 24.5 rounds a 30-tooth
    // gear to 29, the phase grid drifts and every defect count goes wild (2026-10-24, real gears with teeth
    // missing, ~1 frame in 20).
    auto refine_pitch = [](const std::vector<float> &v, float p0) {
        double sum = 0;
        int n = 0;
        for (float x : v)
            if (std::fabs(x - p0) < 0.25f * p0) { sum += x; n++; }
        return n >= 3 ? float(sum / n) : p0;
    };
    auto spacing_of = [&](size_t i) { return s.centers[(i + 1) % s.runs.size()] + (i + 1 == s.runs.size() ? NB : 0) - s.centers[i]; };
    auto stats = [&] {
        s.widths.clear();
        s.tips.clear();
        s.centers.clear();
        s.spacing.clear();
        for (const meas::Run &t : s.runs) {
            s.widths.push_back(float(t.b - t.a + 1));
            s.tips.push_back(t.peak);
            s.centers.push_back(0.5f * (t.a + t.b));
        }
        w_med = meas::median(s.widths);
        tip_med = meas::median(s.tips);
        for (size_t i = 0; i < s.runs.size(); i++) s.spacing.push_back(spacing_of(i));
        pitch0 = meas::median(s.spacing);
    };
    stats();
    auto teeth_in = [&](size_t i) { return std::max(1, 1 + int(std::lround((s.widths[i] - w_med) / pitch0))); };
    if (meas::debug_on()) {
        fprintf(stderr, "gear c0 (%.2f,%.2f) mid %.3f w_med %.1f pitch %.1f tip_med %.3f runs:", c0.x, c0.y, mid, w_med,
                pitch0, tip_med);
        for (const meas::Run &t : s.runs) fprintf(stderr, " [%d-%d %.2f]", t.a % NB, t.b % NB, t.peak);
        fprintf(stderr, "\n");
    }
    if (pitch0 > 0) {
        const float P = float(NB) / std::max(1L, std::lround(NB / refine_pitch(s.spacing, pitch0)));
        s.ph.resize(s.runs.size());
        for (size_t i = 0; i < s.runs.size(); i++) s.ph[i] = 2 * kPi * (s.runs[i].a + 0.5f * (w_med - 1)) / P;
        meas::filter_phase(s.ph, s.runs, s.kept, g.flash_gaps);
        if (s.kept.size() != s.runs.size() && s.kept.size() >= 6) {
            s.runs = s.kept;
            stats();
        }
    }
    int present = 0;
    for (size_t i = 0; i < s.runs.size(); i++) {
        present += teeth_in(i);
        g.flash_gaps += teeth_in(i) - 1;
        if (s.runs[i].peak < tip_med - cfg.short_tooth_mm) g.short_teeth++;
    }
    s.single.clear();
    for (size_t i = 0; i < s.runs.size(); i++)
        if (teeth_in(i) == 1 && teeth_in((i + 1) % s.runs.size()) == 1) s.single.push_back(spacing_of(i));
    const float pitch_bins = s.single.empty() ? refine_pitch(s.spacing, pitch0)
                                              : refine_pitch(s.single, meas::median(s.single));
    const int z = pitch_bins > 0 ? int(std::lround(NB / pitch_bins)) : present;
    g.teeth = z;
    g.missing_teeth = std::max(0, z - present);
    const float root_med = meas::filled_valleys(s.prof, s.runs, true, NB, cfg.flash_mm, s.vmins, s.gaps, g.flash_gaps);
    s.tooth_of.assign(NB, -1); s.gap_of.assign(NB, -1);
    for (size_t ti = 0; ti < s.runs.size(); ti++)
        for (int k = s.runs[ti].a; k <= s.runs[ti].b; k++) s.tooth_of[k % NB] = int(ti);
    for (size_t gi = 0; gi < s.gaps.size(); gi++)
        for (int k = s.gaps[gi].first; k <= s.gaps[gi].second; k++) s.gap_of[k % NB] = int(gi);
    s.tip_pts.clear(); s.root_pts.clear();
    for (size_t i = 0; i < s.pts.size(); i++) {
        const int ti = s.tooth_of[s.bin_of[i]], gi = s.gap_of[s.bin_of[i]];
        if (s.rad[i] <= 0 || dot(s.oe[i].n, (s.pts[i] - c0) * (1.f / s.rad[i])) < 0.97f) continue;
        if (ti >= 0) {
            const meas::Run &t = s.runs[ti];
            if (t.peak >= tip_med - cfg.short_tooth_mm && s.rad[i] >= t.peak - 0.12f) s.tip_pts.push_back(s.pts[i]);
        } else if (gi >= 0 && s.vmins[gi] <= root_med + cfg.flash_mm && s.rad[i] <= s.vmins[gi] + 0.08f) {
            s.root_pts.push_back(s.pts[i]);
        }
    }
    Vec2 ct, cr; float rt, rr;
    if (!meas::fit_circle(s.tip_pts, ct, rt, &g.fit_rms)) { g.err = "tip circle fit failed"; return g; }
    g.center = ct;
    g.od = 2 * rt + cfg.od_offset_mm;
    if (meas::fit_circle(s.root_pts, cr, rr)) g.root_d = 2 * rr;
    if (!s.hole.empty()) {
        meas::edges(gray, s.hole, roi.tl(), thr, h, cfg, cfg.gear_h_mm, true, s.he);
        s.hp.clear(); s.near.clear();
        for (const meas::Edge &e : s.he) {
            s.hp.push_back(e.p);
            if (!e.far) s.near.push_back(e.p);
        }
        Vec2 cb; float rb;
        const bool fit = meas::fit_circle(s.hp, cb, rb);
        if (fit && cfg.bore_near_arc && norm(cb - cfg.axis_table) > rb + 1.f && s.near.size() >= 12 &&
            s.near.size() * 5 >= s.hp.size()) {
            Vec2 cn; float rn;
            if (meas::fit_circle(s.near, cn, rn) && std::fabs(rn - rb) < 0.25f * rb) { cb = cn; rb = rn; }
        }
        if (fit) {
            g.bore_d = 2 * rb + cfg.bore_offset_mm;
            g.ecc = norm(cb - ct);
            g.bore_center = cb;
        }
    }
    if (!g.teeth_judged) g.short_teeth = g.missing_teeth = g.flash_gaps = 0;
    g.ok = true;
    return g;
}
}  // namespace des
