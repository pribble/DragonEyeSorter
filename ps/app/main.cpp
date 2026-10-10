// des_app — DragonEyeSorter PS application skeleton.
//
//   des_app probe  [-n sec] [--vmax N] [--exp us] [--gain dB*10]   capture timing: fps, skipped frames, re-arm latency
//   des_app locate [-n frames] [--dump file.png]                    capture + localization, prints blobs
//   des_app track  [-n sec] [--belt mm/s]                           capture + locate + tracking table + picker, no arm
//   des_app ik     <x> <y> [z] [axis_deg]                           table-frame target -> joint angles / servo counts
//   des_app display [-n sec]                                        camera -> HDMI at 60 fps (BGRA->BGR + page flip), timing
//   des_app live   [-n sec] [--belt mm/s]                           60 fps HDMI with locate / tracking overlays (2 cores)
//   des_app calib-cam [--board 9x6] [--square mm] [--travel right|left|down|up] [--image f.png] [--out calib.json]
//                                                                   checkerboard on the belt -> px_to_table (calib.json)
// common: -c <config.json> (default /root/des/des_app.json), --no-rt
//
// Bring-up order: probe (60 fps without skips) -> locate (thresholds, homography) -> track (belt + tracker)
// -> arm (after the servos are wired). The NPU pipeline from vision/ attaches classes to detections later.
#include <sched.h>
#include <signal.h>
#include <sys/mman.h>
#include <time.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include <opencv2/imgproc.hpp>

#include <opencv2/core/utility.hpp>
#include <opencv2/imgcodecs.hpp>

#include "calib.hpp"
#include "camera_rig.hpp"
#include "config.hpp"
#include "des_hw.h"
#include "hdmi_out.hpp"
#include "live.hpp"
#include "locate.hpp"

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
    double n = 0;
    int vmax = -1, exp_us = -1, gain = -1;
    bool rt = true;
    double belt = 0;
    std::vector<double> pos;
};

static void usage()
{
    fprintf(stderr,
            "usage: des_app probe|locate|track|display|live|calib-cam|ik [options]\n"
            "  -c cfg.json  -n N  --vmax N  --exp us  --gain dB*10  --dump file.png  --belt mm/s  --no-rt\n"
            "  calib-cam: --board <cols>x<rows> (inner corners)  --square <mm>  --travel right|left|down|up\n"
            "             --image <file> (instead of the camera)  --out <calib.json>  --exhaustive\n");
}

static bool parse(int argc, char **argv, Args &a)
{
    for (int i = 1; i < argc; i++) {
        std::string k = argv[i];
        auto val = [&](void) -> const char * { return i + 1 < argc ? argv[++i] : nullptr; };
        const char *v = nullptr;
        if (k == "--no-rt") a.rt = false;
        else if (k == "--exhaustive") a.exhaustive = true;
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
        else if (a.mode.empty() && k[0] != '-') a.mode = k;   // first plain word is the mode
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
    return run_live(hw, cfg, cam, opt, g_stop);
}

// ---------------------------------------------------------------- ik
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
    else if (a.mode == "live") rc = run_live_mode(hw, cfg, a);
    else if (a.mode == "calib-cam") rc = run_calib(&hw, cfg, a);
    else { usage(); rc = 1; }
    des_hw_close(&hw);
    return rc;
}
