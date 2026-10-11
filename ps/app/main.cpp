// des_app — DragonEyeSorter PS application skeleton.
//
//   des_app probe  [-n sec] [--vmax N] [--exp us] [--gain dB*10]   capture timing: fps, skipped frames, re-arm latency
//   des_app locate [-n frames] [--dump file.png]                    capture + localization, prints blobs
//   des_app track  [-n sec] [--belt mm/s]                           capture + locate + tracking table + picker, no arm
//   des_app ik     <x> <y> [z] [axis_deg]                           table-frame target -> joint angles / servo counts
//   des_app display [-n sec]                                        camera -> HDMI at 60 fps (BGRA->BGR + page flip), timing
//   des_app live   [-n sec] [--belt mm/s]                           60 fps HDMI with locate / tracking overlays (2 cores)
//   des_app run    [-n sec] [--can can0] [--go] [--belt mm/s]      production: live + belt + arm + CAN "PLC"
//   des_app arm scan|read|teach|torque-on|torque-off      servo bring-up (no motion)
//   des_app arm home|goto <x> <y> [z] [axis]|pick <x> <y> [axis_deg] [bin]   motion: dry run unless --go
//   des_app calib-cam [--board 9x6] [--square mm] [--travel right|left|down|up] [--image f.png] [--out calib.json]
//                                                                   checkerboard on the belt -> px_to_table (calib.json)
//   des_app calib-belt [--steps N] [--belt mm/s]                    one part on the belt -> mm_per_step, dir (calib.json)
//   des_app calib-arm [-n 4] [--marker-h mm]                      hand-eye: arm base_xy / yaw / belt_z (calib.json)
// NPU: the part classifier (des_app.json "npu", train/export_npu.py) runs in live / run / collect when its model exists
//   des_app collect --cls <label> [-n sec] [--belt mm/s] [--every N] [--max N] [--full N] [--out dir]
//                                                                   live + training crops of one class (des_app.json labels)
//   des_app measure [-n frames] [--image f.png] [--out raw.png] [--dump report.png]
//                                                                   measure every gear / rack in view; -n > 1: repeatability
// common: -c <config.json> (default /root/des/des_app.json), --no-rt
//
// Bring-up order: probe (60 fps without skips) -> locate (thresholds, homography) -> track (belt + tracker)
// -> arm (after the servos are wired). The NPU pipeline from vision/ attaches classes to detections later.
#include <sched.h>
#include <signal.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>

#include <opencv2/imgproc.hpp>

#include <opencv2/core/utility.hpp>
#include <opencv2/imgcodecs.hpp>

#include "arm_exec.hpp"
#include "calib.hpp"
#include "camera_rig.hpp"
#include "config.hpp"
#include "des_hw.h"
#include "hdmi_out.hpp"
#include "live.hpp"
#include "locate.hpp"
#include "measure_tool.hpp"

using namespace des;

static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int) { g_stop = 1; }

static double now_s()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

struct Args {
    std::string mode, config = "/root/des/des_app.json", dump;
    std::string board = "9x6", travel = "right", image, out;
    double square = 0;
    bool exhaustive = false;
    std::string sub;       // second plain word (arm sub-command)
    bool go = false;       // arm: really move (default: print the frames only)
    std::string can_if;    // run: CAN interface for the PLC link
    double n = 0;
    int vmax = -1, exp_us = -1, gain = -1;
    bool rt = true;
    double belt = 0;
    std::vector<double> pos;
    std::string cls;           // collect: class label
    int every = 6, maxpp = 10, full = 0;
    int steps = 0;                // calib-belt: long move (0 = to ~75 % of the view)
    float marker_h = -1;          // calib-arm: height of the marker top above the belt (mm); < 0 = keep belt_z
};

static void usage()
{
    fprintf(stderr,
            "usage: des_app probe|locate|track|display|live|run|collect|calib-cam|calib-belt|calib-arm|measure|arm|ik [options]\n"
            "  -c cfg.json  -n N  --vmax N  --exp us  --gain dB*10  --dump file.png  --belt mm/s  --no-rt\n"
            "  calib-cam: --board <cols>x<rows> (inner corners)  --square <mm>  --travel right|left|down|up\n"
            "             --image <file> (instead of the camera)  --out <calib.json>  --exhaustive\n"
            "  calib-belt: ONE part on the belt near the upstream edge (camera calibrated first); the belt moves\n"
            "             forward and back. --steps <long move>  --belt <mm/s> (default 20)  --out <calib.json>\n"
            "  calib-arm: interactive hand-eye calibration (camera calibrated, joint zeros right): -n <points, >= 3>\n"
            "             --marker-h <mm> (marker top above the belt; also fits arm.belt_z)  --out <calib.json>\n"
            "  arm: scan | read | teach | torque-on | torque-off | home | goto x y [z] [axis] | pick x y [axis] [bin]\n"
            "       motion commands only print the servo frames unless --go\n"
            "  collect: --cls <label> (one class per session)  --every <frames>  --max <crops per part>\n"
            "           --full <frames> (also full frames + YOLO boxes)  --out <dir>  --belt <mm/s>\n"
            "  measure: -n <frames> (still parts: repeatability)  --image <file> (instead of the camera)\n"
            "           --out <raw.png> (keep the frame for --image later)  --dump <report.png>\n");
}

