// test_scheduler: geometry, tracker, picker and arm kinematics without hardware. Runs on the board or any
// Linux host:  ./test_scheduler   (exit code = number of failed checks)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>

#include "arm.hpp"
#include "geom.hpp"
#include "picker.hpp"
#include "sim.hpp"
#include "test_check.hpp"
#include "tracker.hpp"

using namespace des;

static void test_geom()
{
    ORect a{{0, 0}, 10, 5, 0}, b{{18, 0}, 10, 5, 0}, c{{21, 0}, 10, 5, 0};
    CHECK(overlap(a, b), "aligned overlap");
    CHECK(!overlap(a, c), "aligned separated");
    ORect touch{{20, 0}, 10, 5, 0};   // left edge exactly on a's right edge: touching counts as overlap
    CHECK(overlap(a, touch), "touching counts as overlap");
    ORect r{{16.5f, 9.f}, 5, 2, kPi / 4};   // diagonal stick next to a's corner, not touching
    CHECK(!overlap(a, r), "rotated near miss");
    ORect r2{{12.f, 6.f}, 5, 2, kPi / 4};
    CHECK(overlap(a, r2), "rotated overlap");
    CHECK(std::fabs(wrap_axis(kPi * 0.75f) - (-kPi / 4)) < 1e-5f, "wrap_axis");
    Homography h;
    h.h = {0.2, 0, -10, 0, 0.2, 5, 0, 0, 1};
    Vec2 m = h.map({100, 50});
    CHECK(std::fabs(m.x - 10) < 1e-4f && std::fabs(m.y - 15) < 1e-4f, "homography affine");
    Homography p;   // perspective
    p.h = {0.16, 0.004, -12, -0.003, 0.155, -80, 1e-5, -2e-5, 1};
    Homography pi = p.inverse();
    float worst = 0;
    for (float u = 0; u <= 1920; u += 480)
        for (float v = 0; v <= 1080; v += 270) {
            Vec2 back = pi.map(p.map({u, v}));
            worst = std::fmax(worst, std::hypot(back.x - u, back.y - v));
        }
    CHECK(worst < 0.01f, "homography inverse round trip %.4f px", worst);
}

static void test_tracker()
{
    std::mt19937 rng(1);
    Tracker tr(tracker_cfg());
    std::vector<SimPart> parts = {
        {-40, 10, 30, 30, 0.3f, 0},       // gear_ok -> pick bin 0
        {-140, -30, 60, 12, 0.7f, 1},     // long thin part, gear_short -> flap outlet 1
        {-230, 40, 25, 25, 0.f, -1},      // no class -> recheck
    };
    double B = 0;
    uint32_t f = 0;
    for (; B < 560; B += 1.0, f++) {
        tr.update(observe(parts, B, f, rng));
        if (f == 100) {   // a one-frame speck of dirt
            FrameObs o;
            o.frame = f; o.belt_mm = B;
            Detection d; d.p = {150, -100}; d.len = d.wid = 3;
            o.dets.push_back(d);
            tr.update(o);
        }
    }
    CHECK(tr.stats.created == 4, "created %llu tracks (3 parts + 1 speck)", (unsigned long long)tr.stats.created);
    CHECK(tr.stats.noise == 1, "noise %llu", (unsigned long long)tr.stats.noise);
    int seen = 0;
    for (const Track &t : tr.tracks()) {
        if (t.state == TrackState::Done) continue;
        seen++;
        const SimPart *p = nullptr;
        for (auto &q : parts) if (std::fabs(q.s - t.s) < 5) p = &q;
        CHECK(p != nullptr, "track %d s=%.2f matches no part", t.id, t.s);
        if (!p) continue;
        CHECK(std::fabs(t.s - p->s) < 0.5f && std::fabs(t.y - p->y) < 0.5f, "track %d pos err s %.2f y %.2f",
              t.id, t.s - p->s, t.y - p->y);
        CHECK(t.state == TrackState::Decided, "track %d state %s", t.id, to_str(t.state));
        if (p->cls == 0) CHECK(t.disp == Disposition::Pick && t.dest == 0, "gear_ok -> %s", to_str(t.disp));
        if (p->cls == 1) {
            CHECK(t.disp == Disposition::Flap && t.dest == 1, "gear_short -> %s/%d", to_str(t.disp), t.dest);
            CHECK(std::fabs(wrap_axis(t.axis - p->axis)) < 0.02f, "long part axis err %.3f", t.axis - p->axis);
        }
        if (p->cls < 0) CHECK(t.disp == Disposition::Recheck, "unclassified -> %s", to_str(t.disp));
    }
    CHECK(seen == 3, "live tracks %d", seen);
}

// SizeRule: the measurement (filled while the part crosses the view, like measure/track_measure) overrides "pick" only
static PartMeasure meas_gear(int teeth, float od, float ecc, int shorts, bool judged, float bore = 6.02f)
{
    PartMeasure m;
    m.valid = true; m.kind = 'g'; m.teeth_judged = judged; m.teeth = teeth; m.size = od; m.bore = bore;
    m.ecc = ecc; m.short_teeth = shorts;
    return m;
}

static PartMeasure meas_rack(float len)
{
    PartMeasure m;
    m.valid = true; m.kind = 'r'; m.teeth_judged = true; m.size = len; m.teeth = 15; m.pitch = 3.14f;
    return m;
}

struct SizeCase { int cls; PartMeasure m; Disposition disp; int dest; const char *what; };

// runs the parts through the tracker (measurement injected mid-view) and checks every disposition
static void check_size_cases(const TrackerConfig &cfg, const std::vector<SizeCase> &cases, const char *tag)
{
    std::mt19937 rng(7);
    Tracker tr(cfg);
    std::vector<SimPart> parts;
    for (size_t i = 0; i < cases.size(); i++) parts.push_back({-40.f - 70.f * i, 0, 30, 30, 0.f, cases[i].cls});
    uint32_t f = 0;
    for (double B = 0; B < 40 + 70 * cases.size() + 300; B += 1.0, f++) {
        tr.update(observe(parts, B, f, rng));
        for (Track &t : tr.tracks()) {
            float x = t.x_at(B);
            if (t.meas.valid || x < 100 || x > 200) continue;
            for (size_t i = 0; i < parts.size(); i++)
                if (std::fabs(parts[i].s - t.s) < 5) t.meas = cases[i].m;
        }
    }
    for (size_t i = 0; i < cases.size(); i++) {
        const Track *tk = nullptr;
        for (const Track &t : tr.tracks())
            if (std::fabs(parts[i].s - t.s) < 5) tk = &t;
        CHECK(tk && tk->state == TrackState::Decided, "%s %s: no decided track", tag, cases[i].what);
        if (!tk) continue;
        CHECK(tk->disp == cases[i].disp && tk->dest == cases[i].dest, "%s %s -> %s/%d (%s), want %s/%d", tag,
              cases[i].what, to_str(tk->disp), tk->dest, tk->why, to_str(cases[i].disp), cases[i].dest);
    }
}

