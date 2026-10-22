// measure_tool.cpp — see measure_tool.hpp
#include "measure_tool.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <opencv2/imgproc.hpp>
namespace des {
namespace {
const char *kGearQ[] = {"OD", "root", "bore", "ecc", "teeth"};
const char *kRackQ[] = {"length", "height", "pitch", "teeth"};
std::vector<cv::Point> to_px(const std::vector<Vec2> &pts, const Homography &hinv)
{
    std::vector<cv::Point> out;
    for (const Vec2 &v : pts) {
        Vec2 p = hinv.map(v);
        out.emplace_back(int(std::lround(p.x)), int(std::lround(p.y)));
    }
    return out;
}
std::vector<Vec2> circle_pts(Vec2 c, float r)
{
    std::vector<Vec2> out;
    for (int i = 0; i < 90; i++) out.push_back({c.x + r * std::cos(i * 2 * kPi / 90), c.y + r * std::sin(i * 2 * kPi / 90)});
    return out;
}
}  // namespace
std::vector<PartResult> measure_parts(const cv::Mat &gray, const std::vector<Detection> &dets, const AppConfig &cfg, float belt_mm_per_row)
{
    std::vector<PartResult> out;
    MeasureConfig mc = cfg.measure.m;
    if (cfg.measure.axis_from_image) mc.axis_table = cfg.px_to_table.map({0.5f * gray.cols, 0.5f * gray.rows});
    mc.belt_mm_per_row = belt_mm_per_row;
    mc.row0 = 0.5f * gray.rows;
    const Homography hinv = cfg.px_to_table.inverse();
    const float margin = cfg.measure.margin_mm;
    for (const Detection &d : dets) {
        if (d.partial || d.merged) continue;
        ORect fr{d.p, d.len / 2 + margin, d.wid / 2 + margin, d.axis};
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (const Vec2 &v : fr.corners()) {
            Vec2 p = hinv.map(v);
            x0 = std::min(x0, p.x);
            x1 = std::max(x1, p.x);
            y0 = std::min(y0, p.y);
            y1 = std::max(y1, p.y);
        }
        cv::Rect roi(int(std::floor(x0)), int(std::floor(y0)), int(std::ceil(x1 - x0)) + 1, int(std::ceil(y1 - y0)) + 1);
        if (roi.x < 2 || roi.y < 2 || roi.br().x > gray.cols - 2 || roi.br().y > gray.rows - 2) continue;
        PartResult r;
        r.p = d.p;
        r.kind = d.len > 0 && d.wid / d.len > cfg.measure.gear_ratio ? 'g' : 'r';
        if (r.kind == 'g') r.g = measure_gear(gray, roi, cfg.px_to_table, mc, &r.r_theta);
        else r.r = measure_rack(gray, roi, cfg.px_to_table, mc);
        out.push_back(std::move(r));
    }
    return out;
}
std::string describe(const PartResult &r)
{
    char b[320];
    if (r.kind == 'g') {
        const GearMeasure &g = r.g;
        if (!g.ok) snprintf(b, sizeof b, "gear at (%6.1f, %6.1f): FAILED %s", r.p.x, r.p.y, g.err.c_str());
        else
            snprintf(b, sizeof b, "gear at (%6.1f, %6.1f): OD %.3f root %.3f bore %.3f ecc %.3f | teeth %d short %d missing %d "
                     "flash %d%s | fit rms %.3f smear %.2f", r.p.x, r.p.y, g.od, g.root_d, g.bore_d, g.ecc, g.teeth, g.short_teeth, g.missing_teeth,
                     g.flash_gaps, g.teeth_judged ? "" : " (NOT judged: parallax)", g.fit_rms, g.smear_mm);
    } else {
        const RackMeasure &k = r.r;
        if (!k.ok) snprintf(b, sizeof b, "rack at (%6.1f, %6.1f): FAILED %s", r.p.x, r.p.y, k.err.c_str());
        else
            snprintf(b, sizeof b, "rack at (%6.1f, %6.1f): length %.3f height %.3f pitch %.4f | teeth %d short %d missing %d "
                     "flash %d%s | axis %.1f deg smear %.2f",
                     r.p.x, r.p.y, k.length, k.height, k.pitch, k.teeth, k.short_teeth, k.missing_teeth, k.flash_gaps,
                     k.teeth_judged ? "" : " (NOT judged: parallax)", k.axis * 180 / kPi, k.smear_mm);
    }
    return b;
}
void draw_parts(cv::Mat &bgr, const std::vector<PartResult> &res, const Homography &px_to_table)
{
    const Homography hinv = px_to_table.inverse();
    int inset = 0;
    for (const PartResult &r : res) {
        Vec2 at = hinv.map(r.p);
        char t[96];
        if (r.kind == 'g' && r.g.ok) {
            const GearMeasure &g = r.g;
            std::vector<std::vector<cv::Point>> polys = {to_px(circle_pts(g.center, g.od / 2), hinv)};
            cv::polylines(bgr, polys, true, cv::Scalar(0, 220, 0), 1, cv::LINE_AA);
            if (g.root_d > 0) {
                polys = {to_px(circle_pts(g.center, g.root_d / 2), hinv)};
                cv::polylines(bgr, polys, true, cv::Scalar(0, 200, 255), 1, cv::LINE_AA);
            }
            if (g.bore_d > 0) {
                polys = {to_px(circle_pts(g.bore_center, g.bore_d / 2), hinv)};
                cv::polylines(bgr, polys, true, cv::Scalar(255, 200, 0), 1, cv::LINE_AA);
            }
            snprintf(t, sizeof t, "OD %.2f z%d bore %.2f ecc %.2f%s", g.od, g.teeth, g.bore_d, g.ecc, g.teeth_judged ? "" : " ?");
            cv::putText(bgr, t, cv::Point(int(at.x) - 120, int(at.y) - 130), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 0), 1, cv::LINE_AA);
            if (g.short_teeth || g.missing_teeth || g.flash_gaps) {
                snprintf(t, sizeof t, "short %d missing %d flash %d", g.short_teeth, g.missing_teeth, g.flash_gaps);
                cv::putText(bgr, t, cv::Point(int(at.x) - 120, int(at.y) - 108), cv::FONT_HERSHEY_SIMPLEX, 0.6,
                            cv::Scalar(0, 0, 255), 1, cv::LINE_AA);
            }
            // r(theta) inset: 720 bins -> 360 px wide, radius range (root - 0.3 .. tip + 0.3) -> 100 px high
            const int W = 360, H = 100, x0 = bgr.cols - W - 20, y0 = 130 + inset * (H + 30);
            if (y0 + H < bgr.rows && !r.r_theta.empty()) {
                cv::rectangle(bgr, cv::Rect(x0 - 2, y0 - 2, W + 4, H + 4), cv::Scalar(0, 0, 0), cv::FILLED);
                const float lo = (g.root_d > 0 ? g.root_d / 2 : g.od / 2 - 2.5f) - 0.3f, hi = g.od / 2 + 0.3f;
                auto yy = [&](float rr) { return y0 + H - int((rr - lo) / (hi - lo) * H); };
                cv::line(bgr, cv::Point(x0, yy(g.od / 2)), cv::Point(x0 + W, yy(g.od / 2)), cv::Scalar(0, 120, 0), 1);
                std::vector<cv::Point> curve;
                for (size_t i = 0; i < r.r_theta.size(); i += 2)
                    curve.emplace_back(x0 + int(i / 2), std::clamp(yy(r.r_theta[i]), y0, y0 + H));
                cv::polylines(bgr, curve, false, cv::Scalar(255, 255, 255), 1, cv::LINE_8);
                snprintf(t, sizeof t, "r(theta) gear at x %.0f", r.p.x);
                cv::putText(bgr, t, cv::Point(x0, y0 - 6), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);
                inset++;
            }
        } else if (r.kind == 'r' && r.r.ok) {
            const RackMeasure &k = r.r;
            ORect box{k.center, k.length / 2, k.height / 2, k.axis};
            auto c = box.corners();
            std::vector<std::vector<cv::Point>> polys = {to_px(std::vector<Vec2>(c.begin(), c.end()), hinv)};
            cv::polylines(bgr, polys, true, cv::Scalar(0, 220, 0), 1, cv::LINE_AA);
            snprintf(t, sizeof t, "L %.2f p %.3f teeth %d%s", k.length, k.pitch, k.teeth, k.teeth_judged ? "" : " ?");
            cv::putText(bgr, t, cv::Point(int(at.x) - 120, int(at.y) - 70), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 0), 1, cv::LINE_AA);
        } else {
            cv::putText(bgr, r.kind == 'g' ? r.g.err : r.r.err, cv::Point(int(at.x) - 100, int(at.y)),
                        cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 0, 255), 1, cv::LINE_AA);
        }
    }
}
void RepeatStats::add(const std::vector<PartResult> &res)
{
    for (const PartResult &r : res) {
        Acc *a = nullptr;
        for (Acc &q : parts_)
            if (q.kind == r.kind && norm(q.p - r.p) < 3) a = &q;
        if (!a) {
            parts_.push_back({});
            a = &parts_.back();
            a->p = r.p;
            a->kind = r.kind;
            a->v.resize(r.kind == 'g' ? 5 : 4);
        }
        if (!r.ok()) { a->fails++; continue; }
        if (r.kind == 'g') {
            const double q[] = {r.g.od, r.g.root_d, r.g.bore_d, r.g.ecc, double(r.g.teeth)};
            for (int i = 0; i < 5; i++) a->v[i].push_back(q[i]);
        } else {
            const double q[] = {r.r.length, r.r.height, r.r.pitch, double(r.r.teeth)};
            for (int i = 0; i < 4; i++) a->v[i].push_back(q[i]);
        }
    }
}
void RepeatStats::print() const
{
    printf("\n== repeatability (still parts, same frame position)\n");
    for (const Acc &a : parts_) {
        printf("%s at (%.1f, %.1f): %zu ok, %d failed\n", a.kind == 'g' ? "gear" : "rack", a.p.x, a.p.y,
               a.v.empty() ? size_t(0) : a.v[0].size(), a.fails);
        for (size_t i = 0; i < a.v.size(); i++) {
            const auto &s = a.v[i];
            if (s.empty()) continue;
            double m = 0, sq = 0, lo = s[0], hi = s[0];
            for (double x : s) { m += x; lo = std::min(lo, x); hi = std::max(hi, x); }
            m /= s.size();
            for (double x : s) sq += (x - m) * (x - m);
            const double sd = s.size() > 1 ? std::sqrt(sq / (s.size() - 1)) : 0;
            printf("   %-7s mean %9.4f  sigma %.4f  range %.4f (%.4f .. %.4f)\n", a.kind == 'g' ? kGearQ[i] : kRackQ[i], m, sd, hi - lo, lo, hi);
        }
    }
}
}  // namespace des
