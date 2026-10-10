// live.cpp — see live.hpp
//
// Core 0 (SCHED_FIFO, the caller's thread): capture every frame, hand the newest one to the worker, convert it
// to BGR into the HDMI back buffer, draw the latest tracking snapshot re-projected to this frame's belt position,
// flip. Core 1 (worker, SCHED_OTHER): locate -> tracker -> picker on the newest frame it can get (frames that
// arrive while it is busy are skipped by the worker only, never by the display), publishes a snapshot.
#include "live.hpp"

#include <pthread.h>
#include <sched.h>
#include <time.h>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include <opencv2/core/utility.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "hdmi_out.hpp"
#include "locate.hpp"
#include "picker.hpp"
#include "tracker.hpp"

namespace des {
namespace {

double now_s()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

void pin_to_core(int core)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    pthread_setaffinity_np(pthread_self(), sizeof set, &set);
}

struct TrackView {
    int id = 0;
    TrackState st = TrackState::New;
    Disposition disp = Disposition::None;
    float s = 0, y = 0, len = 0, wid = 0, axis = 0;
    std::string why;
};

struct Snapshot {
    uint64_t seq = 0;                           // capture sequence number the result belongs to
    uint32_t frame = 0;
    std::vector<std::array<float, 8>> blobs;    // raw blobs of that frame, full-res px
    std::vector<TrackView> tracks;
    LocateTiming tm;
    double work_ms = 0;
    BeltPlan plan;
    Tracker::Stats st;
    uint64_t processed = 0, stale = 0;
};

struct Shared {
    std::mutex m;
    std::condition_variable cv;
    bool has_job = false;
    CapturedFrame job;
    uint64_t job_seq = 0;
    Snapshot snap;
    std::atomic<uint64_t> cap_seq{0};
    bool quit = false;
};

void worker(Shared &sh, des_hw_t &hw, const AppConfig &cfg)
{
    pin_to_core(1);
    sched_param sp{};
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp);   // real-time priority stays with the capture loop

    Locator loc(cfg.locate);
    Tracker tr(cfg.tracker);
    Picker pk(cfg.picker);
    const double mmps = cfg.belt.mm_per_step * cfg.belt.dir;
    const uint64_t max_age = uint64_t(cfg.camera.buffers > 2 ? cfg.camera.buffers - 1 : 1);
    uint64_t processed = 0, stale = 0;

    for (;;) {
        CapturedFrame f;
        uint64_t seq;
        {
            std::unique_lock<std::mutex> lk(sh.m);
            sh.cv.wait(lk, [&] { return sh.has_job || sh.quit; });
            if (sh.quit) break;
            f = sh.job;
            seq = sh.job_seq;
            sh.has_job = false;
        }
        double t0 = now_s();
        FrameObs obs;
        obs.frame = f.pl_frame;
        const double b0 = f.step_pos * mmps, b1 = f.step_pos_end * mmps;
        obs.belt_mm = 0.5 * (b0 + b1);
        LocateTiming tm;
        obs.dets = loc.run(f.bgra, f.width, f.height, f.stride, cfg.px_to_table, &tm);
        // the DMA rewrites a buffer buffers-1 frames after it was handed out: drop results read from a reused one
        if (sh.cap_seq.load() - seq >= max_age) {
            stale++;
            continue;
        }
        std::vector<std::array<float, 8>> blobs = loc.rects_full();
        for (Detection &d : obs.dets) d.p.x -= float((b1 - b0) * (d.img_v - 0.5f));   // rolling-shutter row timing
        tr.update(obs);

        Picker::Input in;
        in.belt_mm = des_step_pos(&hw) * mmps;
        double v = des_step_speed(&hw) * cfg.belt.mm_per_step;
        in.brake_mm = v * v / (2 * cfg.belt.accel);
        in.belt_stopped = !(des_step_status(&hw) & DES_ST_STATUS_MOVING);
        Picker::Output out = pk.tick(tr, in);
        if (out.have_job) pk.job_done(tr, out.job.track_id, true);   // no arm yet: pretend the pick succeeded
        processed++;

        Snapshot s;
        s.seq = seq;
        s.frame = f.pl_frame;
        s.blobs = std::move(blobs);
        s.tm = tm;
        s.plan = out.belt;
        s.st = tr.stats;
        s.processed = processed;
        s.stale = stale;
        for (const Track &t : tr.tracks())
            if (t.state != TrackState::Done)
                s.tracks.push_back({t.id, t.state, t.disp, t.s, t.y, t.len, t.wid, t.axis, t.why});
        tr.retire(in.belt_mm);
        s.work_ms = (now_s() - t0) * 1e3;
        std::lock_guard<std::mutex> lk(sh.m);
        sh.snap = std::move(s);
    }
}

cv::Scalar color_of(const TrackView &t)
{
    if (t.st == TrackState::New) return cv::Scalar(170, 170, 170);
    switch (t.disp) {
    case Disposition::Pick: return cv::Scalar(0, 210, 0);
    case Disposition::Flap: return cv::Scalar(40, 40, 255);
    case Disposition::Recheck: return cv::Scalar(0, 165, 255);
    default: return cv::Scalar(0, 220, 220);   // tracking, not decided yet
    }
}

