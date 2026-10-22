// picker.cpp — see picker.hpp
#include "picker.hpp"

#include <algorithm>
#include <cstring>

#include "arm.hpp"

namespace des {

int Picker::gate_for(int outlet) const
{
    for (size_t g = 0; g < cfg_.gates.size(); g++) if (cfg_.gates[g].outlet == outlet) return int(g);
    return -1;
}

void Picker::to_recheck(Track &t, const char *why)
{
    t.disp = Disposition::Recheck;
    t.set_why(why);
    t.gate = -2;   // the cached gate belonged to the old outlet
}

const char *Picker::plan_grip(const Tracker &tr, const Track &t, double belt_mm, float &close_axis,
                              float &open_mm) const
{
    // pure function of (tracks, belt travel): between two ticks nothing moves while the belt is stopped
    if (grip_tr_ == &tr && grip_id_ == t.id && grip_rev_ == tr.rev() && grip_belt_ == belt_mm) {
        close_axis = grip_axis_;
        open_mm = grip_open_;
        return grip_why_;
    }
    float axis = 0, open = 0;
    const char *why = nullptr;
    const bool round = t.wid > 0 && t.len / t.wid < cfg_.round_ratio;
    open = (round ? t.len : t.wid) + 2 * cfg_.grip_clear;
    if (open > cfg_.max_open) {
        why = "too wide";
    } else {
        // elongated part: close across its width; round part: any angle, try every 15 deg (starting tangential to the
        // arm base when tilted grasps are enabled: only that axis keeps both fingertips level while leaning)
        const Vec2 c{t.x_at(belt_mm), t.y};
        float cand[12];
        int n = 0;
        if (round) {
            const float a0 = cfg_.tangential_first ? arm_tangential_axis(cfg_.arm_base, c) : t.axis;
            for (int k = 0; k < 12; k++) cand[n++] = a0 + k * kPi / 12;
        } else {
            cand[n++] = t.axis + kPi / 2;
        }
        why = "crowded";
        for (int i = 0; i < n; i++)
            if (fingers_clear(tr, t, belt_mm, cand[i], open)) { axis = wrap_axis(cand[i]); why = nullptr; break; }
    }
    grip_tr_ = &tr; grip_id_ = t.id; grip_rev_ = tr.rev(); grip_belt_ = belt_mm;
    grip_why_ = why; grip_axis_ = axis; grip_open_ = open;
    close_axis = axis;
    open_mm = open;
    return why;
}

bool Picker::fingers_clear(const Tracker &tr, const Track &t, double belt_mm, float axis, float open_mm) const
{
    const Vec2 c{t.x_at(belt_mm), t.y}, u{std::cos(axis), std::sin(axis)};
    const float off = open_mm / 2 + cfg_.finger_t / 2;
    const ORect f1{c + u * off, cfg_.finger_t / 2, cfg_.finger_w / 2, axis};
    const ORect f2{c - u * off, cfg_.finger_t / 2, cfg_.finger_w / 2, axis};
    for (const Track &o : tr.tracks()) {
        if (o.id == t.id || o.state == TrackState::Done || o.n_geom == 0) continue;
        const ORect fo = o.footprint(belt_mm, cfg_.neighbor_gap);
        if (overlap(f1, fo) || overlap(f2, fo)) return false;
    }
    return true;
}

bool Picker::flying_ok(const Tracker &tr, const Track &t, double belt_mm, float &open_mm) const
{
    if (!(t.wid > 0 && t.len / t.wid < cfg_.round_ratio)) return false;   // round parts only: any closing axis
    open_mm = cfg_.max_open;                                              // widest: most lateral tolerance
    if (t.len + 2 * cfg_.grip_clear > open_mm) return false;
    const float r = norm(Vec2{cfg_.pick_x, t.y} - cfg_.arm_base);
    if (r < cfg_.flying_r0 || r > cfg_.flying_r1) return false;
    for (const Track &o : tr.tracks()) {   // the gripper stays down for a while: nobody may ride in after it
        if (o.id == t.id || o.state == TrackState::Done || o.n_geom == 0) continue;
        if (std::fabs(o.s - t.s) < cfg_.flying_gap) return false;
    }
    return fingers_clear(tr, t, belt_mm, cfg_.flying_axis, open_mm);
}

Picker::Output Picker::tick(Tracker &tr, const Input &in)
{
    Output out;
    const double B = in.belt_mm;
    if (!in.arm_clear) {   // arm down on the belt: nothing moves until it lifts
        out.belt.kind = BeltPlan::Hold;
        out.belt.stop_belt_mm = B;
    }
    // pick candidates: drop those that can no longer be picked, keep the most downstream one
    Track *target = nullptr;
    float grip_axis = 0, grip_open = 0;
    for (Track &t : tr.tracks()) {
        if (t.state != TrackState::Decided || t.disp != Disposition::Pick) continue;
        float x = t.x_at(B);
        if (t.y < cfg_.reach_y0 || t.y > cfg_.reach_y1) { to_recheck(t, "out of reach"); stats.out_of_reach++; continue; }
        if (x > cfg_.pick_x + cfg_.pick_tol) { to_recheck(t, "missed pick line"); stats.missed++; continue; }
        float ax, op;
        if (const char *why = plan_grip(tr, t, B, ax, op)) {
            to_recheck(t, why);
            if (!strcmp(why, "too wide")) stats.too_wide++; else stats.crowded++;
            continue;
        }
        if (!target || x > target->x_at(B)) { target = &t; grip_axis = ax; grip_open = op; }
    }
    // intercept: an early qualifying part keeps the belt running; dispatched once it is need .. need + window away
    // (need = the arm's way down at the current speed). Too late or arm busy: stop and pick as below.
    if (target && cfg_.flying && out.belt.kind != BeltPlan::Hold && committed_id_ != target->id) {
        float fop;
        if (flying_ok(tr, *target, B, fop)) {
            const double arrive = cfg_.pick_x - target->s, dist = arrive - B;
            const double need = std::max(in.speed_mm_s, 5.0) * cfg_.flying_ready_s;
            if (dist > need + cfg_.flying_window) {
                target = nullptr;   // still early: let it come
            } else if (dist >= need && in.arm_idle) {
                target->state = TrackState::Picking;
                out.have_job = true;
                out.job.track_id = target->id; out.job.p = {cfg_.pick_x, target->y};
                out.job.close_axis = wrap_axis(cfg_.flying_axis); out.job.open_mm = fop;
                out.job.part_wid = target->len; out.job.bin = target->dest; out.job.cls = target->cls;
                out.job.flying = true; out.job.arrive_belt_mm = arrive;
                stats.jobs++;
                stats.flying++;
                target = nullptr;   // the belt keeps running
            }
        }
    }
    if (target && out.belt.kind != BeltPlan::Hold) {
        double stop_b = cfg_.pick_x - target->s;
        double dist = stop_b - B;
        if (committed_id_ != target->id && !in.belt_stopped && dist < in.brake_mm + cfg_.brake_margin) {
            to_recheck(*target, "cannot stop in time");   // decided too late: let it ride to the flap
            stats.missed++;
            target = nullptr;
        } else {
            committed_id_ = target->id;
            out.belt.kind = BeltPlan::StopAt;
            out.belt.stop_belt_mm = stop_b;
            out.belt.track_id = target->id;
            if (in.belt_stopped && std::fabs(dist) <= cfg_.pick_tol && in.arm_idle) {
                target->state = TrackState::Picking;
                out.have_job = true;
                out.job.track_id = target->id; out.job.p = {target->x_at(B), target->y};
                out.job.close_axis = grip_axis; out.job.open_mm = grip_open;
                out.job.part_wid = grip_open - 2 * cfg_.grip_clear;
                out.job.bin = target->dest; out.job.cls = target->cls;
                stats.jobs++;
            }
        }
    }
    // NG / recheck parts: an outlet with a side gate is served there, everything else rides to the belt end
    out.flaps.clear();
    flap_tracks_.clear();
    for (Track &t : tr.tracks()) {
        if (t.state != TrackState::Decided) continue;
        if (t.disp != Disposition::Flap && t.disp != Disposition::Recheck) continue;
        const int outlet = t.disp == Disposition::Flap ? t.dest : cfg_.recheck_outlet;
        if (t.gate == -2) t.gate = int8_t(gate_for(outlet));   // resolved once, then read from the track
        FlapEvent e;
        e.track_id = t.id; e.outlet = outlet; e.gate = t.gate;
        e.belt_mm = (e.gate >= 0 ? cfg_.gates[e.gate].x : cfg_.tail_x) - t.s;
        out.flaps.push_back(e);
        flap_tracks_.push_back(&t);
    }
    // a closed gate diverts whatever reaches it: if another part not meant for that gate would meet it while it is
    // closed (or still sweeping in), this part is not diverted and rides on to the recheck box. Decided only before
    // the gate starts closing; once committed it stays committed.
    for (size_t i = 0; i < out.flaps.size(); i++) {
        FlapEvent &e = out.flaps[i];
        if (e.gate < 0 || B >= e.belt_mm - cfg_.gate_prepare) continue;
        const float gx = cfg_.gates[e.gate].x;
        const double lo = e.belt_mm - cfg_.gate_prepare - cfg_.gate_gap, hi = e.belt_mm + cfg_.gate_clear;
        bool hit = false;
        for (const Track &o : tr.tracks()) {
            if (o.id == e.track_id || o.state == TrackState::Done || o.state == TrackState::Picking) continue;
            if (o.state == TrackState::Decided && o.disp == Disposition::Pick) continue;   // stops at the pick line
            if (o.state == TrackState::Decided && o.disp == Disposition::Flap && o.gate == e.gate) continue;
            const double b = gx - o.s;
            if (b >= lo && b <= hi) { hit = true; break; }
        }
        if (!hit) continue;
        Track *t = flap_tracks_[i];
        to_recheck(*t, "gate conflict");
        e.outlet = cfg_.recheck_outlet;
        e.gate = -1;
        e.belt_mm = cfg_.tail_x - t->s;
        stats.flap_conflicts++;
    }
    if (out.flaps.size() > 1)
        std::sort(out.flaps.begin(), out.flaps.end(), [](const FlapEvent &a, const FlapEvent &b) { return a.belt_mm < b.belt_mm; });
    return out;
}

void Picker::job_done(Tracker &tr, int track_id, bool ok)
{
    Track *t = tr.find(track_id);
    if (!t) return;
    if (ok) {
        t->state = TrackState::Done;
        t->set_why("picked");
        stats.picked++;
        grip_id_ = -1;   // a Done track is skipped by fingers_clear: the cached grip may no longer hold
    } else {
        t->state = TrackState::Decided;
        to_recheck(*t, "pick failed");
        stats.failed++;
    }
}

// ---------------------------------------------------------------- side diverter gates (see picker.hpp)
const FlapSequencer::State &FlapSequencer::update(const std::vector<FlapEvent> &events, double belt_mm)
{
    const int n = int(std::min<size_t>(cfg_.gates.size(), 2));
    // closed for a part: stays closed until that part is clear_mm past the contact point, even if its event vanished
    // meanwhile (the track was retired, re-decided, ...)
    for (int g = 0; g < n; g++)
        if (st_.track_id[g] >= 0 && belt_mm >= hold_until_[g]) st_.track_id[g] = -1;
    // events are ordered by belt_mm: the first one inside a gate's window is the earliest one, one pass over them all
    for (const FlapEvent &e : events) {
        if (e.gate < 0 || e.gate >= n || st_.track_id[e.gate] >= 0) continue;
        if (belt_mm >= e.belt_mm - cfg_.prepare_mm && belt_mm < e.belt_mm + cfg_.clear_mm) {
            st_.track_id[e.gate] = e.track_id;
            hold_until_[e.gate] = e.belt_mm + cfg_.clear_mm;
            served++;
        }
    }
    for (int g = 0; g < n; g++) st_.us[g] = st_.track_id[g] >= 0 ? cfg_.gates[g].closed_us : cfg_.gates[g].open_us;
    return st_;
}

}  // namespace des
