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
#include <map>
#include <memory>
#include <set>
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
#include "hmi.hpp"
#include "locate.hpp"
#include "npu_async.hpp"
#include "part_log.hpp"
#include "picker.hpp"
#include "track_measure.hpp"
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
    std::string meas;   // measurement summary for the label (empty = not measured)
};

std::string meas_text(const PartMeasure &m)
{
    if (!m.valid) return "";
    char b[48];
    if (m.kind == 'g')
        snprintf(b, sizeof b, "OD%.2f z%d%s", m.size, m.teeth, m.teeth_judged ? "" : "?");
    else
        snprintf(b, sizeof b, "L%.2f p%.3f%s", m.size, m.pitch, m.teeth_judged ? "" : "?");
    return b;
}

struct Snapshot {
    uint64_t seq = 0;                           // capture sequence number the result belongs to
    uint32_t frame = 0;
    std::vector<std::array<float, 8>> blobs;    // raw blobs of that frame, full-res px
    std::vector<TrackView> tracks;
    LocateTiming tm;
    double work_ms = 0;
    double meas_ms = 0;                         // geometric measurement in that cycle (0 = none)
    bool npu = false;                           // classifier loaded
    float npu_ms = 0, npu_pre_ms = 0;
    uint64_t npu_runs = 0, npu_dropped = 0;
    uint64_t measured = 0, meas_failed = 0;
    uint64_t logged = 0, log_images = 0, log_dropped = 0;
    BeltPlan plan;
    Tracker::Stats st;
    uint64_t processed = 0, stale = 0;
    std::string run_line;                       // Runner status (run mode)
    cv::Mat panel;                              // status panel, rendered by the worker (~10 Hz), blitted per frame
    cv::Mat hmi;                                // operator interface band (Hmi::H rows at Hmi::Y0), same scheme
};

struct Shared {
    std::mutex m;
    std::condition_variable cv;
    bool has_job = false;
    CapturedFrame job;
    uint64_t job_seq = 0;
    Snapshot snap;
    std::atomic<uint64_t> cap_seq{0};
    std::atomic<float> disp_fps{0};             // display stats for the panel text (written by the main thread)
    std::atomic<uint64_t> disp_skipped{0};
    bool quit = false;
};

// Hershey text is expensive (three lines at thickness 2 ~5 ms): rendered here on core 1, never on the capture core.
cv::Mat render_panel(const Snapshot &s, const Shared &sh, uint32_t pl_frame, uint64_t lag, double belt_mm)
{
    static const char *plans[] = {"run", "stop-at", "hold"};
    char l1[160], l2[160], l3[160];
    snprintf(l1, sizeof l1, "DragonEyeSorter %s  %.1f fps  PL frame %u  skipped %llu  worker lag %llu",
             s.run_line.empty() ? "live" : "run", sh.disp_fps.load(), pl_frame,
             (unsigned long long)sh.disp_skipped.load(), (unsigned long long)lag);
    char npu[64] = "npu off";
    if (s.npu) snprintf(npu, sizeof npu, "npu %.1f ms (%llu)", s.npu_ms, (unsigned long long)s.npu_runs);
    snprintf(l2, sizeof l2, "locate %.1f ms  measure %.1f ms (%llu/%llu)  %s  worker %.1f ms  tracks %zu  belt %.1f mm",
             (s.tm.gray_us + s.tm.thresh_us + s.tm.contour_us) / 1e3, s.meas_ms, (unsigned long long)s.measured,
             (unsigned long long)(s.measured + s.meas_failed), npu, s.work_ms, s.tracks.size(), belt_mm);
    snprintf(l3, sizeof l3, "pick %llu  flap %llu  recheck %llu  noise %llu  belt plan %s  saved %llu (img %llu)",
             (unsigned long long)s.st.decided_pick, (unsigned long long)s.st.decided_flap,
             (unsigned long long)s.st.decided_recheck, (unsigned long long)s.st.noise, plans[s.plan.kind],
             (unsigned long long)s.logged, (unsigned long long)s.log_images);
    cv::Mat panel(118, 1150, CV_8UC3, cv::Scalar(0, 0, 0));
    cv::putText(panel, l1, cv::Point(14, 34), cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 0), 2);
    cv::putText(panel, l2, cv::Point(14, 70), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);
    cv::putText(panel, s.run_line.empty() ? l3 : s.run_line.c_str(), cv::Point(14, 104), cv::FONT_HERSHEY_SIMPLEX, 0.7,
                s.run_line.empty() ? cv::Scalar(255, 255, 255) : cv::Scalar(0, 255, 255), 2);
    return panel;
}

