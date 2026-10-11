// test_scheduler: geometry, tracker, picker and arm kinematics without hardware. Runs on the board or any
// Linux host:  ./test_scheduler   (exit code = number of failed checks)
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>

#include "arm.hpp"
#include "geom.hpp"
#include "flap.hpp"
#include "picker.hpp"
#include "tracker.hpp"

using namespace des;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (cond) g_pass++;                                           \
        else { g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__);  \
               printf(__VA_ARGS__); printf("\n"); }                   \
    } while (0)

static TrackerConfig tracker_cfg()
{
    TrackerConfig c;
    c.fov_x0 = 0; c.fov_x1 = 300; c.edge_margin = 10; c.decide_x = 270; c.tail_x = 600;
    c.rules = {
        {"gear_ok", Disposition::Pick, 0}, {"gear_short", Disposition::Flap, 1}, {"gear_flash", Disposition::Flap, 2},
        {"gear_dirty", Disposition::Flap, 1}, {"rack_ok", Disposition::Pick, 1}, {"rack_short", Disposition::Flap, 1},
        {"rack_flash", Disposition::Flap, 2}, {"rack_dirty", Disposition::Flap, 1},
    };
    return c;
}

struct SimPart { float s, y, len, wid, axis; int cls; };

// camera model: parts fully inside the view are seen with noise, cut by the edge they are partial
static FrameObs observe(const std::vector<SimPart> &parts, double B, uint32_t frame, std::mt19937 &rng)
{
    std::normal_distribution<float> n(0.f, 0.4f), na(0.f, 0.02f);
    FrameObs o;
    o.frame = frame;
    o.belt_mm = B;
    for (const SimPart &p : parts) {
        float x = float(p.s + B);
        float half = p.len / 2;
        if (x + half < 0 || x - half > 300) continue;
        Detection d;
        d.p = {x + n(rng), p.y + n(rng)};
        d.len = p.len + n(rng); d.wid = p.wid + n(rng); d.axis = wrap_axis(p.axis + na(rng));
        d.cls = p.cls; d.conf = p.cls >= 0 ? 0.9f : 0.f;
        d.partial = x - half < 0 || x + half > 300;
        if (d.partial) d.p.x += x - half < 0 ? 0.25f * half : -0.25f * half;   // cut blob: shifted center
        o.dets.push_back(d);
    }
    return o;
}

static void test_geom()
{
    ORect a{{0, 0}, 10, 5, 0}, b{{18, 0}, 10, 5, 0}, c{{21, 0}, 10, 5, 0};
    CHECK(overlap(a, b), "aligned overlap");
    CHECK(!overlap(a, c), "aligned separated");
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
        {-140, -30, 60, 12, 0.7f, 5},     // rack_short -> flap outlet 1
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
        if (p->cls == 5) {
            CHECK(t.disp == Disposition::Flap && t.dest == 1, "rack_short -> %s/%d", to_str(t.disp), t.dest);
            CHECK(std::fabs(wrap_axis(t.axis - p->axis)) < 0.02f, "rack axis err %.3f", t.axis - p->axis);
        }
        if (p->cls < 0) CHECK(t.disp == Disposition::Recheck, "unclassified -> %s", to_str(t.disp));
    }
    CHECK(seen == 3, "live tracks %d", seen);
}