static bool parse(int argc, char **argv, Args &a)
{
    for (int i = 1; i < argc; i++) {
        std::string k = argv[i];
        auto val = [&](void) -> const char * { return i + 1 < argc ? argv[++i] : nullptr; };
        const char *v = nullptr;
        if (k == "--no-rt") a.rt = false;
        else if (k == "--exhaustive") a.exhaustive = true;
        else if (k == "--go") a.go = true;
        else if (k == "--can" && (v = val())) a.can_if = v;
        else if (k == "-c" && (v = val())) a.config = v;
        else if (k == "-n" && (v = val())) a.n = atof(v);
        else if (k == "--vmax" && (v = val())) a.vmax = atoi(v);
        else if (k == "--exp" && (v = val())) a.exp_us = atoi(v);
        else if (k == "--gain" && (v = val())) a.gain = atoi(v);
        else if (k == "--dump" && (v = val())) a.dump = v;
        else if (k == "--belt" && (v = val())) a.belt = atof(v);
        else if (k == "--board" && (v = val())) a.board = v;
        else if (k == "--square" && (v = val())) a.square = atof(v);
        else if (k == "--travel" && (v = val())) a.travel = v;
        else if (k == "--image" && (v = val())) a.image = v;
        else if (k == "--out" && (v = val())) a.out = v;
        else if (k == "--cls" && (v = val())) a.cls = v;
        else if (k == "--every" && (v = val())) a.every = atoi(v);
        else if (k == "--max" && (v = val())) a.maxpp = atoi(v);
        else if (k == "--full" && (v = val())) a.full = atoi(v);
        else if (k == "--steps" && (v = val())) a.steps = atoi(v);
        else if (k == "--marker-h" && (v = val())) a.marker_h = float(atof(v));
        else if (a.mode.empty() && k[0] != '-') a.mode = k;   // first plain word is the mode
        else if (a.sub.empty() && isalpha((unsigned char)k[0])) a.sub = k;   // then an optional sub-command
        else if (k[0] != '-' || (k.size() > 1 && (isdigit(k[1]) || k[1] == '.'))) a.pos.push_back(atof(k.c_str()));
        else return false;
    }
    return !a.mode.empty();
}

static void go_realtime()
{
    // PREEMPT_RT kernel: a SCHED_FIFO capture loop re-arms the DMA within tens of microseconds
    sched_param sp{};
    sp.sched_priority = 60;
    if (sched_setscheduler(0, SCHED_FIFO, &sp) != 0) perror("SCHED_FIFO");
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) perror("mlockall");
}

static int open_camera(CameraRig &cam, const AppConfig &cfg, const Args &a)
{
    int vmax = a.vmax >= 0 ? a.vmax : cfg.camera.vmax;
    int exp = a.exp_us >= 0 ? a.exp_us : 0;
    int gain = a.gain >= 0 ? a.gain : 0;
    std::string err = cam.open(cfg.camera.buffers, vmax, exp, gain);
    if (!err.empty()) {
        fprintf(stderr, "camera: %s\n", err.c_str());
        return -1;
    }
    printf("camera open: VMAX %d, %d buffers\n", cam.sensor_vmax(), cfg.camera.buffers);
    return 0;
}

// ---------------------------------------------------------------- probe
static int run_probe(des_hw_t &hw, const AppConfig &cfg, const Args &a)
{
    CameraRig cam(&hw);
    if (open_camera(cam, cfg, a)) return 1;
    CapturedFrame f;
    for (int i = 0; i < 30; i++)   // let exposure / VMAX changes settle
        if (!cam.next(f, false).empty()) return 1;

    double secs = a.n > 0 ? a.n : 10;
    uint64_t frames = 0, skipped = 0, gap_hist[4] = {0, 0, 0, 0};
    double rearm_sum = 0, rearm_max = 0, wait_max = 0;
    double t_start = now_s(), t_sec = t_start;
    uint64_t sec_frames = 0;
    while (!g_stop && now_s() - t_start < secs) {
        std::string err = cam.next(f, false);
        if (!err.empty()) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        frames++;
        sec_frames++;
        uint32_t g = f.pl_gap;
        gap_hist[g >= 3 ? 3 : g]++;
        if (g > 1) skipped += g - 1;
        rearm_sum += f.rearm_us;
        rearm_max = std::max(rearm_max, f.rearm_us);
        wait_max = std::max(wait_max, f.wait_us);
        double t = now_s();
        if (t - t_sec >= 1.0) {
            des_frame_t fr;
            des_frame_read(&hw, &fr);
            printf("%5.1f fps (PS)  %6.2f fps (PL sensor)  skipped so far %llu\n", sec_frames / (t - t_sec),
                   fr.period ? double(DES_FREE_CNT_HZ) / fr.period : 0.0, (unsigned long long)skipped);
            fflush(stdout);
            t_sec = t;
            sec_frames = 0;
        }
    }
    double el = now_s() - t_start;
    des_frame_t fr;
    des_frame_read(&hw, &fr);
    printf("\n== probe: %llu frames in %.2f s = %.2f fps captured; sensor %.3f fps (FRAME_PERIOD %u)\n",
           (unsigned long long)frames, el, frames / el, fr.period ? double(DES_FREE_CNT_HZ) / fr.period : 0.0,
           fr.period);
    printf("   PL frame gap: 1 -> %llu, 2 -> %llu, >=3 -> %llu  (skipped %llu = %.2f%%)\n",
           (unsigned long long)gap_hist[1], (unsigned long long)gap_hist[2], (unsigned long long)gap_hist[3],
           (unsigned long long)skipped, frames ? 100.0 * skipped / (frames + skipped) : 0.0);
    printf("   re-arm after completion: avg %.1f us, max %.1f us; longest wait %.1f ms\n",
           frames ? rearm_sum / frames : 0.0, rearm_max, wait_max / 1000);
    printf("   last frame: PL frame %u, belt pos %d -> %d steps (%s)\n", f.pl_frame, f.step_pos, f.step_pos_end,
           f.latched ? "PL-latched at writer start / done" : "read after completion, pre-v5 bitstream");
    return 0;
}

// ---------------------------------------------------------------- locate
static int run_locate(des_hw_t &hw, const AppConfig &cfg, const Args &a)
{
    CameraRig cam(&hw);
    if (open_camera(cam, cfg, a)) return 1;
    Locator loc(cfg.locate);
    CapturedFrame f;
    int n = a.n > 0 ? int(a.n) : 60;
    double sum_ms = 0;
    for (int i = 0; i < n && !g_stop; i++) {
        std::string err = cam.next(f, true);
        if (!err.empty()) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        LocateTiming tm;
        auto dets = loc.run(f.bgra, f.width, f.height, f.stride, cfg.px_to_table, &tm);
        double ms = (f.sync_us + tm.gray_us + tm.thresh_us + tm.contour_us) / 1000;
        sum_ms += ms;
        printf("frame %u: %zu blobs, thr %d | sync %.1f gray %.1f thr %.1f contour %.1f = %.1f ms\n", f.pl_frame,
               dets.size(), tm.threshold, f.sync_us / 1000, tm.gray_us / 1000, tm.thresh_us / 1000,
               tm.contour_us / 1000, ms);
        if (i == n - 1 || i % 30 == 0)
            for (const Detection &d : dets)
                printf("   x %7.1f y %7.1f mm  len %5.1f wid %5.1f  axis %6.1f deg%s%s\n", d.p.x, d.p.y, d.len, d.wid,
                       d.axis * 180 / kPi, d.partial ? "  partial" : "", d.merged ? "  MERGED" : "");
    }
    printf("== locate: avg %.1f ms per frame\n", sum_ms / n);
    if (!a.dump.empty()) printf("dump %s: %s\n", a.dump.c_str(), loc.dump(a.dump.c_str()) ? "ok" : "FAILED");
    return 0;
}