static void test_size_rule()
{
    TrackerConfig cfg = tracker_cfg();
    cfg.size.enabled = true;
    check_size_cases(cfg, {
        {0, meas_gear(30, 32.01f, 0.05f, 0, true), Disposition::Pick, 0, "good gear"},
        {0, meas_gear(28, 30.0f, 0.05f, 0, true), Disposition::Flap, 1, "28-tooth gear (spec mix-up)"},
        {0, meas_gear(30, 32.5f, 0.05f, 0, true), Disposition::Flap, 1, "OD out of tolerance"},
        {0, meas_gear(30, 32.0f, 0.5f, 0, true), Disposition::Flap, 1, "eccentric bore"},
        {0, meas_gear(30, 32.0f, 0.05f, 1, true), Disposition::Recheck, 0, "NPU OK, measured short tooth"},
        {0, meas_gear(30, 32.0f, 0.05f, 0, false), Disposition::Recheck, 0, "NPU OK, teeth not judged (far off-axis)"},
        {0, meas_gear(30, 32.5f, 0.05f, 0, false), Disposition::Recheck, 0, "off-axis OD is not trusted for scrap"},
        {0, meas_gear(28, 30.0f, 0.05f, 0, false), Disposition::Recheck, 0, "off-axis tooth count is not a spec NG"},
        {0, meas_rack(48.0f), Disposition::Flap, 1, "48 mm rack"},
        {0, meas_rack(50.1f), Disposition::Pick, 0, "50 mm rack"},
        // racks are cut at whole teeth (pitch 3.125 mm, 16 teeth = 50.0): hand-cut 16-tooth parts scatter a few
        // tenths, a tooth more or less is 3.1 mm -> tolerance 1.0 (2026-10-17)
        {0, meas_rack(50.8f), Disposition::Pick, 0, "16-tooth rack, hand-cut +0.8"},
        {0, meas_rack(46.875f), Disposition::Flap, 1, "15-tooth rack"},
        {0, meas_rack(53.125f), Disposition::Flap, 1, "17-tooth rack"},
        {2, meas_gear(30, 32.0f, 0.05f, 0, true), Disposition::Flap, 2, "NPU flash stays NPU flash"},
        {0, PartMeasure{}, Disposition::Pick, 0, "not measured, require off"},
    }, "size rule:");
    cfg.size.unjudged_recheck = false;   // old behaviour: unjudged parts keep the class decision
    check_size_cases(cfg, {
        {0, meas_gear(30, 32.0f, 0.05f, 0, false), Disposition::Pick, 0, "teeth not judged, unjudged_recheck off"},
        {2, meas_gear(30, 32.0f, 0.05f, 0, false), Disposition::Flap, 2, "NPU flash, teeth not judged"},
    }, "size rule (unjudged_recheck off):");
}

// Recipes (recipes.json): changeover to the 28-tooth gear swaps which gears are good; the bore-8 recipe rejects bore 6
static void test_recipes()
{
    std::vector<Recipe> list = {Recipe::from_rule(SizeRule{}, 1, "G30 bore 6"), Recipe::from_rule(SizeRule{}, 2, "G28"),
                                Recipe::from_rule(SizeRule{}, 3, "G30 bore 8")};
    list[1].gear_teeth = 28; list[1].gear_od = 30;
    list[2].gear_bore = 8;
    CHECK(find_recipe(list, 2) == &list[1] && !find_recipe(list, 7), "find_recipe");
    TrackerConfig cfg = tracker_cfg();
    cfg.size.enabled = true;
    cfg.size.ng_outlet = 1;
    find_recipe(list, 2)->apply(cfg.size);
    CHECK(cfg.size.gear_teeth == 28 && cfg.size.gear_od == 30 && cfg.size.enabled && cfg.size.ng_outlet == 1,
          "apply sets the spec only (teeth %d od %.1f)", cfg.size.gear_teeth, cfg.size.gear_od);
    check_size_cases(cfg, {
        {0, meas_gear(28, 30.02f, 0.05f, 0, true), Disposition::Pick, 0, "28-tooth gear is good now"},
        {0, meas_gear(30, 32.0f, 0.05f, 0, true), Disposition::Flap, 1, "30-tooth gear is the mix-up now"},
        {0, meas_rack(50.0f), Disposition::Pick, 0, "rack unchanged"},
    }, "recipe 2:");
    find_recipe(list, 3)->apply(cfg.size);
    check_size_cases(cfg, {
        {0, meas_gear(30, 32.0f, 0.05f, 0, true, 6.02f), Disposition::Flap, 1, "bore 6 under the bore-8 recipe"},
        {0, meas_gear(30, 32.0f, 0.05f, 0, true, 8.05f), Disposition::Pick, 0, "bore 8 is good"},
    }, "recipe 3:");
    Recipe back = Recipe::from_rule(cfg.size, 9, "x");
    CHECK(back.gear_bore == 8 && back.gear_teeth == 30 && back.id == 9, "from_rule round trip");
}

// NPU crop classifier: blob detections carry no class, a few soft votes per track decide (mean probability)
static void test_votes()
{
    std::mt19937 rng(3);
    Tracker tr(tracker_cfg());
    std::vector<SimPart> parts = {{-40, 0, 30, 30, 0.f, -1}, {-120, 10, 30, 30, 0.f, -1}};
    float flash[4] = {0.04f, 0.04f, 0.72f, 0.04f};   // gear_flash
    float unsure[4] = {0.4f, 0.f, 0.35f, 0.25f};
    int voted[2] = {0, 0};
    uint32_t f = 0;
    for (double B = 0; B < 450; B += 1.0, f++) {
        tr.update(observe(parts, B, f, rng));
        for (Track &t : tr.tracks()) {
            float x = t.x_at(B);
            for (int i = 0; i < 2; i++)
                if (std::fabs(t.s - parts[i].s) < 5 && x > 100 && x < 200 && voted[i] < 3 && int(B) % 20 == 0)
                    voted[i] += tr.vote(t.id, i == 0 ? flash : unsure, 4);
        }
    }
    CHECK(voted[0] == 3 && voted[1] == 3, "votes accepted %d %d", voted[0], voted[1]);
    for (const Track &t : tr.tracks()) {
        if (std::fabs(t.s - parts[0].s) < 5)
            CHECK(t.disp == Disposition::Flap && t.dest == 2 && std::fabs(t.cls_conf - 0.72f) < 1e-3f,
                  "voted flash -> %s/%d conf %.3f", to_str(t.disp), t.dest, t.cls_conf);
        if (std::fabs(t.s - parts[1].s) < 5)
            CHECK(t.disp == Disposition::Recheck && !strcmp(t.why, "low confidence"), "unsure votes -> %s (%s)",
                  to_str(t.disp), t.why);
    }
    CHECK(!tr.vote(9999, flash, 4), "vote for a missing track is refused");
    // the per-track score buffer is fixed (no heap): classes past Track::kMaxScore are dropped, the call says so
    Tracker t3(tracker_cfg());
    t3.update(observe({{-40, 0, 30, 30, 0.f, -1}}, 100, 0, rng));   // part at table x 60: inside the view
    const Track *nt = t3.find(1);
    float wide[20] = {};
    CHECK(nt && !t3.vote(nt->id, wide, 20), "vote with more than 16 classes is refused");
    CHECK(nt && nt->n_score == Track::kMaxScore, "score buffer clamped to 16 classes (%d)", nt ? int(nt->n_score) : -1);
}

