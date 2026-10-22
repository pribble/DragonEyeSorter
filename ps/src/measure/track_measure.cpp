// track_measure.cpp — see track_measure.hpp
#include "track_measure.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <opencv2/imgproc.hpp>
#include "measure.hpp"
#include "roi_crop.hpp"
namespace des {
namespace {
// H * T(ox, oy): pixels of an image cropped at (ox, oy) -> table mm
Homography shifted(const Homography &H, double ox, double oy)
{
    Homography r = H;
    r.h[2] = H.h[0] * ox + H.h[1] * oy + H.h[2];
    r.h[5] = H.h[3] * ox + H.h[4] * oy + H.h[5];
    r.h[8] = H.h[6] * ox + H.h[7] * oy + H.h[8];
    return r;
}
}  // namespace
TrackMeasurer::TrackMeasurer(const TrackMeasureConfig &cfg, const Homography &px_to_table,
                             const Homography &px_to_part)
    : cfg_(cfg), h_(px_to_table), hp_(px_to_part), hpinv_(px_to_part.inverse())
{
}
std::vector<MeasureResult> TrackMeasurer::measure(const Tracker &tr, const uint8_t *bgra, int width, int height,
                                                  int stride, double belt_mm, double belt_span_mm, uint32_t frame)
{
    std::vector<MeasureResult> out;
    if (!cfg_.enabled || cfg_.per_frame <= 0) return out;
    const auto t0 = std::chrono::steady_clock::now();
    if (cfg_.axis_from_image) cfg_.m.axis_table = h_.map({0.5f * width, 0.5f * height});
    const Vec2 O = cfg_.m.axis_table;
    struct Cand { const Track *t; float d, gain; cv::Rect roi; };
    const std::vector<Track> &tracks = tr.tracks();
    std::vector<Cand> cands;
    cands.reserve(tracks.size());
    for (const Track &t : tracks) {
        if ((t.state != TrackState::New && t.state != TrackState::Tracking) || t.merged || t.n_geom == 0) continue;
        const float d = norm(Vec2{t.x_at(belt_mm), t.y} - O);
        if (t.meas.count > 0 && !(d < cfg_.improve * t.meas.axis_dist - 1.f)) continue;
        const Box b = image_box(hpinv_, t.footprint(belt_mm, cfg_.margin_mm));
        const int ix0 = int(std::floor(b.x0)), iy0 = int(std::floor(b.y0)), ix1 = int(std::ceil(b.x1)),
                  iy1 = int(std::ceil(b.y1));
        if (ix0 < 2 || iy0 < 2 || ix1 > width - 3 || iy1 > height - 3) continue;   // not fully in the image
        cands.push_back({&t, d, t.meas.count > 0 ? t.meas.axis_dist - d : 1e9f,
                         cv::Rect(ix0, iy0, ix1 - ix0 + 1, iy1 - iy0 + 1)});
    }
    auto by_gain = [](const Cand &a, const Cand &b) { return a.gain > b.gain; };
    if (cfg_.per_frame == 1 && cands.size() > 1) std::nth_element(cands.begin(), cands.begin(), cands.end(), by_gain);
    else std::sort(cands.begin(), cands.end(), by_gain);
    cv::Mat src(height, width, CV_8UC4, const_cast<uint8_t *>(bgra), size_t(stride));
    MeasureConfig mc = cfg_.m;   // one copy per frame; row0 is set per ROI below
    mc.belt_mm_per_row = float(belt_span_mm / height);
    CropSpec cs;   // same for every candidate
    cs.side_mm = cfg_.crop_side_mm; cs.out_px = cfg_.crop_px; cs.flat = cfg_.flat.get();
    out.reserve(std::min<size_t>(cands.size(), size_t(cfg_.per_frame)));
    cv::Mat g;   // reused across candidates (cvtColor re-creates only when the ROI size changes)
    for (size_t k = 0; k < cands.size() && int(k) < cfg_.per_frame; k++) {
        const Cand &c = cands[k];
        cv::cvtColor(src(c.roi), g, cv::COLOR_BGRA2GRAY);   // one pass over the (uncached) camera buffer
        const Homography hs = shifted(h_, c.roi.x, c.roi.y);
        mc.row0 = 0.5f * height - float(c.roi.y);   // mid-frame row = the tracker's belt position
        const cv::Rect all(0, 0, g.cols, g.rows);
        PartMeasure pm = c.t->meas;   // a failed attempt keeps the previous result
        pm.count++;
        pm.axis_dist = c.d;           // also after a failure: retry only once the part is clearly nearer
        bool ok = false;
        Vec2 center{c.t->x_at(belt_mm), c.t->y};
        float axis = c.t->axis;
        if (c.t->len > 0 && c.t->wid / c.t->len > cfg_.gear_ratio) {
            const GearMeasure r = measure_gear(g, all, hs, mc);
            if ((ok = r.ok)) {
                center = r.center;
                pm.kind = 'g';
                pm.size = r.od; pm.bore = r.bore_d; pm.ecc = r.ecc; pm.pitch = 0;
                pm.teeth = r.teeth; pm.short_teeth = r.short_teeth; pm.missing_teeth = r.missing_teeth;
                pm.flash_gaps = r.flash_gaps; pm.teeth_judged = r.teeth_judged; pm.smear = r.smear_mm;
            }
        } else {
            const RackMeasure r = measure_rack(g, all, hs, mc);
            if ((ok = r.ok)) {
                center = r.center;
                axis = r.axis;
                pm.kind = 'r';
                pm.size = r.length; pm.bore = 0; pm.ecc = 0; pm.pitch = r.pitch;
                pm.teeth = r.teeth; pm.short_teeth = r.short_teeth; pm.missing_teeth = r.missing_teeth;
                pm.flash_gaps = r.flash_gaps; pm.teeth_judged = r.teeth_judged; pm.smear = r.smear_mm;
            }
        }
        MeasureResult res;
        res.id = c.t->id;
        if (ok) { pm.valid = true; pm.frame = frame; measured++; } else { failed++; }
        if (cfg_.crop_px > 0) {   // also after a failed measurement (tracked position): defective parts need classes
            res.crop = crop_part(src, hp_, center, axis, cs);   // centre = where the part stands
            const PartColor pc = part_color(res.crop);   // ~0.5 ms on the A35
            pm.hue = pc.hue; pm.sat = pc.sat; pm.color = classify_color(cfg_.colors, pc);
        }
        res.m = pm;
        out.push_back(std::move(res));
    }
    last_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return out;
}
void TrackMeasurer::apply(Tracker &tr, const std::vector<MeasureResult> &res)
{
    for (const MeasureResult &r : res) {
        Track *t = tr.find(r.id);
        if (t && (t->state == TrackState::New || t->state == TrackState::Tracking)) t->meas = r.m;
    }
}
}  // namespace des