// ---------------------------------------------------------------- track
static int run_track(des_hw_t &hw, const AppConfig &cfg, const Args &a)
{
    CameraRig cam(&hw);
    if (open_camera(cam, cfg, a)) return 1;
    Locator loc(cfg.locate);
    Tracker tr(cfg.tracker);
    Picker pk(cfg.picker);
    const double mmps = cfg.belt.mm_per_step * cfg.belt.dir;

    if (a.belt != 0) {
        double sps = a.belt / cfg.belt.mm_per_step * cfg.belt.dir;
        des_step_vel(&hw, sps, cfg.belt.accel / cfg.belt.mm_per_step);
        printf("belt: velocity mode %.1f mm/s = %.0f steps/s (dry run: picker belt plans are only printed)\n",
               a.belt, sps);
    }

    std::map<int, std::pair<TrackState, Disposition>> seen;
    BeltPlan last_plan;
    last_plan.kind = BeltPlan::Run;
    double secs = a.n > 0 ? a.n : 30, t_start = now_s(), t_sec = t_start;
    CapturedFrame f;
    while (!g_stop && now_s() - t_start < secs) {
        std::string err = cam.next(f, true);
        if (!err.empty()) { fprintf(stderr, "%s\n", err.c_str()); break; }
        FrameObs obs;
        obs.frame = f.pl_frame;
        // belt position at mid-frame; each part is corrected to the instant its own rows were read out
        double b0 = f.step_pos * mmps, b1 = f.step_pos_end * mmps;
        obs.belt_mm = 0.5 * (b0 + b1);
        obs.dets = loc.run(f.bgra, f.width, f.height, f.stride, cfg.px_to_table);
        for (Detection &d : obs.dets) d.p.x -= float((b1 - b0) * (d.img_v - 0.5f));
        tr.update(obs);

        Picker::Input in;
        in.belt_mm = des_step_pos(&hw) * mmps;
        double v = des_step_speed(&hw) * cfg.belt.mm_per_step;
        in.brake_mm = v * v / (2 * cfg.belt.accel);
        in.belt_stopped = !(des_step_status(&hw) & DES_ST_STATUS_MOVING);
        auto out = pk.tick(tr, in);
        if (out.have_job) {
            printf("[%u] PICK track %d at (%.1f, %.1f) close %.0f deg open %.1f mm -> bin %d (dry run: done)\n",
                   f.pl_frame, out.job.track_id, out.job.p.x, out.job.p.y, out.job.close_axis * 180 / kPi,
                   out.job.open_mm, out.job.bin);
            pk.job_done(tr, out.job.track_id, true);
        }
        if (out.belt.kind != last_plan.kind || std::fabs(out.belt.stop_belt_mm - last_plan.stop_belt_mm) > 0.5) {
            static const char *kinds[] = {"run", "stop-at", "hold"};
            printf("[%u] belt plan: %s %.1f mm (track %d)\n", f.pl_frame, kinds[out.belt.kind],
                   out.belt.stop_belt_mm, out.belt.track_id);
            last_plan = out.belt;
        }
        for (const Track &t : tr.tracks()) {
            auto cur = std::make_pair(t.state, t.disp);
            auto it = seen.find(t.id);
            if (it == seen.end() || it->second != cur) {
                printf("[%u] track %d: %s %s  s %.1f y %.1f len %.1f wid %.1f axis %.0f deg hits %d %s\n", f.pl_frame,
                       t.id, to_str(t.state), to_str(t.disp), t.s, t.y, t.len, t.wid, t.axis * 180 / kPi, t.hits,
                       t.why.c_str());
                seen[t.id] = cur;
            }
        }
        tr.retire(in.belt_mm);
        double tnow = now_s();
        if (tnow - t_sec >= 5.0) {
            printf("-- %llu frames, %zu live tracks, belt %.1f mm, created %llu noise %llu lost %llu, "
                   "pick %llu flap %llu recheck %llu\n",
                   (unsigned long long)tr.stats.frames, tr.tracks().size(), in.belt_mm,
                   (unsigned long long)tr.stats.created, (unsigned long long)tr.stats.noise,
                   (unsigned long long)tr.stats.lost, (unsigned long long)tr.stats.decided_pick,
                   (unsigned long long)tr.stats.decided_flap, (unsigned long long)tr.stats.decided_recheck);
            fflush(stdout);
            t_sec = tnow;
        }
    }
    if (a.belt != 0) des_step_stop(&hw);
    return 0;
}