// belt speed adaptation (sched/picker.hpp): slow down so the next OK part arrives when the arm is ready
static void test_speed_adapt()
{
    SpeedAdaptConfig c;
    c.enabled = true;
    c.min_mm_s = 15;
    CHECK(adapt_speed(c, 50, -1, 5) == 50, "no pick part ahead: base speed");
    CHECK(adapt_speed(c, 50, 100, 0) == 50, "arm idle: base speed");
    CHECK(std::fabs(adapt_speed(c, 50, 100, 4) - 25) < 1e-9, "100 mm, arm ready in 4 s: 25 mm/s");
    CHECK(adapt_speed(c, 50, 100, 1) == 50, "arm ready before the part could arrive: base (never faster)");
    CHECK(adapt_speed(c, 50, 30, 10) == 15, "would need 3 mm/s: floor at min_mm_s");
    CHECK(adapt_speed(c, 10, 30, 10) == 10, "base below the floor: base");
    c.enabled = false;
    CHECK(adapt_speed(c, 50, 100, 4) == 50, "disabled: base");
}

// clutter: a blob first seen past the decision line (a hand, the gate paddle) gets no track; a part coming in from the
// upstream edge is tracked through it. With spawn_before_decide off, the clutter is tracked (old behaviour).
static void test_spawn_before_decide()
{
    for (int on = 1; on >= 0; on--) {
        std::mt19937 rng(5);
        TrackerConfig tc = tracker_cfg();   // decide_x 270, view 0..300
        tc.spawn_before_decide = on != 0;
        Tracker tr(tc);
        // part riding in from upstream; clutter sitting at table x 285 (moves with nothing: s changes with B)
        for (uint32_t f = 0; f < 320; f++) {
            const double B = f;
            std::vector<SimPart> parts = {{-30, 0, 30, 30, 0.f, 0}, {float(285 - B), 40, 25, 25, 0.f, -1}};
            tr.update(observe(parts, B, f, rng));
        }
        int part = 0, clutter = 0;
        for (const Track &t : tr.tracks()) {
            if (std::fabs(t.y) < 5) part++;
            if (std::fabs(t.y - 40) < 5) clutter++;
        }
        if (on)
            CHECK(part == 1 && clutter == 0 && tr.stats.created == 1, "spawn before decide: part %d clutter %d created %llu",
                  part, clutter, (unsigned long long)tr.stats.created);
        else
            CHECK(part == 1 && tr.stats.created > 1, "spawn anywhere: clutter tracked (created %llu)",
                  (unsigned long long)tr.stats.created);
    }
}

// gate geometry of the 10-22 layout (10_项目文档/侧边导流挡板方案.md §2c): gate 1 pivot on the far edge opposite the arm,
// 230 mm paddle closed at 55 deg / open at -20 deg; checked against the hand-computed numbers of that section
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

static GateGeom gate_geom(float px, float len, float closed_deg, float open_deg)
{
    GateGeom g;
    g.pivot = {px, -150};
    g.len = len;
    g.closed = deg2rad(closed_deg);
    g.open = deg2rad(open_deg);
    return g;
}

static void test_gate_geometry()
{
    const GateGeom g = gate_geom(438, 230, 55, -20);
    CHECK(near(g.tip(g.closed).x, 438 - 230 * std::cos(deg2rad(55)), 1e-3) && near(g.tip(g.closed).y, 38.4f, 0.1),
          "closed tip (%.1f, %.1f)", g.tip(g.closed).x, g.tip(g.closed).y);
    XRange c = gate_closed_range(g, 10, 0);   // a point at y 10 meets the closed paddle at x = 438 - 160 / tan 55
    CHECK(c.hit && near(c.x0, 438 - 160 / std::tan(deg2rad(55)), 0.05), "closed paddle at y 10: x %.2f", c.x0);
    c = gate_closed_range(g, 10, 16);         // a 32 mm disc touches it 16 / sin 55 earlier
    CHECK(c.hit && near(c.x0, 438 - 160 / std::tan(deg2rad(55)) - 16 / std::sin(deg2rad(55)), 0.05),
          "32 mm disc meets it at x %.2f", c.x0);
    XRange s = gate_sweep_range(g, 10, 0);    // the tip first reaches y 10 at sin a = 160 / 230
    const float a10 = std::asin(160.f / 230.f);
    CHECK(s.hit && near(s.x0, 438 - 230 * std::cos(a10), 0.05) && near(s.x1, c.x0 + 16 / std::sin(deg2rad(55)), 0.05),
          "sweep at y 10: x %.2f .. %.2f", s.x0, s.x1);
    CHECK(!gate_closed_range(g, 60, 10).hit && !gate_reaches(g, 60, 0) && gate_reaches(g, 25, 10),
          "the paddle reaches y 25 (margin 10) but not y 60");
    CHECK(near(gate_slide_mm(g, 10), (160 / std::sin(deg2rad(55))) / std::cos(deg2rad(55)), 0.1),
          "slide estimate %.1f mm", gate_slide_mm(g, 10));
    // a disc behind the pivot (downstream, beside the edge) is not in the sector
    CHECK(gate_sweep_dist(g, {470, -100}) > 0 && gate_sweep_dist(g, {350, -100}) == 0.f, "sector membership");
}

// sequencer: events with close / release belt travel -> ramped pulse widths (smoothstep over move_ms), stagger
static void test_flaps()
{
    FlapConfig fc;   // move 300 ms, stagger 120 ms
    fc.gates.resize(2);
    fc.gates[0].outlet = 1; fc.gates[0].open_us = 1000; fc.gates[0].closed_us = 1900;
    fc.gates[1].outlet = 2; fc.gates[1].open_us = 1100; fc.gates[1].closed_us = 1800;
    FlapSequencer fs(fc);
    std::vector<FlapEvent> ev;
    FlapEvent e1;   // part 1 -> gate 0: close from belt 100, release at 400
    e1.track_id = 1; e1.outlet = 1; e1.gate = 0; e1.belt_mm = 200; e1.close_b = 100; e1.release_b = 400;
    FlapEvent e2;   // part 2 -> gate 1: close from belt 100 too (same moment: the stagger delays it)
    e2.track_id = 2; e2.outlet = 2; e2.gate = 1; e2.belt_mm = 260; e2.close_b = 100; e2.release_b = 500;
    ev = {e1, e2};
    FlapSequencer::State st = fs.update(ev, 0, 10.0);
    CHECK(st.us[0] == 1000 && st.us[1] == 1100 && !st.moving[0] && st.track_id[0] == -1, "start: both open at once");
    st = fs.update(ev, 99, 10.1);
    CHECK(st.us[0] == 1000 && st.track_id[0] == -1, "not before close_b");
    st = fs.update(ev, 100, 11.0);   // gate 0 starts its ramp at t 11.0; gate 1 must wait 120 ms
    CHECK(st.track_id[0] == 1 && st.track_id[1] == 2 && st.us[0] == 1000 && st.us[1] == 1100 && st.moving[0],
          "both latched, ramps begin at the start value (%d %d)", st.us[0], st.us[1]);
    st = fs.update(ev, 101, 11.06);
    CHECK(st.us[1] == 1100, "gate 1 waits for the stagger (%d)", st.us[1]);
    st = fs.update(ev, 102, 11.15);  // gate 0 half way: smoothstep(0.5) = 0.5; gate 1 started at 11.15
    CHECK(near(st.us[0], 1450, 1) && st.us[1] == 1100, "half way %d, gate 1 just started %d", st.us[0], st.us[1]);
    st = fs.update(ev, 103, 11.20);  // gate 0 at u = 2/3: 1000 + 900 * (3 u^2 - 2 u^3)
    CHECK(near(st.us[0], 1000 + 900 * (3 * 4.0 / 9 - 2 * 8.0 / 27), 1), "smoothstep at 2/3: %d", st.us[0]);
    st = fs.update(ev, 104, 11.30);
    CHECK(st.us[0] == 1900 && !st.moving[0] && st.us[1] > 1100 && st.us[1] < 1800, "gate 0 closed, gate 1 moving");
    st = fs.update(ev, 105, 11.45);
    CHECK(st.us[1] == 1800, "gate 1 closed 300 ms after its delayed start (%d)", st.us[1]);
    ev[0].release_b = 300;           // another part is coming: part 1's gate opens earlier
    st = fs.update(ev, 299, 12.0);
    CHECK(st.track_id[0] == 1 && st.us[0] == 1900, "still closed before the new release");
    st = fs.update(ev, 300, 12.1);
    CHECK(st.track_id[0] == -1 && st.us[0] == 1900 && st.moving[0], "early release: opening ramp starts");
    st = fs.update(ev, 301, 12.4);
    CHECK(st.us[0] == 1000, "open again (%d)", st.us[0]);
    ev.erase(ev.begin() + 1);        // part 2's track vanished: gate 1 keeps its release
    st = fs.update(ev, 450, 13.0);
    CHECK(st.track_id[1] == 2 && st.us[1] == 1800, "vanished event: held until its release");
    st = fs.update(ev, 500, 13.1);
    CHECK(st.track_id[1] == -1, "released at 500");
    CHECK(fs.served == 2, "served %llu", (unsigned long long)fs.served);
}

