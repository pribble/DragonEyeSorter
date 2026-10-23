// measure_rack.cpp — rack branch of the geometric measurement (measure.hpp): main axis, length, tooth positions on
// the toothed edge -> mean pitch by linear regression (~pi for m = 1), height, short / missing / flash.
#include "measure.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <opencv2/imgproc.hpp>
#include "fit.hpp"
namespace des {

RackMeasure measure_rack(const cv::Mat &gray, cv::Rect roi, const Homography &h, const MeasureConfig &cfg)
{
    RackMeasure m;
    meas::Scratch &s = meas::scratch();
    float thr;
    if (!meas::find_part(gray, roi, s.outer, s.hole, thr, s)) { m.err = "no part in ROI"; return m; }
    meas::edges(gray, s.outer, roi.tl(), thr, h, cfg, cfg.rack_h_mm, false, s.oe);
    if (s.oe.size() < 60) { m.err = "outline too small"; return m; }
    s.cvp.clear(); s.cvp.reserve(s.oe.size());
    for (const meas::Edge &e : s.oe) s.cvp.emplace_back(e.p.x, e.p.y);
    const cv::RotatedRect rr = cv::minAreaRect(s.cvp);
    const float ang = rr.angle * kPi / 180;
    Vec2 eu{std::cos(ang), std::sin(ang)};
    if (rr.size.height > rr.size.width) eu = {-eu.y, eu.x};   // u along the long side
    const Vec2 ev{-eu.y, eu.x};
    const Vec2 c{rr.center.x, rr.center.y};
    s.us.clear(); s.vs.clear();
    float umin = 1e9f, umax = -1e9f, vmin = 1e9f, vmax = -1e9f;
    for (const meas::Edge &e : s.oe) {
        const float u = dot(e.p - c, eu), v = dot(e.p - c, ev);
        s.us.push_back(u);
        s.vs.push_back(v);
        umin = std::min(umin, u); umax = std::max(umax, u);
        vmin = std::min(vmin, v); vmax = std::max(vmax, v);
    }
    m.length = umax - umin; m.height = vmax - vmin;
    auto median_where = [&](const std::vector<float> &val, auto pred) {
        s.tmp.clear();
        for (size_t i = 0; i < s.oe.size(); i++)
            if (pred(i)) s.tmp.push_back(val[i]);
        return std::make_pair(s.tmp.size() >= 5, meas::median(s.tmp));
    };
    {   // end faces: facing along the length AND at the extremes (steep tooth flanks also face along the length)
        const auto l = median_where(s.us, [&](size_t i) { return dot(s.oe[i].n, eu) < -0.95f && s.us[i] < umin + 0.3f; });
        const auto r = median_where(s.us, [&](size_t i) { return dot(s.oe[i].n, eu) > 0.95f && s.us[i] > umax - 0.3f; });
        if (l.first && r.first) m.length = r.second - l.second;
    }
    m.axis = wrap_axis(std::atan2(eu.y, eu.x));
    m.center = c;
    const float bin = 0.1f, margin = 1.0f;
    const int NB = std::max(1, int((umax - umin - 2 * margin) / bin));
    s.top.assign(NB, -1e9f); s.bot.assign(NB, 1e9f);
    for (size_t i = 0; i < s.us.size(); i++) {
        const int b = int((s.us[i] - umin - margin) / bin);
        if (b < 0 || b >= NB) continue;
        const float nv = dot(s.oe[i].n, ev);
        if (nv > 0.05f) s.top[b] = std::max(s.top[b], s.vs[i]);
        if (nv < -0.05f) s.bot[b] = std::min(s.bot[b], s.vs[i]);
    }
    auto fill = [](std::vector<float> &p, float sign) {   // sign-flip to "outward = larger", fill empty bins
        int first = -1;
        for (size_t i = 0; i < p.size(); i++)
            if (std::fabs(p[i]) < 1e8f) { first = int(i); break; }
        if (first < 0) return false;
        float last = p[first];
        for (float &x : p) {
            if (std::fabs(x) < 1e8f) last = x;
            x = sign * last;
        }
        return true;
    };
    if (!fill(s.top, 1.f) || !fill(s.bot, -1.f)) { m.err = "rack edges not found"; return m; }
    auto spread = [](const std::vector<float> &p) { return meas::percentile(p, 0.95f) - meas::percentile(p, 0.05f); };
    const bool teeth_pos = spread(s.top) >= spread(s.bot);   // teeth on the +v side
    const std::vector<float> &prof = teeth_pos ? s.top : s.bot;
    const float tip = meas::percentile(prof, 0.95f), root = meas::percentile(prof, 0.05f), mid = 0.5f * (tip + root);
    {   // height = tip land line - back line (bottom-of-tooth side flat), medians of points facing straight out
        const float sg = teeth_pos ? 1.f : -1.f;
        const auto back = median_where(s.vs, [&](size_t i) { return sg * dot(s.oe[i].n, ev) < -0.95f; });
        const auto tips = median_where(s.vs, [&](size_t i) { return sg * dot(s.oe[i].n, ev) > 0.95f && sg * s.vs[i] > mid; });
        if (back.first && tips.first) m.height = std::fabs(tips.second - back.second);
    }
    if (meas::debug_on()) {
        fprintf(stderr, "rack tip %.3f root %.3f mid %.3f NB %d\n", tip, root, mid, NB);
        for (int i = 0; i < NB; i++) fprintf(stderr, "%.2f%c", prof[i], i % 20 == 19 ? '\n' : ' ');
        fprintf(stderr, "\n");
    }
    meas::runs_above(prof, mid, false, s.all_runs);
    s.runs.clear();
    for (const meas::Run &r : s.all_runs)
        if (r.a > 0 && r.b < NB - 1) s.runs.push_back(r);
    if (s.runs.size() < 3) { m.err = "no tooth structure"; return m; }
    auto cross = [&](int i0, int i1) {   // sub-bin mid crossing, u in mm from the profile start
        const float a = prof[i0], b = prof[i1];
        const float f = std::fabs(b - a) > 1e-6f ? (mid - a) / (b - a) : 0.5f;
        return (i0 + 0.5f + f * float(i1 - i0)) * bin;
    };
    float w_med = 0, tip_med = 0;
    auto stats = [&] {
        s.widths.clear();
        s.tips.clear();
        s.ua.clear();
        s.ub.clear();
        for (const meas::Run &r : s.runs) {
            s.ua.push_back(cross(r.a - 1, r.a));
            s.ub.push_back(cross(r.b, r.b + 1));
            s.widths.push_back(s.ub.back() - s.ua.back());
            s.tips.push_back(r.peak);
        }
        w_med = meas::median(s.widths);
        tip_med = meas::median(s.tips);
    };
    stats();
    const float pitch_nom = kPi;   // module 1
    s.ph.resize(s.runs.size());
    for (size_t i = 0; i < s.runs.size(); i++) s.ph[i] = 2 * kPi * (s.ua[i] + 0.5f * w_med) / pitch_nom;
    meas::filter_phase(s.ph, s.runs, s.kept, m.flash_gaps);
    if (s.kept.size() != s.runs.size() && s.kept.size() >= 3) {
        s.runs = s.kept;
        stats();
    }
    auto teeth_in = [&](size_t i) { return std::max(1, 1 + int(std::lround((s.widths[i] - w_med) / pitch_nom))); };
    const float u_first = s.ua[0] + 0.5f * w_med;   // center of the first tooth = slot 0
    int present = 0, k_last = 0, n = 0;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (size_t i = 0; i < s.runs.size(); i++) {
        const int nt = teeth_in(i);
        m.flash_gaps += nt - 1;
        present += nt;
        if (s.runs[i].peak < tip_med - cfg.short_tooth_mm) m.short_teeth++;
        const int k = int(std::lround((s.ua[i] + 0.5f * w_med - u_first) / pitch_nom));
        k_last = k + nt - 1;
        if (nt == 1) {   // mean pitch: regression of clean tooth centers on their slot index
            const float uc = 0.5f * (s.ua[i] + s.ub[i]);
            sx += k; sy += uc; sxx += double(k) * k; sxy += k * uc; n++;
        }
    }
    m.teeth = k_last + 1; m.missing_teeth = std::max(0, m.teeth - present);
    meas::filled_valleys(prof, s.runs, false, NB, cfg.flash_mm, s.vmins, s.gaps, m.flash_gaps);
    if (n >= 2) m.pitch = float((n * sxy - sx * sy) / (n * sxx - sx * sx));
    m.smear_mm = meas::smear_at(cfg, cfg.rack_h_mm, c); m.teeth_judged = m.smear_mm <= cfg.max_smear_mm &&
        (cfg.judge_max_dist_mm <= 0 || norm(c - cfg.axis_table) <= cfg.judge_max_dist_mm);
    if (!m.teeth_judged) m.short_teeth = m.missing_teeth = m.flash_gaps = 0;
    m.ok = true;
    return m;
}
}  // namespace des