// SizeRule: the measurement (filled while the part crosses the view, like app/track_measure) overrides "pick" only
static void test_size_rule()
{
    std::mt19937 rng(7);
    TrackerConfig cfg = tracker_cfg();
    cfg.size.enabled = true;
    Tracker tr(cfg);
    auto gear = [](int teeth, float od, float ecc, int shorts, bool judged) {
        PartMeasure m;
        m.valid = true; m.kind = 'g'; m.teeth_judged = judged; m.teeth = teeth; m.size = od; m.bore = 6.02f;
        m.ecc = ecc; m.short_teeth = shorts;
        return m;
    };
    auto rack = [](float len) {
        PartMeasure m;
        m.valid = true; m.kind = 'r'; m.teeth_judged = true; m.size = len; m.teeth = 15; m.pitch = 3.14f;
        return m;
    };
    struct Case { int cls; PartMeasure m; Disposition disp; int dest; const char *what; };
    std::vector<Case> cases = {
        {0, gear(30, 32.01f, 0.05f, 0, true), Disposition::Pick, 0, "good gear"},
        {0, gear(28, 30.0f, 0.05f, 0, true), Disposition::Flap, 1, "28-tooth gear (spec mix-up)"},
        {0, gear(30, 32.5f, 0.05f, 0, true), Disposition::Flap, 1, "OD out of tolerance"},
        {0, gear(30, 32.0f, 0.5f, 0, true), Disposition::Flap, 1, "eccentric bore"},
        {0, gear(30, 32.0f, 0.05f, 1, true), Disposition::Recheck, 0, "NPU OK, measured short tooth"},
        {0, gear(30, 32.0f, 0.05f, 1, false), Disposition::Pick, 0, "short tooth not judged (far off-axis)"},
        {4, rack(48.0f), Disposition::Flap, 1, "48 mm rack"},
        {4, rack(50.1f), Disposition::Pick, 1, "50 mm rack"},
        {2, gear(30, 32.0f, 0.05f, 0, true), Disposition::Flap, 2, "NPU flash stays NPU flash"},
        {0, PartMeasure{}, Disposition::Pick, 0, "not measured, require off"},
    };
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
        CHECK(tk && tk->state == TrackState::Decided, "%s: no decided track", cases[i].what);
        if (!tk) continue;
        CHECK(tk->disp == cases[i].disp && tk->dest == cases[i].dest, "%s -> %s/%d (%s), want %s/%d", cases[i].what,
              to_str(tk->disp), tk->dest, tk->why.c_str(), to_str(cases[i].disp), cases[i].dest);
    }
}

// NPU crop classifier: blob detections carry no class, a few soft votes per track decide (mean probability)
static void test_votes()
{
    std::mt19937 rng(3);
    Tracker tr(tracker_cfg());
    std::vector<SimPart> parts = {{-40, 0, 30, 30, 0.f, -1}, {-120, 10, 30, 30, 0.f, -1}};
    std::vector<float> flash(8, 0.04f), unsure(8, 0.f);
    flash[2] = 0.72f;                                   // gear_flash
    unsure[0] = 0.4f; unsure[2] = 0.35f; unsure[3] = 0.25f;
    int voted[2] = {0, 0};
    uint32_t f = 0;
    for (double B = 0; B < 450; B += 1.0, f++) {
        tr.update(observe(parts, B, f, rng));
        for (Track &t : tr.tracks()) {
            float x = t.x_at(B);
            for (int i = 0; i < 2; i++)
                if (std::fabs(t.s - parts[i].s) < 5 && x > 100 && x < 200 && voted[i] < 3 && int(B) % 20 == 0)
                    voted[i] += tr.vote(t.id, i == 0 ? flash : unsure);
        }
    }
    CHECK(voted[0] == 3 && voted[1] == 3, "votes accepted %d %d", voted[0], voted[1]);
    for (const Track &t : tr.tracks()) {
        if (std::fabs(t.s - parts[0].s) < 5)
            CHECK(t.disp == Disposition::Flap && t.dest == 2 && std::fabs(t.cls_conf - 0.72f) < 1e-3f,
                  "voted flash -> %s/%d conf %.3f", to_str(t.disp), t.dest, t.cls_conf);
        if (std::fabs(t.s - parts[1].s) < 5)
            CHECK(t.disp == Disposition::Recheck && t.why == "low confidence", "unsure votes -> %s (%s)",
                  to_str(t.disp), t.why.c_str());
    }
    CHECK(!tr.vote(9999, flash), "vote for a missing track is refused");
}

