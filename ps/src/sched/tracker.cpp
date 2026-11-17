// tracker.cpp — see tracker.hpp
#include "tracker.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace des {

const char *to_str(TrackState s)
{
    static const char *n[] = {"new", "tracking", "decided", "picking", "done"};
    return n[size_t(s)];
}
const char *to_str(Disposition d)
{
    static const char *n[] = {"-", "pick", "flap", "recheck"};
    return n[size_t(d)];
}

bool Tracker::vote(int track_id, const float *probs, int n)
{
    Track *t = find(track_id);
    if (!t || (t->state != TrackState::New && t->state != TrackState::Tracking)) return false;
    const bool fits = n <= Track::kMaxScore;
    const int k = fits ? n : Track::kMaxScore;
    for (int c = 0; c < k; c++) t->score[c] += probs[c];
    if (k > t->n_score) t->n_score = uint8_t(k);
    t->votes++;
    return fits;
}

const Track *Tracker::find(int id) const
{
    for (const Track &t : tracks_) if (t.id == id) return &t;
    return nullptr;
}
Track *Tracker::find(int id) { return const_cast<Track *>(static_cast<const Tracker &>(*this).find(id)); }

void Tracker::fuse(Track &t, const Detection &d, double belt_mm, uint32_t frame)
{
    t.hits++;
    t.misses = 0;
    t.last_frame = frame;
    t.merged = t.merged || d.merged;
    if (d.cls >= 0 && d.cls < Track::kMaxScore) {
        t.score[d.cls] += d.conf;
        if (d.cls >= t.n_score) t.n_score = uint8_t(d.cls + 1);
        t.votes++;
    }
    if (d.partial) return;   // presence only: a cut blob has a shifted center and wrong size
    const int n = ++t.n_geom;
    const float sd = float(d.p.x - belt_mm);
    t.s += (sd - t.s) / n;
    t.y += (d.p.y - t.y) / n;
    t.len += (d.len - t.len) / n;
    t.wid += (d.wid - t.wid) / n;
    t.c2 += std::cos(2 * d.axis);
    t.s2 += std::sin(2 * d.axis);
    t.axis = wrap_axis(circ_mean2(t.s2, t.c2));
    t.ext_x = 0.5f * (t.len * std::fabs(std::cos(t.axis)) + t.wid * std::fabs(std::sin(t.axis)));
    ++rev_;   // geometry changed: cached consumers (Picker::plan_grip) are stale
}

// Size / spec check of a part the class vote would pick (SizeRule): spec (tooth count), sizes, then measured tooth
// defects the class missed.
void Tracker::apply_size(Track &t) const
{
    const SizeRule &r = cfg_.size;
    const PartMeasure &m = t.meas;
    if (!m.valid) {
        if (r.require) { t.disp = Disposition::Recheck; t.dest = 0; t.set_why("not measured"); }
        return;
    }
    if (r.unjudged_recheck && !m.teeth_judged && r.unjudged_pick_conf > 0 && t.cls_conf >= r.unjudged_pick_conf) {
        t.set_why("off-axis: model only");   // a confident class: picked on it alone (nothing else can be checked)
        return;
    }
    if (r.unjudged_recheck && !m.teeth_judged) {   // off-axis: neither the teeth nor the OD can be trusted
        t.disp = Disposition::Recheck;
        t.dest = 0;
        t.set_why("off-axis: teeth not judged");
        return;
    }
    char buf[48] = "";
    if (m.kind == 'g') {
        if (m.teeth_judged && m.teeth != r.gear_teeth) snprintf(buf, sizeof buf, "spec: %d teeth", m.teeth);
        else if (std::fabs(m.size - r.gear_od) > r.gear_od_tol) snprintf(buf, sizeof buf, "OD %.2f", m.size);
        else if (m.bore > 0 && std::fabs(m.bore - r.gear_bore) > r.gear_bore_tol)
            snprintf(buf, sizeof buf, "bore %.2f", m.bore);
        else if (m.ecc > r.gear_max_ecc) snprintf(buf, sizeof buf, "ecc %.2f", m.ecc);
    }
    if (buf[0]) {
        t.disp = Disposition::Flap;
        t.dest = r.ng_outlet;
        t.set_why(buf);
    } else if (r.defect_recheck && m.teeth_judged && (m.short_teeth || m.missing_teeth || m.flash_gaps)) {
        t.disp = Disposition::Recheck;
        t.dest = 0;
        snprintf(buf, sizeof buf, "measured: short %d missing %d flash %d", m.short_teeth, m.missing_teeth, m.flash_gaps);
        t.set_why(buf);
    }
}

void Tracker::decide(Track &t)
{
    t.state = TrackState::Decided;
    int best = -1;
    float best_score = 0;
    for (int c = 0; c < int(t.n_score); c++)
        if (t.score[c] > best_score) { best_score = t.score[c]; best = c; }
    t.cls = best;
    t.cls_conf = best >= 0 ? best_score / std::max(1, t.votes) : 0.f;   // mean confidence of the winning class
    if (t.merged) { t.disp = Disposition::Recheck; t.set_why("merged blob"); }
    else if (best < 0) {
        if (cfg_.pick_unclassified) { t.disp = Disposition::Pick; t.dest = cfg_.unclassified_bin; t.set_why("no class (bring-up)"); }
        else { t.disp = Disposition::Recheck; t.set_why("no class"); }
    } else if (t.cls_conf < cfg_.min_conf) { t.disp = Disposition::Recheck; t.set_why("low confidence"); }
    else if (size_t(best) >= cfg_.rules.size()) { t.disp = Disposition::Recheck; t.set_why("class without rule"); }
    else { t.disp = cfg_.rules[best].disp; t.dest = cfg_.rules[best].dest; t.set_why(cfg_.rules[best].label.c_str()); }
    if (cfg_.size.enabled && t.disp == Disposition::Pick) apply_size(t);
    if (t.disp == Disposition::Pick) stats.decided_pick++;
    else if (t.disp == Disposition::Flap) stats.decided_flap++;
    else stats.decided_recheck++;
}

