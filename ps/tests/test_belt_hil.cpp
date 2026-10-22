// test_belt_hil: hardware-in-the-loop test of the belt control chain on the board (PL v5+, no motor needed):
// simulated parts ride a virtual belt whose travel is the REAL stepper position from PL; the tracker sees them
// through a simulated camera, the picker decides, BeltDriver drives stepper_ctrl. Checks that every OK part stops
// on the pick line (true error, from the real step count), NG parts flow to the tail, nothing is stopped late.
//   ./test_belt_hil [speed_mm_s]      exit code = failed checks
#include <time.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "belt_ctl.hpp"
#include "picker.hpp"
#include "sim.hpp"
#include "test_check.hpp"
#include "tracker.hpp"
#include "util.hpp"

using namespace des;

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

    TrackerConfig tc = tracker_cfg();
    PickerConfig pc;
    pc.pick_x = 380; pc.tail_x = tc.tail_x; pc.reach_y0 = -80; pc.reach_y1 = 80;

    // 12 gear parts, 70 mm apart; every third one is gear_flash (NG, rework) and rides to the belt end
    std::vector<SimPart> parts;
    int expect_pick = 0;
    for (int k = 0; k < 12; k++) {
        const int cls = (k % 3 == 2) ? 2 : 0;   // gear_flash / gear_ok
        if (cls == 0) expect_pick++;
        parts.push_back({-40.f - 70.f * k, k % 2 ? 20.f : -20.f, 32.f, 32.f, 0.f, cls});
    }

    Tracker tr(tc);
    Picker pk(pc);
    BeltDriver belt(&hw, bc);
    des_step_halt(&hw);
    des_step_zero(&hw);
    belt.enable(true);

    std::mt19937 rng(3);
    enum { ArmIdle, ArmDown, ArmPlacing } arm = ArmIdle;
    double arm_t = 0;
    PickJob job{};
    std::vector<double> stop_err;
    int flapped = 0;
    const double t_start = mono_s(), period = 1.0 / 60;
    double next = t_start;
    uint32_t frame = 0;
    std::vector<bool> logged(4096, false);

    while (mono_s() - t_start < 120) {
        next += period;
        timespec ts;
        double wake = next;
        ts.tv_sec = time_t(wake);
        ts.tv_nsec = long((wake - double(ts.tv_sec)) * 1e9);
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);
        const double B = belt.belt_mm(), t = mono_s();

        // simulated camera: parts past the belt end are gone, the rest are seen through sim.hpp's camera model
        for (SimPart &p : parts) {
            if (p.gone) continue;
            const float x = p.s + float(B);
            if (x <= tc.tail_x + 20) continue;
            p.gone = true;
            if (!p.picked) flapped++;
            if (p.cls == 0 && !p.picked)
                for (const Track &tk : tr.tracks())
                    if (std::fabs(tk.s - p.s) < 5)
                        printf("   OK part s=%.0f went to the tail: track %d %s/%s (%s) len %.1f wid %.1f\n", p.s, tk.id,
                               to_str(tk.state), to_str(tk.disp), tk.why, tk.len, tk.wid);
        }
        tr.update(observe(parts, B, frame++, rng));

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
                       to_str(tk.disp), tk.why);
            }
        tr.retire(B);

        bool all_gone = true;
        for (const SimPart &p : parts) all_gone = all_gone && p.gone;
        if (all_gone && arm == ArmIdle) break;
    }
    des_step_stop(&hw);
    double el = mono_s() - t_start;
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
    const int fails = test_summary();
    des_hw_close(&hw);
    return fails;
}
