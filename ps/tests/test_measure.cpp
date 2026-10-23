// test_measure: gear / rack measurement on synthetic images with a known ground truth (4x supersampled polygons,
// anti-aliased, blurred, noisy; optional parallax: union of the slices from the bottom face to the top face magnified
// about the optical axis, or the top face only).  ./test_measure     exit code = failed checks
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "measure.hpp"
#include "part_log.hpp"
#include "roi_crop.hpp"
#include "test_check.hpp"
#include "track_measure.hpp"
#include "tracker.hpp"

using namespace des;

static const float S = 0.15f;                 // mm per px
// fillPoly fills boundary pixels: rendered shapes are 2 * 0.5 / SS px = 0.019 mm larger than drawn (holes too)
static const float GROW = 0.15f / 8;
// diameter / length checks: |measured - (drawn + GROW)| < TOL. 0.02 mm = 0.13 px on these synthetic images.
static const float TOL = 0.02f;
// supersampling: coarse pixel i averages fine pixels 8i..8i+7 (centroid 8i + 3.5) -> the image is shifted by -7/16 px
static const float SHIFT = -0.15f * 7 / 16;
static const Vec2 AXIS{960 * S, 540 * S};     // optical axis foot point = image center

using Poly = std::vector<Vec2>;

// gear outline: 3600 samples of R(theta); tip[k] = tip radius of tooth k (rr = missing), fill[k] = floor of the gap
// after tooth k (0 = none; negative = a blob of that height standing in the middle of the gap only)
static Poly gear(Vec2 c, int z, float ra, float rr, float phi, std::vector<float> tip, std::vector<float> fill,
                 std::vector<float> jit = {})   // jit[k]: angular offset of tooth k (rad), moulding / edge noise
{
    if (tip.empty()) tip.assign(z, ra);
    if (fill.empty()) fill.assign(z, 0.f);
    const float p = 2 * kPi / z;
    Poly out;
    for (int i = 0; i < 3600; i++) {
        float th = 2 * kPi * i / 3600;
        float rel = th - phi;
        int k = int(std::floor(rel / p + 0.5f));
        float a = rel - k * p;   // in [-p/2, p/2)
        k = ((k % z) + z) % z;
        if (!jit.empty()) a -= jit[k];
        float t = tip[k], r;
        float aa = std::fabs(a);
        if (aa <= 0.13f * p) r = t;
        else if (aa < 0.32f * p) r = t + (rr - t) * (aa - 0.13f * p) / (0.19f * p);
        else r = rr;
        if (aa > 0.13f * p) {   // gap after tooth k (a > 0) or before it (a < 0): flash fills it up to fill[]
            int gk = a > 0 ? k : (k - 1 + z) % z;
            if (fill[gk] > 0) r = std::max(r, fill[gk]);
            if (fill[gk] < 0 && aa >= 0.36f * p) r = std::max(r, -fill[gk]);   // blob in the middle of the gap
        }
        out.push_back({c.x + r * std::cos(th), c.y + r * std::sin(th)});
    }
    return out;
}

static Poly circle(Vec2 c, float r)
{
    Poly out;
    for (int i = 0; i < 720; i++) {
        float th = 2 * kPi * i / 720;
        out.push_back({c.x + r * std::cos(th), c.y + r * std::sin(th)});
    }
    return out;
}

// rack on its side: back at v = 0, root line at 12.75, tips at 15; teeth centers 2.5 + k*pi; tip[k] overrides height,
// fill[k] > 0 raises the floor of the gap after tooth k, fill[k] < 0 stands a blob of that height in its middle
static Poly rack(Vec2 c, float len, int n, float ang, std::vector<float> tip, std::vector<float> fill = {})
{
    if (tip.empty()) tip.assign(n, 15.f);
    if (fill.empty()) fill.assign(n, 0.f);
    const float hb = 12.75f;
    std::vector<Vec2> loc{{0, 0}, {len, 0}, {len, hb}};
    for (int k = n - 1; k >= 0; k--) {
        float uc = 2.5f + k * kPi;
        float yr = fill[k] > 0 ? fill[k] : hb, yl = k > 0 && fill[k - 1] > 0 ? fill[k - 1] : hb;
        if (fill[k] < 0 && k + 1 < n) {   // blob in the middle of the gap after tooth k
            float gc = uc + kPi / 2;
            loc.push_back({gc + 0.35f, hb});
            loc.push_back({gc + 0.25f, -fill[k]});
            loc.push_back({gc - 0.25f, -fill[k]});
            loc.push_back({gc - 0.35f, hb});
        }
        loc.push_back({uc + 1.0f, yr});
        loc.push_back({uc + 0.5f, tip[k]});
        loc.push_back({uc - 0.5f, tip[k]});
        loc.push_back({uc - 1.0f, yl});
    }
    loc.push_back({0, hb});
    Poly out;
    float ca = std::cos(ang), sa = std::sin(ang);
    for (Vec2 q : loc) {
        Vec2 d{q.x - len / 2, q.y - 7.5f};
        out.push_back({c.x + ca * d.x - sa * d.y, c.y + sa * d.x + ca * d.y});
    }
    return out;
}

// rolling shutter: image row r is exposed (r - row0) row times late, the belt has moved mm_per_row per row along +x
// (in this test image rows run along table y: row = y / S)
static Poly sheared(const Poly &p, float mm_per_row, float row0)
{
    Poly out;
    for (Vec2 q : p) out.push_back({q.x + mm_per_row * (q.y / S - row0), q.y});
    return out;
}

