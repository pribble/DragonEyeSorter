// fit.cpp — see fit.hpp
#include "fit.hpp"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <opencv2/imgproc.hpp>
namespace des {
namespace meas {
namespace {
std::vector<float> &stat_buf()
{
    static thread_local std::vector<float> b;   // median / percentile scratch: the caller's vector stays untouched
    return b;
}
}  // namespace
Scratch &scratch()
{
    static thread_local Scratch s;
    return s;
}

bool debug_on()
{
    static const bool on = std::getenv("DES_MEASURE_DEBUG") != nullptr;
    return on;
}

float bilinear(const cv::Mat &g, float x, float y)
{
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    if (x0 < 0 || y0 < 0 || x0 + 1 >= g.cols || y0 + 1 >= g.rows) return -1;
    const float fx = x - x0, fy = y - y0;
    const uint8_t *r0 = g.ptr<uint8_t>(y0), *r1 = g.ptr<uint8_t>(y0 + 1);
    return (1 - fy) * ((1 - fx) * r0[x0] + fx * r0[x0 + 1]) + fy * ((1 - fx) * r1[x0] + fx * r1[x0 + 1]);
}
// belt), into table mm with parallax correction. A bore contour follows the same rule: on the axis every inner wall
void edges(const cv::Mat &g, const std::vector<cv::Point> &c, cv::Point off, float thr, const Homography &h,
           const MeasureConfig &cfg, float part_h, bool hole, std::vector<Edge> &out)
{
    out.clear();
    const int n = int(c.size());
    if (n < 8) return;
    out.reserve(size_t(n));
    const bool parallax = cfg.camera_h_mm > 0 && part_h > 0 && part_h < cfg.camera_h_mm;
    const float k = parallax ? cfg.camera_h_mm / (cfg.camera_h_mm - part_h) : 1.f;
    const Vec2 O = cfg.axis_table;
    const float step = 0.25f;
    for (int i = 0; i < n; i++) {
        const cv::Point2f t = cv::Point2f(c[(i + 3) % n]) - cv::Point2f(c[(i - 3 + n) % n]);
        const float len = std::hypot(t.x, t.y);
        if (len < 1e-3f) continue;
        cv::Point2f nrm(-t.y / len, t.x / len);
        const cv::Point2f p(float(c[i].x + off.x), float(c[i].y + off.y));
        const float s1 = bilinear(g, p.x + 1.5f * nrm.x, p.y + 1.5f * nrm.y), s2 = bilinear(g, p.x - 1.5f * nrm.x, p.y - 1.5f * nrm.y);
        if (s1 < 0 || s2 < 0) continue;
        if (s1 > s2) nrm = -nrm;   // +nrm points to the dark side
        // 0.25 px steps + linear interpolation at the crossing (same as 0.05 px steps in test_measure, 5x cheaper);
        const float dx = step * nrm.x, dy = step * nrm.y;
        float sx = p.x - 2.5f * nrm.x, sy = p.y - 2.5f * nrm.y;
        float prev = bilinear(g, sx, sy);
        sx += dx; sy += dy;
        cv::Point2f e = p;
        for (int m = 0; m < 20; m++, sx += dx, sy += dy) {
            const float v = bilinear(g, sx, sy);
            if (v >= 0 && prev >= 0 && prev >= thr && v < thr) {
                const float f = (prev - thr) / (prev - v), dd = -2.5f + float(m) * step + step * f;
                e = cv::Point2f(p.x + dd * nrm.x, p.y + dd * nrm.y);
                break;
            }
            prev = v;
        }
        const Vec2 p0 = h.map({e.x, e.y});
        const Vec2 q = h.map({e.x + nrm.x, e.y + nrm.y}) - p0;
        const float ql = norm(q);
        Edge ed{p0, ql > 0 ? q * (1.f / ql) : Vec2{0, 0}, false};
        ed.p.x -= cfg.belt_mm_per_row * (e.y - cfg.row0);   // rolling shutter: back to the row0 instant
        if (parallax && cfg.walls_visible) {
            const Vec2 r = ed.p - O;
            const float d = dot(ed.n, r);
            if (std::fabs(d) < 0.25f * norm(r)) continue;
            if (d > 0) ed.p = O + r * (1.f / k);
            if (hole && cfg.bore_near_arc) ed.far = d < 0.5f * norm(r);
        } else if (parallax) {
            ed.p = O + (ed.p - O) * (1.f / k);
        }
        out.push_back(ed);
    }
}
// parallax smear of a part centered at c (see measure.hpp): 0 without correction or with dark walls
float smear_at(const MeasureConfig &cfg, float part_h, Vec2 c)
{
    if (!cfg.walls_visible || cfg.camera_h_mm <= 0 || part_h <= 0 || part_h >= cfg.camera_h_mm) return 0;
    return part_h / (cfg.camera_h_mm - part_h) * norm(c - cfg.axis_table);
}

bool fit_circle(const std::vector<Vec2> &pts, Vec2 &c, float &r, float *rms)
{
    return des::fit_circle(pts.data(), pts.size(), c, r, rms);
}

float median(const std::vector<float> &v)
{
    if (v.empty()) return 0;
    std::vector<float> &b = stat_buf();
    b.assign(v.begin(), v.end());
    std::nth_element(b.begin(), b.begin() + b.size() / 2, b.end());
    return b[b.size() / 2];
}

float percentile(const std::vector<float> &v, float q)
{
    if (v.empty()) return 0;
    std::vector<float> &b = stat_buf();
    b.assign(v.begin(), v.end());
    const size_t k = size_t(std::clamp(q, 0.f, 1.f) * (b.size() - 1));
    std::nth_element(b.begin(), b.begin() + k, b.end());
    return b[k];
}

void runs_above(const std::vector<float> &prof, float mid, bool circular, std::vector<Run> &out)
{
    out.clear();
    const int n = int(prof.size());
    int start = 0;
    if (circular) {   // begin at a low bin so no run wraps around the start
        while (start < n && prof[start] > mid) start++;
        if (start == n) return;
    }
    bool in = false;
    Run cur{0, 0, 0};
    for (int k = 0; k < n; k++) {
        const int i = (start + k) % n;
        const bool hi = prof[i] > mid;
        if (hi && !in) { cur = {start + k, start + k, prof[i]}; in = true; }
        if (hi) { cur.b = start + k; cur.peak = std::max(cur.peak, prof[i]); }
        if (!hi && in) { out.push_back(cur); in = false; }
    }
    if (in) out.push_back(cur);   // indices are start-relative (may exceed n for circular): use % n
}

void filter_phase(const std::vector<float> &ph, const std::vector<Run> &runs, std::vector<Run> &kept, int &flash)
{
    double sc = 0, ss = 0;
    for (float p : ph) { sc += std::cos(p); ss += std::sin(p); }
    const float ph0 = float(std::atan2(ss, sc));
    kept.clear();
    for (size_t i = 0; i < runs.size(); i++) {
        if (std::fabs(wrap_pi(ph[i] - ph0)) > kPi / 2) flash++;
        else kept.push_back(runs[i]);
    }
}

float filled_valleys(const std::vector<float> &prof, const std::vector<Run> &runs, bool circular, int n_bins,
                     float flash_mm, std::vector<float> &vmins, std::vector<std::pair<int, int>> &gaps, int &flash)
{
    vmins.clear(); gaps.clear();
    for (size_t i = 0; i < runs.size(); i++) {
        const int a = runs[i].b + 1;
        const int b = (i + 1 < runs.size() ? runs[i + 1].a : (circular ? runs[0].a + n_bins : 0)) - 1;
        if (b < a) continue;   // linear profile: the last run has no valley after it
        float m = 1e9f;
        for (int k = a; k <= b; k++) m = std::min(m, prof[circular ? k % n_bins : k]);
        vmins.push_back(m);
        gaps.push_back({a, b});
    }
    const float root_med = median(vmins);
    for (float m : vmins)
        if (m > root_med + flash_mm) flash++;
    return root_med;
}

int otsu8(const int hist[256], size_t n)
{
    // the algebra of cv::THRESH_OTSU (getThreshVal_Otsu_8u): same binary image and class medians
    const double scale = 1.0 / double(n);
    double mu = 0, mu1 = 0, q1 = 0, max_sigma = 0;
    int max_val = 0;
    for (int i = 0; i < 256; i++) mu += i * double(hist[i]);
    mu *= scale;
    for (int i = 0; i < 256; i++) {
        const double p_i = hist[i] * scale;
        mu1 *= q1;
        q1 += p_i;
        const double q2 = 1. - q1;
        if (std::min(q1, q2) < FLT_EPSILON || std::max(q1, q2) > 1. - FLT_EPSILON) continue;
        mu1 = (mu1 + i * p_i) / q1;
        const double mu2 = (mu - q1 * mu1) / q2, sigma = q1 * q2 * (mu1 - mu2) * (mu1 - mu2);
        if (sigma > max_sigma) { max_sigma = sigma; max_val = i; }
    }
    return max_val;
}
// Largest outer contour and its largest hole inside the ROI: one histogram pass gives the Otsu level and the 50 %
// sub-pixel threshold (median of each Otsu class — Otsu leans toward the larger class, which moved rack edges
// outward by 0.04 mm per side in test_measure), then one pass writes the binary image.
bool find_part(const cv::Mat &gray, cv::Rect roi, std::vector<cv::Point> &outer, std::vector<cv::Point> &hole,
               float &thr, Scratch &s)
{
    outer.clear(); hole.clear();
    roi &= cv::Rect(0, 0, gray.cols, gray.rows);
    if (roi.area() <= 0) return false;
    int hist[256] = {0};
    for (int y = roi.y; y < roi.y + roi.height; y++) {
        const uint8_t *r = gray.ptr<uint8_t>(y) + roi.x;
        for (int x = 0; x < roi.width; x++) hist[r[x]]++;
    }
    const int t = otsu8(hist, size_t(roi.area()));
    auto class_median = [&](int lo, int hi) {   // [lo, hi]
        long n = 0, acc = 0;
        for (int v = lo; v <= hi; v++) n += hist[v];
        for (int v = lo; v <= hi; v++)
            if ((acc += hist[v]) * 2 >= n) return v;
        return (lo + hi) / 2;
    };
    thr = 0.5f * float(class_median(0, t) + class_median(t + 1, 255));
    s.bin.create(roi.size(), CV_8UC1);
    for (int y = 0; y < roi.height; y++) {
        const uint8_t *r = gray.ptr<uint8_t>(roi.y + y) + roi.x;
        uint8_t *d = s.bin.ptr<uint8_t>(y);
        for (int x = 0; x < roi.width; x++) d[x] = r[x] > t ? 255 : 0;
    }
    s.cs.clear(); s.hier.clear();
    cv::findContours(s.bin, s.cs, s.hier, cv::RETR_CCOMP, cv::CHAIN_APPROX_NONE);
    int best = -1; double best_a = 0;
    for (size_t i = 0; i < s.cs.size(); i++)
        if (s.hier[i][3] < 0) {
            const double a = cv::contourArea(s.cs[i]);
            if (a > best_a) { best_a = a; best = int(i); }
        }
    if (best < 0) return false;
    outer.assign(s.cs[best].begin(), s.cs[best].end());
    double hole_a = 0;
    for (size_t i = 0; i < s.cs.size(); i++)
        if (s.hier[i][3] == best) {
            const double a = cv::contourArea(s.cs[i]);
            if (a > hole_a) { hole_a = a; hole.assign(s.cs[i].begin(), s.cs[i].end()); }
        }
    return true;
}

void fill_empty_bins(std::vector<float> &prof)
{
    const int n = int(prof.size());
    auto fill_run = [&](int s, int e, float a, float b) {
        float v = 0.5f * (a + b);
        for (int i = s; i <= e; i++) {
            prof[i] = v;
            v = 0.5f * (v + b);
        }
    };
    int e0 = -1;   // empty run at the start of the array
    if (prof[0] < 0)
        while (e0 + 1 < n && prof[e0 + 1] < 0) e0++;
    if (e0 == n - 1) return;   // everything empty: nothing to interpolate between
    int s1 = n;   // empty run at the end of the array
    if (prof[n - 1] < 0)
        for (s1 = n - 1; s1 - 1 > e0 && prof[s1 - 1] < 0; s1--) {}
    if (e0 >= 0) {   // prefix run [0, e0]: right neighbour prof[e0 + 1], left the first non-empty on its left
        int lp = n - 1;
        while (lp > e0 && prof[lp] < 0) lp--;
        fill_run(0, e0, prof[lp], prof[e0 + 1]);
    }
    for (int i = e0 + 1; i < s1; i++) {   // linear runs between the two ends
        if (prof[i] >= 0) continue;
        int e = i;
        while (e + 1 < s1 && prof[e + 1] < 0) e++;
        fill_run(i, e, prof[i - 1], prof[e + 1]);
        i = e;
    }
    if (s1 < n) fill_run(s1, n - 1, prof[s1 - 1], prof[0]);   // suffix: right neighbour = prof[0] (already filled)
}
}  // namespace meas
}  // namespace des
