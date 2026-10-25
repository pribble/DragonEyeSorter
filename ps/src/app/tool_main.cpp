// tool_main.cpp — des_tool: the operator entry point. Modes: probe | locate | track | display | collect | measure |
//   calib-cam | calib-belt | calib-enc | calib-beam | calib-flat | calib-arm | arm (scan|read|teach|torque-*|home|goto|pick) | ik,
//   --version; options: -c cfg.json -n N --vmax N --exp us --gain dB*10 --dump f.png --belt mm/s --no-rt --out …
// Human-driven (prints measurements, writes calib.json / training crops, can move hardware with --go / calib-*), so it is
// NOT part of the deployed product binary (run / live = main.cpp -> des_app); pass/fail checks are the test_* binaries.
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <opencv2/core/utility.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include "arm_io.hpp"
#include "calib.hpp"
#include "camera_rig.hpp"
#include "cli_common.hpp"
#include "config.hpp"
#include "des_hw.h"
#include "display.hpp"
#include "flat_field.hpp"
#include "live.hpp"
#include "locate.hpp"
#include "measure_tool.hpp"
#include "tool_util.hpp"
using namespace des;
// des_tool's usage text lives here (not in cli_common.cpp) so that the product binary does not carry the operator command list: only this entry translation unit references it.
static void usage_tool()
{
    fprintf(stderr,
            "usage: des_tool probe|locate|track|display|collect|measure|calib-cam|calib-belt|calib-enc|calib-beam|calib-flat|calib-arm|arm|ik\n       [options]\n  -c cfg.json  -n N  --vmax N  --exp us  --gain dB*10  --dump file.png  --belt mm/s  --no-rt\n"
            "  calib-beam: ONE gear in view upstream of the through-beam (IR1); -n rounds (default 3) down across the\n             beam and back at --belt mm/s (default 20)  --od <caliper OD>  --steps <move>  -> calib.json \"beam\"\n"
            "  calib-cam: --board <cols>x<rows> (inner corners)  --square <mm>  --travel right|left|down|up\n             --image <file> (instead of the camera)  --out <calib.json>  --exhaustive\n  calib-belt: ONE part on the belt near the upstream edge (camera calibrated first); the belt moves\n"
            "             forward and back. --steps <long move>  --belt <mm/s> (default 20)  --out <calib.json>\n  calib-enc: EMPTY belt; moves --steps (default 300 mm) downstream and back, -n rounds, at --belt mm/s\n"
            "             (default belt.run_speed): measuring-wheel encoder vs stepper -> calib.json \"encoder\"\n  calib-flat: a blank white sheet covering the upstream edge of the view; the belt carries it across\n"
            "             in --steps steps (default 40 mm) and back: white level of the view -> flat.json (crops)\n  calib-arm: interactive hand-eye calibration (camera calibrated, joint zeros right): -n <points, >= 3>\n"
            "             --carry <mm> (belt carries each located marker downstream to the arm before the touch)\n             --marker-h <mm> (marker top above the belt; also fits arm.belt_z)  --out <calib.json>\n"
            "  arm: scan | read | teach | torque-on | torque-off | home | goto x y [z] [axis] | pick x y [axis] [bin]\n       motion commands only print the servo frames unless --go\n  collect: --cls <label> (one class per session)  --every <frames>  --max <crops per part>\n"
            "           --full <frames> (also full frames + YOLO boxes)  --out <dir>  --belt <mm/s>\n  measure: -n <frames> (still parts: repeatability)  --image <file> (instead of the camera)\n           --out <raw.png> (keep the frame for --image later)  --dump <report.png>\n"
            "  --version  print the build date/time\nproduct runtime (run / live) is des_app\n");
}
static int run_probe(des_hw_t &hw, const AppConfig &cfg, const Args &a)
{
    CameraSession cam(&hw, cfg, a);
    if (!cam.ok()) return 1;
    CapturedFrame f;
    if (!capture_frames(*cam, 30, f).empty()) return 1;   // let exposure / VMAX changes settle
    double secs = a.n > 0 ? a.n : 10;
    uint64_t frames = 0, skipped = 0, gap_hist[4] = {0, 0, 0, 0}, rearm_sum = 0; uint32_t rearm_max = 0, wait_max = 0;
    double t_start = now_s(), t_sec = t_start;
    uint64_t sec_frames = 0;
    while (!g_stop && now_s() - t_start < secs) {
        std::string err = cam->next(f, false);
        if (!err.empty()) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        frames++; sec_frames++;
        uint32_t g = f.pl_gap; gap_hist[g >= 3 ? 3 : g]++;
        if (g > 1) skipped += g - 1;
        rearm_sum += f.rearm_us; rearm_max = std::max(rearm_max, f.rearm_us);
        wait_max = std::max(wait_max, f.wait_us);
        double t = now_s();
        if (t - t_sec >= 1.0) {
            des_frame_t fr; des_frame_read(&hw, &fr);
            printf("%5.1f fps (PS)  %6.2f fps (PL sensor)  skipped so far %llu\n", sec_frames / (t - t_sec), fr.period ? double(DES_FREE_CNT_HZ) / fr.period : 0.0, (unsigned long long)skipped);
            fflush(stdout); t_sec = t;
            sec_frames = 0;
        }
    }
    double el = now_s() - t_start;
    des_frame_t fr; des_frame_read(&hw, &fr);
    printf("\n== probe: %llu frames in %.2f s = %.2f fps captured; sensor %.3f fps (FRAME_PERIOD %u)\n", (unsigned long long)frames, el, frames / el, fr.period ? double(DES_FREE_CNT_HZ) / fr.period : 0.0, fr.period);
    printf("   PL frame gap: 1 -> %llu, 2 -> %llu, >=3 -> %llu  (skipped %llu = %.2f%%)\n", (unsigned long long)gap_hist[1], (unsigned long long)gap_hist[2], (unsigned long long)gap_hist[3], (unsigned long long)skipped, frames ? 100.0 * skipped / (frames + skipped) : 0.0);
    printf("   re-arm after completion: avg %.1f us, max %.1f us; longest wait %.1f ms\n", frames ? double(rearm_sum) / double(frames) : 0.0, double(rearm_max), wait_max / 1e3);
    printf("   last frame: PL frame %u, belt pos %d -> %d steps (%s)\n", f.pl_frame, f.step_pos, f.step_pos_end, f.latched ? "PL-latched at writer start / done" : "read after completion, pre-v5 bitstream");
    return 0;
}
static int run_locate(des_hw_t &hw, const AppConfig &cfg, const Args &a)
{
    CameraSession cam(&hw, cfg, a);
    if (!cam.ok()) return 1;
    Locator loc(cfg.locate); CapturedFrame f;
    int n = a.n > 0 ? int(a.n) : 60;
    double sum_ms = 0;
    for (int i = 0; i < n && !g_stop; i++) {
        std::string err = cam->next(f, true);
        if (!err.empty()) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        LocateTiming tm; auto dets = loc.run(f.bgra, f.width, f.height, f.stride, cfg.px_to_table, &tm);
        double ms = double(f.sync_us + tm.gray_us + tm.thresh_us + tm.contour_us) / 1e3;
        sum_ms += ms;
        printf("frame %u: %zu blobs, thr %d | sync %.1f gray %.1f thr %.1f contour %.1f = %.1f ms\n", f.pl_frame, dets.size(), int(tm.threshold), f.sync_us / 1e3, tm.gray_us / 1e3, tm.thresh_us / 1e3, tm.contour_us / 1e3, ms);
        if (i == n - 1 || i % 30 == 0)
            for (const Detection &d : dets)
                printf("   x %7.1f y %7.1f mm  len %5.1f wid %5.1f  axis %6.1f deg%s%s\n", d.p.x, d.p.y, d.len, d.wid, d.axis * 180 / kPi, d.partial ? "  partial" : "", d.merged ? "  MERGED" : "");
    }
    printf("== locate: avg %.1f ms per frame\n", sum_ms / n);
    if (!a.dump.empty()) printf("dump %s: %s\n", a.dump.c_str(), loc.dump(a.dump.c_str()) ? "ok" : "FAILED");
    return 0;
}
static int run_track(des_hw_t &hw, const AppConfig &cfg, const Args &a)
{
    CameraSession cam(&hw, cfg, a);
    if (!cam.ok()) return 1;
    Locator loc(cfg.locate); Tracker tr(cfg.tracker);
    Picker pk(cfg.picker); const double mmps = cfg.belt.mm_per_step * cfg.belt.dir;
    if (a.belt != 0) {
        const BeltRates r = belt_rates(cfg, a.belt, cfg.belt.accel); const double sps = r.vmax * cfg.belt.dir;
        des_step_vel(&hw, sps, r.acc); printf("belt: velocity mode %.1f mm/s = %.0f steps/s (dry run: picker belt plans are only printed)\n", a.belt, sps);
    }
    std::map<int, std::pair<TrackState, Disposition>> seen; BeltPlan last_plan;
    last_plan.kind = BeltPlan::Run;
    double secs = a.n > 0 ? a.n : 30, t_start = now_s(), t_sec = t_start;
    CapturedFrame f;
    while (!g_stop && now_s() - t_start < secs) {
        std::string err = cam->next(f, true);
        if (!err.empty()) { fprintf(stderr, "%s\n", err.c_str()); break; }
        FrameObs obs; obs.frame = f.pl_frame;
        // belt position at mid-frame; each part is corrected to the instant its own rows were read out
        double b0 = f.step_pos * mmps, b1 = f.step_pos_end * mmps;
        obs.belt_mm = 0.5 * (b0 + b1); obs.dets = loc.run(f.bgra, f.width, f.height, f.stride, cfg.px_to_table);
        for (Detection &d : obs.dets) d.p.x -= float((b1 - b0) * (d.img_v - 0.5f));
        tr.update(obs); Picker::Input in;
        in.belt_mm = des_step_pos(&hw) * mmps;
        double v = des_step_speed(&hw) * cfg.belt.mm_per_step;
        in.brake_mm = v * v / (2 * cfg.belt.accel); in.belt_stopped = !(des_step_status(&hw) & DES_ST_STATUS_MOVING);
        auto out = pk.tick(tr, in);
        if (out.have_job) {
            printf("[%u] PICK track %d at (%.1f, %.1f) close %.0f deg open %.1f mm -> bin %d (dry run: done)\n", f.pl_frame, out.job.track_id, out.job.p.x, out.job.p.y, out.job.close_axis * 180 / kPi, out.job.open_mm, out.job.bin);
            pk.job_done(tr, out.job.track_id, true);
        }
        if (out.belt.kind != last_plan.kind || std::fabs(out.belt.stop_belt_mm - last_plan.stop_belt_mm) > 0.5) {
            static const char *kinds[] = {"run", "stop-at", "hold"};
            printf("[%u] belt plan: %s %.1f mm (track %d)\n", f.pl_frame, kinds[out.belt.kind], out.belt.stop_belt_mm, out.belt.track_id); last_plan = out.belt;
        }
        for (const Track &t : tr.tracks()) {
            auto cur = std::make_pair(t.state, t.disp); auto it = seen.find(t.id);
            if (it == seen.end() || it->second != cur) {
                printf("[%u] track %d: %s %s  s %.1f y %.1f len %.1f wid %.1f axis %.0f deg hits %d %s\n", f.pl_frame, t.id, to_str(t.state), to_str(t.disp), t.s, t.y, t.len, t.wid, t.axis * 180 / kPi, t.hits, t.why);
                seen[t.id] = cur;
            }
        }
        tr.retire(in.belt_mm);
        double tnow = now_s();
        if (tnow - t_sec >= 5.0) {
            printf("-- %llu frames, %zu live tracks, belt %.1f mm, created %llu noise %llu lost %llu, " "pick %llu flap %llu recheck %llu\n", (unsigned long long)tr.stats.frames, tr.tracks().size(), in.belt_mm, (unsigned long long)tr.stats.created, (unsigned long long)tr.stats.noise,
                   (unsigned long long)tr.stats.lost, (unsigned long long)tr.stats.decided_pick,
                   (unsigned long long)tr.stats.decided_flap, (unsigned long long)tr.stats.decided_recheck); fflush(stdout);
            t_sec = tnow;
        }
    }
    if (a.belt != 0) des_step_stop(&hw);
    return 0;
}
// camera -> HDMI only (no overlays): measures the BGRA->BGR conversion + page flip cost (docs/软件与UI.md)
static int run_display(des_hw_t &hw, const AppConfig &cfg, const Args &a)
{
    CameraSession cam(&hw, cfg, a);
    if (!cam.ok()) return 1;
    HdmiOut hdmi; std::string err = hdmi.open();
    if (!err.empty()) { fprintf(stderr, "hdmi: %s\n", err.c_str()); return 1; }
    double secs = a.n > 0 ? a.n : 10, t_start = now_s(), t_sec = t_start;
    double sum_conv = 0, max_conv = 0, sum_flush = 0;
    uint64_t frames = 0, skipped = 0, sec_frames = 0; CapturedFrame f;
    char text[96];
    while (!g_stop && now_s() - t_start < secs) {
        err = cam->next(f, true);
        if (!err.empty()) { fprintf(stderr, "%s\n", err.c_str()); break; }
        if (frames) skipped += f.pl_gap - 1;
        double t0 = now_s();
        cv::Mat src(f.height, f.width, CV_8UC4, const_cast<uint8_t *>(f.bgra)); cv::Mat dst = hdmi.back();
        cv::cvtColor(src, dst, cv::COLOR_BGRA2BGR); snprintf(text, sizeof text, "PL frame %u  skipped %llu", f.pl_frame, (unsigned long long)skipped);
        cv::rectangle(dst, cv::Rect(20, 20, 560, 50), cv::Scalar(0, 0, 0), cv::FILLED);
        cv::putText(dst, text, cv::Point(30, 56), cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 255, 0), 2);
        double t1 = now_s();
        hdmi.flip(); frames++;
        sec_frames++; sum_conv += t1 - t0;
        max_conv = std::max(max_conv, t1 - t0); sum_flush += hdmi.last_flush_ms() / 1e3;
        if (now_s() - t_sec >= 1.0) {
            printf("%5.1f fps shown, convert avg %.1f ms, skipped %llu\n", sec_frames / (now_s() - t_sec), sum_conv / frames * 1e3, (unsigned long long)skipped);
            fflush(stdout); t_sec = now_s();
            sec_frames = 0;
        }
    }
    hdmi.close();
    double el = now_s() - t_start;
    printf("\n== display: %llu frames in %.2f s = %.2f fps; skipped %llu; convert avg %.2f ms max %.2f ms; " "flush avg %.2f ms\n", (unsigned long long)frames, el, frames / el, (unsigned long long)skipped,
           frames ? sum_conv / frames * 1e3 : 0.0, max_conv * 1e3, frames ? sum_flush / frames * 1e3 : 0.0);
    return 0;
}
// live + training crops of one class; out dir: --out, else  (des.env)
static int run_collect(des_hw_t &hw, const AppConfig &cfg, const Args &a)
{
    if (a.cls.empty()) { fprintf(stderr, "collect needs --cls <label>\n"); return 1; }
    CameraSession cam(&hw, cfg, a);
    if (!cam.ok()) return 1;
    LiveOptions opt; opt.seconds = a.n;
    opt.belt_mm_s = a.belt; opt.dump = a.dump;
    for (size_t i = 0; i < cfg.tracker.rules.size(); i++)
        if (cfg.tracker.rules[i].label == a.cls) opt.collect.label_id = int(i);
    if (opt.collect.label_id < 0) {
        fprintf(stderr, "unknown class '%s'; labels:", a.cls.c_str());
        for (const ClassRule &r : cfg.tracker.rules) fprintf(stderr, " %s", r.label.c_str());
        fprintf(stderr, "\n");
        return 1;
    }
    opt.collect.label = a.cls; opt.collect.every = std::max(1, a.every);
    opt.collect.max_per_part = std::max(1, a.maxpp); opt.collect.full_every = std::max(0, a.full);
    if (!a.out.empty()) opt.collect.out = a.out;                             // --out 优先
    else if (const char *e = getenv("DES_COLLECT")) opt.collect.out = e;     // 否则环境里的 DES_COLLECT（des.env）
    return run_live(hw, cfg, cam.rig(), opt, g_stop);
}
static int run_arm(des_hw_t &hw, const AppConfig &cfg, const Args &a)
{
    const ArmConfig &ac = cfg.arm; ArmExec ex(&hw, ac);
    const std::string s = a.sub.empty() ? "scan" : a.sub; const bool dry = !a.go;
    auto ok_or = [](int rc) { return rc == DES_OK ? "ok" : des_strerror(rc); };
    if (s == "scan") {
        int alive = 0;
        for (int j = 0; j < 6; j++) {
            ServoInfo i = ex.probe(j);
            if (!i.alive) {
                printf("joint %d (id %u): no reply\n", j + 1, ex.id(j));
                continue;
            }
            alive++; float deg = j < 5 ? (i.pos - ac.joint[j].zero_count) / ac.joint[j].counts_per_deg : 0.f;
            printf("joint %d (id %u): pos %4d (%+7.1f deg)  %4d mA  %3d C  status 0x%02x%s%s%s\n", j + 1, ex.id(j), i.pos, deg, i.current_ma, i.temp_c, i.status, i.status & 0x20 ? " OVERLOAD" : "", i.status & 0x04 ? " OVER-TEMP" : "", i.status & 0x01 ? " VOLTAGE" : "");
        }
        printf("%d / 6 servos answered%s\n", alive, alive ? "" : " (power, BSS138 wiring, CEP PIN19, ids 1..6? see docs/硬件与接线.md)");
        return alive == 6 ? 0 : 2;
    }
    if (s == "read" || s == "teach") {
        std::array<uint16_t, 6> pos{};
        if (!ex.read_positions(pos)) { fprintf(stderr, "not all servos answered (des_tool arm scan)\n"); return 2; }
        float grip = 0; JointAngles j = arm_from_counts(ac, pos, grip);
        Vec3 tip = arm_fk(ac, j); Vec2 tt = arm_to_table(ac, tip);
        printf("counts %u %u %u %u %u | grip %u\n", pos[0], pos[1], pos[2], pos[3], pos[4], pos[5]);
        printf("angles q1..q5 = %.1f %.1f %.1f %.1f %.1f deg, gripper %.1f mm\n", j.q[0], j.q[1], j.q[2], j.q[3], j.q[4], grip);
        printf("grasp point: arm (%.1f, %.1f, %.1f) mm, table (%.1f, %.1f) mm, %.1f mm above the belt\n", tip.x, tip.y, tip.z, tt.x, tt.y, tip.z - ac.belt_z);
        if (s == "teach")
            printf("paste into des_app.json arm.home_deg or arm.bins_deg:  [%.1f, %.1f, %.1f, %.1f, %.1f]\n", j.q[0], j.q[1], j.q[2], j.q[3], j.q[4]);
        return 0;
    }
    if (s == "torque-on" || s == "torque-off") {
        int rc = ex.torque(s == "torque-on"); printf("torque %s (broadcast): %s\n", s == "torque-on" ? "on" : "off", ok_or(rc));
        return rc == DES_OK ? 0 : 2;
    }
    // ---- motion
    std::array<uint16_t, 6> now{}; bool have_now = !dry && ex.read_positions(now);
    if (!have_now) {   // dry run / no read back: assume the arm sits at home
        JointAngles h; h.q = ac.home_deg;
        arm_to_counts(ac, h, ac.grip_max_open, now);
    }
    std::vector<ServoFrame> wp;
    if (s == "home" || s == "goto") {
        JointAngles target; float grip = ac.grip_max_open;
        if (s == "home") {
            target.q = ac.home_deg;
        } else {
            if (a.npos < 2) { usage_tool(); return 1; }
            float z = a.npos > 2 ? a.pos[2] : ac.z_safe; float axis = a.npos > 3 ? a.pos[3] * kPi / 180 : 0.f;
            if (const char *why = arm_ik(ac, table_to_arm(ac, {a.pos[0], a.pos[1]}, z), table_axis_to_arm(ac, axis), target)) {
                fprintf(stderr, "unreachable: %s\n", why);
                return 2;
            }
        }
        ServoFrame f;
        if (const char *why = arm_to_counts(ac, target, grip, f.pos)) { fprintf(stderr, "%s\n", why); return 2; }
        f.ms = 1500;   // single moves from an unknown pose: slow on purpose
        f.what = s == "home" ? "home" : "goto"; wp.push_back(f);
    } else if (s == "pick") {
        if (a.npos < 2) { usage_tool(); return 1; }
        float axis = a.npos > 2 ? a.pos[2] * kPi / 180 : 0.f; int bin = a.npos > 3 ? int(a.pos[3]) : 0;
        if (const char *why = plan_pick(ac, {a.pos[0], a.pos[1]}, axis, 40.f, 30.f, bin, now, wp)) {
            fprintf(stderr, "plan_pick: %s\n", why);
            return 2;
        }
    } else {
        usage_tool();
        return 1;
    }
    printf("%s %s, %zu frame(s)%s\n", dry ? "DRY RUN" : "MOVING", s.c_str(), wp.size(), dry ? " (add --go to move the arm)" : "");
    ArmExec::Result r = ex.run(wp, dry, [] { printf("  (belt may move again)\n"); });
    printf("done in %.0f ms: %s%s\n", r.ms, r.ok ? "ok" : "FAILED", r.why.empty() ? "" : (" - " + r.why).c_str());
    return r.ok ? 0 : 2;
}
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
        if (!read_image(a.image, bgr, gray)) { fprintf(stderr, "cannot read %s\n", a.image.c_str()); return 1; }
    } else {
        if (!hw) { fprintf(stderr, "PL not available\n"); return 1; }
        CameraSession cam(hw, cfg, a);
        if (!cam.ok()) return 1;
        CapturedFrame f;
        if (!capture_frames(*cam, 20, f, true).empty()) return 1;   // let exposure settle, keep the last frame
        cv::Mat src(f.height, f.width, CV_8UC4, const_cast<uint8_t *>(f.bgra));
        cv::cvtColor(src, bgr, cv::COLOR_BGRA2BGR);
        cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    }
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
    printf("board %dx%d inner corners, %.3f mm squares, travel %s: found %zu corners in %.0f ms\n", b.cols, b.rows, b.square_mm, to_str(travel), r.corners.size(), r.detect_ms);
    printf("fit residual rms %.3f mm, max %.3f mm; %.4f mm/px at the center\n", r.rms_mm, r.max_mm, r.mm_per_px);
    printf("FOV along the belt x %.1f .. %.1f mm; corners (table mm):", r.fov_x0, r.fov_x1);
    for (const Vec2 &v : r.fov_corners) printf(" (%.1f, %.1f)", v.x, v.y);
    printf("\n");
    const std::string out = calib_out(cfg, a, CalibDefault::Camera);
    const std::string err = save_calib(out, r, b, travel);
    report_saved(out, err);
    if (!a.dump.empty()) {
        draw_calib(bgr, r);
        printf("dump %s: %s\n", a.dump.c_str(), cv::imwrite(a.dump, bgr) ? "ok" : "FAILED");
    }
    return err.empty() ? 0 : 1;
}
// Belt scale and direction seen by the calibrated camera: median part position over still frames (largest blob not
// touching the border); short ~8 mm move, long one to ~75 % of the view, then back (slip / backlash); the whole travel
// sets the scale, so the position noise (~0.05 mm) is < 0.1 % of it; the camera scale error adds to it.
static int run_calib_belt(des_hw_t &hw, const AppConfig &cfg, const Args &a)
{
    if (cfg.calib_loaded.empty()) {
        fprintf(stderr, "calibrate the camera first (des_tool calib-cam): belt travel is measured in table mm\n");
        return 1;
    }
    CameraSession cam(&hw, cfg, a);
    if (!cam.ok()) return 1;
    Locator loc(cfg.locate);
    const double est = std::fabs(cfg.belt.mm_per_step), mm_s = a.belt > 0 ? a.belt : 20.0;
    const BeltRates r = belt_rates(cfg, mm_s);   // 0.5 s ramps
    const int dir0 = cfg.belt.dir >= 0 ? 1 : -1; // configured guess: + steps move parts downstream
    belt_prepare(hw);
    auto fail = [&](const std::string &e) {
        fprintf(stderr, "calib-belt: %s\n", e.c_str());
        des_step_stop(&hw);
        return 2;
    };
    Vec2 p0, p1, p2, p3;
    std::string e = still_position(*cam, loc, cfg, p0);
    if (!e.empty()) return fail(e);
    printf("start: part at x %.2f y %.2f mm (view x %.0f .. %.0f); estimate %.5f mm/step, %.0f mm/s\n", p0.x, p0.y, cfg.tracker.fov_x0, cfg.tracker.fov_x1, est, mm_s);
    // 1. short move downstream (with the configured direction guess)
    const int32_t s1 = dir0 * int32_t(std::lround(8.0 / est));
    if (!(e = belt_move(hw, s1, r.vmax, r.acc)).empty() || !(e = still_position(*cam, loc, cfg, p1)).empty()) return fail(e);
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
    if (!(e = belt_move(hw, s2, r.vmax, r.acc)).empty() || !(e = still_position(*cam, loc, cfg, p2)).empty()) return fail(e);
    printf("long move %d steps: x %.2f -> %.2f mm\n", s2, p1.x, p2.x);
    // 3. back to the start
    if (!(e = belt_move(hw, -(s1 + s2), r.vmax, r.acc)).empty() || !(e = still_position(*cam, loc, cfg, p3)).empty())
        return fail(e);
    printf("back %d steps: x %.2f (start %.2f)\n", -(s1 + s2), p3.x, p0.x);
    BeltCalib b = belt_from_positions(p0, p1, p2, p3, s1, s2);
    if (!b.ok) return fail(b.err);
    printf("== belt: %.6f mm/step, dir %+d (%.1f mm over %d steps); effective roller %.2f mm at 3200 steps/rev\n", b.mm_per_step, b.dir, b.travel_mm, b.steps, b.roller_d_mm);
    printf("   return error %+.3f mm after %.1f mm forward + back; lateral drift %+.2f mm per 100 mm\n", b.return_err_mm, b.travel_mm, b.drift_mm_per_100);
    if (std::fabs(b.mm_per_step - est) > 0.3 * est)
        printf("   (config estimate was %.5f: %+.0f %%)\n", est, (b.mm_per_step / est - 1) * 100);
    if (std::fabs(b.drift_mm_per_100) > 2)
        printf("   lateral drift > 2 mm / 100 mm: the camera x axis is not along the belt (re-run calib-cam with the " "board edge against the side guard) or the belt runs off\n");
    const std::string out = calib_out(cfg, a, CalibDefault::Belt);
    e = save_belt_calib(out, b);
    report_saved(out, e);
    return e.empty() ? 0 : 1;
}
// Measuring-wheel encoder vs stepper: empty belt moves --steps downstream and back, -n rounds, sampled every 10 ms;
// no camera, mm from belt.mm_per_step (calib-belt first).
static int run_calib_enc(des_hw_t &hw, const AppConfig &cfg, const Args &a)
{
    if (hw.version < DES_VERSION_IO) {
        fprintf(stderr, "calib-enc: bitstream VERSION %u has no encoder input (needs >= %u)\n", hw.version, DES_VERSION_IO);
        return 1;
    }
    if (!cfg.belt_calibrated)
        printf("note: belt.mm_per_step %.5f is not from calib-belt: the encoder scale is only as good as it\n", cfg.belt.mm_per_step);
    const double mps = std::fabs(cfg.belt.mm_per_step), mm_s = a.belt > 0 ? a.belt : cfg.belt.run_speed;
    const BeltRates r = belt_rates(cfg, mm_s, std::max(cfg.belt.accel, 1.0));
    const int dir = cfg.belt.dir >= 0 ? 1 : -1;
    const int32_t steps = a.steps ? a.steps : dir * int32_t(std::lround(300.0 / mps));   // default 300 mm downstream
    const int rounds = a.n > 0 ? int(a.n) : 1;
    const double nominal = cfg.belt.enc_wheel_mm / cfg.belt.enc_counts_per_rev;
    des_io_t io0, io1;
    des_io_read(&hw, &io0);
    printf("calib-enc: %d round(s) of %d steps (%.0f mm) at %.0f mm/s, forward and back; nominal %.4f mm/count " "(%.0f mm wheel, %d counts/rev)\n", rounds, steps, std::abs(steps) * mps, mm_s, nominal, cfg.belt.enc_wheel_mm, cfg.belt.enc_counts_per_rev);
    printf("encoder inputs now: A%u B%u Z%u (1 = high / not pulled low), count %d, errors %u\n", (io0.status >> 4) & 1, (io0.status >> 5) & 1, (io0.status >> 6) & 1, io0.enc, io0.enc_err);
    belt_prepare(hw);
    // (time, POS, encoder count) sampler: belt_move() calls it every 10 ms in sampled mode
    auto sampler = [&hw](std::vector<EncSample> &out) {
        out.clear();
        return std::function<void()>([&hw, &out]() {
            des_io_t io;
            des_io_read(&hw, &io);
            out.push_back({now_s(), des_step_pos(&hw), io.enc});
        });
    };
    std::vector<EncSample> fwd, back;
    std::vector<double> mpc;
    EncCalib last;
    for (int k = 0; k < rounds && !g_stop; k++) {
        std::function<void()> sf = sampler(fwd);
        std::string e = belt_move(hw, steps, r.vmax, r.acc, &sf);
        if (e.empty()) {
            std::function<void()> sb = sampler(back);
            e = belt_move(hw, -steps, r.vmax, r.acc, &sb);
        }
        if (!e.empty()) {
            fprintf(stderr, "calib-enc: %s\n", e.c_str());
            des_step_stop(&hw);
            return 2;
        }
        EncCalib c = enc_from_samples(fwd, back, mps, dir, cfg.belt.enc_counts_per_rev, nominal);
        if (!c.ok) {
            fprintf(stderr, "calib-enc: %s\n", c.err.c_str());
            return 2;
        }
        printf("[%d] %.6f mm/count (wheel %.2f mm), dir %+d, %.5f counts/step forward / %.5f back; return %+.3f mm; " "lag max %.3f mm; cruise %.1f mm/s, encoder %.2f +- %.2f mm/s (%zu + %zu samples)\n", k + 1, c.mm_per_count, c.wheel_circ_mm, c.dir, c.counts_per_step, c.back_counts_per_step,
               c.return_err_mm, c.lag_max_mm, c.cruise_mm_s, c.enc_speed_mean_mm_s, c.enc_speed_sd_mm_s, fwd.size(),
               back.size());
        mpc.push_back(c.mm_per_count);
        last = c;
    }
    if (mpc.empty()) return 2;
    des_io_read(&hw, &io1);
    double mean = 0, sd = 0;
    for (double v : mpc) mean += v;
    mean /= mpc.size();
    for (double v : mpc) sd += (v - mean) * (v - mean);
    sd = mpc.size() > 1 ? std::sqrt(sd / (mpc.size() - 1)) : 0;
    last.mm_per_count = mean;
    last.wheel_circ_mm = mean * cfg.belt.enc_counts_per_rev;
    printf("== encoder: %.6f mm/count (sd %.6f over %zu rounds = %.3f %%), wheel %.2f mm (nominal %.0f, %+.2f %%), " "dir %+d\n", mean, sd, mpc.size(), mean > 0 ? 100 * sd / mean : 0, last.wheel_circ_mm, cfg.belt.enc_wheel_mm, (last.wheel_circ_mm / cfg.belt.enc_wheel_mm - 1) * 100, last.dir);
    printf("   index pulses %u during the test, illegal A/B transitions %u (> 0: noise or too fast -> 4.7k pull-ups)\n", io1.enc_zcnt - io0.enc_zcnt, io1.enc_err - io0.enc_err);
    const std::string out = calib_out(cfg, a, CalibDefault::Encoder);
    std::string e = save_enc_calib(out, last, int(mpc.size()), sd);
    report_saved(out, e);
    return e.empty() ? 0 : 1;
}
// Through-beam position (sched/beam.hpp, des_io IR1): ONE gear lying in view upstream of the beam. Each round the belt
// carries it downstream across the beam (the PL latches the stepper position at the "blocked" edge, read here within
// 10 ms) and back to the start; the part's still position there plus the belt travel since the edge give where its
// centre was at the edge, and the leading edge (+ R) is the beam. Standing still: no camera timing in the number, so
// the run-time difference to it is the tracking error of the moving line.
static int run_calib_beam(des_hw_t &hw, const AppConfig &cfg, const Args &a)
{
    if (hw.version < DES_VERSION_IO) {
        fprintf(stderr, "calib-beam: bitstream VERSION %u has no IR1 input (needs >= %u)\n", hw.version, DES_VERSION_IO);
        return 1;
    }
    if (cfg.calib_loaded.empty()) {
        fprintf(stderr, "calibrate the camera first (des_tool calib-cam): the beam position is in table mm\n");
        return 1;
    }
    if (!cfg.belt_calibrated)
        printf("note: belt.mm_per_step %.5f is not from calib-belt: the beam position is only as good as it\n", cfg.belt.mm_per_step);
    des_io_t io;
    des_io_read(&hw, &io);
    if (io.status & DES_IO_ST_IR1_BLOCKED) {
        fprintf(stderr, "calib-beam: the beam reads blocked now: something in it, emitter / receiver not powered or not "
                        "aligned, or the polarity is inverted (check with desreg io watch 10; invert: desreg w 0x538 1)\n");
        return 1;
    }
    CameraSession cam(&hw, cfg, a);
    if (!cam.ok()) return 1;
    Locator loc(cfg.locate);
    const double mmps = cfg.belt.mm_per_step * (cfg.belt.dir >= 0 ? 1 : -1);   // belt travel per POS step, + = downstream
    const double mm_s = a.belt > 0 ? a.belt : 20.0;
    const BeltRates r = belt_rates(cfg, mm_s);   // 0.5 s ramps
    const int rounds = a.n > 0 ? int(a.n) : 3;
    belt_prepare(hw);
    auto fail = [&](const std::string &e) {
        fprintf(stderr, "calib-beam: %s\n", e.c_str());
        des_step_stop(&hw);
        return 2;
    };
    Vec2 p0;
    float len0 = 0;
    std::string e = still_position(*cam, loc, cfg, p0, &cfg.px_to_part, &len0);   // where the part stands (its height)
    if (!e.empty()) return fail(e);
    const float R = 0.5f * (a.part_od > 0 ? a.part_od : len0);
    printf("start: part at x %.2f y %.2f mm, size in the image %.2f mm%s -> R %.2f mm; view x %.0f .. %.0f\n", p0.x, p0.y, len0,
           a.part_od > 0 ? fmt(", caliper OD %.2f used", a.part_od).c_str() : " (give --od <caliper OD> for a better R)",
           R, cfg.tracker.fov_x0, cfg.tracker.fov_x1);
    const double room = cfg.tracker.fov_x1 - 5 - (p0.x + R);   // default: leading edge up to the downstream view edge
    if (!a.steps && room < 20)
        return fail("not enough room downstream: put the part near the upstream edge of the view, upstream of the beam");
    const int32_t steps = a.steps ? a.steps : int32_t(std::lround(room / mmps));
    printf("each round: %d steps (%+.0f mm) at %.0f mm/s and back; the part's leading edge must cross the beam on the way\n",
           steps, steps * mmps, mm_s);
    std::vector<double> xs;
    for (int k = 0; k < rounds && !g_stop; k++) {
        des_io_read(&hw, &io);
        const uint32_t cnt0 = io.ir1_cnt;
        bool seen = false;
        int32_t pos_e = 0;
        uint32_t n_first = 0;
        std::function<void()> sf = [&]() {   // every 10 ms while moving: the first new edge and its latched POS
            des_io_t s;
            if (seen || des_io_read(&hw, &s) != DES_OK || s.ir1_cnt == cnt0) return;
            seen = true; pos_e = s.ir1_pos; n_first = s.ir1_cnt - cnt0;
        };
        if (!(e = belt_move(hw, steps, r.vmax, r.acc, &sf)).empty()) return fail(e);
        des_io_read(&hw, &io);
        const uint32_t fwd_edges = io.ir1_cnt - cnt0;
        if (!(e = belt_move(hw, -steps, r.vmax, r.acc)).empty()) return fail(e);
        if (!seen)
            return fail(fmt("the part went %+.0f mm without breaking the beam: beam further downstream (--steps), above the "
                            "part, or IR1 not wired (desreg io watch 10 and block it by hand)", steps * mmps));
        if (n_first > 1) printf("  note: %u edges within 10 ms: the latched position is the last of them\n", n_first);
        Vec2 p1;
        float l1 = 0;
        if (!(e = still_position(*cam, loc, cfg, p1, &cfg.px_to_part, &l1)).empty()) return fail(e);
        const double b_end = des_step_pos(&hw) * mmps, b_e = pos_e * mmps;
        const double xc = p1.x - (b_end - b_e);   // part centre when the beam went dark
        const double bx = xc + R;
        printf("[%d] edge at POS %d (%u edge(s) on the way down); back at x %.2f (start %.2f, %+.2f); centre at the edge "
               "%.2f -> beam x %.2f mm\n", k + 1, pos_e, fwd_edges, p1.x, p0.x, p1.x - p0.x, xc, bx);
        xs.push_back(bx);
    }
    if (xs.empty()) return 2;
    double mean = 0, sd = 0;
    for (double v : xs) mean += v;
    mean /= xs.size();
    for (double v : xs) sd += (v - mean) * (v - mean);
    sd = xs.size() > 1 ? std::sqrt(sd / (xs.size() - 1)) : 0;
    printf("== beam: x %.2f mm (sd %.2f over %zu rounds), part y %.1f\n", mean, sd, xs.size(), p0.y);
    if (sd > 0.5) printf("   sd > 0.5 mm: belt slipping, beam edge soft (laser spot half on the part?), or the part moved\n");
    if (mean > cfg.tracker.decide_x - 20)
        printf("   WARNING: the beam is not upstream of decide_x %.0f - 20: part records are written before the part reaches "
               "it (move the beam upstream)\n", cfg.tracker.decide_x);
    if (mean < cfg.tracker.fov_x0)
        printf("   beam upstream of the view: set beam.expire_mm >= %.0f in des_app.json\n", cfg.tracker.fov_x0 - mean + 60);
    const std::string out = calib_out(cfg, a, CalibDefault::Beam);
    e = save_beam_calib(out, mean, sd, int(xs.size()), R, p0.y);
    report_saved(out, e);
    if (e.empty() && !cfg.beam.enabled) printf("   turn it on: des_app.json \"beam\": {\"enabled\": true}, then restart des_app\n");
    return e.empty() ? 0 : 1;
}
// Flat field for the part crops (flat_field.hpp): a blank white sheet (A4 across the belt is enough) lies at the
// upstream edge of the view and covers it; the belt carries it downstream in --steps steps (default 40 mm), 4 frames
// averaged per stop, until the sheet's leading edge has reached the downstream edge of the view plus one more stop
// (every column then seen under the sheet), at most 15 stops; then back to the start. Writes flat.json next to the config (des_app.json "flat_file") or --out.
static int run_calib_flat(des_hw_t &hw, const AppConfig &cfg, const Args &a)
{
    if (cfg.calib_loaded.empty()) {
        fprintf(stderr, "calibrate the camera first (des_tool calib-cam): belt direction and mm per step\n");
        return 1;
    }
    CameraSession cam(&hw, cfg, a);
    if (!cam.ok()) return 1;
    const double mmps = std::fabs(cfg.belt.mm_per_step), mm_s = a.belt > 0 ? a.belt : 20.0;
    const BeltRates r = belt_rates(cfg, mm_s);   // 0.5 s ramps
    const int dir = cfg.belt.dir >= 0 ? 1 : -1;   // + steps move parts downstream when belt.dir = +1
    const int32_t step = a.steps ? a.steps : dir * int32_t(std::lround(40.0 / mmps));
    const Vec2 pl = cfg.px_to_table.map({0, 540}), pr = cfg.px_to_table.map({1919, 540});
    const bool down_left = pl.x > pr.x;   // table x grows downstream
    belt_prepare(hw);
    cv::Mat sum;   // scratch for capture_gray_avg: one CV_32F allocation for all stops
    auto grab = [&](cv::Mat &gray) -> std::string { return capture_gray_avg(*cam, cfg.camera.buffers + 2, sum, gray); };
    // sheet columns: mean of the middle 40 % of the rows above belt + half the contrast (vignetting is mild there)
    auto span = [](const cv::Mat &g, int &c0, int &c1) {
        cv::Mat band = g.rowRange(g.rows * 3 / 10, g.rows * 7 / 10), prof;
        cv::reduce(band, prof, 0, cv::REDUCE_AVG, CV_32F);
        std::vector<float> p(prof.begin<float>(), prof.end<float>()), s = p;
        std::sort(s.begin(), s.end());
        const float lo = s[s.size() / 10], hi = s[s.size() * 99 / 100], thr = lo + 0.5f * (hi - lo);
        c0 = -1;
        c1 = -1;
        if (hi - lo < 40) return;   // no sheet
        for (int x = 0; x < int(p.size()); x++)
            if (p[x] > thr) {
                if (c0 < 0) c0 = x;
                c1 = x;
            }
    };
    FlatBuilder fb;
    int32_t moved = 0;
    bool last = false;
    std::string e;
    for (int k = 0; k < 15 && !g_stop; k++) {
        cv::Mat g;
        if (!(e = grab(g)).empty()) break;
        int c0, c1;
        span(g, c0, c1);
        if (c0 < 0) {
            // where the view is dim the leading edge can read a few px short of the downstream edge (10-24: column
            // 4 at the last stops), so the sheet may leave the view before `last` fires: that is the end, not an error
            if (k < 2) e = "no white sheet in view at stop " + std::to_string(k);
            else printf("stop %2d: the sheet has left the view\n", k);
            break;
        }
        fb.add(g);
        const int W = g.cols, lead = down_left ? c0 : W - 1 - c1, trail = down_left ? W - 1 - c1 : c0;
        printf("stop %2d (belt %+.0f mm): sheet columns %d..%d\n", k, moved * mmps * dir, c0, c1);
        if (k == 0 && trail > 3)
            printf("  note: the sheet does not reach the upstream edge of the view (%d px short): that strip is " "filled from its neighbours\n", trail);
        if (last) break;
        if (lead <= 3) last = true;   // leading edge at the downstream edge: one more stop for the eroded margin
        if (!(e = belt_move(hw, step, r.vmax, r.acc)).empty()) break;
        moved += step;
    }
    if (moved) {
        std::string b = belt_move(hw, -moved, r.vmax, r.acc);
        printf("belt back %d steps: %s\n", -moved, b.empty() ? "ok" : b.c_str());
    }
    des_step_stop(&hw);
    if (!e.empty()) {
        fprintf(stderr, "calib-flat: %s\n", e.c_str());
        return 2;
    }
    if (!last) printf("note: the sheet never reached the downstream edge of the view; the rest is filled in\n");
    FlatField ff;
    FlatStats st;
    if (!(e = fb.build(ff, 48, 27, &st)).empty()) {
        fprintf(stderr, "calib-flat: %s\n", e.c_str());
        return 2;
    }
    printf("== flat field: %d stops, %.1f %% of the view under the sheet; centre white %.1f, darkest %.0f %%; " "corners TL %.0f TR %.0f BL %.0f BR %.0f %%\n", st.frames, st.covered * 100, st.centre, st.min_rel * 100,
           st.corner_rel[0] * 100, st.corner_rel[1] * 100, st.corner_rel[2] * 100, st.corner_rel[3] * 100);
    printf("   corrected sheet p5..p95 = %.1f .. %.1f %% of the centre\n", st.resid_p5 * 100, st.resid_p95 * 100);
    printf("   white level / centre (%%), 8 x 4 over the view (left = image left):\n");
    for (int row = 0; row < 4; row++) {
        printf("  ");
        for (int col = 0; col < 8; col++)
            printf(" %4.0f", ff.white_at((col + 0.5f) * ff.width / 8 - 0.5f, (row + 0.5f) * ff.height / 4 - 0.5f) / ff.ref * 100);
        printf("\n");
    }
    const std::string out = calib_out(cfg, a, CalibDefault::Flat);
    char note[96];
    snprintf(note, sizeof note, "%d stops of %d steps, %.1f %% covered", st.frames, step, st.covered * 100);
    e = save_flat(out, ff, note);
    report_saved(out, e, cfg.flat_file.empty() ? " (des_app.json flat_file is empty: crops stay raw)" : nullptr);
    return e.empty() ? 0 : 1;
}
// Hand-eye: where the arm stands on the table (base_xy, base_yaw) and how high the belt is in the arm frame, from
// >= 3 marker positions that the camera sees and the gripper tip touches. Interactive (Enter between the steps).
// The arm stands beside the pick line, downstream of the view: with --carry <mm> the camera locates each marker in
// the view and the belt then carries it that far downstream (belt.mm_per_step, calib-belt) into the arm's working area, where the tip touches it - the fit then covers the area the arm really picks in.
// The joint zeros / directions must be right first (arm read = geometric angles, docs/运动控制与分拣.md bring-up step 3).
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
        fprintf(stderr, "calibrate the camera first (des_tool calib-cam): marker positions are table mm\n");
        return 1;
    }
    const int n = a.n >= 3 ? int(a.n) : 4;
    ArmExec ex(&hw, cfg.arm);
    std::array<uint16_t, 6> pos{};
    if (!ex.read_positions(pos)) { fprintf(stderr, "not all servos answered (des_tool arm scan)\n"); return 2; }
    CameraSession cam(&hw, cfg, a);
    if (!cam.ok()) return 1;
    Locator loc(cfg.locate);
    printf("Hand-eye calibration, %d points. Marker: a small flat part (or a 10~20 mm disc) whose centre you can\n" "touch with the gripper tip; spread the points over the part of the view the arm reaches.\n" "The arm torque goes OFF now - hold the arm.\n", n);
    if (!wait_enter("Enter to switch the torque off (q quits)")) return 1;
    ex.torque(false);
    std::vector<Vec3> arm_pts;
    std::vector<Vec2> tab_pts;
    for (int i = 0; i < n && !g_stop; i++) {
        char msg[200];
        snprintf(msg, sizeof msg, a.carry_mm > 0 ? "[%d/%d] put the marker in view (downstream half), take the hand and the arm off the belt, Enter:" : "[%d/%d] put the marker in view (reachable), move the arm out of the picture, Enter:", i + 1, n);
        if (!wait_enter(msg)) break;
        Vec2 p;
        std::string e = still_position(*cam, loc, cfg, p);
        if (!e.empty()) { printf("  %s - again\n", e.c_str()); i--; continue; }
        printf("  marker at table (%.1f, %.1f) mm\n", p.x, p.y);
        if (a.carry_mm > 0) {   // into the arm's area: table x grows downstream
            const double mmps = std::fabs(cfg.belt.mm_per_step);
            const BeltRates r = belt_rates(cfg, 20.0);
            const int32_t steps = (cfg.belt.dir >= 0 ? 1 : -1) * int32_t(std::lround(a.carry_mm / mmps));
            des_step_enable(&hw, 1);
            e = belt_move(hw, steps, r.vmax, r.acc);
            des_step_stop(&hw);
            if (!e.empty()) { printf("  belt: %s - stopped\n", e.c_str()); break; }
            p.x += float(std::abs(steps) * mmps);
            printf("  carried %.1f mm downstream: marker now at table (%.1f, %.1f) mm\n", std::abs(steps) * mmps, p.x, p.y);
        }
        if (!wait_enter("  touch the marker centre with the gripper tip (between the fingers), Enter:")) break;
        if (!ex.read_positions(pos)) { printf("  servo read failed - again\n"); i--; continue; }
        float grip = 0;
        JointAngles j = arm_from_counts(cfg.arm, pos, grip);
        Vec3 tip = arm_fk(cfg.arm, j);
        printf("  tip at arm (%.1f, %.1f, %.1f) mm (q %.1f %.1f %.1f %.1f)\n", tip.x, tip.y, tip.z, j.q[0], j.q[1], j.q[2], j.q[3]);
        arm_pts.push_back(tip);
        tab_pts.push_back(p);
    }
    ArmBaseFit f = fit_arm_base(arm_pts, tab_pts, a.marker_h > 0 ? a.marker_h : 0.f);
    if (!f.ok) { fprintf(stderr, "calib-arm: %s\n", f.err.c_str()); return 2; }
    for (size_t i = 0; i < f.resid.size(); i++)
        printf("  point %zu: table (%.1f, %.1f) residual %.2f mm\n", i + 1, tab_pts[i].x, tab_pts[i].y, f.resid[i]);
    printf("== arm base at table (%.1f, %.1f) mm, arm +x points %.1f deg from table +x; residual rms %.2f, max %.2f mm\n", f.base_xy.x, f.base_xy.y, f.base_yaw * 180 / kPi, f.rms, f.max);
    if (a.marker_h >= 0) printf("   belt surface %.1f mm above the arm base plate (marker %.1f mm)\n", f.belt_z, a.marker_h);
    else printf("   belt_z not fitted (give --marker-h to fit it)\n");
    if (f.max > 3)
        printf("   max residual > 3 mm: a marker moved, a touch was off, or joint zeros / link lengths are wrong\n");
    const std::string out = calib_out(cfg, a, CalibDefault::Arm);
    std::string e = save_arm_calib(out, f, a.marker_h >= 0);
    printf("saved %s: %s. Torque is still OFF: des_tool arm torque-on before moving.\n", out.c_str(), e.empty() ? "ok" : e.c_str());
    return e.empty() ? 0 : 1;
}
// One frame (or -n frames of still parts) -> locate -> measure every gear / rack. For accuracy work with real
// parts: compare with the caliper values, report the repeatability, keep the raw frame to re-run offline.
static int run_measure(des_hw_t *hw, const AppConfig &cfg, const Args &a)
{
    cv::Mat bgr, bgra, gray;   // bgr: --image original / camera frame, so --dump needs no BGRA round trip
    int frames = a.n > 0 ? int(a.n) : 1;
    const bool from_camera = a.image.empty();
    CameraRig cam(hw);
    if (from_camera) {
        if (open_camera(cam, cfg, a)) return 1;
        CapturedFrame f;
        if (!capture_frames(cam, 30, f).empty()) return 1;   // exposure settles
    } else {
        if (!read_image(a.image, bgr, gray)) { fprintf(stderr, "cannot read %s\n", a.image.c_str()); return 1; }
        cv::cvtColor(bgr, bgra, cv::COLOR_BGR2BGRA);
        frames = 1;
    }
    Locator loc(cfg.locate);
    RepeatStats st;
    std::vector<PartResult> last;
    double sum_ms = 0;
    for (int i = 0; i < frames && !g_stop; i++) {
        float mm_per_row = 0;
        if (from_camera) {
            CapturedFrame f;
            std::string err = cam.next(f, true);
            if (!err.empty()) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
            // copy out of the DMA ring (it is rewritten a few frames later)
            bgra = cv::Mat(f.height, f.width, CV_8UC4, const_cast<uint8_t *>(f.bgra), size_t(f.stride)).clone();
            cv::cvtColor(bgra, gray, cv::COLOR_BGRA2GRAY);
            bgr = cv::Mat();
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
        if (bgr.empty()) cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
        draw_parts(bgr, last, cfg.px_to_table);
        printf("report %s: %s\n", a.dump.c_str(), cv::imwrite(a.dump, bgr) ? "ok" : "FAILED");
    }
    return 0;
}
static int run_ik(const AppConfig &cfg, const Args &a)
{
    if (a.npos < 2) { usage_tool(); return 1; }
    Vec2 p{a.pos[0], a.pos[1]};
    float z = a.npos > 2 ? a.pos[2] : cfg.arm.z_grasp;
    float axis = a.npos > 3 ? a.pos[3] * kPi / 180 : 0.f;
    const float tilt = a.npos > 4 ? a.pos[4] * kPi / 180 : 0.f;   // lean the tool away from the base (reach scans)
    Vec3 pa = table_to_arm(cfg.arm, p, z);
    JointAngles j;
    const char *why = arm_ik(cfg.arm, pa, table_axis_to_arm(cfg.arm, axis), j, tilt);
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
    if (print_version_if_requested(argc, argv, "des_tool")) return 0;
    Args a;
    if (!parse(argc, argv, a)) { usage_tool(); return 1; }
    if (a.config.empty()) {   // 没给 -c：环境里的 DES_CONFIG（des.env export 的），否则当前目录的 des_app.json
        const char *e = getenv("DES_CONFIG");
        a.config = (e && *e) ? e : "des_app.json";
    }
    if (a.mode == "run" || a.mode == "live") {
        fprintf(stderr, "des_tool: '%s' is the product runtime - use des_app %s\n", a.mode.c_str(), a.mode.c_str());
        usage_tool();
        return 1;
    }
    AppConfig cfg;
    std::string err = load_config(a.config, cfg);
    if (!err.empty()) fprintf(stderr, "config: %s (using defaults)\n", err.c_str());
    print_config_summary(cfg, a.mode != "calib-cam");
    if (a.mode == "ik") return run_ik(cfg, a);
    if (a.mode == "calib-cam" && !a.image.empty()) return run_calib(nullptr, cfg, a);
    if (a.mode == "measure" && !a.image.empty()) return run_measure(nullptr, cfg, a);
    des_hw_t hw;
    if (app_bootstrap(hw, cfg, a)) return 1;
    int rc;
    if (a.mode == "probe") rc = run_probe(hw, cfg, a);
    else if (a.mode == "locate") rc = run_locate(hw, cfg, a);
    else if (a.mode == "track") rc = run_track(hw, cfg, a);
    else if (a.mode == "display") rc = run_display(hw, cfg, a);
    else if (a.mode == "collect") rc = run_collect(hw, cfg, a);
    else if (a.mode == "calib-cam") rc = run_calib(&hw, cfg, a);
    else if (a.mode == "calib-belt") rc = run_calib_belt(hw, cfg, a);
    else if (a.mode == "calib-enc") rc = run_calib_enc(hw, cfg, a);
    else if (a.mode == "calib-beam") rc = run_calib_beam(hw, cfg, a);
    else if (a.mode == "calib-flat") rc = run_calib_flat(hw, cfg, a);
    else if (a.mode == "calib-arm") rc = run_calib_arm(hw, cfg, a);
    else if (a.mode == "arm") rc = run_arm(hw, cfg, a);
    else if (a.mode == "measure") rc = run_measure(&hw, cfg, a);
    else {
        fprintf(stderr, "des_tool: unknown mode '%s' (product run/live is des_app)\n", a.mode.c_str());
        usage_tool();
        rc = 1;
    }
    // des_tool owns the stepper while it runs (des_app is stopped): a belt mode leaves the driver enabled
    // (belt_prepare / belt_move / des_step_stop) -> stand still, then release it, as des_app does on exit
    if (des_step_status(&hw) & DES_ST_STATUS_ENABLED) des_step_stop_release(&hw, 3000);
    des_hw_close(&hw);
    return rc;
}
