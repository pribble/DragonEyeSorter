// des_app — DragonEyeSorter PS application skeleton.
//
//   des_app probe  [-n sec] [--vmax N] [--exp us] [--gain dB*10]   capture timing: fps, skipped frames, re-arm latency
//   des_app locate [-n frames] [--dump file.png]                    capture + localization, prints blobs
//   des_app track  [-n sec] [--belt mm/s]                           capture + locate + tracking table + picker, no arm
//   des_app ik     <x> <y> [z] [axis_deg]                           table-frame target -> joint angles / servo counts
//   des_app display [-n sec]                                        camera -> HDMI at 60 fps (BGRA->BGR + page flip), timing
// common: -c <config.json> (default /root/des/des_app.json), --no-rt
//
// Bring-up order: probe (60 fps without skips) -> locate (thresholds, homography) -> track (belt + tracker)
// -> arm (after the servos are wired). The NPU pipeline from vision/ attaches classes to detections later.
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include <opencv2/imgproc.hpp>

#include "camera_rig.hpp"
#include "flow_common_types.hpp"
#include "config.hpp"
#include "des_hw.h"
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
    double n = 0;
    int vmax = -1, exp_us = -1, gain = -1;
    bool rt = true;
    double belt = 0;
    std::vector<double> pos;
};

static void usage()
{
    fprintf(stderr,
            "usage: des_app probe|locate|track|display|ik [options]\n"
            "  -c cfg.json  -n N  --vmax N  --exp us  --gain dB*10  --dump file.png  --belt mm/s  --no-rt\n");
}

static bool parse(int argc, char **argv, Args &a)
{
    for (int i = 1; i < argc; i++) {
        std::string k = argv[i];
        auto val = [&](void) -> const char * { return i + 1 < argc ? argv[++i] : nullptr; };
        const char *v = nullptr;
        if (k == "--no-rt") a.rt = false;
        else if (k == "-c" && (v = val())) a.config = v;
        else if (k == "-n" && (v = val())) a.n = atof(v);
        else if (k == "--vmax" && (v = val())) a.vmax = atoi(v);
        else if (k == "--exp" && (v = val())) a.exp_us = atoi(v);
        else if (k == "--gain" && (v = val())) a.gain = atoi(v);
        else if (k == "--dump" && (v = val())) a.dump = v;
        else if (k == "--belt" && (v = val())) a.belt = atof(v);
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
// HDMI scan-out is the vendor "vdma" at 0x8020_0000 reading 24 bpp BGR (H_SIZE / stride 5760) from START_1
// (0x5C, normally the fb0 memory 0x17D0_0000). The camera writes 32 bpp BGRA, so every frame is converted
// into one of two BGR buffers and START_1 is pointed at it (page flip). Restored on exit.
static constexpr uint32_t kVdmaBase = 0x80200000u, kVdmaStart1 = 0x5C;

static int run_display(des_hw_t &hw, const AppConfig &cfg, const Args &a)
{
    CameraRig cam(&hw);
    if (open_camera(cam, cfg, a)) return 1;
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    void *vp = fd >= 0 ? mmap(nullptr, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, kVdmaBase) : MAP_FAILED;
    if (vp == MAP_FAILED) { perror("map vdma"); return 1; }
    volatile uint32_t *vdma = static_cast<volatile uint32_t *>(vp);
    const uint32_t start_orig = vdma[kVdmaStart1 / 4];
    printf("vdma: V %u lines, H %u bytes, stride %u, START_1 0x%08x\n", vdma[0x50 / 4], vdma[0x54 / 4],
           vdma[0x58 / 4], start_orig);
    if (vdma[0x54 / 4] != 1920 * 3) { fprintf(stderr, "display is not 1920 x 24 bpp, refusing\n"); return 1; }

    CmaBuf out[2] = {CmaBuf(1920 * 1080 * 3), CmaBuf(1920 * 1080 * 3)};
    for (auto &b : out)
        if (!b || !b->phys_addr || (b->phys_addr >> 32)) { fprintf(stderr, "CMA alloc for display failed\n"); return 1; }

    double secs = a.n > 0 ? a.n : 10, t_start = now_s(), t_sec = t_start;
    double sum_conv = 0, max_conv = 0, sum_flush = 0;
    uint64_t frames = 0, skipped = 0, sec_frames = 0;
    int k = 0;
    CapturedFrame f;
    char text[96];
    while (!g_stop && now_s() - t_start < secs) {
        std::string err = cam.next(f, true);
        if (!err.empty()) { fprintf(stderr, "%s\n", err.c_str()); break; }
        if (frames) skipped += f.pl_gap - 1;
        double t0 = now_s();
        cv::Mat src(f.height, f.width, CV_8UC4, const_cast<uint8_t *>(f.bgra));
        cv::Mat dst(1080, 1920, CV_8UC3, out[k]->virt_addr);
        cv::cvtColor(src, dst, cv::COLOR_BGRA2BGR);
        snprintf(text, sizeof text, "PL frame %u  skipped %llu", f.pl_frame, (unsigned long long)skipped);
        cv::rectangle(dst, cv::Rect(20, 20, 560, 50), cv::Scalar(0, 0, 0), cv::FILLED);
        cv::putText(dst, text, cv::Point(30, 56), cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 255, 0), 2);
        double t1 = now_s();
        out[k].sync_device();
        vdma[kVdmaStart1 / 4] = uint32_t(out[k]->phys_addr);
        double t2 = now_s();
        k ^= 1;
        frames++;
        sec_frames++;
        sum_conv += t1 - t0;
        max_conv = std::max(max_conv, t1 - t0);
        sum_flush += t2 - t1;
        if (t2 - t_sec >= 1.0) {
            printf("%5.1f fps shown, convert avg %.1f ms, skipped %llu\n", sec_frames / (t2 - t_sec),
                   sum_conv / frames * 1e3, (unsigned long long)skipped);
            fflush(stdout);
            t_sec = t2;
            sec_frames = 0;
        }
    }
    vdma[kVdmaStart1 / 4] = start_orig;   // back to the fb0 console
    double el = now_s() - t_start;
    printf("\n== display: %llu frames in %.2f s = %.2f fps; skipped %llu; convert avg %.2f ms max %.2f ms; "
           "flush avg %.2f ms\n",
           (unsigned long long)frames, el, frames / el, (unsigned long long)skipped,
           frames ? sum_conv / frames * 1e3 : 0.0, max_conv * 1e3, frames ? sum_flush / frames * 1e3 : 0.0);
    munmap(vp, 0x1000);
    close(fd);
    return 0;
}

// ---------------------------------------------------------------- ik
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
    if (a.mode == "ik") return run_ik(cfg, a);

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
    else { usage(); rc = 1; }
    des_hw_close(&hw);
    return rc;
}
