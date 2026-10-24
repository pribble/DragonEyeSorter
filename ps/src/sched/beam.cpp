// beam.cpp — see beam.hpp
#include "beam.hpp"
#include <algorithm>
#include <cmath>
namespace des {
const char *to_str(BeamKind k)
{
    static const char *n[] = {"matched", "repeat", "no_track", "missed", "lost"};
    return size_t(k) < 5 ? n[size_t(k)] : "?";
}
float beam_part_r(const Track &t)
{
    if (t.meas.valid && t.meas.kind == 'g' && t.meas.size > 0) return 0.5f * t.meas.size;
    return t.ext_x;
}
double BeamMonitor::Stats::sd() const
{
    if (n_err < 2) return 0;
    const double m = mean();
    return std::sqrt(std::max(0.0, (err_sq - n_err * m * m) / double(n_err - 1)));
}
void BeamMonitor::poll(uint32_t cnt, double edge_belt_mm, int32_t edge_enc, std::vector<BeamResult> &out)
{
    if (!have_cnt_) {   // edges counted before we started are not ours
        have_cnt_ = true;
        last_cnt_ = cnt;
        return;
    }
    const uint32_t d = cnt >= last_cnt_ ? cnt - last_cnt_ : cnt;   // smaller: someone cleared the counter (desreg io ir1-clr)
    last_cnt_ = cnt;
    if (!d) return;
    stats.edges += d;
    if (d > 1) {   // the PL keeps only the last edge: the earlier ones of this cycle are gone
        stats.lost += d - 1;
        BeamResult r;
        r.kind = BeamKind::Lost; r.belt_mm = edge_belt_mm; r.enc = edge_enc; r.n = d - 1;
        out.push_back(r);
    }
    edge({edge_belt_mm, edge_enc});
}
void BeamMonitor::update(std::vector<Track> &tracks, double b_now, std::vector<BeamResult> &out)
{
    const float x = cfg_.x_mm, m = cfg_.match_mm;
    const bool upstream = x < cfg_.fov_x0;   // beam before the view: every part is first seen already past it
    // 1. tracks seen for the first time (once they have a position: partial blobs at the view edge have none)
    for (Track &t : tracks) {
        if (t.beam != 0 || t.n_geom == 0) continue;
        const float e = beam_lead_err(t, b_now, x);
        if (t.merged) t.beam = -2;
        else if (upstream ? e > cfg_.expire_mm : e > m) t.beam = -2;   // came into being past the beam (hand, gate paddle)
        else t.beam = 1;
    }
    // 2. edges -> the waiting track whose leading edge was nearest to the beam at that belt position
    for (auto it = pending_.begin(); it != pending_.end();) {
        const BeamEdge &e = *it;
        Track *best = nullptr;
        float best_d = 0;
        for (Track &t : tracks) {
            if (t.beam != 1 || t.n_geom == 0) continue;
            const float d = beam_lead_err(t, e.belt_mm, x);
            if (std::fabs(d) <= m && (!best || std::fabs(d) < std::fabs(best_d))) { best = &t; best_d = d; }
        }
        BeamResult r;
        r.belt_mm = e.belt_mm; r.enc = e.enc;
        if (best) {
            best->beam = 2; best->beam_b = e.belt_mm;
            stats.matched++;
            r.kind = BeamKind::Matched; r.track = best->id; r.err = best_d;
            out.push_back(r);
            it = pending_.erase(it);
            continue;
        }
        // a matched part still covers the beam: its edge chattered (teeth at the trailing edge, a bounce)
        const Track *cover = nullptr;
        for (const Track &t : tracks) {
            if (t.beam != 2) continue;
            const float d = beam_lead_err(t, e.belt_mm, x);
            if (d > -m && d <= 2 * beam_part_r(t) + m) { cover = &t; break; }
        }
        if (cover) {
            stats.repeats++;
            r.kind = BeamKind::Repeat; r.track = cover->id; r.err = beam_lead_err(*cover, e.belt_mm, x);
            out.push_back(r);
            it = pending_.erase(it);
            continue;
        }
        if (b_now - e.belt_mm > cfg_.expire_mm) {   // nobody claimed it: an object vision did not track
            stats.no_track++;
            r.kind = BeamKind::NoTrack;
            out.push_back(r);
            it = pending_.erase(it);
            continue;
        }
        ++it;   // its part may not be tracked yet (beam upstream of the view, track still without a position)
    }
    // 3. waiting tracks gone past the beam without an edge
    for (Track &t : tracks) {
        if (t.beam != 1) continue;
        if (beam_lead_err(t, b_now, x) <= cfg_.miss_mm) continue;
        t.beam = -1;
        stats.missed++;
        BeamResult r;
        r.kind = BeamKind::Missed; r.belt_mm = b_now; r.track = t.id;
        out.push_back(r);
    }
}
void BeamMonitor::finalize(const Track &t)
{
    if (t.beam != 2) return;
    const double e = beam_track_err(t, cfg_.x_mm);
    stats.n_err++;
    stats.err_sum += e;
    stats.err_sq += e * e;
    stats.err_min = std::min(stats.err_min, e);
    stats.err_max = std::max(stats.err_max, e);
}
}  // namespace des