// ---------------------------------------------------------------- display
// camera -> HDMI only (no overlays): measures the BGRA->BGR conversion + page flip cost (docs/06)
static int run_display(des_hw_t &hw, const AppConfig &cfg, const Args &a)
{
    CameraRig cam(&hw);
    if (open_camera(cam, cfg, a)) return 1;
    HdmiOut hdmi;
    std::string err = hdmi.open();
    if (!err.empty()) { fprintf(stderr, "hdmi: %s\n", err.c_str()); return 1; }

    double secs = a.n > 0 ? a.n : 10, t_start = now_s(), t_sec = t_start;
    double sum_conv = 0, max_conv = 0, sum_flush = 0;
    uint64_t frames = 0, skipped = 0, sec_frames = 0;
    CapturedFrame f;
    char text[96];
    while (!g_stop && now_s() - t_start < secs) {
        err = cam.next(f, true);
        if (!err.empty()) { fprintf(stderr, "%s\n", err.c_str()); break; }
        if (frames) skipped += f.pl_gap - 1;
        double t0 = now_s();
        cv::Mat src(f.height, f.width, CV_8UC4, const_cast<uint8_t *>(f.bgra));
        cv::Mat dst = hdmi.back();
        cv::cvtColor(src, dst, cv::COLOR_BGRA2BGR);
        snprintf(text, sizeof text, "PL frame %u  skipped %llu", f.pl_frame, (unsigned long long)skipped);
        cv::rectangle(dst, cv::Rect(20, 20, 560, 50), cv::Scalar(0, 0, 0), cv::FILLED);
        cv::putText(dst, text, cv::Point(30, 56), cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 255, 0), 2);
        double t1 = now_s();
        hdmi.flip();
        frames++;
        sec_frames++;
        sum_conv += t1 - t0;
        max_conv = std::max(max_conv, t1 - t0);
        sum_flush += hdmi.last_flush_ms() / 1e3;
        if (now_s() - t_sec >= 1.0) {
            printf("%5.1f fps shown, convert avg %.1f ms, skipped %llu\n", sec_frames / (now_s() - t_sec),
                   sum_conv / frames * 1e3, (unsigned long long)skipped);
            fflush(stdout);
            t_sec = now_s();
            sec_frames = 0;
        }
    }
    hdmi.close();
    double el = now_s() - t_start;
    printf("\n== display: %llu frames in %.2f s = %.2f fps; skipped %llu; convert avg %.2f ms max %.2f ms; "
           "flush avg %.2f ms\n",
           (unsigned long long)frames, el, frames / el, (unsigned long long)skipped,
           frames ? sum_conv / frames * 1e3 : 0.0, max_conv * 1e3, frames ? sum_flush / frames * 1e3 : 0.0);
    return 0;
}

// ---------------------------------------------------------------- live
static int run_live_mode(des_hw_t &hw, const AppConfig &cfg, const Args &a)
{
    CameraRig cam(&hw);
    if (open_camera(cam, cfg, a)) return 1;
    LiveOptions opt;
    opt.seconds = a.n;
    opt.belt_mm_s = a.belt;
    opt.dump = a.dump;
    if (a.mode == "collect") {
        if (a.cls.empty()) { fprintf(stderr, "collect needs --cls <label>\n"); return 1; }
        for (size_t i = 0; i < cfg.tracker.rules.size(); i++)
            if (cfg.tracker.rules[i].label == a.cls) opt.collect.label_id = int(i);
        if (opt.collect.label_id < 0) {
            fprintf(stderr, "unknown class '%s'; labels:", a.cls.c_str());
            for (const ClassRule &r : cfg.tracker.rules) fprintf(stderr, " %s", r.label.c_str());
            fprintf(stderr, "\n");
            return 1;
        }
        opt.collect.label = a.cls;
        opt.collect.every = std::max(1, a.every);
        opt.collect.max_per_part = std::max(1, a.maxpp);
        opt.collect.full_every = std::max(0, a.full);
        if (!a.out.empty()) opt.collect.out = a.out;
    }
    if (a.mode == "run") {
        opt.run = true;
        opt.run_opt.can_if = a.can_if;
        opt.run_opt.arm_go = a.go;
        opt.run_opt.speed_mm_s = a.belt;
        opt.belt_mm_s = 0;
    }
    return run_live(hw, cfg, cam, opt, g_stop);
}

// ---------------------------------------------------------------- ik
// ---------------------------------------------------------------- arm
static Vec2 arm_to_table(const ArmConfig &c, Vec3 p)
{
    float cy = std::cos(c.base_yaw), sy = std::sin(c.base_yaw);
    return {c.base_xy.x + cy * p.x - sy * p.y, c.base_xy.y + sy * p.x + cy * p.y};
}