// intercept ("flying") picks: belt at 50 mm/s (1 mm per tick), arm base 150 mm beside the belt centre at the pick line
struct FlyRun {
    bool job = false, flying = false, stopped_for = false, belt_held = false;
    PickJob j{};
    double dispatch_B = 0;
};
static FlyRun run_flying(const std::vector<SimPart> &parts, double arm_busy_until)
{
    std::mt19937 rng(11);
    Tracker tr(tracker_cfg());
    PickerConfig pc;
    pc.pick_x = 400; pc.tail_x = 600; pc.reach_y0 = -80; pc.reach_y1 = 80;
    pc.arm_base = {400, -150};
    pc.flying = true;
    pc.flying_r0 = 50; pc.flying_r1 = 150;
    pc.flying_ready_s = 1.0f; pc.flying_window = 40; pc.flying_gap = 60;
    Picker pk(pc);
    FlyRun r;
    double B = 0;
    bool stopped = false;
    for (uint32_t f = 0; f < 900 && B < 700; f++) {
        tr.update(observe(parts, B, f, rng));
        Picker::Input in;
        in.belt_mm = B; in.brake_mm = 3; in.belt_stopped = stopped; in.speed_mm_s = stopped ? 0 : 50;
        in.arm_idle = !r.job && B >= arm_busy_until;
        in.arm_clear = true;   // intercept: the gripper being down never holds the belt
        Picker::Output out = pk.tick(tr, in);
        if (out.have_job && !r.job) {
            r.job = true;
            r.j = out.job;
            r.flying = out.job.flying;
            r.dispatch_B = B;
            if (!out.job.flying) break;   // stop-and-pick path taken: enough for these checks
        }
        if (out.belt.kind == BeltPlan::StopAt && !r.job) r.stopped_for = true;
        if (out.belt.kind == BeltPlan::Hold) r.belt_held = true;
        if (r.job && r.flying && B >= r.j.arrive_belt_mm + 5) {
            pk.job_done(tr, r.j.track_id, true);
            break;
        }
        if (out.belt.kind == BeltPlan::StopAt && B + 1.0 >= out.belt.stop_belt_mm) {
            B = out.belt.stop_belt_mm;
            stopped = true;
        } else {
            B += 1.0;
            stopped = false;
        }
        tr.retire(B);
    }
    return r;
}

static void test_picker_flying()
{
    // within the vertical reach (pick point 130 mm from the base axis), alone: intercepted, the belt never stops
    FlyRun a = run_flying({{-30, -20, 32, 32, 0.f, 0}}, 0);
    CHECK(a.job && a.flying && !a.stopped_for && !a.belt_held, "intercept: job %d flying %d stopped %d held %d", a.job,
          a.flying, a.stopped_for, a.belt_held);
    if (a.job && a.flying) {
        const double dist = a.j.arrive_belt_mm - a.dispatch_B;
        CHECK(dist >= 50 - 1e-6 && dist <= 90 + 1e-6, "dispatched %.1f mm ahead (need 50 + window 40)", dist);
        CHECK(std::fabs(a.j.p.x - 400) < 1e-3 && std::fabs(a.j.p.y + 20) < 2 && std::fabs(a.j.open_mm - 60) < 1e-3 &&
                  std::fabs(wrap_axis(a.j.close_axis - kPi / 2)) < 1e-4,
              "intercept job: at (%.1f, %.1f), open %.0f, closing %.2f rad", a.j.p.x, a.j.p.y, a.j.open_mm,
              a.j.close_axis);
        CHECK(std::fabs(a.j.arrive_belt_mm - (400 - (-30))) < 2, "arrives at B %.1f (expected ~430)", a.j.arrive_belt_mm);
    }
    // out of the vertical reach (190 mm from the base axis): stop and pick as before
    FlyRun b = run_flying({{-30, 40, 32, 32, 0.f, 0}}, 0);
    CHECK(b.job && !b.flying && b.stopped_for, "far part: stop-and-pick (job %d flying %d stopped %d)", b.job, b.flying,
          b.stopped_for);
    // a second part 40 mm behind would ride under the open fingers: no intercept
    FlyRun c = run_flying({{-30, -20, 32, 32, 0.f, 0}, {-70, 20, 32, 32, 0.f, 1}}, 0);
    CHECK(c.job && !c.flying, "neighbour within the gap: stop-and-pick (flying %d)", c.flying);
    // arm busy until the part is closer than the arm's way down: falls back to stopping on the line
    FlyRun d = run_flying({{-30, -20, 32, 32, 0.f, 0}}, 395);
    CHECK(d.job && !d.flying && d.stopped_for, "arm busy too long: stop-and-pick (flying %d stopped %d)", d.flying,
          d.stopped_for);
}