void line_at_x(cv::Mat &img, const Homography &hinv, float x, const char *label, cv::Scalar col)
{
    Vec2 a = hinv.map({x, -400.f}), b = hinv.map({x, 400.f});
    cv::line(img, cv::Point2f(a.x, a.y), cv::Point2f(b.x, b.y), col, 2, cv::LINE_8);
    Vec2 m = hinv.map({x, 0.f});
    if (m.x > 0 && m.x < img.cols - 120)
        cv::putText(img, label, cv::Point2f(m.x + 6, img.rows - 20), cv::FONT_HERSHEY_SIMPLEX, 0.7, col, 2);
}

void draw(cv::Mat &img, const Snapshot &s, const Homography &hinv, double belt_mm, const AppConfig &cfg,
          const char *status1)
{
    // raw blobs of the frame the worker last processed (thin)
    for (const auto &r : s.blobs) {
        cv::Point pts[4];
        for (int i = 0; i < 4; i++) pts[i] = cv::Point(int(r[2 * i]), int(r[2 * i + 1]));
        cv::polylines(img, std::vector<cv::Point>(pts, pts + 4), true, cv::Scalar(200, 200, 200), 1, cv::LINE_8);
    }
    // tracks, re-projected to this frame's belt position
    char text[96];
    for (const TrackView &t : s.tracks) {
        ORect r{{float(t.s + belt_mm), t.y}, t.len / 2, t.wid / 2, t.axis};
        auto c = r.corners();
        std::vector<cv::Point> poly;
        int top = 1 << 30, left = 1 << 30;
        for (const Vec2 &v : c) {
            Vec2 p = hinv.map(v);
            poly.emplace_back(int(p.x), int(p.y));
            top = std::min(top, int(p.y));
            left = std::min(left, int(p.x));
        }
        cv::Scalar col = color_of(t);
        cv::polylines(img, poly, true, col, 3, cv::LINE_8);
        snprintf(text, sizeof text, "#%d %s", t.id,
                 t.st == TrackState::Decided || t.st == TrackState::Picking ? to_str(t.disp) : to_str(t.st));
        // thickness 1: Hershey text at thickness 2 is drawn as many filled thick segments (~1 ms per label)
        cv::putText(img, text, cv::Point(left, std::max(top - 10, 20)), cv::FONT_HERSHEY_SIMPLEX, 0.8, col, 1);
    }
    line_at_x(img, hinv, cfg.tracker.decide_x, "decide", cv::Scalar(255, 255, 255));
    line_at_x(img, hinv, cfg.picker.pick_x, "pick", cv::Scalar(0, 210, 0));

    // Status panel. Measured on the board: three thick putText lines cost ~5 ms, so the text is re-rendered at
    // 10 Hz in a cached Mat and only blitted (memcpy rows, ~0.3 ms) on the other frames.
    static cv::Mat panel(118, 1150, CV_8UC3, cv::Scalar(0, 0, 0));
    static int frame_no = 0;
    if (frame_no++ % 6 != 0) {
        for (int r = 0; r < panel.rows; r++) memcpy(img.ptr(r), panel.ptr(r), size_t(panel.cols) * 3);
        return;
    }
    static const char *plans[] = {"run", "stop-at", "hold"};
    char l2[160], l3[160];
    snprintf(l2, sizeof l2, "locate %.1f ms (worker %.1f ms)  blobs %zu  tracks %zu  belt %.1f mm", s.tm.gray_us / 1e3 +
             s.tm.thresh_us / 1e3 + s.tm.contour_us / 1e3, s.work_ms, s.blobs.size(), s.tracks.size(), belt_mm);
    snprintf(l3, sizeof l3, "pick %llu  flap %llu  recheck %llu  noise %llu  belt plan %s",
             (unsigned long long)s.st.decided_pick, (unsigned long long)s.st.decided_flap,
             (unsigned long long)s.st.decided_recheck, (unsigned long long)s.st.noise, plans[s.plan.kind]);
    panel.setTo(cv::Scalar(0, 0, 0));
    cv::putText(panel, status1, cv::Point(14, 34), cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 0), 2);
    cv::putText(panel, l2, cv::Point(14, 70), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);
    cv::putText(panel, l3, cv::Point(14, 104), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);
    for (int r = 0; r < panel.rows; r++) memcpy(img.ptr(r), panel.ptr(r), size_t(panel.cols) * 3);
}

}  // namespace