void Tracker::update(const FrameObs &obs)
{
    stats.frames++;
    stats.dets += obs.dets.size();
    const double B = obs.belt_mm;
    const size_t nt = tracks_.size(), nd = obs.dets.size();
    // candidate pairs inside the gates, cheapest first (few parts per frame: brute force). Insertion sort into the
    // member scratch instead of std::sort: stable, so on equal cost the earlier track / detection wins.
    pairs_.clear();
    for (size_t ti = 0; ti < nt; ti++) {
        const Track &t = tracks_[ti];
        if (t.state == TrackState::Picking || t.state == TrackState::Done) continue;
        for (size_t di = 0; di < nd; di++) {
            const Detection &d = obs.dets[di];
            const float k = d.partial ? 2.f : 1.f;   // cut blobs: center shifted, use a looser gate
            const float ds = float(d.p.x - B) - t.s, dy = d.p.y - t.y;
            if (std::fabs(ds) > k * cfg_.gate_s || std::fabs(dy) > k * cfg_.gate_y) continue;
            const float cs = ds / cfg_.gate_s, cy = dy / cfg_.gate_y;
            pairs_.push_back({cs * cs + cy * cy, uint16_t(ti), uint16_t(di)});
        }
    }
    for (size_t i = 1; i < pairs_.size(); i++) {
        const Pair p = pairs_[i];
        size_t j = i;
        while (j > 0 && p.cost < pairs_[j - 1].cost) { pairs_[j] = pairs_[j - 1]; --j; }
        pairs_[j] = p;
    }
    t_used_.assign(nt, 0);
    d_used_.assign(nd, 0);
    for (const Pair &p : pairs_) {
        if (t_used_[p.t] || d_used_[p.d]) continue;
        t_used_[p.t] = d_used_[p.d] = 1;
        fuse(tracks_[p.t], obs.dets[p.d], B, obs.frame);
    }
    // missed tracks: only count frames where the whole part should have been visible. Decided tracks are leaving the
    // view (or waiting at the pick line): the belt position carries them, misses no longer matter. (Counting by the
    // center alone dropped 50 mm racks near the FOV exit as "lost": HIL test 10-10.)
    // Upstream of the decision line (right half of the screen) every frame counts while the center is in the picture:
    // an object taken away near the entry edge, or a big blob (a gear held by hand) while the belt stands (line
    // stopped, waiting for the arm), must not leave its box behind (11-18 user). Downstream — decided parts, NG parts
    // the gate pushes off — stays as it was: decided tracks are never dropped there, their records are safe.
    for (size_t ti = 0; ti < nt; ti++) {
        Track &t = tracks_[ti];
        if (t_used_[ti] || (t.state != TrackState::New && t.state != TrackState::Tracking)) continue;
        // right of the line the entry side counts from the centre on; the exit side keeps the full-footprint rule
        const float tx = t.x_at(B);
        const bool upstream_in = tx > cfg_.fov_x0 && tx < cfg_.decide_x && tx + t.ext_x < cfg_.fov_x1 - cfg_.edge_margin;
        if (!fully_in_view(t, B) && !upstream_in) continue;
        t.misses++;
        if (t.state == TrackState::New && t.misses > cfg_.max_miss_new) {
            t.state = TrackState::Done; t.set_why("noise"); stats.noise++; ++rev_;
        } else if (t.state != TrackState::New && t.misses > cfg_.max_miss) {
            // lost before the decision line (decided tracks are never counted as missing): not a part — a gear held
            // by hand in view, a part taken off the belt. Like noise: no record, no count, no report (11-18 user:
            // only a part that was recognised and crossed the decision line with the belt counts)
            t.state = TrackState::Done; t.disp = Disposition::None; t.set_why("lost in view"); stats.lost++; ++rev_;
        }
    }
    // new tracks from unmatched, complete blobs (constructed in place: no per-track copy)
    for (size_t di = 0; di < nd; di++) {
        const Detection &d = obs.dets[di];
        if (d_used_[di] || d.partial) continue;
        if (cfg_.spawn_before_decide && d.p.x >= cfg_.decide_x) continue;   // clutter, not a part coming down the belt
        tracks_.emplace_back();
        Track &t = tracks_.back();
        t.id = next_id_++;
        t.first_frame = obs.frame;
        fuse(t, d, B, obs.frame);
        stats.created++;
    }
    for (Track &t : tracks_) {   // promotions
        float x = t.x_at(B);
        if (t.state == TrackState::New && t.hits >= cfg_.min_hits) t.state = TrackState::Tracking;
        if (t.state == TrackState::Tracking && x >= cfg_.decide_x) decide(t);
        if (t.state == TrackState::New && x >= cfg_.fov_x1 - cfg_.edge_margin) {
            // left the view before it was confirmed: a real but barely seen part goes to recheck
            if (t.hits >= 2) {
                t.state = TrackState::Decided; t.disp = Disposition::Recheck; t.set_why("too few frames");
                stats.decided_recheck++;
            }
            else { t.state = TrackState::Done; t.set_why("noise"); stats.noise++; }
            ++rev_;
        }
    }
}

void Tracker::retire(double belt_mm)
{
    const size_t before = tracks_.size();
    tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(),
                                 [&](const Track &t) {
                                     return t.state == TrackState::Done || t.x_at(belt_mm) > cfg_.tail_x + 20.f;
                                 }),
                  tracks_.end());
    if (tracks_.size() != before) ++rev_;
}

}  // namespace des