void worker(Shared &sh, des_hw_t &hw, const AppConfig &cfg, Runner *runner, const LiveOptions &opt, UiInput *input)
{
    pin_to_core(1);
    sched_param sp{};
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp);   // real-time priority stays with the capture loop

    Locator loc(cfg.locate);
    Tracker tr(cfg.tracker);
    TrackMeasurer meas(cfg.measure, cfg.px_to_table);
    PartLog plog;
    {
        std::string e = plog.open(cfg.storage);
        if (!e.empty()) fprintf(stderr, "storage: %s (parts are NOT recorded)\n", e.c_str());
        else if (plog.is_open()) printf("storage: %s (images: %s)\n", plog.csv_path.c_str(), cfg.storage.images.c_str());
    }
    std::unique_ptr<Collector> col;
    if (!opt.collect.label.empty()) {
        col.reset(new Collector(opt.collect, cfg));
        std::string e = col->open();
        if (!e.empty()) {
            fprintf(stderr, "collect: %s\n", e.c_str());
            col.reset();
        } else {
            printf("collect: %s (class %d %s, every %d frames, max %d per part%s)\n", col->dir.c_str(),
                   opt.collect.label_id, opt.collect.label.c_str(), opt.collect.every, opt.collect.max_per_part,
                   opt.collect.full_every > 0 ? ", + full frames" : "");
        }
    }
    AsyncClassifier npu;
    if (cfg.npu.enabled) {
        std::string e = npu.start(cfg.npu.model, cfg.npu.core);
        if (!e.empty()) fprintf(stderr, "npu: %s (parts get no NPU class)\n", e.c_str());
        else printf("npu: %s, %s\n", cfg.npu.model.c_str(), npu.model().describe().c_str());
    }
    // operator interface: parameters from ui_params.json override des_app.json, applied here (the worker owns them)
    Hmi hmi(cfg, cfg.config_dir + "/ui_params.json");
    if (!hmi.fonts_ok()) fprintf(stderr, "hmi: Chinese font not found, ASCII fallback\n");
    auto apply_params = [&](const HmiParams &p) {
        SizeRule &r = tr.config().size;
        r.enabled = p.size_rule;
        r.gear_od = p.gear_od;
        r.gear_od_tol = p.gear_od_tol;
        r.rack_len = p.rack_len;
        r.rack_len_tol = p.rack_len_tol;
        tr.config().min_conf = p.min_conf;
        plog.set_images(p.images == 0 ? "none" : p.images == 2 ? "all" : "ng");
        if (runner) runner->set_speed(p.belt_speed, "HMI");
        des_vis_config(&hw, p.pl_mode, 0, p.pl_thr, 200);   // PL display mode + threshold (no-op before v6)
    };
    // PL coarse measurement (des_vision ROI statistics): the ROI follows the part nearest the optical axis; results
    // count for that part once its ROI has been in force for a whole frame (settings apply at the next frame start,
    // statistics are published at the one after)
    struct PlMeas {
        uint32_t area = 0;
        float d = 0, w = 0, h = 0;
    };
    std::map<int, PlMeas> pl_meas;
    struct NpuStat {
        int n = 0;
        double npu_ms = 0, lat_ms = 0;
    };
    std::map<int, NpuStat> npu_stat;   // per track, for the record (test report: AI time / latency)
    int roi_track = -1;
    uint32_t roi_seq = 0;
    const bool pl_vision = hw.version >= DES_VERSION_VISION;
    const Homography hinv = cfg.px_to_table.inverse();
    apply_params(hmi.params());
    cv::Mat hmi_panel;
    double last_hmi = 0, prev_work_ms = 0;
    std::map<int, cv::Mat> crops;   // latest part crop per track (from its best measurement frame)
    std::set<int> logged;
    Picker pk(cfg.picker);
    const double mmps = cfg.belt.mm_per_step * cfg.belt.dir;
    const uint64_t max_age = uint64_t(cfg.camera.buffers > 2 ? cfg.camera.buffers - 1 : 1);
    uint64_t processed = 0, stale = 0;
    cv::Mat panel;
    double last_panel = 0;

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
        if (col)   // training crops; dropped if the DMA reused the buffer while they were copied
            col->step(tr, f.bgra, f.width, f.height, f.stride, obs.belt_mm, f.pl_frame, obs.dets, blobs,
                      [&] { return sh.cap_seq.load() - seq < max_age; });
        // geometric measurement of (at most per_frame) parts nearest the optical axis; it reads the camera buffer
        // again, so its results are dropped if the DMA reused the buffer meanwhile
        auto mres = meas.measure(tr, f.bgra, f.width, f.height, f.stride, obs.belt_mm, b1 - b0, f.pl_frame);
        if (!mres.empty() && sh.cap_seq.load() - seq < max_age) {
            TrackMeasurer::apply(tr, mres);
            for (MeasureResult &r : mres)
                if (!r.crop.empty()) {
                    crops[r.id] = r.crop;
                    npu.submit(r.id, r.crop, f.t_us);   // classified on the NPU thread, voted in a later cycle
                }
        }

        for (const NpuResult &v : npu.poll())   // NPU soft votes (before decisions)
            if (tr.vote(v.id, v.probs)) {
                NpuStat &ns = npu_stat[v.id];
                ns.n++;
                ns.npu_ms += v.npu_ms;
                ns.lat_ms += v.lat_ms;
            }

        Picker::Output out;
        if (runner) {
            runner->cycle(tr, pk);   // state machine, belt, arm, CAN
        } else {
            Picker::Input in;
            in.belt_mm = des_step_pos(&hw) * mmps;
            double v = des_step_speed(&hw) * cfg.belt.mm_per_step;
            in.brake_mm = v * v / (2 * cfg.belt.accel);
            in.belt_stopped = !(des_step_status(&hw) & DES_ST_STATUS_MOVING);
            out = pk.tick(tr, in);
            if (out.have_job) pk.job_done(tr, out.job.track_id, true);   // dry: pretend the pick succeeded
        }
        processed++;

        // local storage: one record per newly decided part (+ its crop for NG / recheck)
        for (const Track &t : tr.tracks()) {
            if (t.state == TrackState::New || t.state == TrackState::Tracking || t.disp == Disposition::None) continue;
            if (!logged.insert(t.id).second) continue;
            PartRecord rec;
            rec.wall_ms = wall_now_ms();   // + offset from the PLC's TIME frame (no RTC on the board)
            rec.t_s = plog.now_s();
            rec.track_id = t.id;
            rec.frame_first = t.first_frame;
            rec.frame_last = t.last_frame;
            rec.cls = t.cls;
            rec.label = t.cls >= 0 && size_t(t.cls) < cfg.tracker.rules.size() ? cfg.tracker.rules[t.cls].label
                                                                              : "unclassified";
            rec.conf = t.cls_conf;
            rec.disp = to_str(t.disp);
            const auto &names = t.disp == Disposition::Pick ? cfg.bins : cfg.outlets;
            rec.dest = t.disp == Disposition::Recheck ? (cfg.outlets.empty() ? "recheck" : cfg.outlets[0])
                       : t.dest >= 0 && size_t(t.dest) < names.size() ? names[t.dest] : std::to_string(t.dest);
            rec.why = t.why;
            rec.meas = t.meas;
            rec.s = t.s; rec.y = t.y; rec.axis = t.axis; rec.len = t.len; rec.wid = t.wid;
            auto it = crops.find(t.id);
            auto pm = pl_meas.find(t.id);
            if (pm != pl_meas.end()) {
                rec.pl_area = pm->second.area;
                rec.pl_d_mm = pm->second.d;
                rec.pl_w_mm = pm->second.w;
                rec.pl_h_mm = pm->second.h;
                pl_meas.erase(pm);
            }
            auto ns = npu_stat.find(t.id);
            if (ns != npu_stat.end()) {
                rec.votes = ns->second.n;
                rec.npu_ms = float(ns->second.npu_ms / ns->second.n);
                rec.lat_ms = float(ns->second.lat_ms / ns->second.n);
                npu_stat.erase(ns);
            }
            hmi.add_part(rec, it != crops.end() ? it->second : cv::Mat(), now_s());
            plog.add(std::move(rec), it != crops.end() ? it->second : cv::Mat());
            if (it != crops.end()) crops.erase(it);
        }
        if (processed % 120 == 0) {   // forget crops of tracks that vanished undecided, and old ids
            for (auto it = crops.begin(); it != crops.end();) it = tr.find(it->first) ? std::next(it) : crops.erase(it);
            while (logged.size() > 5000) logged.erase(logged.begin());
        }

        // PL coarse measurement: read the statistics of the ROI set earlier, then aim the ROI at the part nearest
        // the optical axis (fully in view)
        if (pl_vision) {
            des_vis_t vs;
            des_vis_read(&hw, &vs);
            if (roi_track >= 0 && vs.seq >= roi_seq + 2 && vs.area > 0 && vs.x0 <= vs.x1) {
                const Vec2 c = cfg.px_to_table.map({float(vs.cx), float(vs.cy)});
                const Vec2 cx1 = cfg.px_to_table.map({float(vs.cx) + 1, float(vs.cy)});
                const float mm_px = norm(cx1 - c);
                PlMeas m;
                m.area = vs.area;
                m.d = 2 * std::sqrt(vs.area / kPi) * mm_px;   // area-equivalent diameter
                m.w = norm(cfg.px_to_table.map({float(vs.x1), float(vs.y0)}) - cfg.px_to_table.map({float(vs.x0), float(vs.y0)}));
                m.h = norm(cfg.px_to_table.map({float(vs.x0), float(vs.y1)}) - cfg.px_to_table.map({float(vs.x0), float(vs.y0)}));
                if (tr.find(roi_track)) pl_meas[roi_track] = m;
            }
            const Track *best = nullptr;
            float best_d = 1e9f;
            const Vec2 axis = cfg.px_to_table.map({0.5f * f.width, 0.5f * f.height});
            for (const Track &t : tr.tracks()) {
                if ((t.state != TrackState::Tracking && t.state != TrackState::New) || t.merged || t.n_geom == 0) continue;
                const float d = norm(Vec2{t.x_at(obs.belt_mm), t.y} - axis);
                if (d < best_d) { best_d = d; best = &t; }
            }
            if (best && best->id != roi_track) {
                float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
                for (const Vec2 &v : best->footprint(obs.belt_mm, 4.f).corners()) {
                    const Vec2 p = hinv.map(v);
                    x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); y1 = std::max(y1, p.y);
                }
                des_vis_roi(&hw, std::max(0, int(x0)), std::max(0, int(y0)), std::min(f.width - 1, int(x1)),
                            std::min(f.height - 1, int(y1)));
                roi_track = best->id;
                roi_seq = vs.seq;
            }
            if (pl_meas.size() > 200) pl_meas.erase(pl_meas.begin());
        }

        // operator interface: status in, clicks / keys -> parameter changes or run commands, redraw ~8 Hz
        {
            HmiStatus hs;
            hs.mode = runner ? "run" : col ? "collect" : "live";
            hs.run_state = runner ? int(runner->state()) : -1;
            hs.belt_mm_s = des_step_speed(&hw) * cfg.belt.mm_per_step;
            hs.worker_ms = float(prev_work_ms);
            hs.npu_ms = npu.last_npu_ms;
            hs.npu = npu.running();
            hs.pl_vision = pl_vision;
            hs.clock_synced = wall_synced();
            hs.inputs = input ? input->devices() : 0;
            hs.tracks = int(tr.tracks().size());
            hs.logged = plog.written;
            hs.log_images = plog.images;
            hmi.status(hs, now_s());
            if (input)
                for (const HmiAction &a : hmi.handle(input->take())) {
                    if (a.kind == HmiAction::Params) apply_params(hmi.params());
                    else if (runner) runner->command(uint8_t(a.cmd), "HMI");
                }
            if (now_s() - last_hmi >= 0.12) {
                hmi_panel = hmi.render(now_s());
                last_hmi = now_s();
            }
        }

        Snapshot s;
        s.seq = seq;
        s.frame = f.pl_frame;
        s.blobs = std::move(blobs);
        s.tm = tm;
        s.meas_ms = mres.empty() ? 0 : meas.last_ms;
        s.measured = meas.measured;
        s.meas_failed = meas.failed;
        s.npu = npu.running();
        s.npu_ms = npu.last_npu_ms;
        s.npu_pre_ms = npu.last_pre_ms;
        s.npu_runs = npu.runs;
        s.npu_dropped = npu.dropped;
        s.logged = plog.written;
        s.log_images = plog.images;
        s.log_dropped = plog.dropped;
        s.plan = out.belt;
        s.st = tr.stats;
        s.processed = processed;
        s.stale = stale;
        if (runner) s.run_line = runner->status_line();
        else if (col) s.run_line = col->status();
        for (const Track &t : tr.tracks())
            if (t.state != TrackState::Done)
                s.tracks.push_back({t.id, t.state, t.disp, t.s, t.y, t.len, t.wid, t.axis, t.why, meas_text(t.meas)});
        tr.retire(des_step_pos(&hw) * mmps);
        s.work_ms = (now_s() - t0) * 1e3;
        prev_work_ms = s.work_ms;
        s.hmi = hmi_panel;
        if (now_s() - last_panel >= 0.1) {   // after the snapshot is complete (tracks, timing)
            panel = render_panel(s, sh, f.pl_frame, sh.cap_seq.load() - seq, obs.belt_mm);
            last_panel = now_s();
        }
        s.panel = panel;   // reference-counted: the main thread only reads it
        std::lock_guard<std::mutex> lk(sh.m);
        sh.snap = std::move(s);
    }
    if (col) {
        col->finish();
        printf("%s -> %s\n", col->status().c_str(), col->dir.c_str());
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
        cv::putText(img, label, cv::Point2f(m.x + 6, Hmi::Y0 - 20), cv::FONT_HERSHEY_SIMPLEX, 0.7, col, 2);
}

