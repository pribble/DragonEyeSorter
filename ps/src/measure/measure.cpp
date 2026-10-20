// measure.cpp — see measure.hpp
#include "measure.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <opencv2/imgproc.hpp>

namespace des {
namespace {

struct Edge {
    Vec2 p;      // table mm (after parallax correction)
    Vec2 n;      // unit normal pointing to the dark side (out of the part / into a hole), table frame
    bool far = false;   // hole edge not on the clean near-axis half of the rim (see edges(): lit inner wall)
};

float bilinear(const cv::Mat &g, float x, float y)
{
    int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    if (x0 < 0 || y0 < 0 || x0 + 1 >= g.cols || y0 + 1 >= g.rows) return -1;
    float fx = x - x0, fy = y - y0;
    const uint8_t *r0 = g.ptr<uint8_t>(y0), *r1 = g.ptr<uint8_t>(y0 + 1);
    return (1 - fy) * ((1 - fx) * r0[x0] + fx * r0[x0 + 1]) + fy * ((1 - fx) * r1[x0] + fx * r1[x0 + 1]);
}

// Sub-pixel edge for every contour pixel: along the normal, where the interpolated gray crosses thr (bright part ->
// dark belt), then into table mm with parallax correction.
// hole = the contour is a bore. Hole edges follow the same rule as the outline (bottom edge where the dark side
// faces the axis, top-face edge where it faces away), which is right on the axis: there every inner wall faces the
// camera, is lit, and the dark boundary is the bottom rim (real gears, 10-21). Off the axis the near half (the inner
// wall there faces away from the camera) is a clean top-face rim, but on the far half the camera looks at the inner
// wall facing it, lit by the opposite light bar only down to a depth that depends on the lighting, so the boundary
// sits somewhere between the bottom and the top rim (bore +0.75 mm at 43 mm off-centre). With bore_near_arc those
// far edges are flagged (Edge::far) so the bore fit can leave them out.
std::vector<Edge> edges(const cv::Mat &g, const std::vector<cv::Point> &c, cv::Point off, float thr,
                        const Homography &h, const MeasureConfig &cfg, float part_h, bool hole = false)
{
    std::vector<Edge> out;
    const int n = int(c.size());
    if (n < 8) return out;
    out.reserve(c.size());
    const bool parallax = cfg.camera_h_mm > 0 && part_h > 0 && part_h < cfg.camera_h_mm;
    const float k = parallax ? cfg.camera_h_mm / (cfg.camera_h_mm - part_h) : 1.f;
    const Vec2 O = cfg.axis_table;
    for (int i = 0; i < n; i++) {
        cv::Point2f a = c[(i - 3 + n) % n], b = c[(i + 3) % n];
        cv::Point2f t = b - a;
        float len = std::hypot(t.x, t.y);
        if (len < 1e-3f) continue;
        cv::Point2f nrm(-t.y / len, t.x / len);
        cv::Point2f p(float(c[i].x + off.x), float(c[i].y + off.y));
        float s1 = bilinear(g, p.x + 1.5f * nrm.x, p.y + 1.5f * nrm.y);
        float s2 = bilinear(g, p.x - 1.5f * nrm.x, p.y - 1.5f * nrm.y);
        if (s1 < 0 || s2 < 0) continue;
        if (s1 > s2) nrm = -nrm;   // +nrm points to the dark side
        // 0.25 px steps + linear interpolation at the crossing: the blurred edge profile is smooth on that scale
        // (same results as 0.05 px steps in test_measure, 5x cheaper)
        const float step = 0.25f;
        float prev = bilinear(g, p.x - 2.5f * nrm.x, p.y - 2.5f * nrm.y);
        cv::Point2f e = p;
        for (float d = -2.5f + step; d <= 2.5f + 1e-3f; d += step) {
            float v = bilinear(g, p.x + d * nrm.x, p.y + d * nrm.y);
            if (v >= 0 && prev >= 0 && prev >= thr && v < thr) {
                float f = (prev - thr) / (prev - v);
                float dd = d - step + step * f;
                e = cv::Point2f(p.x + dd * nrm.x, p.y + dd * nrm.y);
                break;
            }
            prev = v;
        }
        Edge ed;
        ed.p = h.map({e.x, e.y});
        Vec2 q = h.map({e.x + nrm.x, e.y + nrm.y}) - ed.p;
        float ql = norm(q);
        ed.n = ql > 0 ? q * (1.f / ql) : Vec2{0, 0};
        ed.p.x -= cfg.belt_mm_per_row * (e.y - cfg.row0);   // rolling shutter: back to the row0 instant
        // top-face edges (the magnified top projection is the visible boundary where the normal faces away from
        // the optical axis) are scaled back about the axis; bottom edges (facing the axis) are true size.
        // Walls dark: every visible edge is a top-face edge.
        if (parallax && cfg.walls_visible) {
            // an edge running along the ray from the axis is the projection of a vertical wall corner (sweep line,
            // e.g. the ramp beside each tooth tip at the gear's sides): neither top nor bottom -> dropped
            Vec2 r = ed.p - O;
            float d = dot(ed.n, r);
            if (std::fabs(d) < 0.25f * norm(r)) continue;
            if (d > 0) ed.p = O + r * (1.f / k);
            // clean near-side rim: dark side (into the hole) pointing away from the axis within 60 deg; towards the
            // two corners where the near (top) rim meets the far boundary the edge is neither -> counted as far
            if (hole && cfg.bore_near_arc) ed.far = d < 0.5f * norm(r);
        } else if (parallax) {
            ed.p = O + (ed.p - O) * (1.f / k);
        }
        out.push_back(ed);
    }
    return out;
}

// parallax smear of a part centered at c (see measure.hpp): 0 without correction or with dark walls
float smear_at(const MeasureConfig &cfg, float part_h, Vec2 c)
{
    if (!cfg.walls_visible || cfg.camera_h_mm <= 0 || part_h <= 0 || part_h >= cfg.camera_h_mm) return 0;
    return part_h / (cfg.camera_h_mm - part_h) * norm(c - cfg.axis_table);
}

// algebraic (Kasa) circle fit on centered coordinates; returns false with fewer than 3 points
bool fit_circle(const std::vector<Vec2> &pts, Vec2 &c, float &r, float *rms = nullptr)
{
    const size_t n = pts.size();
    if (n < 3) return false;
    double mx = 0, my = 0;
    for (const Vec2 &p : pts) { mx += p.x; my += p.y; }
    mx /= n; my /= n;
    double suu = 0, svv = 0, suv = 0, suuu = 0, svvv = 0, suvv = 0, svuu = 0;
    for (const Vec2 &p : pts) {
        double u = p.x - mx, v = p.y - my;
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
        for (const Vec2 &p : pts) {
            double d = norm(p - c) - r;
            se += d * d;
        }
        *rms = float(std::sqrt(se / n));
    }
    return true;
}

float median(std::vector<float> v)
{
    if (v.empty()) return 0;
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    return v[v.size() / 2];
}

float percentile(std::vector<float> v, float q)
{
    if (v.empty()) return 0;
    size_t k = size_t(std::clamp(q, 0.f, 1.f) * (v.size() - 1));
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return v[k];
}

// largest outer contour and its largest hole inside the ROI
bool find_part(const cv::Mat &gray, cv::Rect roi, std::vector<cv::Point> &outer, std::vector<cv::Point> &hole,
               float &thr)
{
    roi &= cv::Rect(0, 0, gray.cols, gray.rows);
    if (roi.area() <= 0) return false;
    cv::Mat bin;
    const double otsu = cv::threshold(gray(roi), bin, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    // Sub-pixel edges use the 50 % level between the belt and the part (median of each Otsu class): Otsu itself
    // leans toward the larger class, which moved rack edges outward by 0.04 mm per side in test_measure.
    {
        int hist[256] = {0};
        for (int y = roi.y; y < roi.y + roi.height; y++) {
            const uint8_t *r = gray.ptr<uint8_t>(y);
            for (int x = roi.x; x < roi.x + roi.width; x++) hist[r[x]]++;
        }
        auto class_median = [&](int lo, int hi) {   // [lo, hi]
            long n = 0, acc = 0;
            for (int v = lo; v <= hi; v++) n += hist[v];
            for (int v = lo; v <= hi; v++)
                if ((acc += hist[v]) * 2 >= n) return v;
            return (lo + hi) / 2;
        };
        const int t = int(otsu);
        thr = 0.5f * float(class_median(0, t) + class_median(t + 1, 255));
    }
    std::vector<std::vector<cv::Point>> cs;
    std::vector<cv::Vec4i> hier;
    cv::findContours(bin, cs, hier, cv::RETR_CCOMP, cv::CHAIN_APPROX_NONE);
    int best = -1;
    double best_a = 0;
    for (size_t i = 0; i < cs.size(); i++)
        if (hier[i][3] < 0) {
            double a = cv::contourArea(cs[i]);
            if (a > best_a) { best_a = a; best = int(i); }
        }
    if (best < 0) return false;
    outer = cs[best];
    hole.clear();
    double hole_a = 0;
    for (size_t i = 0; i < cs.size(); i++)
        if (hier[i][3] == best) {
            double a = cv::contourArea(cs[i]);
            if (a > hole_a) { hole_a = a; hole = cs[i]; }
        }
    return true;
}

// runs of bins above `mid` in a circular (or linear) profile: [start, end] inclusive, in bin indices
struct Run {
    int a, b;
    float peak;
};

std::vector<Run> runs_above(const std::vector<float> &prof, float mid, bool circular)
{
    std::vector<Run> runs;
    const int n = int(prof.size());
    int start = 0;
    if (circular) {   // begin at a low bin so no run wraps around the start
        while (start < n && prof[start] > mid) start++;
        if (start == n) return runs;
    }
    bool in = false;
    Run cur{0, 0, 0};
    for (int k = 0; k < n; k++) {
        int i = (start + k) % n;
        bool hi = prof[i] > mid;
        if (hi && !in) { cur = {start + k, start + k, prof[i]}; in = true; }
        if (hi) { cur.b = start + k; cur.peak = std::max(cur.peak, prof[i]); }
        if (!hi && in) { runs.push_back(cur); in = false; }
    }
    if (in) runs.push_back(cur);
    return runs;   // indices are start-relative (may exceed n for circular): use % n
}

}  // namespace

GearMeasure measure_gear(const cv::Mat &gray, cv::Rect roi, const Homography &h, const MeasureConfig &cfg)
{
    GearMeasure g;
    std::vector<cv::Point> outer, hole;
    float thr;
    if (!find_part(gray, roi, outer, hole, thr)) { g.err = "no part in ROI"; return g; }
    std::vector<Edge> oe = edges(gray, outer, roi.tl(), thr, h, cfg, cfg.gear_h_mm);
    if (oe.size() < 60) { g.err = "outline too small"; return g; }

    std::vector<Vec2> pts;
    for (const Edge &e : oe) pts.push_back(e.p);
    Vec2 c0;
    float r0;
    fit_circle(pts, c0, r0);
    g.smear_mm = smear_at(cfg, cfg.gear_h_mm, c0);
    g.teeth_judged = g.smear_mm <= cfg.max_smear_mm;

    // r(theta), 720 bins, outer radius per bin
    const int NB = 720;
    std::vector<float> prof(NB, -1.f);
    std::vector<int> bin_of(pts.size());
    std::vector<float> rad(pts.size());
    for (size_t i = 0; i < pts.size(); i++) {
        Vec2 d = pts[i] - c0;
        float th = std::atan2(d.y, d.x);
        if (th < 0) th += 2 * kPi;
        int b = std::min(NB - 1, int(th / (2 * kPi) * NB));
        bin_of[i] = b;
        rad[i] = norm(d);
        prof[b] = std::max(prof[b], rad[i]);
    }
    for (int i = 0; i < NB; i++)   // fill empty bins from the neighbours
        if (prof[i] < 0) {
            int l = i, r = i;
            while (prof[(l - 1 + NB) % NB] < 0 && l > i - NB) l--;
            while (prof[(r + 1) % NB] < 0 && r < i + NB) r++;
            prof[i] = 0.5f * (prof[(l - 1 + NB) % NB] + prof[(r + 1) % NB]);
        }
    g.r_theta = prof;
    const float tip_est = percentile(prof, 0.95f), root_est = percentile(prof, 0.05f);
    const float mid = 0.5f * (tip_est + root_est);
    std::vector<Run> teeth = runs_above(prof, mid, true);
    if (teeth.size() < 6) { g.err = "no tooth structure"; return g; }

    // A run holding n teeth (gaps between them filled above mid = flash) is (n - 1) pitches + one tooth wide.
    // Pitch first from the median spacing of neighbouring runs (robust to the odd double gap / merged run).
    std::vector<float> widths, tips, centers;
    float w_med = 0, tip_med = 0, pitch0 = 0;
    auto spacing_of = [&](size_t i) {   // center of run i+1 minus center of run i, circular
        return centers[(i + 1) % teeth.size()] + (i + 1 == teeth.size() ? NB : 0) - centers[i];
    };
    auto stats = [&] {
        widths.clear(); tips.clear(); centers.clear();
        for (const Run &t : teeth) {
            widths.push_back(float(t.b - t.a + 1));
            tips.push_back(t.peak);
            centers.push_back(0.5f * (t.a + t.b));
        }
        w_med = median(widths);
        tip_med = median(tips);
        std::vector<float> spacing;
        for (size_t i = 0; i < teeth.size(); i++) spacing.push_back(spacing_of(i));
        pitch0 = median(spacing);
    };
    stats();
    auto teeth_in = [&](size_t i) { return std::max(1, 1 + int(std::lround((widths[i] - w_med) / pitch0))); };
    if (getenv("DES_MEASURE_DEBUG")) {
        fprintf(stderr, "gear c0 (%.2f,%.2f) mid %.3f w_med %.1f pitch %.1f tip_med %.3f runs:", c0.x, c0.y, mid, w_med,
                pitch0, tip_med);
        for (const Run &t : teeth) fprintf(stderr, " [%d-%d %.2f]", t.a % NB, t.b % NB, t.peak);
        fprintf(stderr, "\n");
    }
    // Tooth phase: teeth sit on a grid of NB / z bins. A run whose first tooth is off the grid by more than a quarter
    // pitch is material standing in a gap (flash blob), not a tooth.
    if (pitch0 > 0) {
        const float P = float(NB) / std::max(1L, std::lround(NB / pitch0));
        double sc = 0, ss = 0;
        std::vector<float> ph(teeth.size());
        for (size_t i = 0; i < teeth.size(); i++) {
            ph[i] = 2 * kPi * (teeth[i].a + 0.5f * (w_med - 1)) / P;   // first tooth center, as a phase
            sc += std::cos(ph[i]);
            ss += std::sin(ph[i]);
        }
        const float ph0 = float(std::atan2(ss, sc));
        std::vector<Run> kept;
        for (size_t i = 0; i < teeth.size(); i++) {
            float d = std::fabs(wrap_pi(ph[i] - ph0));
            if (d > kPi / 2) g.flash_gaps++;
            else kept.push_back(teeth[i]);
        }
        if (kept.size() != teeth.size() && kept.size() >= 6) {
            teeth = kept;
            stats();
        }
    }
    int present = 0;
    for (size_t i = 0; i < teeth.size(); i++) {
        present += teeth_in(i);
        g.flash_gaps += teeth_in(i) - 1;
        if (teeth[i].peak < tip_med - cfg.short_tooth_mm) g.short_teeth++;
    }

    // Spec tooth count from the pitch, not from counting: a 30-tooth gear with 2 teeth missing and a 28-tooth gear
    // both show 28 teeth, but the 28-tooth one has an even 12.86 deg pitch. Median spacing of single-tooth
    // neighbours -> z = 360 deg / pitch; missing = z - present.
    std::vector<float> single;
    for (size_t i = 0; i < teeth.size(); i++)
        if (teeth_in(i) == 1 && teeth_in((i + 1) % teeth.size()) == 1) single.push_back(spacing_of(i));
    const float pitch_bins = single.empty() ? pitch0 : median(single);
    const int z = pitch_bins > 0 ? int(std::lround(NB / pitch_bins)) : present;
    g.teeth = z;
    g.missing_teeth = std::max(0, z - present);

    // valleys: minimum radius between consecutive teeth; filled valleys = flash
    std::vector<float> vmins;
    std::vector<std::pair<int, int>> gaps;
    for (size_t i = 0; i < teeth.size(); i++) {
        int a = teeth[i].b + 1, b = (i + 1 < teeth.size() ? teeth[i + 1].a : teeth[0].a + NB) - 1;
        if (b < a) continue;
        float m = 1e9f;
        for (int k = a; k <= b; k++) m = std::min(m, prof[k % NB]);
        vmins.push_back(m);
        gaps.push_back({a, b});
    }
    const float root_med = median(vmins);
    for (float m : vmins)
        if (m > root_med + cfg.flash_mm) g.flash_gaps++;

    // tip circle: points near each (full height) tooth top; root circle: points near each (unfilled) valley floor.
    // Bin -> tooth / gap lookup tables, so this stays O(points).
    std::vector<int> tooth_of(NB, -1), gap_of(NB, -1);
    for (size_t ti = 0; ti < teeth.size(); ti++)
        for (int k = teeth[ti].a; k <= teeth[ti].b; k++) tooth_of[k % NB] = int(ti);
    for (size_t gi = 0; gi < gaps.size(); gi++)
        for (int k = gaps[gi].first; k <= gaps[gi].second; k++) gap_of[k % NB] = int(gi);
    // Only points whose normal is (nearly) radial: the blur rounds the corners of the narrow tip land inward, and
    // those corner points pulled the OD down by 0.05 mm in test_measure.
    std::vector<Vec2> tip_pts, root_pts;
    for (size_t i = 0; i < pts.size(); i++) {
        const int ti = tooth_of[bin_of[i]], gi = gap_of[bin_of[i]];
        if (rad[i] <= 0 || dot(oe[i].n, (pts[i] - c0) * (1.f / rad[i])) < 0.97f) continue;
        if (ti >= 0) {
            const Run &t = teeth[ti];
            if (t.peak >= tip_med - cfg.short_tooth_mm && rad[i] >= t.peak - 0.12f) tip_pts.push_back(pts[i]);
        } else if (gi >= 0 && vmins[gi] <= root_med + cfg.flash_mm && rad[i] <= vmins[gi] + 0.08f) {
            root_pts.push_back(pts[i]);
        }
    }
    float rt, rr;
    Vec2 ct, cr;
    if (!fit_circle(tip_pts, ct, rt, &g.fit_rms)) { g.err = "tip circle fit failed"; return g; }
    g.center = ct;
    g.od = 2 * rt + cfg.od_offset_mm;
    if (fit_circle(root_pts, cr, rr)) g.root_d = 2 * rr;

    if (!hole.empty()) {
        std::vector<Edge> he = edges(gray, hole, roi.tl(), thr, h, cfg, cfg.gear_h_mm, true);
        std::vector<Vec2> hp, near;
        for (const Edge &e : he) {
            hp.push_back(e.p);
            if (!e.far) near.push_back(e.p);
        }
        Vec2 cb;
        float rb;
        bool fit = fit_circle(hp, cb, rb);
        // off-centre (axis outside the bore + 1 mm): fit the clean near-side arc only (~120 deg); on the axis every
        // edge is a bottom-rim edge and all points are kept. An implausible arc fit (dark corners of the view: 3~5 mm
        // bores 160 mm off-centre) keeps the full fit.
        if (fit && cfg.bore_near_arc && norm(cb - cfg.axis_table) > rb + 1.f && near.size() >= 12 &&
            near.size() * 5 >= hp.size()) {
            Vec2 cn;
            float rn;
            if (fit_circle(near, cn, rn) && std::fabs(rn - rb) < 0.25f * rb) {
                cb = cn;
                rb = rn;
            }
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

RackMeasure measure_rack(const cv::Mat &gray, cv::Rect roi, const Homography &h, const MeasureConfig &cfg)
{
    RackMeasure m;
    std::vector<cv::Point> outer, hole;
    float thr;
    if (!find_part(gray, roi, outer, hole, thr)) { m.err = "no part in ROI"; return m; }
    std::vector<Edge> oe = edges(gray, outer, roi.tl(), thr, h, cfg, cfg.rack_h_mm);
    if (oe.size() < 60) { m.err = "outline too small"; return m; }

    std::vector<cv::Point2f> cvp;
    for (const Edge &e : oe) cvp.emplace_back(e.p.x, e.p.y);
    cv::RotatedRect rr = cv::minAreaRect(cvp);
    float ang = rr.angle * kPi / 180;
    Vec2 eu{std::cos(ang), std::sin(ang)};
    if (rr.size.height > rr.size.width) eu = {-eu.y, eu.x};   // u along the long side
    Vec2 ev{-eu.y, eu.x};
    const Vec2 c{rr.center.x, rr.center.y};
    std::vector<float> us, vs;
    float umin = 1e9f, umax = -1e9f, vmin = 1e9f, vmax = -1e9f;
    for (const Edge &e : oe) {
        float u = dot(e.p - c, eu), v = dot(e.p - c, ev);
        us.push_back(u);
        vs.push_back(v);
        umin = std::min(umin, u); umax = std::max(umax, u);
        vmin = std::min(vmin, v); vmax = std::max(vmax, v);
    }
    // extent of the edge points = first guess (the profile bins need it); the reported length / height come from
    // medians of the end-face / back / tip-land points below (the extremes of noisy points sit ~0.015 mm outside)
    m.length = umax - umin;
    m.height = vmax - vmin;
    auto median_where = [&](const std::vector<float> &val, auto pred) {
        std::vector<float> v;
        for (size_t i = 0; i < oe.size(); i++)
            if (pred(i)) v.push_back(val[i]);
        return std::make_pair(v.size() >= 5, median(v));
    };
    {   // end faces: facing along the length AND at the extremes (steep tooth flanks also face along the length)
        auto l = median_where(us, [&](size_t i) { return dot(oe[i].n, eu) < -0.95f && us[i] < umin + 0.3f; });
        auto r = median_where(us, [&](size_t i) { return dot(oe[i].n, eu) > 0.95f && us[i] > umax - 0.3f; });
        if (l.first && r.first) m.length = r.second - l.second;
    }
    m.axis = wrap_axis(std::atan2(eu.y, eu.x));
    m.center = c;

    // edge profiles along u (0.1 mm bins), interior only (ends excluded): the toothed side varies, the back is flat.
    // Points go to the side their normal faces, so a bin with no tooth-side point never takes a back-side value.
    const float bin = 0.1f, margin = 1.0f;
    const int NB = std::max(1, int((umax - umin - 2 * margin) / bin));
    std::vector<float> top(NB, -1e9f), bot(NB, 1e9f);
    for (size_t i = 0; i < us.size(); i++) {
        int b = int((us[i] - umin - margin) / bin);
        if (b < 0 || b >= NB) continue;
        float nv = dot(oe[i].n, ev);
        if (nv > 0.05f) top[b] = std::max(top[b], vs[i]);
        if (nv < -0.05f) bot[b] = std::min(bot[b], vs[i]);
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
    if (!fill(top, 1.f) || !fill(bot, -1.f)) { m.err = "rack edges not found"; return m; }
    auto spread = [](const std::vector<float> &p) { return percentile(p, 0.95f) - percentile(p, 0.05f); };
    const bool teeth_pos = spread(top) >= spread(bot);   // teeth on the +v side
    const std::vector<float> &prof = teeth_pos ? top : bot;
    const float tip = percentile(prof, 0.95f), root = percentile(prof, 0.05f), mid = 0.5f * (tip + root);
    {   // height = tip land line - back line (bottom-of-tooth side flat), medians of points facing straight out
        const float sg = teeth_pos ? 1.f : -1.f;
        auto back = median_where(vs, [&](size_t i) { return sg * dot(oe[i].n, ev) < -0.95f; });
        auto tips = median_where(vs, [&](size_t i) { return sg * dot(oe[i].n, ev) > 0.95f && sg * vs[i] > mid; });
        if (back.first && tips.first) m.height = std::fabs(tips.second - back.second);
    }
    if (getenv("DES_MEASURE_DEBUG")) {
        fprintf(stderr, "rack tip %.3f root %.3f mid %.3f NB %d\n", tip, root, mid, NB);
        for (int i = 0; i < NB; i++) fprintf(stderr, "%.2f%c", prof[i], i % 20 == 19 ? '\n' : ' ');
        fprintf(stderr, "\n");
    }
    // teeth = runs above mid; a run touching either end of the profile may be a tooth cut by the saw -> ignored
    std::vector<Run> runs;
    for (const Run &r : runs_above(prof, mid, false))
        if (r.a > 0 && r.b < NB - 1) runs.push_back(r);
    if (runs.size() < 3) { m.err = "no tooth structure"; return m; }
    // sub-bin run edges (linear interpolation of the mid crossing), u in mm from the profile start
    auto cross = [&](int i0, int i1) {   // crossing between bins i0 and i1 (adjacent)
        float a = prof[i0], b = prof[i1];
        float f = std::fabs(b - a) > 1e-6f ? (mid - a) / (b - a) : 0.5f;
        return (i0 + 0.5f + f * float(i1 - i0)) * bin;
    };
    std::vector<float> widths, tips, ua, ub;
    float w_med = 0, tip_med = 0;
    auto stats = [&] {
        widths.clear(); tips.clear(); ua.clear(); ub.clear();
        for (const Run &r : runs) {
            ua.push_back(cross(r.a - 1, r.a));
            ub.push_back(cross(r.b, r.b + 1));
            widths.push_back(ub.back() - ua.back());
            tips.push_back(r.peak);
        }
        w_med = median(widths);
        tip_med = median(tips);
    };
    stats();
    const float pitch_nom = kPi;   // module 1
    // tooth phase (see measure_gear): a run off the pitch grid by more than a quarter pitch = flash blob in a gap
    {
        double sc = 0, ss = 0;
        std::vector<float> ph(runs.size());
        for (size_t i = 0; i < runs.size(); i++) {
            ph[i] = 2 * kPi * (ua[i] + 0.5f * w_med) / pitch_nom;
            sc += std::cos(ph[i]);
            ss += std::sin(ph[i]);
        }
        const float ph0 = float(std::atan2(ss, sc));
        std::vector<Run> kept;
        for (size_t i = 0; i < runs.size(); i++) {
            if (std::fabs(wrap_pi(ph[i] - ph0)) > kPi / 2) m.flash_gaps++;
            else kept.push_back(runs[i]);
        }
        if (kept.size() != runs.size() && kept.size() >= 3) {
            runs = kept;
            stats();
        }
    }
    // a run holding n teeth (gaps filled above mid = flash) is (n - 1) pitches + one tooth wide
    auto teeth_in = [&](size_t i) { return std::max(1, 1 + int(std::lround((widths[i] - w_med) / pitch_nom))); };
    const float u_first = ua[0] + 0.5f * w_med;   // center of the first tooth = slot 0
    int present = 0, k_last = 0;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int n = 0;
    for (size_t i = 0; i < runs.size(); i++) {
        const int nt = teeth_in(i);
        m.flash_gaps += nt - 1;
        present += nt;
        if (runs[i].peak < tip_med - cfg.short_tooth_mm) m.short_teeth++;
        // slot index of the run's first tooth, from the nominal pitch (missing teeth keep their slot)
        const int k = int(std::lround((ua[i] + 0.5f * w_med - u_first) / pitch_nom));
        k_last = k + nt - 1;
        if (nt == 1) {   // mean pitch: regression of clean tooth centers on their slot index
            float uc = 0.5f * (ua[i] + ub[i]);
            sx += k; sy += uc; sxx += double(k) * k; sxy += k * uc; n++;
        }
    }
    m.teeth = k_last + 1;
    m.missing_teeth = std::max(0, m.teeth - present);
    // valleys between neighbouring runs: floor this far above the median floor = partly filled (flash)
    std::vector<float> vmins;
    for (size_t i = 0; i + 1 < runs.size(); i++) {
        float mn = 1e9f;
        for (int k = runs[i].b + 1; k < runs[i + 1].a; k++) mn = std::min(mn, prof[k]);
        vmins.push_back(mn);
    }
    const float root_med = median(vmins);
    for (float v : vmins)
        if (v > root_med + cfg.flash_mm) m.flash_gaps++;
    if (n >= 2) m.pitch = float((n * sxy - sx * sy) / (n * sxx - sx * sx));

    m.smear_mm = smear_at(cfg, cfg.rack_h_mm, c);
    m.teeth_judged = m.smear_mm <= cfg.max_smear_mm;
    if (!m.teeth_judged) m.short_teeth = m.missing_teeth = m.flash_gaps = 0;
    m.ok = true;
    return m;
}

}  // namespace des