// gate decisions on a simulated belt (1 mm per tick, planned at 50 mm/s). Test layout scaled to the 0..300 mm view:
// gate A (outlet 1) upstream of the pick line 470, gate B (outlet 2) downstream of it; tracks kept to x 900.
struct GateRun {
    Picker::Output out;
    PickerConfig pc;
    Picker::Stats stats;
    FlapEvent ng;                      // the NG part's (parts[0]) event at the end
    std::string why;
    bool final = false;                // its gate decision is final (committed or re-routed)
};
static GateRun run_gates(const std::vector<SimPart> &parts, double until_b, float decide_x = 270)
{
    std::mt19937 rng(11);
    TrackerConfig tc = tracker_cfg();
    tc.tail_x = 900;
    tc.decide_x = decide_x;
    Tracker tr(tc);
    GateRun r;
    PickerConfig &pc = r.pc;
    pc.pick_x = 470; pc.tail_x = 900;
    pc.gates = {{gate_geom(520, 230, 55, -20), 1, 0}, {gate_geom(700, 210, 65, 25), 2, 0}};
    Picker pk(pc);
    for (uint32_t f = 0; f < 3000; f++) {
        const double B = f;
        if (B > until_b) break;
        tr.update(observe(parts, B, f, rng));
        Picker::Input in;
        in.belt_mm = B; in.brake_mm = 2; in.belt_stopped = false; in.cruise_mm_s = 50;
        r.out = pk.tick(tr, in);
        tr.retire(B);
    }
    r.stats = pk.stats;
    for (const FlapEvent &e : r.out.flaps) {
        const Track *t = tr.find(e.track_id);
        if (t && std::fabs(t->s - parts[0].s) < 8) { r.ng = e; r.why = t->why; r.final = t->flap_final; }
    }
    return r;
}

static void test_gate_events()
{
    // a lone NG part (gear_short -> outlet 1): gate A, closed before it enters the sweep, released after its slide
    {
        GateRun r = run_gates({{-20, 0, 32, 32, 0.f, 1}}, 400);
        const GateGeom &g = r.pc.gates[0].geom;
        const float meet = gate_closed_range(g, 0, 16).x0, in = gate_sweep_range(g, 0, 26).x0;
        CHECK(r.ng.gate == 0 && r.ng.outlet == 1, "lone NG: gate %d outlet %d (%s)", r.ng.gate, r.ng.outlet, r.why.c_str());
        CHECK(near(r.ng.belt_mm, meet + 20, 1.5) && near(r.ng.close_b, in + 20 - 50 * r.pc.gate_move_s, 1.5),
              "meets at belt %.1f (%.1f), closes from %.1f (%.1f)", r.ng.belt_mm, meet + 20, r.ng.close_b,
              in + 20 - 50 * r.pc.gate_move_s);
        CHECK(near(r.ng.release_b - r.ng.belt_mm, gate_slide_mm(g, 0) + 10, 2), "hold %.1f mm",
              r.ng.release_b - r.ng.belt_mm);
        CHECK(r.final && r.stats.flap_conflicts == 0 && r.stats.gate_late == 0, "committed, no conflict");
    }
    // no gates: the same part rides to the belt end
    {
        std::mt19937 rng(7);
        Tracker tr(tracker_cfg());
        PickerConfig pc;
        pc.pick_x = 400; pc.tail_x = 800;
        Picker pk(pc);
        const std::vector<SimPart> parts = {{-30, 5, 28, 28, 0.f, 1}};
        Picker::Output out;
        for (uint32_t f = 0; f < 400 && out.flaps.empty(); f++) {
            tr.update(observe(parts, f, f, rng));
            Picker::Input in;
            in.belt_mm = f; in.brake_mm = 2; in.belt_stopped = false;
            out = pk.tick(tr, in);
        }
        CHECK(out.flaps.size() == 1 && out.flaps[0].gate == -1 && out.flaps[0].outlet == 1,
              "no gates: belt end (%zu events)", out.flaps.size());
    }
    // an OK part 70 mm behind would cross gate A's sector on its way to the pick line: the NG part goes to recheck
    {
        GateRun r = run_gates({{-20, 0, 32, 32, 0.f, 1}, {-90, 5, 32, 32, 0.f, 0}}, 400);
        CHECK(r.ng.gate == -1 && r.ng.outlet == 0 && r.why == "gate conflict" && r.stats.flap_conflicts >= 1,
              "OK part behind: NG -> recheck (gate %d, %s)", r.ng.gate, r.why.c_str());
    }
    // the same OK part behind does not matter for gate B (downstream of the pick line, where OK parts stop)
    {
        GateRun r = run_gates({{-20, 0, 32, 32, 0.f, 2}, {-90, 5, 32, 32, 0.f, 0}}, 540);
        CHECK(r.ng.gate == 1 && r.final && r.stats.flap_conflicts == 0,
              "gate B ignores parts stopping upstream (gate %d final %d, %s)", r.ng.gate, r.final, r.why.c_str());
    }
    // two NG parts for gate A 40 mm apart: one closing serves both
    {
        GateRun r = run_gates({{-20, 0, 32, 32, 0.f, 1}, {-60, 0, 32, 32, 0.f, 3}}, 400, 200);
        int on_a = 0;
        for (const FlapEvent &e : r.out.flaps) on_a += e.gate == 0;
        CHECK(on_a == 2 && r.stats.flap_conflicts == 0, "two parts for gate A share a closing (%d on gate A)", on_a);
    }
    // decided too late (decide_x past the latest closing point) -> recheck
    {
        GateRun r = run_gates({{-20, 0, 32, 32, 0.f, 1}}, 360, 330);
        CHECK(r.ng.gate == -1 && r.why == "gate too late" && r.stats.gate_late == 1, "late decision: %s",
              r.why.c_str());
    }
    // a part beside the paddle's reach (y 60 > closed tip y 38) -> recheck
    {
        GateRun r = run_gates({{-20, 60, 32, 32, 0.f, 1}}, 300);
        CHECK(r.ng.gate == -1 && r.why == "beyond gate reach" && r.stats.gate_unreachable == 1, "out of reach: %s",
              r.why.c_str());
    }
    // committed, then a part shows up 330 mm behind (not in view when the gate committed): it opens early, not later
    // than that part's way into the sweep allows, and not before the NG part met the paddle
    {
        GateRun r = run_gates({{-20, 0, 32, 32, 0.f, 1}, {-350, 0, 32, 32, 0.f, 3}}, 420);
        const GateGeom &g = r.pc.gates[0].geom;
        const double move = 50 * r.pc.gate_move_s;
        const double latest = (gate_sweep_range(g, 0, 26).x0 + 350) - move - 10;
        const double meet = gate_closed_range(g, 0, 16).x0 + 20, full = meet + gate_slide_mm(g, 0) + 10;
        const double min_rel = meet + 0.5 * (full - meet);
        CHECK(r.ng.gate == 0 && r.ng.release_b < full - 50 && near(r.ng.release_b, std::max(latest, min_rel), 2),
              "early release %.1f (latest %.1f, minimum %.1f, full hold %.1f)", r.ng.release_b, latest, min_rel, full);
    }
}

