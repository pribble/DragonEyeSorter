// picker.cpp — see picker.hpp
#include "picker.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "arm.hpp"

namespace des {

int Picker::gate_for(int outlet) const
{
    if (!cfg_.gates_on) return -1;   // table loaded but the gates are not mounted: everything rides to the belt end
    for (size_t g = 0; g < cfg_.gates.size(); g++) if (cfg_.gates[g].serves(outlet)) return int(g);
    return -1;
}

void Picker::to_recheck(Track &t, const char *why)
{
    t.disp = Disposition::Recheck;
    t.set_why(why);
    t.gate = -2;   // the cached gate belonged to the old outlet
}

static float part_r(const Track &t) { return 0.5f * std::max(t.len, t.wid); }

// Share of a gate's sweep-out time after which the opening paddle no longer reaches a part at lateral position y
// (disc radius r, clearance included): the tip's reach len * sin(angle) has dropped below the part's near edge.
// The paddle swings back from the lane toward its own edge, so a part in the lane is only exposed at the start of
// the sweep (11-01 user: an OK part 186 mm behind an NG part counted as a conflict although the paddle had long
// left its line). 0 = never reached (closing only), 1 = reached until fully open.
static double sweep_clear_frac(const GateGeom &g, const GateRelease &R, float y, float r)
{
    const double d = double(y - g.pivot.y) * g.into() - r;   // part's near edge, lateral distance from the pivot
    const double a_closed = g.closed, a_open = g.open;
    if (d >= g.len * std::sin(a_closed)) return 0;
    if (d <= g.len * std::sin(a_open) || a_closed <= a_open) return 1;
    const double a = std::asin(std::min(1.0, d / g.len));               // paddle angle where its tip leaves that line
    const double k = std::clamp((a_closed - a) / (a_closed - a_open), 0.0, 1.0);   // travel done by then
    const double u = 0.5 - std::sin(std::asin(1 - 2 * k) / 3);          // smoothstep^-1
    const double w = std::log(0.5) / std::log(1.0 - double(R.tail));   // the sequencer's time warp u0^w
    return std::clamp(std::pow(u, 1.0 / w), 0.0, 1.0);
}

const Picker::GateAns &Picker::gate_geom_at(int gi, float y, float r) const
{
    GateAns &a = gans_[size_t(gi) & 1];
    if (!a.valid || a.gate != gi || a.y != y || a.r != r) {
        const GateGeom &g = cfg_.gates[size_t(gi)].geom;
        const float m = cfg_.gate_margin;
        a = GateAns{int8_t(gi), y, r, true, gate_reaches(g, y, m), gate_closed_range(g, y, r),
                    gate_sweep_range(g, y, r + m)};
    }
    return a;
}

bool Picker::gate_event(const Track &t, int gi, double move_mm, FlapEvent &e, const char *&why, bool strict) const
{
    const Gate &G = cfg_.gates[size_t(gi)];
    const GateAns &a = gate_geom_at(gi, t.y, part_r(t));
    if (strict && (!a.reaches || !a.closed.hit || !a.sweep.hit)) {
        why = "beyond gate reach";
        return false;
    }
    // not strict (already committed): never fails, falls back to the sector / the pivot
    const float x_meet = a.closed.hit ? a.closed.x0 : (a.sweep.hit ? a.sweep.x1 : G.geom.pivot.x),
                x_in = a.sweep.hit ? a.sweep.x0 : x_meet;
    e.belt_mm = x_meet - t.s;              // its disc meets the closed paddle
    e.close_b = (x_in - t.s) - move_mm;    // closed before the disc (+ margin) enters the swept sector
    e.rel = G.release(t.disp == Disposition::Flap ? t.dest : cfg_.recheck_outlet);
    if (e.rel.ms > 0)     // slow sweep-out: start opening once the part is pressed on the paddle
        e.release_b = e.belt_mm + e.rel.after_mm;
    else                  // hold while it slides along the paddle and off the belt
        e.release_b = e.belt_mm + (G.hold_mm > 0 ? G.hold_mm : gate_slide_mm(G.geom, t.y)) + cfg_.gate_margin;
    return true;
}

bool Picker::sweep_window(const Track &o, int g, double &b0, double &b1) const
{
    const XRange &s = gate_geom_at(g, o.y, part_r(o)).sweep;
    if (!s.hit) return false;
    b0 = s.x0 - o.s;
    b1 = s.x1 - o.s;
    // OK parts stop on the pick line: a gate downstream of it never sees them, one around it keeps seeing them
    const bool stops = o.state == TrackState::Picking || (o.state == TrackState::Decided && o.disp == Disposition::Pick);
    if (stops) {
        if (cfg_.pick_x < s.x0) return false;
        if (cfg_.pick_x <= s.x1) b1 = 1e18;
    }
    return true;
}

bool Picker::committed(int track_id) const
{
    return std::find(committed_.begin(), committed_.end(), track_id) != committed_.end();
}

const char *Picker::plan_grip(const Tracker &tr, const Track &t, double belt_mm, float &close_axis,
                              float &open_mm, float *alt_axis) const
{
    // pure function of (tracks, belt travel): between two ticks nothing moves while the belt is stopped
    if (grip_tr_ == &tr && grip_id_ == t.id && grip_rev_ == tr.rev() && grip_belt_ == belt_mm) {
        close_axis = grip_axis_;
        open_mm = grip_open_;
        if (alt_axis) *alt_axis = grip_alt_;
        return grip_why_;
    }
    float axis = 0, open = 0, alt = NAN;
    const char *why = nullptr;
    const bool round = t.wid > 0 && t.len / t.wid < cfg_.round_ratio;
    open = (round ? t.len : t.wid) + 2 * cfg_.grip_clear;
    if (open > cfg_.max_open) {
        why = "too wide";
    } else {
        // elongated part: close across its width; round part: any angle, try every 15 deg, starting at the line the
        // wrist already has at the ready pose (no wrist turn), else tangential to the arm base when tilted grasps are
        // enabled (only that axis keeps both fingertips level while leaning), else the vision axis
        const Vec2 c{t.x_at(belt_mm), t.y};
        const bool wrist = round && !std::isnan(cfg_.wrist_rel);
        auto first_clear = [&](float a0, int n, float &found) {
            for (int k = 0; k < n; k++)
                if (fingers_clear(tr, t, belt_mm, a0 + k * kPi / 12, open)) { found = wrap_axis(a0 + k * kPi / 12); return true; }
            return false;
        };
        float a0 = t.axis + kPi / 2;
        if (round)
            a0 = wrist ? arm_wrist_axis(cfg_.arm_base, cfg_.wrist_rel, cfg_.wrist_tau, c)
                       : cfg_.tangential_first ? arm_tangential_axis(cfg_.arm_base, c) : t.axis;
        why = first_clear(a0, round ? 12 : 1, axis) ? nullptr : "crowded";
        if (!why && wrist && cfg_.tangential_first) {   // fallback for points only a tilted grasp reaches
            float a;
            if (first_clear(arm_tangential_axis(cfg_.arm_base, c), 1, a)) alt = a;
        }
    }
    grip_tr_ = &tr; grip_id_ = t.id; grip_rev_ = tr.rev(); grip_belt_ = belt_mm;
    grip_why_ = why; grip_axis_ = axis; grip_open_ = open; grip_alt_ = alt;
    close_axis = axis;
    open_mm = open;
    if (alt_axis) *alt_axis = alt;
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

const Picker::Output &Picker::tick(Tracker &tr, const Input &in)
{
    Output &out = out_;
    out.belt = BeltPlan{};
    out.have_job = false;
    out.job = PickJob{};
    out.flaps.clear();
    const double B = in.belt_mm;
    if (!in.arm_clear) {   // arm down on the belt: nothing moves until it lifts
        out.belt.kind = BeltPlan::Hold;
        out.belt.stop_belt_mm = B;
    }
    // pick candidates: drop those that can no longer be picked, keep the most downstream one
    Track *target = nullptr;
    float grip_axis = 0, grip_open = 0, grip_alt = NAN;
    for (Track &t : tr.tracks()) {
        if (t.state != TrackState::Decided || t.disp != Disposition::Pick) continue;
        float x = t.x_at(B);
        if (t.y < cfg_.reach_y0 || t.y > cfg_.reach_y1) { to_recheck(t, "out of reach"); stats.out_of_reach++; continue; }
        if (x > cfg_.pick_x + cfg_.pick_tol) { to_recheck(t, "missed pick line"); stats.missed++; continue; }
        float ax, op, alt;
        if (const char *why = plan_grip(tr, t, B, ax, op, &alt)) {
            to_recheck(t, why);
            if (!strcmp(why, "too wide")) stats.too_wide++; else stats.crowded++;
            continue;
        }
        if (!target || x > target->x_at(B)) { target = &t; grip_axis = ax; grip_open = op; grip_alt = alt; }
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
                out.job.close_axis = grip_axis; out.job.alt_axis = grip_alt; out.job.open_mm = grip_open;
                out.job.part_wid = grip_open - 2 * cfg_.grip_clear;
                out.job.bin = target->dest; out.job.cls = target->cls;
                stats.jobs++;
            }
        }
    }
    // NG / recheck parts: an outlet with a side gate is served there, everything else rides to the belt end
    flap_tracks_.clear();
    const double move_mm = std::max({in.speed_mm_s, in.cruise_mm_s, 1.0}) * cfg_.gate_move_s;
    const double m = cfg_.gate_margin;
    for (Track &t : tr.tracks()) {
        if (t.state != TrackState::Decided) continue;
        if (t.disp != Disposition::Flap && t.disp != Disposition::Recheck) continue;
        FlapEvent e;
        e.track_id = t.id;
        const char *why = nullptr;
        if (t.gate == -2) {   // first tick after the decision: its gate, if that gate can still close in time
            const int g = gate_for(t.disp == Disposition::Flap ? t.dest : cfg_.recheck_outlet);
            if (g >= 0 && !gate_event(t, g, move_mm, e, why)) {
                to_recheck(t, why);
                stats.gate_unreachable++;
            } else if (g >= 0 && B >= e.close_b) {
                to_recheck(t, "gate too late");   // decided too late for the paddle to close ahead of it
                stats.gate_late++;
            }
            t.gate = int8_t(t.disp == Disposition::Flap ? g : gate_for(cfg_.recheck_outlet));
        }
        e.outlet = t.disp == Disposition::Flap ? t.dest : cfg_.recheck_outlet;
        e.gate = t.gate;
        if (e.gate >= 0 && !gate_event(t, e.gate, move_mm, e, why, !committed(t.id))) {
            to_recheck(t, why);   // its refined position is out of reach after all (not committed yet)
            stats.gate_unreachable++;
            t.gate = -1;
            e.gate = -1;
            e.outlet = cfg_.recheck_outlet;
        }
        if (e.gate < 0) e.belt_mm = cfg_.tail_x - t.s;
        out.flaps.push_back(e);
        flap_tracks_.push_back(&t);
    }
    // A gate diverts whatever reaches the closed paddle, and its moving paddle sweeps a sector of the belt. The final
    // check is made when the gate is due to start closing (the latest moment: parts behind have been decided by then):
    // every other part (OK parts on their way to the pick line included; undecided parts ride on) must stay out of
    // the sector from the start of closing until the gate could open again at the earliest (the part half way along
    // the paddle), else this part is not diverted and rides on to the recheck box. Parts for the same gate whose closed
    // windows overlap share one closing. Once committed it is never re-routed: a part that comes later makes the gate
    // open early instead (no earlier than that same minimum).
    for (size_t i = 0; i < out.flaps.size(); i++) {
        FlapEvent &e = out.flaps[i];
        Track *t = flap_tracks_[i];
        if (e.gate < 0) {
            t->flap_final = true;
            continue;
        }
        bool done = committed(e.track_id);
        if (!done && B < e.close_b) continue;   // not due yet
        const Gate &G = cfg_.gates[size_t(e.gate)];
        const size_t gi = size_t(e.gate) & 1;
        const GateRelease &R = e.rel;   // how this part's outlet is released (Gate::release)
        const bool sweep = R.ms > 0;
        const double v = std::max({in.speed_mm_s, in.cruise_mm_s, 1.0});
        // opening = the normal move, or the slow sweep-out: the gate is busy until it is fully open again
        const double open_mm = sweep ? v * R.s : move_mm;
        // earliest opening: half way along the paddle (sliding off), or as soon as it is pressed on it (sweep-out)
        const double min_release = sweep ? e.release_b : e.belt_mm + 0.5 * (e.release_b - e.belt_mm);
        bool hit = false;
        // This part needs the paddle closed while it is still sweeping an earlier part out -> never reverse a sweep
        // half way (that part would be pushed back onto the belt); this one rides on to recheck. Kept per gate, not
        // looked up from the tracks: the swept part leaves the view and its track may be gone. Closing no later than
        // that sweep starts is fine: one closing serves both (the sequencer holds it closed).
        if (!done && sweep_[gi].rel < e.close_b && e.close_b < sweep_[gi].rel + sweep_[gi].len + m)
            hit = true;
        for (const Track &o : tr.tracks()) {
            if (hit) break;
            if (o.id == e.track_id || o.state == TrackState::Done || o.n_geom == 0) continue;
            if (o.state == TrackState::Decided && o.disp == Disposition::Flap && o.gate == e.gate) {
                FlapEvent eo;
                const char *w = nullptr;
                if (gate_event(o, e.gate, move_mm, eo, w) && eo.close_b <= e.release_b && e.close_b <= eo.release_b) {
                    if (eo.rel == e.rel) continue;   // one closing serves both
                    // a different release (another drop point along the edge, another bin) cannot share one
                    // closing — the one that has to close later rides on to recheck, the earlier one is served
                    if (done || e.close_b < eo.close_b || (e.close_b == eo.close_b && e.track_id < o.id)) continue;
                    hit = true;
                    break;
                }
            }
            double b0, b1;
            if (!sweep_window(o, e.gate, b0, b1)) continue;
            // sweep-out: o only needs the part of the sweep until the paddle has swung back past o's line
            const double open_o = sweep ? v * (sweep_clear_frac(G.geom, R, o.y, part_r(o) + float(m)) * R.ms / 1000.0 +
                                               (R.s - R.ms / 1000.0))
                                        : open_mm;
            if (!done) {
                if (b1 >= e.close_b - m && b0 <= min_release + open_o + m) { hit = true; break; }
            } else if (b0 > B) {   // committed: open (and finish opening) before o enters the sector
                const double latest = b0 - open_o - m;
                if (latest < e.release_b) {
                    e.release_b = std::max(latest, min_release);
                    if (latest < min_release) stats.gate_squeezed++;   // o will meet the paddle anyway
                }
            }
        }
        // remember this gate's sweep (the committed event that releases last; its own release may move earlier)
        if (sweep && (done || !hit)) {
            Sweep &w = sweep_[gi];
            if (w.id == e.track_id || e.release_b > w.rel) w = {e.track_id, e.release_b, open_mm};
        }
        if (!done && !hit) {
            committed_.push_back(e.track_id);
            t->flap_final = true;
            if (committed_.size() > 64) committed_.erase(committed_.begin(), committed_.begin() + 32);
            continue;   // early opening for parts behind it starts with the next cycle (sequencer follows release_b)
        }
        if (!hit) continue;
        to_recheck(*t, "gate conflict");
        t->gate = -1;
        t->flap_final = true;
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
int FlapSequencer::us_at(int g, double now) const
{
    const Ramp &r = ramp_[g];
    const double T = r.dur;   // move_ms, or the gate's sweep_ms when it opens after a part
    if (T <= 0 || now >= r.t0 + T) return r.to;
    // smoothstep: soft start and stop; warped time u^warp (warp < 1) spends longer on the second half of the travel
    const double u0 = std::max(0.0, (now - r.t0) / T), u = r.warp == 1 ? u0 : std::pow(u0, r.warp),
                 k = u * u * (3 - 2 * u);
    return int(std::lround(r.from + (r.to - r.from) * k));
}

const FlapSequencer::State &FlapSequencer::update(const std::vector<FlapEvent> &events, double belt_mm, double now)
{
    const int n = int(std::min<size_t>(gates_.size(), 2));
    if (!started_) {   // position unknown: straight to open, no ramp
        for (int g = 0; g < n; g++) ramp_[g] = {gates_[g].open_us, gates_[g].open_us, -1e18, 0};
        started_ = true;
    }
    const double T = cfg_.move_ms / 1000.0, stagger = cfg_.stagger_ms / 1000.0;
    for (int g = 0; g < n; g++) {
        // closed while a committed event (belt past its close_b) is not released yet. The latched part's own event
        // may move its release earlier (another part is coming); if its event vanished (track retired / re-decided)
        // the gate keeps the release it had.
        double rel = -1e18;
        int tid = -1;
        bool seen = false;
        GateRelease how = rel_how_[g];   // how the release that keeps it closed goes
        for (const FlapEvent &e : events) {
            if (e.gate != g || belt_mm < e.close_b) continue;
            if (e.track_id == st_.track_id[g]) seen = true;
            if (belt_mm < e.release_b && e.release_b > rel) {
                rel = e.release_b;
                how = e.rel;
                if (tid < 0 || e.track_id == st_.track_id[g]) tid = e.track_id;
            }
        }
        if (st_.track_id[g] >= 0 && !seen && release_[g] > rel) {
            rel = release_[g];
            how = rel_how_[g];
            tid = st_.track_id[g];
        }
        if (belt_mm < rel) {
            if (st_.track_id[g] < 0) served++;
            st_.track_id[g] = tid;
            release_[g] = rel;
            rel_how_[g] = how;   // kept after the release: the opening ramp just below reads it
        } else {
            st_.track_id[g] = -1;
            release_[g] = -1e18;
        }
        // pulse: ramp to the wanted end; a new ramp does not start within stagger of the other gate's start
        const int want = st_.track_id[g] >= 0 ? gates_[g].closed_us : gates_[g].open_us;
        if (want != ramp_[g].to) {
            const bool other_just_started = n == 2 && now - ramp_[1 - g].t0 < stagger;
            // opening after a part released by the sweep-out: the slow swing that pushes it off (a part that slid
            // off along the closed paddle, or any other opening: the normal move)
            const GateRelease &H = rel_how_[g];
            const bool sweep_out = want == gates_[g].open_us && ramp_[g].to == gates_[g].closed_us && H.ms > 0;
            if (!other_just_started)
                ramp_[g] = {us_at(g, now), want, now, sweep_out ? H.ms / 1000.0 : T,
                            // the second half of the travel starts at u = 0.5^(1/warp) = 1 - tail
                            sweep_out ? std::log(0.5) / std::log(1.0 - double(H.tail)) : 1.0};
        }
        st_.us[g] = us_at(g, now);
        st_.moving[g] = st_.us[g] != ramp_[g].to || (now < ramp_[g].t0 + ramp_[g].dur && ramp_[g].from != ramp_[g].to);
    }
    return st_;
}

}  // namespace des