static Poly scaled(const Poly &p, Vec2 o, float k)
{
    Poly out;
    for (Vec2 q : p) out.push_back(o + (q - o) * k);
    return out;
}

struct Scene {
    cv::Mat img;
    cv::Rect roi;
};

// slice = outer polygon minus hole. Walls visible: the bright region is the union of every slice from the bottom
// (scale 1) to the top (scale k) about the optical axis; top_only (walls dark): just the top slice.
static Scene render(const Poly &outer, const Poly &hole, float k, bool top_only = false)
{
    float xmin = 1e9f, ymin = 1e9f, xmax = -1e9f, ymax = -1e9f;
    for (Vec2 q : scaled(outer, AXIS, k)) {
        xmin = std::min(xmin, q.x); xmax = std::max(xmax, q.x);
        ymin = std::min(ymin, q.y); ymax = std::max(ymax, q.y);
    }
    for (Vec2 q : outer) {
        xmin = std::min(xmin, q.x); xmax = std::max(xmax, q.x);
        ymin = std::min(ymin, q.y); ymax = std::max(ymax, q.y);
    }
    cv::Rect roi(int(xmin / S) - 15, int(ymin / S) - 15, int((xmax - xmin) / S) + 30, int((ymax - ymin) / S) + 30);
    const int SS = 8;   // fillPoly fills boundary pixels: shapes grow by 0.5 / SS px per side (8: 0.009 mm)
    cv::Mat mask(roi.height * SS, roi.width * SS, CV_8UC1, cv::Scalar(0));
    auto to_pts = [&](const Poly &p) {
        std::vector<cv::Point> v;
        for (Vec2 q : p)   // fixed point, 4 fractional bits
            v.emplace_back(int(std::lround((q.x / S - roi.x) * SS * 16)), int(std::lround((q.y / S - roi.y) * SS * 16)));
        return v;
    };
    const int slices = top_only || k <= 1 ? 1 : 64;
    for (int i = 0; i < slices; i++) {
        float kk = top_only ? k : 1.f + (k - 1.f) * i / std::max(1, slices - 1);
        cv::Mat f(mask.size(), CV_8UC1, cv::Scalar(0));
        std::vector<std::vector<cv::Point>> o{to_pts(scaled(outer, AXIS, kk))};
        cv::fillPoly(f, o, cv::Scalar(255), cv::LINE_8, 4);
        if (!hole.empty()) {
            std::vector<std::vector<cv::Point>> h{to_pts(scaled(hole, AXIS, kk))};
            cv::fillPoly(f, h, cv::Scalar(0), cv::LINE_8, 4);
        }
        mask |= f;
    }
    cv::Mat small;
    cv::resize(mask, small, roi.size(), 0, 0, cv::INTER_AREA);
    Scene s;
    s.img = cv::Mat(1080, 1920, CV_8UC1, cv::Scalar(40));
    cv::Mat dst = s.img(roi);
    small.convertTo(small, CV_32F, 180.0 / 255.0, 40);   // belt 40 -> part 220
    small.convertTo(dst, CV_8UC1);
    cv::GaussianBlur(s.img, s.img, cv::Size(5, 5), 0.7);
    cv::Mat noise(s.img.size(), CV_16SC1), w;
    cv::randn(noise, 0, 2);
    s.img.convertTo(w, CV_16SC1);
    w += noise;
    w.convertTo(s.img, CV_8UC1);
    s.roi = roi;
    return s;
}

