// tracker.cpp — see tracker.hpp
#include "tracker.hpp"

#include <algorithm>

namespace des {

const char *to_str(TrackState s)
{
    switch (s) {
    case TrackState::New: return "new";
    case TrackState::Tracking: return "tracking";
    case TrackState::Decided: return "decided";
    case TrackState::Picking: return "picking";
    case TrackState::Done: return "done";
    }
    return "?";
}

const char *to_str(Disposition d)
{
    switch (d) {
    case Disposition::None: return "-";
    case Disposition::Pick: return "pick";
    case Disposition::Flap: return "flap";
    case Disposition::Recheck: return "recheck";
    }
    return "?";
}

Track *Tracker::find(int id)
{
    for (auto &t : tracks_)
        if (t.id == id) return &t;
    return nullptr;
}

void Tracker::fuse(Track &t, const Detection &d, double belt_mm, uint32_t frame)
{
    t.hits++;
    t.misses = 0;
    t.last_frame = frame;
    t.merged = t.merged || d.merged;
    if (d.cls >= 0) {
        if (size_t(d.cls) >= t.score.size()) t.score.resize(d.cls + 1, 0.f);
        t.score[d.cls] += d.conf;
    }
    if (d.partial) return;   // presence only: a cut blob has a shifted center and wrong size
    int n = ++t.n_geom;
    float sd = float(d.p.x - belt_mm);
    t.s += (sd - t.s) / n;
    t.y += (d.p.y - t.y) / n;
    t.len += (d.len - t.len) / n;
    t.wid += (d.wid - t.wid) / n;
    t.c2 += std::cos(2 * d.axis);
    t.s2 += std::sin(2 * d.axis);
    t.axis = wrap_axis(0.5f * float(std::atan2(t.s2, t.c2)));
}

void Tracker::decide(Track &t)
{
    t.state = TrackState::Decided;
    int best = -1;
    float best_score = 0;
    for (size_t c = 0; c < t.score.size(); c++)
        if (t.score[c] > best_score) { best_score = t.score[c]; best = int(c); }
    t.cls = best;
    t.cls_conf = best >= 0 ? best_score / t.hits : 0.f;

    if (t.merged) {
        t.disp = Disposition::Recheck; t.why = "merged blob";
    } else if (best < 0) {
        if (cfg_.pick_unclassified) { t.disp = Disposition::Pick; t.dest = cfg_.unclassified_bin; t.why = "no class (bring-up)"; }
        else { t.disp = Disposition::Recheck; t.why = "no class"; }
    } else if (t.cls_conf < cfg_.min_conf) {
        t.disp = Disposition::Recheck; t.why = "low confidence";
    } else if (size_t(best) >= cfg_.rules.size()) {
        t.disp = Disposition::Recheck; t.why = "class without rule";
    } else {
        t.disp = cfg_.rules[best].disp;
        t.dest = cfg_.rules[best].dest;
        t.why = cfg_.rules[best].label;
    }
    if (t.disp == Disposition::Pick) stats.decided_pick++;
    else if (t.disp == Disposition::Flap) stats.decided_flap++;
    else stats.decided_recheck++;
}

void Tracker::update(const FrameObs &obs)
{
    stats.frames++;
    stats.dets += obs.dets.size();
    const double B = obs.belt_mm;

    // candidate pairs inside the gates, cheapest first (few parts per frame: brute force is fine)
    struct Pair { float cost; size_t t, d; };
    std::vector<Pair> pairs;
    for (size_t ti = 0; ti < tracks_.size(); ti++) {
        const Track &t = tracks_[ti];
        if (t.state == TrackState::Picking || t.state == TrackState::Done) continue;
        for (size_t di = 0; di < obs.dets.size(); di++) {
            const Detection &d = obs.dets[di];
            float k = d.partial ? 2.f : 1.f;   // cut blobs: center shifted, use a looser gate
            float ds = float(d.p.x - B) - t.s, dy = d.p.y - t.y;
            if (std::fabs(ds) > k * cfg_.gate_s || std::fabs(dy) > k * cfg_.gate_y) continue;
            float cs = ds / cfg_.gate_s, cy = dy / cfg_.gate_y;
            pairs.push_back({cs * cs + cy * cy, ti, di});
        }
    }
    std::sort(pairs.begin(), pairs.end(), [](const Pair &a, const Pair &b) { return a.cost < b.cost; });

    std::vector<char> t_used(tracks_.size(), 0), d_used(obs.dets.size(), 0);
    for (const Pair &p : pairs) {
        if (t_used[p.t] || d_used[p.d]) continue;
        t_used[p.t] = d_used[p.d] = 1;
        fuse(tracks_[p.t], obs.dets[p.d], B, obs.frame);
    }

    // missed tracks: only count frames where the part should have been visible
    for (size_t ti = 0; ti < tracks_.size(); ti++) {
        Track &t = tracks_[ti];
        if (t_used[ti] || t.state == TrackState::Picking || t.state == TrackState::Done) continue;
        if (!in_view(t.x_at(B))) continue;
        t.misses++;
        if (t.state == TrackState::New && t.misses > cfg_.max_miss_new) {
            t.state = TrackState::Done; t.why = "noise"; stats.noise++;
        } else if (t.state != TrackState::New && t.misses > cfg_.max_miss) {
            t.state = TrackState::Done; t.disp = Disposition::Recheck; t.why = "lost in view"; stats.lost++;
        }
    }

    // new tracks from unmatched, complete blobs
    for (size_t di = 0; di < obs.dets.size(); di++) {
        const Detection &d = obs.dets[di];
        if (d_used[di] || d.partial) continue;
        Track t;
        t.id = next_id_++;
        t.score.assign(cfg_.rules.size(), 0.f);
        t.first_frame = obs.frame;
        fuse(t, d, B, obs.frame);
        tracks_.push_back(t);
        stats.created++;
    }

    // promotions
    for (Track &t : tracks_) {
        float x = t.x_at(B);
        if (t.state == TrackState::New && t.hits >= cfg_.min_hits) t.state = TrackState::Tracking;
        if (t.state == TrackState::Tracking && x >= cfg_.decide_x) decide(t);
        if (t.state == TrackState::New && x >= cfg_.fov_x1 - cfg_.edge_margin) {
            // left the view before it was confirmed: a real but barely seen part goes to recheck
            if (t.hits >= 2) {
                t.state = TrackState::Decided; t.disp = Disposition::Recheck; t.why = "too few frames";
                stats.decided_recheck++;
            }
            else { t.state = TrackState::Done; t.why = "noise"; stats.noise++; }
        }
    }
}

void Tracker::retire(double belt_mm)
{
    tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(),
                                 [&](const Track &t) {
                                     return t.state == TrackState::Done || t.x_at(belt_mm) > cfg_.tail_x + 20.f;
                                 }),
                  tracks_.end());
}

}  // namespace des