// two-stage flaps follow the picker's flap events: set prepare_mm ahead, held until clear_mm past, rest = recheck
static void test_flaps()
{
    FlapConfig fc;   // prepare 40, clear 60, f1 1000 / 1900, f2 1000 / 1900
    FlapSequencer fs(fc);
    std::vector<FlapEvent> ev = {{1, 1, 100.0}, {2, 2, 200.0}, {3, 0, 300.0}};   // scrap, rework, recheck
    auto at = [&](double b) { return fs.update(ev, b); };
    FlapSequencer::State s = at(50);
    CHECK(s.track_id == -1 && s.us[0] == 1000, "rest: stage 1 lets parts through to recheck");
    s = at(61);
    CHECK(s.track_id == 1 && s.us[0] == 1900 && s.us[1] == 1900, "part 1 (scrap) set 40 mm ahead: %d %d %d",
          s.track_id, s.us[0], s.us[1]);
    s = at(159);
    CHECK(s.track_id == 1, "held while part 1 passes");
    s = at(160);
    CHECK(s.track_id == 2 && s.us[0] == 1900 && s.us[1] == 1000, "part 2 (rework) next: %d %d %d", s.track_id, s.us[0],
          s.us[1]);
    s = at(261);
    CHECK(s.track_id == 3 && s.us[0] == 1000, "part 3 (recheck): stage 1 straight");
    s = at(400);
    CHECK(s.track_id == -1 && s.us[0] == 1000 && fs.served == 3, "all served, resting (%llu)",
          (unsigned long long)fs.served);
}

static void test_picker()
{
    std::mt19937 rng(2);
    Tracker tr(tracker_cfg());
    PickerConfig pc;
    pc.pick_x = 400; pc.tail_x = 600; pc.reach_y0 = -80; pc.reach_y1 = 80;
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

    // crowded: an OK rack lying 3 mm beside another part cannot be gripped across its width
    Tracker t2(tracker_cfg());
    std::vector<SimPart> crowd = {{-40, 0, 60, 12, 0.f, 4}, {-40, 22, 60, 12, 0.f, 5}};
    for (double b = 0; b < 320; b += 1.0) t2.update(observe(crowd, b, uint32_t(b), rng));
    Picker::Input in;
    in.belt_mm = 320; in.brake_mm = 2; in.belt_stopped = false;
    pk.tick(t2, in);
    const Track *ok = nullptr;
    for (const Track &t : t2.tracks()) if (std::fabs(t.y) < 5) ok = &t;
    CHECK(ok && ok->disp == Disposition::Recheck && ok->why == "crowded", "crowded rack -> %s (%s)",
          ok ? to_str(ok->disp) : "none", ok ? ok->why.c_str() : "");
}

static ArmConfig arm_cfg()
{
    ArmConfig c;
    for (int i = 0; i < 6; i++) { c.joint[i].id = uint8_t(i + 1); c.joint[i].min_deg = -180; c.joint[i].max_deg = 180; }
    c.bins = {{{90, 40, -100, -30, 0}}, {{120, 40, -100, -30, 0}}};
    return c;
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

    // a taught bin pose outside the joint limits must be refused
    ArmConfig lim = c;
    lim.joint[0].min_deg = -135;
    lim.joint[0].max_deg = 135;
    lim.bins = {{{-150, 40, -100, -30, 0}}};
    std::vector<ServoFrame> wp2;
    std::array<uint16_t, 6> home2{{2048, 2048, 2048, 2048, 2048, 2048}};
    const char *w2 = plan_pick(lim, {c.base_xy.x + 10.f, c.base_xy.y + 120.f}, 0.f, 40, 30, 0, home2, wp2);
    CHECK(w2 && std::string(w2) == "bin pose outside joint limits", "bin outside limits: %s", w2 ? w2 : "accepted");

    std::array<uint16_t, 6> home{{2048, 2048, 2048, 2048, 2048, 2048}};
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
}

int main()
{
    test_geom();
    test_tracker();
    test_size_rule();
    test_votes();
    test_flaps();
    test_picker();
    test_arm();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail;
}
