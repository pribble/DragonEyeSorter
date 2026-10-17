// picker.cpp — see picker.hpp
#include "picker.hpp"

#include <algorithm>
#include <cstring>

namespace des {

void Picker::to_recheck(Track &t, const char *why)
{
    t.disp = Disposition::Recheck;
    t.why = why;
}

const char *Picker::plan_grip(const Tracker &tr, const Track &t, double belt_mm, float &close_axis,
                              float &open_mm) const
{
    bool round = t.wid > 0 && t.len / t.wid < cfg_.round_ratio;
    open_mm = (round ? t.len : t.wid) + 2 * cfg_.grip_clear;
    if (open_mm > cfg_.max_open) return "too wide";

    // elongated part: close across its width; round part: any angle, try every 15 deg (starting tangential to the
    // arm base when tilted grasps are enabled: only that axis keeps both fingertips level while leaning)
    Vec2 c{t.x_at(belt_mm), t.y};
    float cand[12];
    int n = 0;
    if (round) {
        const float a0 = cfg_.tangential_first ? std::atan2(c.y - cfg_.arm_base.y, c.x - cfg_.arm_base.x) + kPi / 2
                                               : t.axis;
        for (int k = 0; k < 12; k++) cand[n++] = a0 + k * kPi / 12;
    } else {
        cand[n++] = t.axis + kPi / 2;
    }

    float off = open_mm / 2 + cfg_.finger_t / 2;
    for (int i = 0; i < n; i++) {
        Vec2 u{std::cos(cand[i]), std::sin(cand[i])};
        ORect f1{c + u * off, cfg_.finger_t / 2, cfg_.finger_w / 2, cand[i]};
        ORect f2{c - u * off, cfg_.finger_t / 2, cfg_.finger_w / 2, cand[i]};
        bool hit = false;
        for (const Track &o : tr.tracks()) {
            if (o.id == t.id || o.state == TrackState::Done || o.n_geom == 0) continue;
            ORect fo = o.footprint(belt_mm, cfg_.neighbor_gap);
            if (overlap(f1, fo) || overlap(f2, fo)) { hit = true; break; }
        }
        if (!hit) {
            close_axis = wrap_axis(cand[i]);
            return nullptr;
        }
    }
    return "crowded";
}

Picker::Output Picker::tick(Tracker &tr, const Input &in)
{
    Output out;
    const double B = in.belt_mm;

    // arm down on the belt: nothing moves until it lifts
    if (!in.arm_clear) {
        out.belt.kind = BeltPlan::Hold;
        out.belt.stop_belt_mm = B;
    }

    // pick candidates: drop those that can no longer be picked, keep the most downstream one
    Track *target = nullptr;
    float grip_axis = 0, grip_open = 0;
    for (Track &t : tr.tracks()) {
        if (t.state != TrackState::Decided || t.disp != Disposition::Pick) continue;
        float x = t.x_at(B);
        if (t.y < cfg_.reach_y0 || t.y > cfg_.reach_y1) {
            to_recheck(t, "out of reach"); stats.out_of_reach++; continue;
        }
        if (x > cfg_.pick_x + cfg_.pick_tol) {
            to_recheck(t, "missed pick line"); stats.missed++; continue;
        }
        float ax, op;
        if (const char *why = plan_grip(tr, t, B, ax, op)) {
            to_recheck(t, why);
            if (!strcmp(why, "too wide")) stats.too_wide++; else stats.crowded++;
            continue;
        }
        if (!target || x > target->x_at(B)) { target = &t; grip_axis = ax; grip_open = op; }
    }

    if (target && out.belt.kind != BeltPlan::Hold) {
        double stop_b = cfg_.pick_x - target->s;
        double dist = stop_b - B;
        if (committed_id_ != target->id && !in.belt_stopped && dist < in.brake_mm + cfg_.brake_margin) {
            // decided too late for the belt to stop on the line: let it ride to the flap
            to_recheck(*target, "cannot stop in time");
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
                out.job.track_id = target->id;
                out.job.p = {target->x_at(B), target->y};
                out.job.close_axis = grip_axis;
                out.job.open_mm = grip_open;
                out.job.part_wid = grip_open - 2 * cfg_.grip_clear;
                out.job.bin = target->dest;
                out.job.cls = target->cls;
                stats.jobs++;
            }
        }
    }

    // tail flap: every decided part that is not going to be picked
    for (const Track &t : tr.tracks()) {
        if (t.state != TrackState::Decided) continue;
        if (t.disp != Disposition::Flap && t.disp != Disposition::Recheck) continue;
        FlapEvent e;
        e.track_id = t.id;
        e.outlet = t.disp == Disposition::Flap ? t.dest : cfg_.recheck_outlet;
        e.belt_mm = cfg_.tail_x - cfg_.flap_lead - t.s;
        out.flaps.push_back(e);
    }
    std::sort(out.flaps.begin(), out.flaps.end(),
              [](const FlapEvent &a, const FlapEvent &b) { return a.belt_mm < b.belt_mm; });
    for (size_t i = 1; i < out.flaps.size(); i++) {
        FlapEvent &a = out.flaps[i - 1], &b = out.flaps[i];
        if (a.outlet != b.outlet && b.belt_mm - a.belt_mm < cfg_.flap_conflict) {
            // the flap cannot switch between two close parts: both go to recheck
            for (FlapEvent *e : {&a, &b}) {
                e->outlet = cfg_.recheck_outlet;
                Track *t = tr.find(e->track_id);
                if (t && t->disp != Disposition::Recheck) to_recheck(*t, "flap conflict");   // keep earlier reason
            }
            stats.flap_conflicts++;
        }
    }
    return out;
}

void Picker::job_done(Tracker &tr, int track_id, bool ok)
{
    Track *t = tr.find(track_id);
    if (!t) return;
    if (ok) {
        t->state = TrackState::Done;
        t->why = "picked";
        stats.picked++;
    } else {
        t->state = TrackState::Decided;
        to_recheck(*t, "pick failed");
        stats.failed++;
    }
}

}  // namespace des