static void test_picker()
{
    std::mt19937 rng(2);
    Tracker tr(tracker_cfg());
    PickerConfig pc;
    pc.pick_x = 400; pc.tail_x = 600; pc.reach_y0 = -80; pc.reach_y1 = 80;
    pc.gates = {{gate_geom(560, 210, 65, 25), 1, 0}, {gate_geom(660, 210, 65, 25), 2, 0}};   // scrap, rework
    Picker pk(pc);
    std::vector<SimPart> parts = {
        {-30, 5, 30, 30, 0.f, 0},          // gear_ok
        {-150, 0, 28, 28, 0.f, 1},         // gear_short -> outlet 1
        {-165, 30, 28, 28, 0.f, 2},        // gear_flash -> outlet 2, 15 mm behind: flap conflict
    };
    double B = 0;
    uint32_t f = 0;
    bool got_job = false, held = false, stopped_ok = false, stopped = false;
    PickJob job{};
    int phase = 0;   // 0 arm ready, 1 arm down on the belt, 2 arm lifted and placing
    for (int it = 0; it < 3000 && B < 900; it++, f++) {
        tr.update(observe(parts, B, f, rng));
        Picker::Input in;
        in.belt_mm = B;
        in.brake_mm = 2;
        in.belt_stopped = stopped;
        in.arm_idle = phase == 0;
        in.arm_clear = phase != 1;
        auto out = pk.tick(tr, in);
        if (phase == 1) {
            held = out.belt.kind == BeltPlan::Hold;
            pk.job_done(tr, job.track_id, true);
            phase = 2;
            continue;
        }
        if (phase == 2) phase = 0;
        if (out.have_job) {
            got_job = true;
            job = out.job;
            stopped_ok = std::fabs(job.p.x - pc.pick_x) <= pc.pick_tol;
            phase = 1;
            continue;
        }
        // belt model: 1 mm per tick, stops exactly where told
        if (out.belt.kind == BeltPlan::StopAt && B + 1.0 >= out.belt.stop_belt_mm) {
            B = out.belt.stop_belt_mm;
            stopped = true;
        } else if (out.belt.kind != BeltPlan::Hold) {
            B += 1.0;
            stopped = false;
        }
        tr.retire(B);
    }
    CHECK(got_job, "no pick job");
    CHECK(stopped_ok, "part not on pick line: x=%.2f", job.p.x);
    CHECK(held, "belt not held while the arm is down");
    CHECK(pk.stats.picked == 1, "picked %llu", (unsigned long long)pk.stats.picked);
    CHECK(pk.stats.flap_conflicts >= 1, "flap conflict not detected");
    CHECK(job.open_mm > 30 && job.open_mm < 45, "gear opening %.1f", job.open_mm);

    // crowded: an OK part lying 22 mm beside another part cannot be gripped across its width
    Tracker t2(tracker_cfg());
    std::vector<SimPart> crowd = {{-40, 0, 60, 12, 0.f, 0}, {-40, 22, 60, 12, 0.f, 1}};
    for (double b = 0; b < 320; b += 1.0) t2.update(observe(crowd, b, uint32_t(b), rng));
    Picker::Input in;
    in.belt_mm = 320; in.brake_mm = 2; in.belt_stopped = false;
    pk.tick(t2, in);
    const Track *ok = nullptr;
    for (const Track &t : t2.tracks()) if (std::fabs(t.y) < 5) ok = &t;
    CHECK(ok && ok->disp == Disposition::Recheck && !strcmp(ok->why, "crowded"), "crowded part -> %s (%s)",
          ok ? to_str(ok->disp) : "none", ok ? ok->why : "");
}

static ArmConfig arm_cfg()
{
    ArmConfig c;
    for (int i = 0; i < 6; i++) { c.joint[i].id = uint8_t(i + 1); c.joint[i].min_deg = -180; c.joint[i].max_deg = 180; }
    c.bins = {{{90, 40, -100, -30, 0}}, {{120, 40, -100, -30, 0}}};
    return c;
}

// servo counts of the ready pose: picks start gripper-down (plan_pick refuses anything else)
static std::array<uint16_t, 6> ready_counts(const ArmConfig &a)
{
    std::array<uint16_t, 6> k{};
    JointAngles j;
    j.q = a.home_deg;
    arm_to_counts(a, j, 30, k);
    return k;
}

// arm_ik -> arm_fk of a grasp point: CHECKs the solve and the round-trip error (tag names the case), so the call
// sites only add their own convention checks.
static bool check_ik_fk(const ArmConfig &c, Vec3 p, float close_axis, const char *tag, JointAngles &j, float tol = 0.02f,
                        float tilt = 0)
{
    const char *w = arm_ik(c, p, close_axis, j, tilt);
    CHECK(!w, "%s: %s", tag, w ? w : "ok");
    if (w) return false;
    const Vec3 q = arm_fk(c, j);
    const float e = std::hypot(std::hypot(q.x - p.x, q.y - p.y), q.z - p.z);
    CHECK(e < tol, "%s: ik/fk round trip %.4f mm", tag, e);
    return true;
}