int run_live(des_hw_t &hw, const AppConfig &cfg, CameraRig &cam, const LiveOptions &opt, const volatile sig_atomic_t &stop)
{
    // We split the two cores ourselves. OpenCV's own pool would be created after pin_to_core(0) and inherit
    // SCHED_FIFO + core 0, so the capture thread spin-waits on pool threads that cannot run (measured: display
    // 20 ms/frame instead of 7.5 ms, 40 fps). Single-threaded OpenCV everywhere.
    cv::setNumThreads(1);

    HdmiOut hdmi;
    std::string err = hdmi.open();
    if (!err.empty()) {
        fprintf(stderr, "hdmi: %s\n", err.c_str());
        return 1;
    }
    const Homography hinv = cfg.px_to_table.inverse();
    const double mmps = cfg.belt.mm_per_step * cfg.belt.dir;
    if (opt.belt_mm_s != 0) {
        des_step_vel(&hw, opt.belt_mm_s / cfg.belt.mm_per_step * cfg.belt.dir, cfg.belt.accel / cfg.belt.mm_per_step);
        printf("belt: velocity mode %.1f mm/s (picker plans are only displayed)\n", opt.belt_mm_s);
    }

    Shared sh;
    std::thread th(worker, std::ref(sh), std::ref(hw), std::cref(cfg));
    pin_to_core(0);

    uint64_t frames = 0, skipped = 0, sec_frames = 0;
    double t_start = now_s(), t_sec = t_start, fps = 0, sum_draw = 0, max_draw = 0, sum_conv = 0, sum_ovl = 0;
    // debug switches for timing breakdowns (env): LIVE_NO_WORKER=1 / LIVE_NO_OVERLAY=1
    const bool no_worker = getenv("LIVE_NO_WORKER") != nullptr, no_overlay = getenv("LIVE_NO_OVERLAY") != nullptr;
    CapturedFrame f;
    Snapshot snap;
    char status1[160] = "starting";
    while (!stop && (opt.seconds <= 0 || now_s() - t_start < opt.seconds)) {
        err = cam.next(f, true);
        if (!err.empty()) {
            fprintf(stderr, "%s\n", err.c_str());
            break;
        }
        const uint64_t seq = ++frames;
        sh.cap_seq.store(seq);
        if (seq > 1) skipped += f.pl_gap - 1;
        {
            std::lock_guard<std::mutex> lk(sh.m);
            sh.job = f;                  // always the newest frame; an unprocessed older one is simply replaced
            sh.job_seq = seq;
            sh.has_job = true;
            snap = sh.snap;
        }
        if (!no_worker) sh.cv.notify_one();

        double t0 = now_s();
        cv::Mat src(f.height, f.width, CV_8UC4, const_cast<uint8_t *>(f.bgra));
        cv::Mat dst = hdmi.back();
        cv::cvtColor(src, dst, cv::COLOR_BGRA2BGR);
        double tc = now_s();
        snprintf(status1, sizeof status1, "DragonEyeSorter live  %.1f fps  PL frame %u  skipped %llu  worker lag %llu",
                 fps, f.pl_frame, (unsigned long long)skipped, (unsigned long long)(seq - snap.seq));
        if (!no_overlay) draw(dst, snap, hinv, 0.5 * (f.step_pos + f.step_pos_end) * mmps, cfg, status1);
        double td = now_s();
        if (!opt.dump.empty() && seq == 180) {
            cv::Mat copy = dst.clone();   // slow read from the uncached buffer, once
            printf("dump %s: %s\n", opt.dump.c_str(), cv::imwrite(opt.dump, copy) ? "ok" : "FAILED");
        }
        hdmi.flip();
        double dt = now_s() - t0;
        sum_draw += dt;
        sum_conv += tc - t0;
        sum_ovl += td - tc;
        max_draw = std::max(max_draw, dt);

        sec_frames++;
        double t = now_s();
        if (t - t_sec >= 1.0) {
            fps = sec_frames / (t - t_sec);
            printf("%5.1f fps shown | display %.1f ms (convert %.1f, overlay %.1f) | worker: %llu frames, locate %.1f ms, "
                   "%zu tracks, stale %llu\n",
                   fps, sum_draw / frames * 1e3, sum_conv / frames * 1e3, sum_ovl / frames * 1e3,
                   (unsigned long long)snap.processed,
                   (snap.tm.gray_us + snap.tm.thresh_us + snap.tm.contour_us) / 1e3, snap.tracks.size(),
                   (unsigned long long)snap.stale);
            fflush(stdout);
            t_sec = t;
            sec_frames = 0;
        }
    }
    {
        std::lock_guard<std::mutex> lk(sh.m);
        sh.quit = true;
    }
    sh.cv.notify_all();
    th.join();
    if (opt.belt_mm_s != 0) des_step_stop(&hw);
    hdmi.close();
    double el = now_s() - t_start;
    printf("\n== live: %llu frames in %.2f s = %.2f fps shown, skipped %llu; display avg %.2f ms max %.2f ms; "
           "worker processed %llu (%.1f%%), stale %llu\n",
           (unsigned long long)frames, el, frames / el, (unsigned long long)skipped,
           frames ? sum_draw / frames * 1e3 : 0.0, max_draw * 1e3, (unsigned long long)snap.processed,
           frames ? 100.0 * snap.processed / frames : 0.0, (unsigned long long)snap.stale);
    return 0;
}

}  // namespace des