static int run_arm(des_hw_t &hw, const AppConfig &cfg, const Args &a)
{
    const ArmConfig &ac = cfg.arm;
    ArmExec ex(&hw, ac);
    const std::string s = a.sub.empty() ? "scan" : a.sub;
    const bool dry = !a.go;
    auto ok_or = [](int rc) { return rc == DES_OK ? "ok" : des_strerror(rc); };

    if (s == "scan") {
        int alive = 0;
        for (int j = 0; j < 6; j++) {
            ServoInfo i = ex.probe(j);
            if (!i.alive) {
                printf("joint %d (id %u): no reply\n", j + 1, ex.id(j));
                continue;
            }
            alive++;
            float deg = j < 5 ? (i.pos - ac.joint[j].zero_count) / ac.joint[j].counts_per_deg : 0.f;
            printf("joint %d (id %u): pos %4d (%+7.1f deg)  %4d mA  %3d C  status 0x%02x%s%s%s\n", j + 1, ex.id(j), i.pos,
                   deg, i.current_ma, i.temp_c, i.status, i.status & 0x20 ? " OVERLOAD" : "",
                   i.status & 0x04 ? " OVER-TEMP" : "", i.status & 0x01 ? " VOLTAGE" : "");
        }
        printf("%d / 6 servos answered%s\n", alive,
               alive ? "" : " (power, BSS138 wiring, CEP PIN19, ids 1..6? see docs/05)");
        return alive == 6 ? 0 : 2;
    }
    if (s == "read" || s == "teach") {
        std::array<uint16_t, 6> pos{};
        if (!ex.read_positions(pos)) { fprintf(stderr, "not all servos answered (arm scan)\n"); return 2; }
        float grip = 0;
        JointAngles j = arm_from_counts(ac, pos, grip);
        Vec3 tip = arm_fk(ac, j);
        Vec2 tt = arm_to_table(ac, tip);
        printf("counts %u %u %u %u %u | grip %u\n", pos[0], pos[1], pos[2], pos[3], pos[4], pos[5]);
        printf("angles q1..q5 = %.1f %.1f %.1f %.1f %.1f deg, gripper %.1f mm\n", j.q[0], j.q[1], j.q[2], j.q[3], j.q[4],
               grip);
        printf("grasp point: arm (%.1f, %.1f, %.1f) mm, table (%.1f, %.1f) mm, %.1f mm above the belt\n", tip.x, tip.y,
               tip.z, tt.x, tt.y, tip.z - ac.belt_z);
        if (s == "teach")
            printf("paste into des_app.json arm.home_deg or arm.bins_deg:  [%.1f, %.1f, %.1f, %.1f, %.1f]\n", j.q[0], j.q[1],
                   j.q[2], j.q[3], j.q[4]);
        return 0;
    }
    if (s == "torque-on" || s == "torque-off") {
        int rc = ex.torque(s == "torque-on");
        printf("torque %s (broadcast): %s\n", s == "torque-on" ? "on" : "off", ok_or(rc));
        return rc == DES_OK ? 0 : 2;
    }

    // ---- motion
    std::array<uint16_t, 6> now{};
    bool have_now = !dry && ex.read_positions(now);
    if (!have_now) {   // dry run / no read back: assume the arm sits at home
        JointAngles h;
        h.q = ac.home_deg;
        arm_to_counts(ac, h, ac.grip_max_open, now);
    }
    std::vector<ServoFrame> wp;
    if (s == "home" || s == "goto") {
        JointAngles target;
        float grip = ac.grip_max_open;
        if (s == "home") {
            target.q = ac.home_deg;
        } else {
            if (a.pos.size() < 2) { usage(); return 1; }
            float z = a.pos.size() > 2 ? float(a.pos[2]) : ac.z_safe;
            float axis = a.pos.size() > 3 ? float(a.pos[3]) * kPi / 180 : 0.f;
            if (const char *why = arm_ik(ac, table_to_arm(ac, {float(a.pos[0]), float(a.pos[1])}, z),
                                         table_axis_to_arm(ac, axis), target)) {
                fprintf(stderr, "unreachable: %s\n", why);
                return 2;
            }
        }
        ServoFrame f;
        if (const char *why = arm_to_counts(ac, target, grip, f.pos)) { fprintf(stderr, "%s\n", why); return 2; }
        f.ms = 1500;   // single moves from an unknown pose: slow on purpose
        f.what = s == "home" ? "home" : "goto";
        wp.push_back(f);
    } else if (s == "pick") {
        if (a.pos.size() < 2) { usage(); return 1; }
        float axis = a.pos.size() > 2 ? float(a.pos[2]) * kPi / 180 : 0.f;
        int bin = a.pos.size() > 3 ? int(a.pos[3]) : 0;
        if (const char *why = plan_pick(ac, {float(a.pos[0]), float(a.pos[1])}, axis, 40.f, 30.f, bin, now, wp)) {
            fprintf(stderr, "plan_pick: %s\n", why);
            return 2;
        }
    } else {
        usage();
        return 1;
    }
    printf("%s %s, %zu frame(s)%s\n", dry ? "DRY RUN" : "MOVING", s.c_str(), wp.size(),
           dry ? " (add --go to move the arm)" : "");
    ArmExec::Result r = ex.run(wp, dry, [] { printf("  (belt may move again)\n"); });
    printf("done in %.0f ms: %s%s\n", r.ms, r.ok ? "ok" : "FAILED", r.why.empty() ? "" : (" - " + r.why).c_str());
    return r.ok ? 0 : 2;
}

// ---------------------------------------------------------------- calib-cam
static int run_calib(des_hw_t *hw, const AppConfig &cfg, const Args &a)
{
    cv::setNumThreads(1);   // no OpenCV worker threads before detect_board_timeout forks
    BoardSpec b;
    if (sscanf(a.board.c_str(), "%dx%d", &b.cols, &b.rows) != 2 || b.cols < 3 || b.rows < 3) {
        fprintf(stderr, "--board wants <cols>x<rows> inner corners, e.g. 9x6\n");
        return 1;
    }
    if (a.square <= 0) {
        fprintf(stderr, "--square <mm> is required: measure one square on the board with a caliper\n");
        return 1;
    }
    b.square_mm = float(a.square);
    Travel travel;
    if (!parse_travel(a.travel, travel)) { fprintf(stderr, "--travel right|left|down|up\n"); return 1; }

    cv::Mat bgr, gray;
    if (!a.image.empty()) {
        bgr = cv::imread(a.image, cv::IMREAD_COLOR);
        if (bgr.empty()) { fprintf(stderr, "cannot read %s\n", a.image.c_str()); return 1; }
    } else {
        if (!hw) { fprintf(stderr, "PL not available\n"); return 1; }
        CameraRig cam(hw);
        if (open_camera(cam, cfg, a)) return 1;
        CapturedFrame f;
        for (int i = 0; i < 20; i++)   // let exposure settle, keep the last frame
            if (!cam.next(f, i == 19).empty()) return 1;
        cv::Mat src(f.height, f.width, CV_8UC4, const_cast<uint8_t *>(f.bgra));
        cv::cvtColor(src, bgr, cv::COLOR_BGRA2BGR);
    }
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    const double timeout_s = a.exhaustive ? 120 : 20;
    printf("looking for a %dx%d inner-corner board (gives up after %.0f s)...\n", b.cols, b.rows, timeout_s);
    fflush(stdout);
    double t0 = now_s();
    std::vector<cv::Point2f> corners;
    int found = detect_board_timeout(gray, b, a.exhaustive, timeout_s, corners);
    CalibResult r;
    if (found == 1) {
        r = calibrate_from_corners(corners, gray.size(), b, travel);
    } else {
        r.err = found < 0 ? "no board after the time limit: is --board the INNER corner count (squares - 1)? "
                            "is the whole board in view and in focus?"
                          : "board not found (check --board inner-corner count, focus, lighting; whole board in view)";
    }
    r.detect_ms = (now_s() - t0) * 1e3;
    if (!r.ok) {
        fprintf(stderr, "calibration failed: %s\n", r.err.c_str());
        if (!a.dump.empty()) cv::imwrite(a.dump, bgr);
        return 2;
    }
    printf("board %dx%d inner corners, %.3f mm squares, travel %s: found %zu corners in %.0f ms\n", b.cols, b.rows,
           b.square_mm, to_str(travel), r.corners.size(), r.detect_ms);
    printf("fit residual rms %.3f mm, max %.3f mm; %.4f mm/px at the center\n", r.rms_mm, r.max_mm, r.mm_per_px);
    printf("FOV along the belt x %.1f .. %.1f mm; corners (table mm):", r.fov_x0, r.fov_x1);
    for (const Vec2 &v : r.fov_corners) printf(" (%.1f, %.1f)", v.x, v.y);
    printf("\n");
    std::string out = a.out;
    if (out.empty()) {
        size_t k = a.config.find_last_of('/');
        out = (k == std::string::npos ? std::string(".") : a.config.substr(0, k)) + "/calib.json";
    }
    std::string err = save_calib(out, r, b, travel);
    printf("saved %s: %s\n", out.c_str(), err.empty() ? "ok" : err.c_str());
    if (!a.dump.empty()) {
        draw_calib(bgr, r);
        printf("dump %s: %s\n", a.dump.c_str(), cv::imwrite(a.dump, bgr) ? "ok" : "FAILED");
    }
    return err.empty() ? 0 : 1;
}