static void test_arm()
{
    ArmConfig c = arm_cfg();
    int n = 0;
    float worst = 0;
    for (float x = 80; x <= 220; x += 20)
        for (float y = -120; y <= 120; y += 40)
            for (float z : {0.f, 30.f, 60.f}) {
                JointAngles j;
                if (arm_ik(c, {x, y, z}, 0.4f, j)) continue;
                Vec3 p = arm_fk(c, j);
                float e = std::hypot(std::hypot(p.x - x, p.y - y), p.z - z);
                worst = std::fmax(worst, e);
                CHECK(std::fabs(j.q[1] + j.q[2] + j.q[3] + 90.f) < 1e-3f, "tool not vertical");
                n++;
            }
    CHECK(n > 30, "only %d reachable test points", n);
    CHECK(worst < 0.01f, "ik/fk round trip error %.4f mm", worst);
    JointAngles j;
    CHECK(arm_ik(c, {400, 0, 0}, 0, j) != nullptr, "far point must be out of reach");

    // counts <-> angles round trip (teaching by hand reads counts back)
    JointAngles ja;
    ja.q = {30.f, 45.f, -90.f, -45.f, 12.5f};
    std::array<uint16_t, 6> cnt{};
    CHECK(!arm_to_counts(c, ja, 35.f, cnt), "arm_to_counts in range");
    float grip = 0;
    JointAngles jb = arm_from_counts(c, cnt, grip);
    float worst_q = 0;
    for (int i = 0; i < 5; i++) worst_q = std::fmax(worst_q, std::fabs(jb.q[i] - ja.q[i]));
    CHECK(worst_q < 0.1f, "counts -> angles round trip error %.3f deg", worst_q);   // 1 count = 0.073 deg
    CHECK(std::fabs(grip - 35.f) < 0.1f, "gripper round trip %.2f mm", grip);
    {   // a pick only starts from / passes through gripper-down poses (10-22: the 190 mm gripper swinging forward from
        // a gripper-up pose reaches the light bar across the belt)
        ArmConfig u = c;
        JointAngles up;
        up.q = {0, 90, 0, 0, 0};   // arm straight up, gripper up
        std::array<uint16_t, 6> upc;
        CHECK(!arm_to_counts(u, up, 30, upc), "candle pose counts");
        std::vector<ServoFrame> w;
        const char *why = plan_pick(u, {c.base_xy.x + 10.f, c.base_xy.y + 120.f}, 0.f, 40, 30, 0, upc, w);
        CHECK(why && strstr(why, "not pointing down"), "pick from a gripper-up pose refused: %s", why ? why : "planned");
        u.home_deg = up.q;   // ready pose taught gripper-up
        std::array<uint16_t, 6> okc;
        JointAngles dn;
        dn.q = c.home_deg;
        arm_to_counts(c, dn, 30, okc);
        why = plan_pick(u, {c.base_xy.x + 10.f, c.base_xy.y + 120.f}, 0.f, 40, 30, 0, okc, w);
        CHECK(why && strstr(why, "ready pose"), "gripper-up ready pose refused: %s", why ? why : "planned");
    }
    {   // measured gripper (10-22): closed 2919, a 32 mm gear at 2318, fully open 61 mm at 1195 -> piecewise linear
        ArmConfig g = c;
        g.grip_max_open = 61;
        g.grip_table = {{1195, 61}, {2919, 0}, {2318, 32}};
        CHECK(std::fabs(grip_count(g, 0) - 2919) < 0.5f && std::fabs(grip_count(g, 32) - 2318) < 0.5f &&
                  std::fabs(grip_count(g, 61) - 1195) < 0.5f && std::fabs(grip_count(g, 80) - 1195) < 0.5f,
              "grip table: 0 / 32 / 61 / clamp -> %.0f %.0f %.0f %.0f", grip_count(g, 0), grip_count(g, 32),
              grip_count(g, 61), grip_count(g, 80));
        bool rt = true;
        for (float mm : {0.f, 5.f, 30.f, 32.f, 45.f, 61.f}) rt = rt && std::fabs(grip_mm(g, grip_count(g, mm)) - mm) < 0.01f;
        CHECK(rt, "grip table round trip");
        CHECK(std::fabs(grip_counts_per_mm(g, 30) - 601.f / 32) < 0.1f && std::fabs(grip_counts_per_mm(g, 50) - 1123.f / 29) < 0.1f,
              "grip slope %.2f / %.2f counts per mm", grip_counts_per_mm(g, 30), grip_counts_per_mm(g, 50));
        std::array<uint16_t, 6> pc;
        CHECK(!arm_to_counts(g, jb, 30, pc) && pc[5] == uint16_t(std::lround(2919 - 601.f * 30 / 32)),
              "grip 30 mm -> count %u", pc[5]);
    }

    // a taught bin pose outside the joint limits must be refused
    ArmConfig lim = c;
    lim.joint[0].min_deg = -135;
    lim.joint[0].max_deg = 135;
    lim.bins = {{{-150, 40, -100, -30, 0}}};
    std::vector<ServoFrame> wp2;
    std::array<uint16_t, 6> home2{{2048, 2048, 2048, 2048, 2048, 2048}};
    const char *w2 = plan_pick(lim, {c.base_xy.x + 10.f, c.base_xy.y + 120.f}, 0.f, 40, 30, 0, home2, wp2);
    CHECK(w2 && std::string(w2) == "bin pose outside joint limits", "bin outside limits: %s", w2 ? w2 : "accepted");

    std::array<uint16_t, 6> home = ready_counts(c);
    std::vector<ServoFrame> wp;
    Vec2 target = {c.base_xy.x + 10.f, c.base_xy.y + 120.f};   // 120 mm in front of the base
    const char *why = plan_pick(c, target, 0.f, 40, 30, 1, home, wp);
    CHECK(!why, "plan_pick: %s", why ? why : "");
    CHECK(wp.size() == 7, "waypoints %zu", wp.size());
    if (wp.size() == 7) {
        CHECK(wp[3].belt_clear && !wp[2].belt_clear, "belt clear flag on lift");
        CHECK(wp[2].check_grasp, "grasp check after close");
        CHECK(wp[2].pos[5] < wp[1].pos[5], "gripper closes (count drops)");
        CHECK(wp[1].pos[4] == wp[0].pos[4], "wrist rotate fixed while descending");
    }

    // tilted grasp: out of the vertical reach, fingers closing tangentially
    {
        ArmConfig t = arm_cfg();
        JointAngles jv, jt;
        const Vec3 far{200, 0, 6};   // 200 mm in front of the base axis, at grasp height
        CHECK(arm_ik(t, far, kPi / 2, jv) != nullptr, "200 mm is out of the vertical reach");
        const float tilt = 30.f * kPi / 180;
        if (check_ik_fk(t, far, kPi / 2, "200 mm tilted 30 deg", jt, 0.01f, tilt))
            CHECK(std::fabs(jt.q[1] + jt.q[2] + jt.q[3] + 60.f) < 1e-3f, "tool leans 30 deg: q2+q3+q4 = %.3f",
                  jt.q[1] + jt.q[2] + jt.q[3]);
        CHECK(arm_ik(t, far, 0.f, jt, tilt) != nullptr, "a tilted grasp refuses a radial closing axis");

        std::array<uint16_t, 6> h = ready_counts(t);
        std::vector<ServoFrame> wt;
        const Vec2 tp{t.base_xy.x, t.base_xy.y + 200.f};   // base_yaw 90 deg: arm +x = table +y
        float used = -1;
        t.max_tilt_deg = 40;
        why = plan_pick(t, tp, arm_tangential_axis(t, tp), 40, 32, 0, h, wt, &used);
        CHECK(!why && used > 0 && used <= 40 && wt.size() == 7, "plan_pick 200 mm: %s, tilt %.0f deg", why ? why : "ok",
              used);
        CHECK(plan_pick(t, tp, arm_tangential_axis(t, tp) + kPi / 2, 40, 32, 0, h, wt) != nullptr,
              "radial closing axis at 200 mm: out of reach");
        const Vec2 np{t.base_xy.x + 10.f, t.base_xy.y + 120.f};
        why = plan_pick(t, np, arm_tangential_axis(t, np), 40, 32, 0, h, wt, &used);
        CHECK(!why && used == 0, "120 mm stays vertical: %s, tilt %.0f", why ? why : "ok", used);
        t.max_tilt_deg = 0;
        CHECK(plan_pick(t, tp, arm_tangential_axis(t, tp), 40, 32, 0, h, wt) != nullptr,
              "max_tilt_deg 0: vertical only");
    }

    // gripper offset from the #5 axis (measured 9 mm toward the #6 servo, servo beside the finger plane)
    {
        ArmConfig o = arm_cfg();
        o.l_tool = 27;
        o.tool_offset = 9;
        for (float side : {kPi / 2, -kPi / 2}) {
            o.servo_side = side;
            JointAngles j;
            for (float ax : {0.f, 0.6f, -1.2f, 1.5f}) {   // vertical grasps, any closing axis
                const Vec3 p{130, -25, 3};
                if (check_ik_fk(o, p, ax, "offset vertical grasp", j)) {
                    const float closing = j.q[0] / 180 * kPi + j.q[4] / 180 * kPi + o.grip_axis_at_zero;
                    CHECK(std::fabs(wrap_axis(closing - ax)) < 1e-3f, "offset: fingers still close along %.2f", ax);
                }
            }
            for (float ax : {0.f, 0.6f, -1.2f, 1.5f}) {   // near the base: the #6 servo must not face it
                const Vec3 near{90, -12, 3};
                JointAngles jn;
                const char *w = arm_ik(o, near, ax, jn);
                if (w) {
                    CHECK(!strcmp(w, "servo would face the base (joint 5 limit)"), "near base (axis %.1f): %s", ax, w);
                    continue;
                }
                const float phi = jn.q[4] / 180 * kPi + o.grip_axis_at_zero + o.servo_side;
                Vec3 q = arm_fk(o, jn);
                CHECK(std::cos(phi) >= -0.5f, "near base (side %+.0f, axis %.1f): servo toward the base (phi %.0f deg)",
                      side * 180 / kPi, ax, phi * 180 / kPi);
                CHECK(std::hypot(std::hypot(q.x - near.x, q.y - near.y), q.z - near.z) < 0.02f, "near base ik/fk");
                const float closing = jn.q[0] / 180 * kPi + jn.q[4] / 180 * kPi + o.grip_axis_at_zero;
                CHECK(std::fabs(wrap_axis(closing - ax)) < 1e-3f, "near base: fingers still close along %.2f", ax);
            }
            JointAngles z0 = j;   // the offset is real: ignoring it misses by ~9 mm
            ArmConfig n = o;
            n.tool_offset = 0;
            Vec3 a = arm_fk(o, z0), b = arm_fk(n, z0);
            CHECK(std::fabs(std::hypot(a.x - b.x, a.y - b.y) - 9) < 0.05f, "offset size %.2f",
                  std::hypot(a.x - b.x, a.y - b.y));
            const Vec3 far{200, 0, 3};   // tilted: servo on the outer (upper) side, round trip exact
            const float tilt = 30.f * kPi / 180;
            JointAngles jtilt;
            if (check_ik_fk(o, far, kPi / 2, "offset tilted grasp", jtilt, 0.02f, tilt)) {
                const float phi = jtilt.q[4] / 180 * kPi + o.grip_axis_at_zero + o.servo_side;
                CHECK(std::fabs(wrap_pi(phi)) < 1e-4f, "tilted: servo side outward (phi %.3f)", phi);
                ArmConfig lo = o;   // the servo side 9 mm along s: outward + up when leaning away
                lo.tool_offset = 9 + 40;
                CHECK(arm_fk(lo, jtilt).z > arm_fk(o, jtilt).z + 40 * std::sin(tilt) - 0.01f,
                      "tilted: servo side is the upper side");
            }
        }
    }
}

