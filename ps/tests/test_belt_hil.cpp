// test_belt_hil: hardware-in-the-loop test of the belt control chain on the board (PL v5+, no motor needed):
// simulated parts ride a virtual belt whose travel is the REAL stepper position from PL; the tracker sees them
// through a simulated camera, the picker decides, BeltDriver drives stepper_ctrl. Checks that every OK part stops
// on the pick line (true error, from the real step count), NG parts flow to the tail, nothing is stopped late.
//   ./test_belt_hil [speed_mm_s]      exit code = failed checks
#include <time.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "belt_ctl.hpp"
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

static double now_s()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

struct SimPart {
    float s, y, len, wid, axis;
    int cls;
    bool gone = false;
    bool picked = false;
};

int main(int argc, char **argv)
{
    des_hw_t hw;
    if (des_hw_open(&hw) != DES_OK || hw.version < DES_VERSION_CAPTURE) {
        fprintf(stderr, "needs the des_regs bitstream v5+ (BRAKE register)\n");
        return 1;
    }
    BeltConfig bc;
    bc.mm_per_step = 0.03;
    bc.dir = 1;
    bc.run_speed = argc > 1 ? atof(argv[1]) : 50;
    bc.accel = 400;

    TrackerConfig tc;
    tc.fov_x0 = 0; tc.fov_x1 = 300; tc.edge_margin = 10; tc.decide_x = 270; tc.tail_x = 600;
    tc.rules = {{"gear_ok", Disposition::Pick, 0}, {"gear_short", Disposition::Flap, 1},
                {"gear_flash", Disposition::Flap, 2}, {"gear_dirty", Disposition::Flap, 1},
                {"rack_ok", Disposition::Pick, 1}, {"rack_short", Disposition::Flap, 1},
                {"rack_flash", Disposition::Flap, 2}, {"rack_dirty", Disposition::Flap, 1}};
    PickerConfig pc;
    pc.pick_x = 380; pc.tail_x = tc.tail_x; pc.reach_y0 = -80; pc.reach_y1 = 80;

    // 12 parts, 70 mm apart; every third one NG
    std::vector<SimPart> parts;
    int expect_pick = 0;
    for (int k = 0; k < 12; k++) {
        bool rack = k % 2;
        int cls = (k % 3 == 2) ? (rack ? 5 : 1) : (rack ? 4 : 0);
        if (cls == 0 || cls == 4) expect_pick++;
        parts.push_back({-40.f - 70.f * k, k % 2 ? 20.f : -20.f, rack ? 50.f : 32.f, rack ? 15.f : 32.f,
                         rack ? 0.3f : 0.f, cls});
    }

    Tracker tr(tc);
    Picker pk(pc);
    BeltDriver belt(&hw, bc);
    des_step_halt(&hw);
    des_step_zero(&hw);
    belt.enable(true);

    std::mt19937 rng(3);
    std::normal_distribution<float> noise(0.f, 0.3f);
    enum { ArmIdle, ArmDown, ArmPlacing } arm = ArmIdle;
    double arm_t = 0;
    PickJob job{};
    std::vector<double> stop_err;
    int flapped = 0;
    const double t_start = now_s(), period = 1.0 / 60;
    double next = t_start;
    uint32_t frame = 0;
    std::vector<bool> logged(4096, false);

    while (now_s() - t_start < 120) {
        next += period;
        timespec ts;
        double wake = next;
        ts.tv_sec = time_t(wake);
        ts.tv_nsec = long((wake - double(ts.tv_sec)) * 1e9);
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);
        const double B = belt.belt_mm(), t = now_s();

        // simulated camera
        FrameObs obs;
        obs.frame = frame++;
        obs.belt_mm = B;
        for (SimPart &p : parts) {
            if (p.gone) continue;
            float x = p.s + float(B);
            const float half = p.len / 2;
            if (x + half > 0 && x - half < 300 && (x - half < 0 || x + half > 300)) {
                // cut by the image edge: the real locator reports a partial blob with a shifted center
                Detection d;
                float cut_lo = std::max(0.f, x - half), cut_hi = std::min(300.f, x + half);
                d.p = {0.5f * (cut_lo + cut_hi) + noise(rng), p.y + noise(rng)};
                d.len = cut_hi - cut_lo; d.wid = p.wid; d.axis = p.axis;
                d.cls = p.cls; d.conf = 0.6f;
                d.partial = true;
                obs.dets.push_back(d);
                continue;
            }
            if (x - half < 0 || x + half > 300) {
                if (x > tc.tail_x + 20) {
                    p.gone = true;
                    if (!p.picked) flapped++;
                    if ((p.cls == 0 || p.cls == 4) && !p.picked)
                        for (const Track &tk : tr.tracks())
                            if (std::fabs(tk.s - p.s) < 5)
                                printf("   OK part s=%.0f went to the tail: track %d %s/%s (%s) len %.1f wid %.1f\n", p.s,
                                       tk.id, to_str(tk.state), to_str(tk.disp), tk.why.c_str(), tk.len, tk.wid);
                }
                continue;
            }
            Detection d;
            d.p = {x + noise(rng), p.y + noise(rng)};
            d.len = p.len; d.wid = p.wid; d.axis = p.axis;
            d.cls = p.cls; d.conf = 0.9f;
            obs.dets.push_back(d);
        }
        tr.update(obs);

        // simulated arm: 0.3 s on the belt, 0.7 s placing
        if (arm == ArmDown && t - arm_t > 0.3) {
            arm = ArmPlacing;
            for (SimPart &p : parts)
                if (!p.gone && std::fabs(p.s + float(B) - job.p.x) < 5 && std::fabs(p.y - job.p.y) < 5) {
                    p.gone = p.picked = true;
                }
            pk.job_done(tr, job.track_id, true);
        } else if (arm == ArmPlacing && t - arm_t > 1.0) {
            arm = ArmIdle;
        }

        Picker::Input in;
        in.belt_mm = B;
        in.brake_mm = belt.brake_mm();
        in.belt_stopped = belt.stopped();
        in.arm_idle = arm == ArmIdle;
        in.arm_clear = arm != ArmDown;
        Picker::Output out = pk.tick(tr, in);
        static int last_kind = -1, last_track = -1;
        if (int(out.belt.kind) != last_kind || out.belt.track_id != last_track) {
            static const char *kinds[] = {"run", "stop-at", "hold"};
            printf("   [belt %7.2f] plan %s %.2f (track %d) stopped %d arm %d\n", B, kinds[out.belt.kind],
                   out.belt.stop_belt_mm, out.belt.track_id, int(in.belt_stopped), int(arm));
            last_kind = int(out.belt.kind);
            last_track = out.belt.track_id;
        }
        belt.apply(out.belt);
        if (out.have_job) {
            job = out.job;
            // true position of the part under the gripper, from the real step count
            float best = 1e9f;
            for (const SimPart &p : parts)
                if (!p.gone && std::fabs(p.y - job.p.y) < 5) {
                    float x = p.s + float(belt.belt_mm());
                    if (std::fabs(x - pc.pick_x) < std::fabs(best)) best = x - pc.pick_x;
                }
            stop_err.push_back(best);
            printf("[%6.2f s] pick track %d: true x - pick line = %+.3f mm (belt %.2f mm)\n", t - t_start, job.track_id,
                   best, belt.belt_mm());
            arm = ArmDown;
            arm_t = t;
        }
        for (const Track &tk : tr.tracks())
            if (tk.state != TrackState::New && tk.state != TrackState::Tracking && !logged[tk.id % 4096]) {
                logged[tk.id % 4096] = true;
                printf("   [belt %7.2f] track %d s=%.1f y=%.1f cls %d -> %s (%s)\n", B, tk.id, tk.s, tk.y, tk.cls,
                       to_str(tk.disp), tk.why.c_str());
            }
        tr.retire(B);

        bool all_gone = true;
        for (const SimPart &p : parts) all_gone = all_gone && p.gone;
        if (all_gone && arm == ArmIdle) break;
    }
    des_step_stop(&hw);
    double el = now_s() - t_start;
    belt.enable(false);

    double worst = 0;
    for (double e : stop_err) worst = std::max(worst, std::fabs(e));
    printf("\n== %zu picks, %d parts to the tail, %.1f s (%.1f parts/min), belt %.1f mm; worst stop error %.3f mm\n",
           stop_err.size(), flapped, el, 12 / el * 60, belt.belt_mm(), worst);
    printf("   belt driver: runs %llu, stops %llu, late %llu, holds %llu; picker: missed %llu crowded %llu\n",
           (unsigned long long)belt.stats.runs, (unsigned long long)belt.stats.stops,
           (unsigned long long)belt.stats.late, (unsigned long long)belt.stats.holds,
           (unsigned long long)pk.stats.missed, (unsigned long long)pk.stats.crowded);
    CHECK(int(stop_err.size()) == expect_pick, "picks %zu, expected %d", stop_err.size(), expect_pick);
    CHECK(flapped == 12 - expect_pick, "parts to the tail %d, expected %d", flapped, 12 - expect_pick);
    CHECK(worst < 0.5, "worst stop error %.3f mm", worst);
    CHECK(belt.stats.late == 0, "late stops %llu", (unsigned long long)belt.stats.late);
    CHECK(belt.stats.holds == 0, "belt moving while the arm was down (%llu)", (unsigned long long)belt.stats.holds);
    CHECK(pk.stats.missed == 0, "missed %llu", (unsigned long long)pk.stats.missed);
    printf("%d passed, %d failed\n", g_pass, g_fail);
    des_hw_close(&hw);
    return g_fail;
}