// ---------------------------------------------------------------- calib-belt
// Belt scale and direction seen by the calibrated camera. The part's table position is the median over still frames
// (largest blob not touching the image border). Moves: a short one (~8 mm with the configured estimate) for a rough
// scale and the direction, a long one to ~75 % of the view, then back to the start (slip / backlash). The result
// uses the whole travel, so the position noise (~0.05 mm) is < 0.1 % of it; the camera scale error adds to it.
static std::string still_position(CameraRig &cam, Locator &loc, const AppConfig &cfg, Vec2 &p)
{
    CapturedFrame f;
    std::string err;
    for (int i = 0; i < cfg.camera.buffers + 2; i++)   // the ring may still hold frames exposed during the move
        if (!(err = cam.next(f, false)).empty()) return err;
    std::vector<float> xs, ys;
    int blobs = 0;
    for (int i = 0; i < 7; i++) {
        if (!(err = cam.next(f, true)).empty()) return err;
        std::vector<Detection> d = loc.run(f.bgra, f.width, f.height, f.stride, cfg.px_to_table);
        const Detection *best = nullptr;
        for (const Detection &x : d)
            if (!x.partial && (!best || x.len * x.wid > best->len * best->wid)) best = &x;
        blobs = std::max(blobs, int(d.size()));
        if (best) {
            xs.push_back(best->p.x);
            ys.push_back(best->p.y);
        }
    }
    if (xs.size() < 4) return "no part fully in view (one part on the belt, inside the view, threshold ok? try locate)";
    if (blobs > 1) printf("  note: %d blobs in view, using the largest\n", blobs);
    std::nth_element(xs.begin(), xs.begin() + xs.size() / 2, xs.end());
    std::nth_element(ys.begin(), ys.begin() + ys.size() / 2, ys.end());
    p = {xs[xs.size() / 2], ys[ys.size() / 2]};
    return "";
}

static std::string belt_move(des_hw_t &hw, int32_t steps, double vmax, double acc)
{
    const int32_t target = des_step_pos(&hw) + steps;
    des_step_move(&hw, target, vmax, acc);
    const double t0 = now_s(), tmax = std::abs(steps) / vmax + 2 * vmax / acc + 3;
    usleep(20000);
    while (des_step_status(&hw) & DES_ST_STATUS_MOVING) {
        if (g_stop || now_s() - t0 > tmax) {
            des_step_stop(&hw);
            return g_stop ? "interrupted" : "move did not finish in time";
        }
        usleep(5000);
    }
    if (des_step_status(&hw) & DES_ST_STATUS_HALTED) return "stepper halted (e-stop input?)";
    return des_step_pos(&hw) == target ? "" : "stepper stopped short of the target";
}

static int run_calib_belt(des_hw_t &hw, const AppConfig &cfg, const Args &a)
{
    if (cfg.calib_loaded.empty()) {
        fprintf(stderr, "calibrate the camera first (des_app calib-cam): belt travel is measured in table mm\n");
        return 1;
    }
    CameraRig cam(&hw);
    if (open_camera(cam, cfg, a)) return 1;
    Locator loc(cfg.locate);
    const double est = std::fabs(cfg.belt.mm_per_step), mm_s = a.belt > 0 ? a.belt : 20.0;
    const double vmax = mm_s / est, acc = vmax * 2;   // 0.5 s ramps
    const int dir0 = cfg.belt.dir >= 0 ? 1 : -1;      // configured guess: + steps move parts downstream
    des_step_enable(&hw, 1);
    usleep(100000);
    auto fail = [&](const std::string &e) {
        fprintf(stderr, "calib-belt: %s\n", e.c_str());
        des_step_stop(&hw);
        return 2;
    };
    Vec2 p0, p1, p2, p3;
    std::string e = still_position(cam, loc, cfg, p0);
    if (!e.empty()) return fail(e);
    printf("start: part at x %.2f y %.2f mm (view x %.0f .. %.0f); estimate %.5f mm/step, %.0f mm/s\n", p0.x, p0.y,
           cfg.tracker.fov_x0, cfg.tracker.fov_x1, est, mm_s);
    // 1. short move downstream (with the configured direction guess)
    const int32_t s1 = dir0 * int32_t(std::lround(8.0 / est));
    if (!(e = belt_move(hw, s1, vmax, acc)).empty() || !(e = still_position(cam, loc, cfg, p1)).empty()) return fail(e);
    const double rough = std::fabs(p1.x - p0.x) / std::abs(s1);
    printf("short move %d steps: x %.2f -> %.2f mm (%.5f mm/step)\n", s1, p0.x, p1.x, rough);
    if (std::fabs(p1.x - p0.x) < 1.0)
        return fail("the part did not move: is the driver powered and enabled, the belt tensioned?");
    const int s_dir = (p1.x > p0.x) == (s1 > 0) ? 1 : -1;   // sign of steps that move parts downstream
    // 2. long move downstream to ~75 % of the view (or --steps)
    int32_t s2 = a.steps;
    if (!s2) {
        const double room = cfg.tracker.fov_x0 + 0.75 * (cfg.tracker.fov_x1 - cfg.tracker.fov_x0) - p1.x;
        if (room < 20) return fail("not enough room downstream: put the part near the upstream edge of the view");
        s2 = s_dir * int32_t(std::lround(room / rough));
    }
    if (!(e = belt_move(hw, s2, vmax, acc)).empty() || !(e = still_position(cam, loc, cfg, p2)).empty()) return fail(e);
    printf("long move %d steps: x %.2f -> %.2f mm\n", s2, p1.x, p2.x);
    // 3. back to the start
    if (!(e = belt_move(hw, -(s1 + s2), vmax, acc)).empty() || !(e = still_position(cam, loc, cfg, p3)).empty())
        return fail(e);
    printf("back %d steps: x %.2f (start %.2f)\n", -(s1 + s2), p3.x, p0.x);
    BeltCalib b = belt_from_positions(p0, p1, p2, p3, s1, s2);
    if (!b.ok) return fail(b.err);
    printf("== belt: %.6f mm/step, dir %+d (%.1f mm over %d steps); effective roller %.2f mm at 3200 steps/rev\n",
           b.mm_per_step, b.dir, b.travel_mm, b.steps, b.roller_d_mm);
    printf("   return error %+.3f mm after %.1f mm forward + back; lateral drift %+.2f mm per 100 mm\n",
           b.return_err_mm, b.travel_mm, b.drift_mm_per_100);
    if (std::fabs(b.mm_per_step - est) > 0.3 * est)
        printf("   (config estimate was %.5f: %+.0f %%)\n", est, (b.mm_per_step / est - 1) * 100);
    if (std::fabs(b.drift_mm_per_100) > 2)
        printf("   lateral drift > 2 mm / 100 mm: the camera x axis is not along the belt (re-run calib-cam with the "
               "board edge against the side guard) or the belt runs off\n");
    std::string out = a.out.empty() ? cfg.calib_loaded : a.out;
    e = save_belt_calib(out, b);
    printf("saved %s: %s\n", out.c_str(), e.empty() ? "ok" : e.c_str());
    return e.empty() ? 0 : 1;
}