// round parts: with tilted grasps enabled the picker tries the closing axis tangential to the arm base first
static void test_picker_tangential()
{
    std::mt19937 rng(5);
    Tracker tr(tracker_cfg());
    PickerConfig pc;
    pc.tangential_first = true;
    pc.arm_base = {380, -170};
    Picker pk(pc);
    const std::vector<SimPart> parts = {{-30, 20, 30, 30, 0.7f, 0}};
    double B = 0;
    for (uint32_t f = 0; f < 60; f++, B += 2) tr.update(observe(parts, B, f, rng));
    const Track *t = nullptr;
    for (const Track &x : tr.tracks())
        if (x.n_geom > 0) t = &x;
    CHECK(t != nullptr, "no track for the round part");
    if (!t) return;
    float ax = 0, op = 0;
    const char *w = pk.plan_grip(tr, *t, B, ax, op);
    CHECK(!w, "round part grip: %s", w ? w : "ok");
    const float want = std::atan2(t->y - pc.arm_base.y, t->x_at(B) - pc.arm_base.x) + kPi / 2;
    CHECK(std::fabs(wrap_axis(ax - want)) < 1e-4f, "tangential axis first: %.4f vs %.4f", ax, wrap_axis(want));
}

static void test_arm_base_fit()
{
    // true mounting: base (380, -170), arm +x along table +y (yaw 90 deg) + 3 deg, belt 12 mm above the base plate
    const Vec2 base{380, -170};
    const float yaw = (90 + 3) * kPi / 180, belt = 12, h = 8;   // marker 8 mm high
    const Vec2 tbl[5] = {{250, -40}, {320, 30}, {390, -60}, {300, -90}, {360, 60}};
    std::vector<Vec3> arm;
    std::vector<Vec2> table;
    const float noise[5][3] = {{0.3f, -0.2f, 0.4f}, {-0.4f, 0.1f, -0.3f}, {0.2f, 0.3f, 0.2f}, {-0.1f, -0.4f, -0.2f},
                               {0.0f, 0.2f, 0.1f}};   // +-0.4 mm hand placement / FK error
    for (int i = 0; i < 5; i++) {
        const float dx = tbl[i].x - base.x, dy = tbl[i].y - base.y;
        const float c = std::cos(yaw), s = std::sin(yaw);
        arm.push_back({c * dx + s * dy + noise[i][0], -s * dx + c * dy + noise[i][1], belt + h + noise[i][2]});
        table.push_back(tbl[i]);
    }
    ArmBaseFit f = fit_arm_base(arm, table, h);
    CHECK(f.ok, "arm base fit: %s", f.err.c_str());
    CHECK(std::fabs(f.base_xy.x - base.x) < 1 && std::fabs(f.base_xy.y - base.y) < 1, "base (%.2f, %.2f)", f.base_xy.x,
          f.base_xy.y);
    CHECK(std::fabs(f.base_yaw - yaw) < 0.3f * kPi / 180, "yaw %.3f deg", f.base_yaw * 180 / kPi);
    CHECK(std::fabs(f.belt_z - belt) < 0.3f, "belt_z %.2f", f.belt_z);
    CHECK(f.rms < 0.6f && f.max < 1.0f && f.resid.size() == 5, "residual rms %.2f max %.2f", f.rms, f.max);
    // the fitted transform maps a new table point to the same arm point as the true one (within 1 mm), both ways
    ArmConfig cfg;
    cfg.base_xy = f.base_xy;
    cfg.base_yaw = f.base_yaw;
    ArmConfig truth;
    truth.base_xy = base;
    truth.base_yaw = yaw;
    Vec3 a1 = table_to_arm(cfg, {330, 0}, 0), a2 = table_to_arm(truth, {330, 0}, 0);
    CHECK(std::hypot(a1.x - a2.x, a1.y - a2.y) < 1, "pick point through the fit off by %.2f mm",
          std::hypot(a1.x - a2.x, a1.y - a2.y));
    Vec2 t1 = arm_to_table(cfg, a1), t2 = arm_to_table(truth, a2);
    CHECK(std::hypot(t1.x - t2.x, t1.y - t2.y) < 1 && std::hypot(t1.x - 330, t1.y) < 1,
          "arm_to_table inverts table_to_arm: (%.2f, %.2f)", t1.x, t1.y);
    // a wrong pair (marker moved after it was located) shows up in the residuals
    arm[2].x += 15;
    f = fit_arm_base(arm, table, h);
    CHECK(f.ok && f.max > 8, "outlier visible (max residual %.1f mm)", f.max);
    // too few / too close
    CHECK(!fit_arm_base({arm[0], arm[1]}, {table[0], table[1]}, h).ok, "2 points rejected");
    CHECK(!fit_arm_base({arm[0], arm[0], arm[0]}, {table[0], {table[0].x + 1, table[0].y}, table[0]}, h).ok,
          "points 1 mm apart rejected");
}

int main()
{
    test_geom();
    test_tracker();
    test_size_rule();
    test_recipes();
    test_votes();
    test_spawn_before_decide();
    test_gate_geometry();
    test_flaps();
    test_speed_adapt();
    test_gate_events();
    test_picker();
    test_picker_flying();
    test_picker_tangential();
    test_arm();
    test_arm_base_fit();
    return test_summary();
}