void draw(cv::Mat &img, const Snapshot &s, const Homography &hinv, double belt_mm, const AppConfig &cfg)
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
        snprintf(text, sizeof text, "#%d %s %s", t.id,
                 t.st == TrackState::Decided || t.st == TrackState::Picking ? to_str(t.disp) : to_str(t.st),
                 t.meas.c_str());
        // thickness 1: Hershey text at thickness 2 is drawn as many filled thick segments (~1 ms per label)
        cv::putText(img, text, cv::Point(left, std::max(top - 10, 20)), cv::FONT_HERSHEY_SIMPLEX, 0.8, col, 1);
    }
    line_at_x(img, hinv, cfg.tracker.decide_x, "decide", cv::Scalar(255, 255, 255));
    line_at_x(img, hinv, cfg.picker.pick_x, "pick", cv::Scalar(0, 210, 0));

    // status panel: rendered by the worker, blitted with wide stores (the display buffer is uncached)
    if (!s.panel.empty())
        for (int r = 0; r < s.panel.rows; r++) memcpy(img.ptr(r), s.panel.ptr(r), size_t(s.panel.cols) * 3);
    // operator interface band at the bottom (same scheme)
    if (!s.hmi.empty() && s.hmi.cols == img.cols)
        for (int r = 0; r < s.hmi.rows; r++) memcpy(img.ptr(Hmi::Y0 + r), s.hmi.ptr(r), size_t(s.hmi.cols) * 3);
}