// ---------------------------------------------------------------- calib-arm
// Hand-eye: where the arm stands on the table (base_xy, base_yaw) and how high the belt is in the arm frame, from
// >= 3 marker positions that the camera sees and the gripper tip touches. Interactive (Enter between the steps).
// The joint zeros / directions must be right first (arm read = geometric angles, docs/04 bring-up step 3).
static bool wait_enter(const char *prompt)
{
    printf("%s ", prompt);
    fflush(stdout);
    char line[64];
    if (!fgets(line, sizeof line, stdin)) return false;
    return line[0] != 'q' && line[0] != 'Q';
}

static int run_calib_arm(des_hw_t &hw, const AppConfig &cfg, const Args &a)
{
    if (cfg.calib_loaded.empty()) {
        fprintf(stderr, "calibrate the camera first (des_app calib-cam): marker positions are table mm\n");
        return 1;
    }
    const int n = a.n >= 3 ? int(a.n) : 4;
    ArmExec ex(&hw, cfg.arm);
    std::array<uint16_t, 6> pos{};
    if (!ex.read_positions(pos)) { fprintf(stderr, "not all servos answered (des_app arm scan)\n"); return 2; }
    CameraRig cam(&hw);
    if (open_camera(cam, cfg, a)) return 1;
    Locator loc(cfg.locate);
    printf("Hand-eye calibration, %d points. Marker: a small flat part (or a 10~20 mm disc) whose centre you can\n"
           "touch with the gripper tip; spread the points over the part of the view the arm reaches.\n"
           "The arm torque goes OFF now - hold the arm.\n", n);
    if (!wait_enter("Enter to switch the torque off (q quits)")) return 1;
    ex.torque(false);
    std::vector<Vec3> arm_pts;
    std::vector<Vec2> tab_pts;
    for (int i = 0; i < n && !g_stop; i++) {
        char msg[200];
        snprintf(msg, sizeof msg, "[%d/%d] put the marker in view (reachable), move the arm out of the picture, Enter:",
                 i + 1, n);
        if (!wait_enter(msg)) break;
        Vec2 p;
        std::string e = still_position(cam, loc, cfg, p);
        if (!e.empty()) { printf("  %s - again\n", e.c_str()); i--; continue; }
        printf("  marker at table (%.1f, %.1f) mm\n", p.x, p.y);
        if (!wait_enter("  touch the marker centre with the gripper tip (between the fingers), Enter:")) break;
        if (!ex.read_positions(pos)) { printf("  servo read failed - again\n"); i--; continue; }
        float grip = 0;
        JointAngles j = arm_from_counts(cfg.arm, pos, grip);
        Vec3 tip = arm_fk(cfg.arm, j);
        printf("  tip at arm (%.1f, %.1f, %.1f) mm (q %.1f %.1f %.1f %.1f)\n", tip.x, tip.y, tip.z, j.q[0], j.q[1],
               j.q[2], j.q[3]);
        arm_pts.push_back(tip);
        tab_pts.push_back(p);
    }
    ArmBaseFit f = fit_arm_base(arm_pts, tab_pts, a.marker_h > 0 ? a.marker_h : 0.f);
    if (!f.ok) { fprintf(stderr, "calib-arm: %s\n", f.err.c_str()); return 2; }
    for (size_t i = 0; i < f.resid.size(); i++)
        printf("  point %zu: table (%.1f, %.1f) residual %.2f mm\n", i + 1, tab_pts[i].x, tab_pts[i].y, f.resid[i]);
    printf("== arm base at table (%.1f, %.1f) mm, arm +x points %.1f deg from table +x; residual rms %.2f, max %.2f mm\n",
           f.base_xy.x, f.base_xy.y, f.base_yaw * 180 / kPi, f.rms, f.max);
    if (a.marker_h >= 0) printf("   belt surface %.1f mm above the arm base plate (marker %.1f mm)\n", f.belt_z, a.marker_h);
    else printf("   belt_z not fitted (give --marker-h to fit it)\n");
    if (f.max > 3)
        printf("   max residual > 3 mm: a marker moved, a touch was off, or joint zeros / link lengths are wrong\n");
    std::string out = a.out.empty() ? cfg.calib_loaded : a.out;
    std::string e = save_arm_calib(out, f, a.marker_h >= 0);
    printf("saved %s: %s. Torque is still OFF: des_app arm torque-on before moving.\n", out.c_str(),
           e.empty() ? "ok" : e.c_str());
    return e.empty() ? 0 : 1;
}

