// test_measure: gear / rack measurement on synthetic images with a known ground truth (4x supersampled polygons,
// anti-aliased, blurred, noisy; optional parallax: union of the slices from the bottom face to the top face magnified
// about the optical axis, or the top face only).  ./test_measure     exit code = failed checks
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include <opencv2/imgproc.hpp>

#include "measure.hpp"

using namespace des;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (cond) g_pass++;                                           \
        else { g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__);  \
               printf(__VA_ARGS__); printf("\n"); }                   \
    } while (0)

static const float S = 0.15f;                 // mm per px
// fillPoly fills boundary pixels: rendered shapes are 2 * 0.5 / SS px = 0.019 mm larger than drawn (holes too)
static const float GROW = 0.15f / 8;
// diameter / length checks: |measured - (drawn + GROW)| < TOL. 0.02 mm = 0.13 px on these synthetic images.
static const float TOL = 0.02f;
static const Vec2 AXIS{960 * S, 540 * S};     // optical axis foot point = image center

using Poly = std::vector<Vec2>;

// gear outline: 3600 samples of R(theta); tip[k] = tip radius of tooth k (rr = missing), fill[k] = floor of the gap
// after tooth k (0 = none; negative = a blob of that height standing in the middle of the gap only)
static Poly gear(Vec2 c, int z, float ra, float rr, float phi, std::vector<float> tip, std::vector<float> fill)
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
        printf("[timing] measure_gear %.2f ms/part (ROI %dx%d)\n",
               std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 20,
               s.roi.width, s.roi.height);
        printf("[gear30] ok %d teeth %d OD %.3f root %.3f bore %.3f ecc %.3f rms %.3f short %d missing %d flash %d\n",
               g.ok, g.teeth, g.od, g.root_d, g.bore_d, g.ecc, g.fit_rms, g.short_teeth, g.missing_teeth, g.flash_gaps);
        CHECK(g.ok, "gear30 failed: %s", g.err.c_str());
        CHECK(g.teeth == 30, "teeth %d", g.teeth);
        CHECK(std::fabs(g.od - 32 - GROW) < TOL, "OD %.3f", g.od);
        CHECK(std::fabs(g.root_d - 27.5f - GROW) < TOL, "root %.3f", g.root_d);
        CHECK(std::fabs(g.bore_d - 6 - GROW) < TOL, "bore %.3f", g.bore_d);
        CHECK(std::fabs(g.ecc - 0.13f) < 0.01f, "ecc %.3f", g.ecc);
        CHECK(g.short_teeth == 0 && g.missing_teeth == 0 && g.flash_gaps == 0, "false defects %d %d %d", g.short_teeth,
              g.missing_teeth, g.flash_gaps);
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
        printf("[gear30 defects] teeth %d OD %.3f short %d missing %d flash %d\n", g.teeth, g.od, g.short_teeth,
               g.missing_teeth, g.flash_gaps);
        CHECK(g.ok && g.teeth == 30, "spec teeth %d (%s)", g.teeth, g.err.c_str());
        CHECK(g.short_teeth == 1, "short %d", g.short_teeth);
        CHECK(g.missing_teeth == 1, "missing %d", g.missing_teeth);
        CHECK(g.flash_gaps == 3, "flash %d", g.flash_gaps);
        CHECK(std::fabs(g.od - 32 - GROW) < TOL, "OD with defects %.3f", g.od);
    }
    {   // C: spec mix-up — 28 teeth (OD 30), must not be read as 30 with two missing
        Vec2 c{60, 110};
        Scene s = render(gear(c, 28, 15, 12.75f, 0.2f, {}, {}), circle(c, 3), 1.f);
        GearMeasure g = measure_gear(s.img, s.roi, h, cfg);
        printf("[gear28] teeth %d OD %.3f missing %d\n", g.teeth, g.od, g.missing_teeth);
        CHECK(g.ok && g.teeth == 28 && g.missing_teeth == 0, "28-tooth gear read as %d (+%d missing)", g.teeth,
              g.missing_teeth);
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
        printf("[parallax 22mm] uncorrected OD %.3f bore %.3f | corrected OD %.3f bore %.3f teeth %d smear %.2f "
               "judged %d defects %d/%d/%d\n", raw.od, raw.bore_d, cor.od, cor.bore_d, cor.teeth, cor.smear_mm,
               cor.teeth_judged, cor.short_teeth, cor.missing_teeth, cor.flash_gaps);
        CHECK(std::fabs(raw.od - 32) > 0.3f, "parallax should show without correction (%.3f)", raw.od);
        CHECK(cor.ok && std::fabs(cor.od - 32 - GROW) < TOL, "corrected OD %.3f", cor.od);
        CHECK(std::fabs(cor.bore_d - 6 - GROW) < TOL, "corrected bore %.3f", cor.bore_d);
        CHECK(cor.teeth_judged && cor.teeth == 30, "judged %d teeth %d", cor.teeth_judged, cor.teeth);
        CHECK(cor.short_teeth == 0 && cor.missing_teeth == 0 && cor.flash_gaps == 0, "false defects %d %d %d",
              cor.short_teeth, cor.missing_teeth, cor.flash_gaps);
    }
    {   // D2: the defect gear 21 mm off the axis
        Vec2 c = AXIS + Vec2{-15, 15};
        std::vector<float> tip(30, 16.f), fill(30, 0.f);
        tip[5] = 15.f;
        tip[12] = 13.75f;
        fill[20] = 13.75f + 0.8f;
        Scene s = render(gear(c, 30, 16, 13.75f, 0.3f, tip, fill), circle(c, 3), k);
        GearMeasure g = measure_gear(s.img, s.roi, h, pc);
        printf("[parallax defects] OD %.3f teeth %d smear %.2f short %d missing %d flash %d\n", g.od, g.teeth,
               g.smear_mm, g.short_teeth, g.missing_teeth, g.flash_gaps);
        CHECK(g.ok && g.teeth_judged && g.teeth == 30, "teeth %d judged %d", g.teeth, g.teeth_judged);
        CHECK(g.short_teeth == 1 && g.missing_teeth == 1 && g.flash_gaps == 1, "defects %d %d %d", g.short_teeth,
              g.missing_teeth, g.flash_gaps);
    }
    {   // D3: 112 mm off the axis (smear 3.2 mm ~ one pitch: side gaps filled) -> size only, teeth not judged
        Vec2 c = AXIS + Vec2{100, 50};
        Scene s = render(gear(c, 30, 16, 13.75f, 0.0f, {}, {}), circle(c, 3), k);
        GearMeasure g = measure_gear(s.img, s.roi, h, pc);
        printf("[parallax 112mm] OD %.3f bore %.3f smear %.2f judged %d\n", g.od, g.bore_d, g.smear_mm,
               g.teeth_judged);
        CHECK(g.ok && !g.teeth_judged, "far off-axis must not judge teeth (smear %.2f)", g.smear_mm);
        CHECK(g.short_teeth == 0 && g.missing_teeth == 0 && g.flash_gaps == 0, "unjudged counts must be 0");
        CHECK(std::fabs(g.od - 32 - GROW) < 2 * TOL, "far OD %.3f", g.od);
        CHECK(std::fabs(g.bore_d - 6 - GROW) < 2 * TOL, "far bore %.3f", g.bore_d);
    }
    {   // D4: walls dark (only the top face bright), 112 mm off: everything scales back, no smear
        Vec2 c = AXIS + Vec2{100, 50};
        Scene s = render(gear(c, 30, 16, 13.75f, 0.0f, {}, {}), circle(c, 3), k, true);
        MeasureConfig tc = pc;
        tc.walls_visible = false;
        GearMeasure g = measure_gear(s.img, s.roi, h, tc);
        printf("[top face 112mm] OD %.3f bore %.3f teeth %d judged %d\n", g.od, g.bore_d, g.teeth, g.teeth_judged);
        CHECK(g.ok && g.teeth_judged && g.teeth == 30, "teeth %d", g.teeth);
        CHECK(std::fabs(g.od - 32 - GROW) < TOL && std::fabs(g.bore_d - 6 - GROW) < TOL, "OD %.3f bore %.3f", g.od,
              g.bore_d);
        CHECK(g.short_teeth == 0 && g.missing_teeth == 0 && g.flash_gaps == 0, "false defects");
    }
    {   // E: rack 50 mm, 15 teeth, 25 deg
        Scene s = render(rack({150, 80}, 50, 15, 25 * kPi / 180, {}), {}, 1.f);
        RackMeasure r = measure_rack(s.img, s.roi, h, cfg);
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 20; i++) measure_rack(s.img, s.roi, h, cfg);
        printf("[timing] measure_rack %.2f ms/part (ROI %dx%d)\n",
               std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 20,
               s.roi.width, s.roi.height);
        printf("[rack50] ok %d length %.3f height %.3f pitch %.4f teeth %d short %d flash %d\n", r.ok, r.length, r.height,
               r.pitch, r.teeth, r.short_teeth, r.flash_gaps);
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
        CHECK(r.teeth == 14 && r.missing_teeth == 0 && r.flash_gaps == 0, "teeth %d missing %d flash %d", r.teeth,
              r.missing_teeth, r.flash_gaps);
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
        printf("[rack defects] teeth %d missing %d flash %d short %d pitch %.4f\n", r.teeth, r.missing_teeth,
               r.flash_gaps, r.short_teeth, r.pitch);
        CHECK(r.ok && r.teeth == 15, "teeth %d (%s)", r.teeth, r.err.c_str());
        CHECK(r.missing_teeth == 1 && r.flash_gaps == 3 && r.short_teeth == 0, "missing %d flash %d short %d",
              r.missing_teeth, r.flash_gaps, r.short_teeth);
        CHECK(std::fabs(r.pitch - kPi) < 0.01f, "pitch %.4f", r.pitch);
    }
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail;
}