void draw_cursor(cv::Mat &img, const UiInput &in)
{
    int x, y;
    bool vis;
    in.cursor(x, y, vis);
    if (!vis) return;
    const cv::Point pts[] = {{x, y}, {x, y + 22}, {x + 6, y + 17}, {x + 11, y + 27}, {x + 15, y + 25}, {x + 10, y + 15},
                             {x + 17, y + 15}};
    cv::fillConvexPoly(img, pts, 3, cv::Scalar(255, 255, 255));
    std::vector<cv::Point> poly(pts, pts + 7);
    cv::polylines(img, poly, true, cv::Scalar(0, 0, 0), 1, cv::LINE_8);
    cv::line(img, pts[0], pts[1], cv::Scalar(255, 255, 255), 2);
    cv::line(img, pts[0], pts[6], cv::Scalar(255, 255, 255), 2);
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
    if (opt.belt_mm_s != 0 && !opt.run) {
        des_step_vel(&hw, opt.belt_mm_s / cfg.belt.mm_per_step * cfg.belt.dir, cfg.belt.accel / cfg.belt.mm_per_step);
        printf("belt: velocity mode %.1f mm/s (picker plans are only displayed)\n", opt.belt_mm_s);
    }

    std::unique_ptr<Runner> runner;
    if (opt.run) {
        runner.reset(new Runner(&hw, cfg, opt.run_opt));
        err = runner->start();
        if (!err.empty()) {
            fprintf(stderr, "run: %s\n", err.c_str());
            return 1;
        }
    }
    Shared sh;
    UiInput input(1920, 1080);   // USB mouse / keyboard / touch for the operator interface (hot plug)
    input.start();
    std::thread th(worker, std::ref(sh), std::ref(hw), std::cref(cfg), runner.get(), std::cref(opt), &input);
    pin_to_core(0);

    uint64_t frames = 0, skipped = 0, sec_frames = 0;
    double t_start = now_s(), t_sec = t_start, fps = 0, sum_draw = 0, max_draw = 0, sum_conv = 0, sum_ovl = 0;
    // debug switches for timing breakdowns (env): LIVE_NO_WORKER=1 / LIVE_NO_OVERLAY=1
    const bool no_worker = getenv("LIVE_NO_WORKER") != nullptr, no_overlay = getenv("LIVE_NO_OVERLAY") != nullptr;
    CapturedFrame f;
    Snapshot snap;
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
        // the bottom band is covered by the operator interface: convert only the rows that stay visible
        const int vis_rows = snap.hmi.empty() ? f.height : std::min(f.height, Hmi::Y0);
        cv::cvtColor(src.rowRange(0, vis_rows), dst.rowRange(0, vis_rows), cv::COLOR_BGRA2BGR);
        double tc = now_s();
        sh.disp_fps.store(float(fps));
        sh.disp_skipped.store(skipped);
        if (!no_overlay) draw(dst, snap, hinv, 0.5 * (f.step_pos + f.step_pos_end) * mmps, cfg);
        draw_cursor(dst, input);
        double td = now_s();
        if (!opt.dump.empty() && seq == 180) {
            cv::Mat copy = dst.clone();   // slow read from the uncached buffer, once
            printf("dump %s: %s\n", opt.dump.c_str(), cv::imwrite(opt.dump, copy) ? "ok" : "FAILED");
        }
        if (input.take_snap()) {   // FIFO "snap" (remote tests of the interface)
            static int nsnap = 0;
            char path[64];
            snprintf(path, sizeof path, "/tmp/des_snap_%d.png", nsnap++);
            cv::imwrite(path, dst.clone());
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
                   "%zu tracks, stale %llu, measured %llu (failed %llu)\n",
                   fps, sum_draw / frames * 1e3, sum_conv / frames * 1e3, sum_ovl / frames * 1e3,
                   (unsigned long long)snap.processed,
                   (snap.tm.gray_us + snap.tm.thresh_us + snap.tm.contour_us) / 1e3, snap.tracks.size(),
                   (unsigned long long)snap.stale, (unsigned long long)snap.measured,
                   (unsigned long long)snap.meas_failed);
            for (const TrackView &t : snap.tracks)
                if (!t.meas.empty()) printf("    track #%d %s: %s\n", t.id, to_str(t.st), t.meas.c_str());
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
    input.stop();
    if (opt.belt_mm_s != 0 && !opt.run) des_step_stop(&hw);
    runner.reset();   // stops the arm thread and the belt
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