// ---------------------------------------------------------------- measure
// One frame (or -n frames of still parts) -> locate -> measure every gear / rack. For accuracy work with real
// parts: compare with the caliper values, report the repeatability, keep the raw frame to re-run offline.
static int run_measure(des_hw_t *hw, const AppConfig &cfg, const Args &a)
{
    cv::Mat bgra, gray;
    int frames = a.n > 0 ? int(a.n) : 1;
    std::unique_ptr<CameraRig> cam;
    if (!a.image.empty()) {
        cv::Mat img = cv::imread(a.image, cv::IMREAD_COLOR);
        if (img.empty()) { fprintf(stderr, "cannot read %s\n", a.image.c_str()); return 1; }
        cv::cvtColor(img, bgra, cv::COLOR_BGR2BGRA);
        cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
        frames = 1;
    } else {
        cam.reset(new CameraRig(hw));
        if (open_camera(*cam, cfg, a)) return 1;
        CapturedFrame f;
        for (int i = 0; i < 30; i++)   // exposure settles
            if (!cam->next(f, false).empty()) return 1;
    }
    Locator loc(cfg.locate);
    RepeatStats st;
    std::vector<PartResult> last;
    double sum_ms = 0;
    for (int i = 0; i < frames && !g_stop; i++) {
        float mm_per_row = 0;
        if (cam) {
            CapturedFrame f;
            std::string err = cam->next(f, true);
            if (!err.empty()) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
            // copy out of the DMA ring (it is rewritten a few frames later)
            bgra = cv::Mat(f.height, f.width, CV_8UC4, const_cast<uint8_t *>(f.bgra), size_t(f.stride)).clone();
            cv::cvtColor(bgra, gray, cv::COLOR_BGRA2GRAY);
            mm_per_row = float((f.step_pos_end - f.step_pos) * cfg.belt.mm_per_step * cfg.belt.dir / f.height);
        }
        double t0 = now_s();
        auto dets = loc.run(bgra.data, bgra.cols, bgra.rows, int(bgra.step), cfg.px_to_table);
        last = measure_parts(gray, dets, cfg, mm_per_row);
        sum_ms += (now_s() - t0) * 1e3;
        if (i == 0 || i == frames - 1) {
            printf("frame %d: %zu blobs, %zu measured\n", i, dets.size(), last.size());
            for (const PartResult &r : last) printf("   %s\n", describe(r).c_str());
        }
        st.add(last);
    }
    printf("== measure: %d frame(s), locate + measure %.1f ms per frame\n", frames, sum_ms / std::max(1, frames));
    if (frames > 1) st.print();
    if (!a.out.empty()) printf("raw frame %s: %s\n", a.out.c_str(), cv::imwrite(a.out, gray) ? "ok" : "FAILED");
    if (!a.dump.empty() && !bgra.empty()) {
        cv::Mat bgr;
        cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
        draw_parts(bgr, last, cfg.px_to_table);
        printf("report %s: %s\n", a.dump.c_str(), cv::imwrite(a.dump, bgr) ? "ok" : "FAILED");
    }
    return 0;
}

static int run_ik(const AppConfig &cfg, const Args &a)
{
    if (a.pos.size() < 2) { usage(); return 1; }
    Vec2 p{float(a.pos[0]), float(a.pos[1])};
    float z = a.pos.size() > 2 ? float(a.pos[2]) : cfg.arm.z_grasp;
    float axis = a.pos.size() > 3 ? float(a.pos[3]) * kPi / 180 : 0.f;
    Vec3 pa = table_to_arm(cfg.arm, p, z);
    JointAngles j;
    const char *why = arm_ik(cfg.arm, pa, table_axis_to_arm(cfg.arm, axis), j);
    printf("table (%.1f, %.1f, z %.1f) -> arm (%.1f, %.1f, %.1f)\n", p.x, p.y, z, pa.x, pa.y, pa.z);
    printf("q = %.1f %.1f %.1f %.1f %.1f deg  %s\n", j.q[0], j.q[1], j.q[2], j.q[3], j.q[4], why ? why : "ok");
    if (why) return 2;
    std::array<uint16_t, 6> pos{};
    why = arm_to_counts(cfg.arm, j, cfg.arm.grip_max_open, pos);
    printf("counts = %u %u %u %u %u %u  %s\n", pos[0], pos[1], pos[2], pos[3], pos[4], pos[5], why ? why : "");
    Vec3 b = arm_fk(cfg.arm, j);
    printf("fk check: (%.2f, %.2f, %.2f)\n", b.x, b.y, b.z);
    return why ? 2 : 0;
}

int main(int argc, char **argv)
{
    Args a;
    if (!parse(argc, argv, a)) { usage(); return 1; }

    AppConfig cfg;
    std::string err = load_config(a.config, cfg);
    if (!err.empty()) fprintf(stderr, "config: %s (using defaults)\n", err.c_str());
    if (!cfg.calib_loaded.empty()) printf("config: camera calibration from %s\n", cfg.calib_loaded.c_str());
    else if (a.mode != "calib-cam") printf("config: no %s, camera mapping is the uncalibrated placeholder\n",
                                            cfg.calib_file.c_str());
    if (a.mode == "ik") return run_ik(cfg, a);
    if (a.mode == "calib-cam" && !a.image.empty()) return run_calib(nullptr, cfg, a);
    if (a.mode == "measure" && !a.image.empty()) return run_measure(nullptr, cfg, a);

    des_hw_t hw;
    int rc = des_hw_open(&hw);
    if (rc) {
        fprintf(stderr, "PL: %s\n", des_strerror(rc));
        return 1;
    }
    printf("PL des_regs VERSION %u\n", hw.version);
    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);
    if (a.rt && cfg.camera.realtime) go_realtime();

    if (a.mode == "probe") rc = run_probe(hw, cfg, a);
    else if (a.mode == "locate") rc = run_locate(hw, cfg, a);
    else if (a.mode == "track") rc = run_track(hw, cfg, a);
    else if (a.mode == "display") rc = run_display(hw, cfg, a);
    else if (a.mode == "live" || a.mode == "run" || a.mode == "collect") rc = run_live_mode(hw, cfg, a);
    else if (a.mode == "calib-cam") rc = run_calib(&hw, cfg, a);
    else if (a.mode == "calib-belt") rc = run_calib_belt(hw, cfg, a);
    else if (a.mode == "calib-arm") rc = run_calib_arm(hw, cfg, a);
    else if (a.mode == "arm") rc = run_arm(hw, cfg, a);
    else if (a.mode == "measure") rc = run_measure(&hw, cfg, a);
    else { usage(); rc = 1; }
    des_hw_close(&hw);
    return rc;
}