int main()
{
    Homography h;
    h.h = {S, 0, 0, 0, S, 0, 0, 0, 1};
    MeasureConfig cfg;
    cfg.axis_table = AXIS;

    {   // A: good 30-tooth gear, bore 6 with 0.13 mm eccentricity
        Vec2 c{100, 60};
        Scene s = render(gear(c, 30, 16, 13.75f, 0.1f, {}, {}), circle(c + Vec2{0.12f, -0.05f}, 3), 1.f);
        GearMeasure g = measure_gear(s.img, s.roi, h, cfg);
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 20; i++) measure_gear(s.img, s.roi, h, cfg);
        printf("[timing] measure_gear %.2f ms/part (ROI %dx%d)\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 20, s.roi.width, s.roi.height);
        printf("[gear30] ok %d teeth %d OD %.3f root %.3f bore %.3f ecc %.3f rms %.3f short %d missing %d flash %d\n", g.ok, g.teeth, g.od, g.root_d, g.bore_d, g.ecc, g.fit_rms, g.short_teeth, g.missing_teeth, g.flash_gaps);
        CHECK(g.ok, "gear30 failed: %s", g.err.c_str());
        CHECK(g.teeth == 30, "teeth %d", g.teeth);
        std::vector<float> rt;   // optional r(theta) out-param (des_tool report inset): 720 bins, live path passes 0
        GearMeasure gr = measure_gear(s.img, s.roi, h, cfg, &rt);
        float rt_lo = 1e9f, rt_hi = 0;
        for (float v : rt) { rt_lo = std::min(rt_lo, v); rt_hi = std::max(rt_hi, v); }
        CHECK(rt.size() == 720 && rt_lo > 12 && gr.teeth == g.teeth && std::fabs(gr.od - g.od) < 1e-6f, "r(theta): %zu bins %.3f..%.3f, teeth %d", rt.size(), rt_lo, rt_hi, gr.teeth);
        CHECK(std::fabs(rt_hi - g.od / 2) < 0.05f, "r(theta) max %.3f vs od/2 %.3f", rt_hi, g.od / 2);
        MeasureConfig oc = cfg;   // master-part offsets move only the reported OD / bore
        oc.od_offset_mm = 0.41f;
        oc.bore_offset_mm = -0.07f;
        GearMeasure go = measure_gear(s.img, s.roi, h, oc);
        CHECK(std::fabs(go.od - g.od - 0.41f) < 1e-4f && std::fabs(go.bore_d - g.bore_d + 0.07f) < 1e-4f && go.teeth == g.teeth && std::fabs(go.root_d - g.root_d) < 1e-4f, "offsets: OD %+.4f bore %+.4f", go.od - g.od, go.bore_d - g.bore_d);
        CHECK(std::fabs(g.od - 32 - GROW) < TOL, "OD %.3f", g.od);
        CHECK(std::fabs(g.root_d - 27.5f - GROW) < TOL, "root %.3f", g.root_d);
        CHECK(std::fabs(g.bore_d - 6 - GROW) < TOL, "bore %.3f", g.bore_d);
        CHECK(std::fabs(g.ecc - 0.13f) < 0.01f, "ecc %.3f", g.ecc);
        CHECK(g.short_teeth == 0 && g.missing_teeth == 0 && g.flash_gaps == 0, "false defects %d %d %d", g.short_teeth, g.missing_teeth, g.flash_gaps);
    }
    {   // B: defects — tooth 5 short by 1 mm, tooth 12 missing, gap after tooth 20 filled 0.8 mm above the root,
        //    gap after tooth 26 filled almost to the tips (the two teeth merge into one run), a blob standing in the
        //    middle of the gap after tooth 2 (looks like an extra tooth, off the pitch grid)
        Vec2 c{180, 90};
        std::vector<float> tip(30, 16.f), fill(30, 0.f);
        tip[5] = 15.f;
        tip[12] = 13.75f;
        fill[20] = 13.75f + 0.8f;
        fill[26] = 15.6f;
        fill[2] = -15.5f;
        Scene s = render(gear(c, 30, 16, 13.75f, 0.3f, tip, fill), circle(c, 3), 1.f);
        GearMeasure g = measure_gear(s.img, s.roi, h, cfg);
        printf("[gear30 defects] teeth %d OD %.3f short %d missing %d flash %d\n", g.teeth, g.od, g.short_teeth, g.missing_teeth, g.flash_gaps);
        CHECK(g.ok && g.teeth == 30, "spec teeth %d (%s)", g.teeth, g.err.c_str());
        CHECK(g.short_teeth == 1, "short %d", g.short_teeth);
        CHECK(g.missing_teeth == 1, "missing %d", g.missing_teeth);
        CHECK(g.flash_gaps == 3, "flash %d", g.flash_gaps);
        CHECK(std::fabs(g.od - 32 - GROW) < TOL, "OD with defects %.3f", g.od);
    }
    {   // B2: two (and three) teeth missing, every rotation in half-bin steps: the pitch must not be read off the
        //     spacing median (0.5-degree bins; the double spacings over the gaps push it up half a bin -> 720 / 24.5
        //     rounds to 29 teeth and every defect count goes wild; 2026-10-24 real gears, 1 frame in 20)
        int bad = 0, n = 0;
        for (int miss = 2; miss <= 3; miss++)
            for (int k = 0; k < 24; k++) {
                Vec2 c{180, 90};
                std::vector<float> tip(30, 16.f);
                tip[3] = tip[17] = 13.75f;
                if (miss == 3) tip[18] = 13.75f;
                std::vector<float> jit(30);
                for (int i = 0; i < 30; i++) jit[i] = 0.008f * std::sin(2.7f * i + 1.3f * k + miss);   // +-0.46 deg
                Scene s = render(gear(c, 30, 16, 13.75f, 0.01f + k * kPi / 720, tip, {}, jit), circle(c, 3), 1.f);
                GearMeasure g = measure_gear(s.img, s.roi, h, cfg);
                n++;
                if (!(g.ok && g.teeth == 30 && g.missing_teeth == miss && g.short_teeth == 0 && g.flash_gaps == 0)) {
                    if (bad++ < 3)
                        printf("[gear30 -%d rot %d] teeth %d short %d missing %d flash %d\n", miss, k, g.teeth, g.short_teeth,
                               g.missing_teeth, g.flash_gaps);
                }
            }
        printf("[gear30 two / three missing] %d of %d rotations wrong\n", bad, n);
        CHECK(bad == 0, "missing teeth over rotations: %d of %d wrong", bad, n);
    }
    {   // C: spec mix-up — 28 teeth (OD 30), must not be read as 30 with two missing
        Vec2 c{60, 110};
        Scene s = render(gear(c, 28, 15, 12.75f, 0.2f, {}, {}), circle(c, 3), 1.f);
        GearMeasure g = measure_gear(s.img, s.roi, h, cfg);
        printf("[gear28] teeth %d OD %.3f missing %d\n", g.teeth, g.od, g.missing_teeth);
        CHECK(g.ok && g.teeth == 28 && g.missing_teeth == 0, "28-tooth gear read as %d (+%d missing)", g.teeth, g.missing_teeth);
        CHECK(std::fabs(g.od - 30 - GROW) < TOL, "OD28 %.3f", g.od);
    }
    // D: parallax — camera 360 mm above the belt (8 mm lens), gear 10 mm thick: k = 360 / 350
    const float H = 360, hh = 10, k = H / (H - hh);
    MeasureConfig pc = cfg;
    pc.camera_h_mm = H;
    pc.gear_h_mm = hh;
    {   // D1: 22 mm off the axis (smear 0.62 mm): corrected size, teeth still judged
        Vec2 c = AXIS + Vec2{18, -12};
        Scene s = render(gear(c, 30, 16, 13.75f, 0.05f, {}, {}), circle(c, 3), k);
        GearMeasure raw = measure_gear(s.img, s.roi, h, cfg);
        GearMeasure cor = measure_gear(s.img, s.roi, h, pc);
        printf("[parallax 22mm] uncorrected OD %.3f bore %.3f | corrected OD %.3f bore %.3f teeth %d smear %.2f judged %d defects %d/%d/%d\n", raw.od, raw.bore_d, cor.od, cor.bore_d, cor.teeth, cor.smear_mm, cor.teeth_judged, cor.short_teeth, cor.missing_teeth, cor.flash_gaps);
        CHECK(std::fabs(raw.od - 32) > 0.3f, "parallax should show without correction (%.3f)", raw.od);
        CHECK(cor.ok && std::fabs(cor.od - 32 - GROW) < TOL, "corrected OD %.3f", cor.od);
        CHECK(std::fabs(cor.bore_d - 6 - GROW) < TOL, "corrected bore %.3f", cor.bore_d);
        MeasureConfig nc = pc;   // bore from the near-axis rim arc only
        nc.bore_near_arc = true;
        GearMeasure na = measure_gear(s.img, s.roi, h, nc);
        printf("[parallax 22mm near arc] bore %.3f\n", na.bore_d);
        CHECK(na.ok && std::fabs(na.bore_d - 6 - GROW) < TOL, "near-arc bore %.3f", na.bore_d);
        CHECK(cor.teeth_judged && cor.teeth == 30, "judged %d teeth %d", cor.teeth_judged, cor.teeth);
        CHECK(cor.short_teeth == 0 && cor.missing_teeth == 0 && cor.flash_gaps == 0, "false defects %d %d %d", cor.short_teeth, cor.missing_teeth, cor.flash_gaps);
    }
    {   // D2: the defect gear 21 mm off the axis
        Vec2 c = AXIS + Vec2{-15, 15};
        std::vector<float> tip(30, 16.f), fill(30, 0.f);
        tip[5] = 15.f;
        tip[12] = 13.75f;
        fill[20] = 13.75f + 0.8f;
        Scene s = render(gear(c, 30, 16, 13.75f, 0.3f, tip, fill), circle(c, 3), k);
        GearMeasure g = measure_gear(s.img, s.roi, h, pc);
        printf("[parallax defects] OD %.3f teeth %d smear %.2f short %d missing %d flash %d\n", g.od, g.teeth, g.smear_mm, g.short_teeth, g.missing_teeth, g.flash_gaps);
        CHECK(g.ok && g.teeth_judged && g.teeth == 30, "teeth %d judged %d", g.teeth, g.teeth_judged);
        CHECK(g.short_teeth == 1 && g.missing_teeth == 1 && g.flash_gaps == 1, "defects %d %d %d", g.short_teeth, g.missing_teeth, g.flash_gaps);
    }
    {   // D3: 112 mm off the axis (smear 3.2 mm ~ one pitch: side gaps filled) -> size only, teeth not judged
        Vec2 c = AXIS + Vec2{100, 50};
        Scene s = render(gear(c, 30, 16, 13.75f, 0.0f, {}, {}), circle(c, 3), k);
        GearMeasure g = measure_gear(s.img, s.roi, h, pc);
        printf("[parallax 112mm] OD %.3f bore %.3f smear %.2f judged %d\n", g.od, g.bore_d, g.smear_mm, g.teeth_judged);
        CHECK(g.ok && !g.teeth_judged, "far off-axis must not judge teeth (smear %.2f)", g.smear_mm);
        CHECK(g.short_teeth == 0 && g.missing_teeth == 0 && g.flash_gaps == 0, "unjudged counts must be 0");
        CHECK(std::fabs(g.od - 32 - GROW) < 2 * TOL, "far OD %.3f", g.od);
        CHECK(std::fabs(g.bore_d - 6 - GROW) < 2 * TOL, "far bore %.3f", g.bore_d);
        MeasureConfig nc = pc;
        nc.bore_near_arc = true;
        GearMeasure na = measure_gear(s.img, s.roi, h, nc);
        printf("[parallax 112mm near arc] bore %.3f\n", na.bore_d);
        CHECK(na.ok && std::fabs(na.bore_d - 6 - GROW) < 2 * TOL, "far near-arc bore %.3f", na.bore_d);
    }
    {   // D4: walls dark (only the top face bright), 112 mm off: everything scales back, no smear
        Vec2 c = AXIS + Vec2{100, 50};
        Scene s = render(gear(c, 30, 16, 13.75f, 0.0f, {}, {}), circle(c, 3), k, true);
        MeasureConfig tc = pc;
        tc.walls_visible = false;
        GearMeasure g = measure_gear(s.img, s.roi, h, tc);
        printf("[top face 112mm] OD %.3f bore %.3f teeth %d judged %d\n", g.od, g.bore_d, g.teeth, g.teeth_judged);
        CHECK(g.ok && g.teeth_judged && g.teeth == 30, "teeth %d", g.teeth);
        CHECK(std::fabs(g.od - 32 - GROW) < TOL && std::fabs(g.bore_d - 6 - GROW) < TOL, "OD %.3f bore %.3f", g.od, g.bore_d);
        CHECK(g.short_teeth == 0 && g.missing_teeth == 0 && g.flash_gaps == 0, "false defects");
    }
    {   // E: rack 50 mm, 15 teeth, 25 deg
        Scene s = render(rack({150, 80}, 50, 15, 25 * kPi / 180, {}), {}, 1.f);
        RackMeasure r = measure_rack(s.img, s.roi, h, cfg);
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 20; i++) measure_rack(s.img, s.roi, h, cfg);
        printf("[timing] measure_rack %.2f ms/part (ROI %dx%d)\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 20, s.roi.width, s.roi.height);
        printf("[rack50] ok %d length %.3f height %.3f pitch %.4f teeth %d short %d flash %d\n", r.ok, r.length, r.height, r.pitch, r.teeth, r.short_teeth, r.flash_gaps);
        CHECK(r.ok, "rack failed: %s", r.err.c_str());
        CHECK(std::fabs(r.length - 50 - GROW) < TOL, "length %.3f", r.length);
        CHECK(std::fabs(r.height - 15 - GROW) < TOL, "height %.3f", r.height);
        CHECK(std::fabs(r.pitch - kPi) < 0.005f, "pitch %.4f", r.pitch);
        CHECK(r.teeth == 15 && r.short_teeth == 0, "teeth %d short %d", r.teeth, r.short_teeth);
    }
    {   // F: rack 48 mm (length out of tolerance) with tooth 7 short
        std::vector<float> tip(14, 15.f);
        tip[7] = 14.f;
        Scene s = render(rack({120, 70}, 48, 14, -10 * kPi / 180, tip), {}, 1.f);
        RackMeasure r = measure_rack(s.img, s.roi, h, cfg);
        printf("[rack48] length %.3f pitch %.4f teeth %d short %d\n", r.length, r.pitch, r.teeth, r.short_teeth);
        CHECK(r.ok && std::fabs(r.length - 48 - GROW) < TOL, "length %.3f", r.length);
        CHECK(r.short_teeth == 1, "short %d", r.short_teeth);
        CHECK(r.teeth == 14 && r.missing_teeth == 0 && r.flash_gaps == 0, "teeth %d missing %d flash %d", r.teeth, r.missing_teeth, r.flash_gaps);
    }
    {   // G: rack 50 mm, tooth 4 missing, gap after tooth 9 filled 1 mm above the root, gap after tooth 12 filled
        //    almost to the tips, a blob in the middle of the gap after tooth 6
        std::vector<float> tip(15, 15.f), fill(15, 0.f);
        tip[4] = 12.75f;
        fill[9] = 13.75f;
        fill[12] = 14.7f;
        fill[6] = -14.5f;
        Scene s = render(rack({140, 60}, 50, 15, 70 * kPi / 180, tip, fill), {}, 1.f);
        RackMeasure r = measure_rack(s.img, s.roi, h, cfg);
        printf("[rack defects] teeth %d missing %d flash %d short %d pitch %.4f\n", r.teeth, r.missing_teeth, r.flash_gaps, r.short_teeth, r.pitch);
        CHECK(r.ok && r.teeth == 15, "teeth %d (%s)", r.teeth, r.err.c_str());
        CHECK(r.missing_teeth == 1 && r.flash_gaps == 3 && r.short_teeth == 0, "missing %d flash %d short %d", r.missing_teeth, r.flash_gaps, r.short_teeth);
        CHECK(std::fabs(r.pitch - kPi) < 0.01f, "pitch %.4f", r.pitch);
    }
    {   // I: rolling shutter, exaggerated (0.005 mm per row: 1 mm shear across the gear)
        Vec2 c{110, 70};
        const float mpr = 0.005f, row0 = 540;
        Scene s = render(sheared(gear(c, 30, 16, 13.75f, 0.1f, {}, {}), mpr, row0), sheared(circle(c, 3), mpr, row0), 1.f);
        GearMeasure raw = measure_gear(s.img, s.roi, h, cfg);
        MeasureConfig rc = cfg;
        rc.belt_mm_per_row = mpr;
        rc.row0 = row0;
        GearMeasure g = measure_gear(s.img, s.roi, h, rc);
        printf("[rolling shutter] uncorrected OD %.3f rms %.3f | corrected OD %.3f rms %.3f bore %.3f ecc %.3f\n", raw.od, raw.fit_rms, g.od, g.fit_rms, g.bore_d, g.ecc);
        CHECK(raw.fit_rms > 0.05f, "shear should show without correction (rms %.3f)", raw.fit_rms);
        CHECK(g.ok && std::fabs(g.od - 32 - GROW) < TOL && g.fit_rms < 0.02f, "corrected OD %.3f rms %.3f", g.od, g.fit_rms);
        CHECK(std::fabs(g.center.x - c.x - SHIFT) < 0.02f && std::fabs(g.center.y - c.y - SHIFT) < 0.02f, "center back at the row0 instant (%.3f, %.3f)", g.center.x - c.x - SHIFT, g.center.y - c.y - SHIFT);
        CHECK(g.ecc < 0.02f && g.teeth == 30 && g.short_teeth + g.missing_teeth + g.flash_gaps == 0, "ecc %.3f teeth %d defects %d/%d/%d", g.ecc, g.teeth, g.short_teeth, g.missing_teeth, g.flash_gaps);
    }
    {   // J: TrackMeasurer end to end: full frame (BGRA), tracker tracks -> ROI from the track footprint -> gear /
        //    rack by shape -> cropped homography -> results applied to the tracks; best-frame selection
        Vec2 cg = AXIS + Vec2{-30, 10}, cr = AXIS + Vec2{45, -25};
        Scene a = render(gear(cg, 30, 16, 13.75f, 0.2f, {}, {}), circle(cg, 3), 1.f);
        Scene b = render(rack(cr, 50, 15, 25 * kPi / 180, {}), {}, 1.f);
        cv::Mat gray = cv::max(a.img, b.img), bgra;
        cv::cvtColor(gray, bgra, cv::COLOR_GRAY2BGRA);
        if (const char *save = getenv("TEST_MEASURE_SAVE"))   // frame for des_tool measure --image (px_to_table = S)
            printf("saved %s: %d\n", save, int(cv::imwrite(save, gray)));
        TrackerConfig tc;
        tc.fov_x0 = 0; tc.fov_x1 = 1920 * S; tc.decide_x = 1000; tc.tail_x = 2000;
        Tracker tr(tc);
        for (uint32_t f = 1; f <= 3; f++) {
            FrameObs o;
            o.frame = f;
            Detection dg; dg.p = cg; dg.len = 32; dg.wid = 32; dg.axis = 0;
            Detection dr; dr.p = cr; dr.len = 50; dr.wid = 15; dr.axis = wrap_axis(25 * kPi / 180);
            o.dets = {dg, dr};
            tr.update(o);
        }
        TrackMeasureConfig mc;   // axis from the image center = AXIS here
        TrackMeasurer tm(mc, h, h);   // camera_h_mm 0: parts stand where the belt plane says
        int calls = 0, results = 0;
        for (; calls < 4; calls++) {
            auto res = tm.measure(tr, bgra.data, bgra.cols, bgra.rows, int(bgra.step), 0.0, 0.0, 10 + calls);
            results += int(res.size());
            CHECK(res.size() <= 1, "per_frame 1, got %zu", res.size());
            TrackMeasurer::apply(tr, res);
        }
        const PartMeasure *mg = nullptr, *mr = nullptr;
        for (const Track &t : tr.tracks()) {
            if (norm(Vec2{t.s, t.y} - cg) < 2) mg = &t.meas;
            if (norm(Vec2{t.s, t.y} - cr) < 2) mr = &t.meas;
        }
        printf("[track measurer] %d results in %d calls, %.2f ms last | gear %c OD %.3f z%d | rack %c L %.3f p %.4f\n", results, calls, tm.last_ms, mg ? mg->kind : '-', mg ? mg->size : 0, mg ? mg->teeth : 0, mr ? mr->kind : '-', mr ? mr->size : 0, mr ? mr->pitch : 0);
        CHECK(results == 2, "each part measured once (still parts: no nearer frame), got %d", results);
        CHECK(mg && mg->valid && mg->kind == 'g' && std::fabs(mg->size - 32 - GROW) < TOL && mg->teeth == 30, "gear via tracker");
        CHECK(mr && mr->valid && mr->kind == 'r' && std::fabs(mr->size - 50 - GROW) < TOL && mr->teeth == 15, "rack via tracker");
        CHECK(mg && std::fabs(mg->axis_dist - norm(cg - AXIS)) < 0.5f, "gear axis distance %.2f", mg ? mg->axis_dist : -1.f);
        // the gear moves 20 mm toward the axis -> clearly nearer (31.6 -> ~13) -> measured again
        for (Track &t : tr.tracks())
            if (norm(Vec2{t.s, t.y} - cg) < 2) t.s += 20;
        Scene a2 = render(gear(cg + Vec2{20, 0}, 30, 16, 13.75f, 0.2f, {}, {}), circle(cg + Vec2{20, 0}, 3), 1.f);
        gray = cv::max(a2.img, b.img);
        cv::cvtColor(gray, bgra, cv::COLOR_GRAY2BGRA);
        auto res = tm.measure(tr, bgra.data, bgra.cols, bgra.rows, int(bgra.step), 0.0, 0.0, 20);
        CHECK(res.size() == 1 && res[0].m.count == 2 && res[0].m.axis_dist < 15, "re-measured nearer the axis (%zu results)", res.size());
        // the crop that goes with it: 224 x 224 BGR, gear centered (bright center, dark corners)
        if (!res.empty() && !res[0].crop.empty()) {
            const cv::Mat &cr2 = res[0].crop;
            cv::Mat g1;
            cv::cvtColor(cr2, g1, cv::COLOR_BGR2GRAY);
            cv::Moments mo = cv::moments(g1 > 130, true);
            float cx = float(mo.m10 / mo.m00), cy = float(mo.m01 / mo.m00);
            printf("[crop] %dx%d, bright centroid (%.1f, %.1f), area %.0f px (gear ~ %.0f)\n", cr2.cols, cr2.rows, cx, cy, mo.m00, kPi * 16 * 16 / 0.0625 - kPi * 9 / 0.0625 - 30 * 0.36f * 3.35f * 2.25f / 0.0625f);
            CHECK(cr2.cols == 224 && cr2.rows == 224 && cr2.type() == CV_8UC3, "crop size / type");
            CHECK(std::fabs(cx - 111.5f) < 1.5f && std::fabs(cy - 111.5f) < 1.5f, "gear centered in the crop");
        } else {
            CHECK(false, "no crop returned");
        }
    }
    {   // K: local storage — 3 decided parts, images for the NG / recheck ones only, read back over the TCP feed
        StorageConfig sc;
        sc.dir = "/tmp/des_logtest";
        sc.feed_port = 19501;   // live upload: a loopback client receives the same records + images
        if (system("rm -rf /tmp/des_logtest") != 0) {}
        PartLog log;
        std::string e = log.open(sc);
        CHECK(e.empty() && log.is_open(), "open: %s", e.c_str());
        int cfd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(19501);
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        const bool conn = connect(cfd, reinterpret_cast<sockaddr *>(&sa), sizeof sa) == 0;
        CHECK(conn, "feed: connect to port 19501");
        auto read_exact = [&](std::string &out, size_t n) {   // 2 s timeout overall per call
            out.clear();
            while (out.size() < n) {
                pollfd p{cfd, POLLIN, 0};
                if (poll(&p, 1, 2000) <= 0) return false;
                char b[4096];
                ssize_t r = recv(cfd, b, std::min(sizeof b, n - out.size()), 0);
                if (r <= 0) return false;
                out.append(b, size_t(r));
            }
            return true;
        };
        struct Frame { char type; std::string name, payload; };
        auto read_frame = [&](Frame &f) {
            std::string hd;
            if (!read_exact(hd, 10) || hd[0] != 'D' || hd[1] != 'F') return false;
            const size_t nl = (uint8_t(hd[4]) << 8) | uint8_t(hd[5]);
            const size_t pl = (size_t(uint8_t(hd[6])) << 24) | (uint8_t(hd[7]) << 16) | (uint8_t(hd[8]) << 8) | uint8_t(hd[9]);
            f.type = hd[2];
            return read_exact(f.name, nl) && read_exact(f.payload, pl);
        };
        Frame hello;
        CHECK(conn && read_frame(hello) && hello.type == 'H' && hello.payload == csv_header() && hello.name == log.session, "feed: hello carries the session and the CSV header");
        cv::Mat crop(224, 224, CV_8UC3, cv::Scalar(30, 200, 30));
        const Disposition disp[] = {Disposition::Pick, Disposition::Flap, Disposition::Recheck};
        for (int i = 0; i < 3; i++) {
            PartRecord r;
            r.track_id = 10 + i;
            r.cls = int16_t(i);
            r.label = i == 0 ? "gear_ok" : i == 1 ? "gear_short" : "unclassified";
            r.disp = disp[i];
            r.dest = i == 0 ? "gear_box" : i == 1 ? "scrap" : "recheck";
            snprintf(r.why, sizeof r.why, "%s", "test, with a comma");
            r.meas.valid = true; r.meas.kind = 'g'; r.meas.size = 32.01f; r.meas.teeth = 30;
            r.t_s = log.now_s();
            log.add(r, crop);
        }
        log.close();
        FILE *f = fopen(log.csv_path.c_str(), "r");
        int lines = 0, with_img = 0, fields_ok = 0;
        char buf[1024];
        while (f && fgets(buf, sizeof buf, f)) {
            lines++;
            if (lines == 1) continue;
            int commas = 0;
            for (char *c = buf; *c; c++) commas += *c == ',';
            fields_ok += commas == 42;   // 43 columns, the comma in "why" was replaced
            if (strstr(buf, ".jpg")) with_img++;
        }
        if (f) fclose(f);
        printf("[storage] %s: %d lines, %d with image, written %llu images %llu dropped %llu errors %llu\n", log.csv_path.c_str(), lines, with_img, (unsigned long long)log.written.load(), (unsigned long long)log.images.load(), (unsigned long long)log.dropped.load(), (unsigned long long)log.errors.load());
        CHECK(lines == 4 && fields_ok == 3, "csv lines %d, rows with 43 columns %d", lines, fields_ok);
        CHECK(with_img == 2 && log.images == 2, "images only for flap / recheck (%d)", with_img);
        int nr = 0, ni = 0, jpg_ok = 0;
        Frame fr;
        std::string first_line;
        while (read_frame(fr)) {   // the log closed: the feed flushed and hung up
            if (fr.type == 'R' && nr++ == 0) first_line = fr.payload;
            if (fr.type == 'I') {
                ni++;
                jpg_ok += fr.payload.size() > 100 && uint8_t(fr.payload[0]) == 0xFF && uint8_t(fr.payload[1]) == 0xD8 && fr.name.rfind("img/", 0) == 0;
            }
        }
        close(cfd);
        FILE *f2 = fopen(log.csv_path.c_str(), "r");
        char l1[2048] = "", l2[2048] = "";
        if (f2) {
            if (!fgets(l1, sizeof l1, f2) || !fgets(l2, sizeof l2, f2)) l2[0] = 0;
            fclose(f2);
        }
        l2[strcspn(l2, "\n")] = 0;
        printf("[feed] %d records, %d images over TCP\n", nr, ni);
        CHECK(nr == 3 && ni == 2 && jpg_ok == 2, "feed: 3 records + 2 JPEG images (%d / %d / %d)", nr, ni, jpg_ok);
        CHECK(first_line == l2, "feed: record identical to the CSV line");
    }
    {   // part colour from a crop: dark belt, a bright disc; white vs yellow vs blue vs red, and the classes
        std::vector<ColorClass> cl = {{"white", 0, 360, 0, 45}, {"yellow", 35, 75, 60, 255},
                                      {"blue", 180, 260, 50, 255}, {"red", 340, 10, 60, 255}};
        struct { cv::Scalar bgr; const char *want; } cases[] = {{cv::Scalar(225, 222, 230), "white"},
                                                                {cv::Scalar(40, 200, 220), "yellow"},
                                                                {cv::Scalar(220, 120, 40), "blue"},
                                                                {cv::Scalar(50, 40, 210), "red"}};
        for (auto &k2 : cases) {
            cv::Mat crop(224, 224, CV_8UC3, cv::Scalar(22, 24, 20));
            cv::circle(crop, cv::Point(112, 112), 70, k2.bgr, cv::FILLED);
            cv::circle(crop, cv::Point(112, 112), 12, cv::Scalar(22, 24, 20), cv::FILLED);   // bore
            const PartColor pc = part_color(crop);
            const int ci = classify_color(cl, pc);
            CHECK(ci >= 0 && !strcmp(cl[ci].name.c_str(), k2.want), "colour %s: hue %.0f sat %.0f -> %s", k2.want, pc.hue, pc.sat, ci >= 0 ? cl[ci].name.c_str() : "none");
        }
    }
    // flat field (flat_field.hpp): white level 180 * v(x, y), v = 1 at the centre .. 0.4 in the corners
    auto vig = [](float x, float y) {
        const float dx = (x - 959.5f) / 960, dy = (y - 539.5f) / 540;
        return 1.f - 0.3f * (dx * dx + dy * dy);
    };
    {   // crops of a uniform part (150 at the centre) seen through the vignetting come out uniform at 150
        FlatField ff;
        ff.gw = 48;
        ff.gh = 27;
        ff.ref = 180;
        for (int j = 0; j < 27; j++)
            for (int i = 0; i < 48; i++) ff.white.push_back(180 * vig((i + 0.5f) * 40 - 0.5f, (j + 0.5f) * 40 - 0.5f));
        cv::Mat img(1080, 1920, CV_8UC1);
        for (int y = 0; y < 1080; y++)
            for (int x = 0; x < 1920; x++) img.at<uint8_t>(y, x) = uint8_t(std::lround(150 * vig(float(x), float(y))));
        CropSpec cs;
        const Vec2 c{300 * S, 250 * S};   // near the top-left corner (v ~ 0.77)
        cv::Mat raw = crop_part(img, h, c, 0.3f, cs);
        cs.flat = &ff;
        cv::Mat cor = crop_part(img, h, c, 0.3f, cs);
        double mn, mx;
        cv::Mat g;
        cv::cvtColor(cor, g, cv::COLOR_BGR2GRAY);
        cv::minMaxLoc(g, &mn, &mx);
        printf("[flat crop] raw mean %.1f, corrected mean %.1f (%.0f .. %.0f), centre gain %.3f corner gain %.3f\n", cv::mean(raw)[0], cv::mean(cor)[0], mn, mx, ff.gain_at(959.5f, 539.5f), ff.gain_at(0, 0));
        CHECK(cv::mean(raw)[0] < 125 && std::fabs(cv::mean(cor)[0] - 150) < 2 && mx - mn <= 4, "flat crop: raw %.1f corrected %.1f range %.0f", cv::mean(raw)[0], cv::mean(cor)[0], mx - mn);
        CHECK(std::fabs(ff.gain_at(959.5f, 539.5f) - 1) < 0.01f && std::fabs(ff.gain_at(0, 0) * vig(20, 20) - 1) < 0.03f, "flat gains: centre %.3f corner %.3f", ff.gain_at(959.5f, 539.5f), ff.gain_at(0, 0));
    }
    {   // FlatBuilder: a white sheet (180 v) 1100 px wide over the belt (30 v), 7 stops 200 px apart
        FlatBuilder fb;
        for (int k = 0; k < 7; k++) {
            const int x0 = 820 - 200 * k;
            cv::Mat f(1080, 1920, CV_8UC1);
            for (int y = 0; y < 1080; y++)
                for (int x = 0; x < 1920; x++)
                    f.at<uint8_t>(y, x) = uint8_t(std::lround((x >= x0 && x < x0 + 1100 ? 180 : 30) * vig(float(x), float(y))));
            cv::GaussianBlur(f, f, cv::Size(5, 5), 1.0);
            fb.add(f);
        }
        FlatField f;
        FlatStats st;
        const std::string e = fb.build(f, 48, 27, &st);
        printf("[flat build] %s covered %.3f centre %.1f corner %.3f (want %.3f) residual %.3f .. %.3f\n", e.empty() ? "ok" : e.c_str(), st.covered, st.centre, st.corner_rel[0], vig(20, 20), st.resid_p5, st.resid_p95);
        CHECK(e.empty() && st.covered > 0.97f && std::fabs(st.centre - 180) < 2, "flat build: %s covered %.3f centre %.1f", e.c_str(), st.covered, st.centre);
        CHECK(std::fabs(st.corner_rel[0] - vig(20, 20)) < 0.03f && st.resid_p5 > 0.97f && st.resid_p95 < 1.03f, "flat build: corner %.3f residual %.3f .. %.3f", st.corner_rel[0], st.resid_p5, st.resid_p95);
        const std::string fn = "/tmp/des_flat_test.json";
        FlatField back;
        CHECK(save_flat(fn, f, "test").empty() && load_flat(fn, back).empty() && back.gw == 48 && back.gh == 27 && std::fabs(back.white_at(700, 300) - f.white_at(700, 300)) < 0.01f, "flat.json round trip");
    }
    return test_summary();
}
