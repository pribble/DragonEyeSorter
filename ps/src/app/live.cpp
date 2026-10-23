// live.cpp — see live.hpp.
// Core 0 (SCHED_FIFO, caller's thread): capture every frame, hand the newest to the worker, convert it to BGR into
// the HDMI back buffer, draw the latest snapshot re-projected to this frame's belt travel, flip. Core 1 (worker,
// SCHED_OTHER): locate -> tracker -> picker on the newest frame it can get (frames arriving while it is busy are
// skipped by the worker only, never by the display). The snapshot is published as an immutable shared_ptr: the
// capture thread copies the pointer (no strings / vectors / cv::Mat data) and reads it for one frame.
#include "live.hpp"
#include <pthread.h>
#include <sched.h>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <opencv2/core/utility.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include "arm_io.hpp"
#include "display.hpp"
#include "hmi.hpp"
#include "locate.hpp"
#include "npu.hpp"
#include "part_log.hpp"
#include "picker.hpp"
#include "track_measure.hpp"
#include "tracker.hpp"
#include "ui_input.hpp"
#include "util.hpp"
namespace des {
namespace {
struct TrackView {
    int id = 0;
    TrackState st = TrackState::New;
    Disposition disp = Disposition::None;
    float s = 0, y = 0, len = 0, wid = 0, axis = 0;
    char meas[48] = {0};   // measurement summary for the label (empty = not measured)
};

// One processed frame, immutable once published (no strings; the status line lives in the panel).
struct Snapshot {
    std::vector<std::array<float, 8>> blobs;    // raw blobs of that frame, full-res px
    std::vector<TrackView> tracks;
    LocateTiming tm;
    double work_ms = 0;
    double meas_ms = 0;                         // geometric measurement in that cycle (0 = none)
    bool npu = false;                           // classifier loaded
    float npu_ms = 0;
    uint64_t npu_runs = 0;
    uint64_t measured = 0, meas_failed = 0;
    uint64_t logged = 0, log_images = 0;
    BeltPlan plan;
    Tracker::Stats st;
    uint64_t processed = 0, stale = 0;
    cv::Mat panel;                              // status panel, rendered by the worker (~10 Hz), blitted per frame
    cv::Mat hmi;                                // operator interface band (Hmi::H rows at Hmi::Y0)
};
struct Shared {
    std::mutex m;
    std::condition_variable cv;
    bool has_job = false;
    CapturedFrame job;
    uint64_t job_seq = 0;
    std::shared_ptr<const Snapshot> snap;       // newest published snapshot
    std::atomic<uint64_t> cap_seq{0};
    std::atomic<float> disp_fps{0};             // display stats for the panel text (written by the main thread)
    std::atomic<uint64_t> disp_skipped{0};
    bool quit = false;
};
// out-of-range dest: numeric fallbacks live in a process-lifetime pool owned by the worker (old: to_string(dest)).
const char *dest_fallback(int dest)
{
    static std::map<int, std::string> pool;
    auto it = pool.find(dest);
    if (it == pool.end()) it = pool.emplace(dest, std::to_string(dest)).first;
    return it->second.c_str();
}
// Screen layout (1920 x 1080): camera picture scaled to 3/4 (1440 x 780 = source rows 20..1059, 20 rows / ~4 mm of
// belt edge cut top and bottom; scale34, OpenCV resize cannot keep 60 fps), status panel right (480 x 780), operator
// band (Hmi, 1920 x 300) below. Nothing is drawn over the picture except its own overlays.
constexpr int kVidW = 1440, kVidH = Hmi::Y0;
constexpr double kVidScale = 0.75;
constexpr int kSrcH = kVidH * 4 / 3, kCropY = (1080 - kSrcH) / 2;   // source rows shown, first row shown
constexpr int kPanelX = kVidW, kPanelW = 1920 - kVidW, kPanelH = Hmi::Y0;
// Hershey text is expensive: rendered here on core 1 (~10 Hz), never on the capture core, which only blits it.
cv::Mat render_panel(const Snapshot &s, const Shared &sh, uint32_t pl_frame, uint64_t lag, double belt_mm,
                     const std::string &run_line)
{
    static const char *plans[] = {"run", "stop-at", "hold"};
    cv::Mat panel(kPanelH, kPanelW, CV_8UC3, cv::Scalar(34, 31, 29));
    cv::line(panel, {0, 0}, {0, kPanelH - 1}, cv::Scalar(80, 76, 72), 2);
    int y = 40;
    auto line = [&](const std::string &t, const cv::Scalar &c, double size = 0.62, int gap = 34) {
        cv::putText(panel, t, cv::Point(18, y), cv::FONT_HERSHEY_SIMPLEX, size, c, 1, cv::LINE_AA);
        y += gap;
    };
    const cv::Scalar white(235, 235, 235), dim(160, 160, 160), green(90, 220, 90), yellow(70, 215, 235);
    char b[160];
    line(std::string("DragonEyeSorter  ") + (run_line.empty() ? "live" : "run"), green, 0.85, 46);
    snprintf(b, sizeof b, "display %.1f fps   skipped %llu", sh.disp_fps.load(), (unsigned long long)sh.disp_skipped.load());
    line(b, white);
    snprintf(b, sizeof b, "PL frame %u   worker lag %llu", pl_frame, (unsigned long long)lag);
    line(b, dim, 0.62, 48);
    snprintf(b, sizeof b, "locate %.1f ms   worker %.1f ms", (s.tm.gray_us + s.tm.thresh_us + s.tm.contour_us) * 1e-3,
             s.work_ms);
    line(b, white);
    snprintf(b, sizeof b, "measure %.1f ms  (%llu / %llu)", s.meas_ms, (unsigned long long)s.measured,
             (unsigned long long)(s.measured + s.meas_failed));
    line(b, white);
    if (s.npu) snprintf(b, sizeof b, "npu %.1f ms  (%llu runs)", s.npu_ms, (unsigned long long)s.npu_runs);
    else snprintf(b, sizeof b, "npu off");
    line(b, white);
    snprintf(b, sizeof b, "tracks %zu   belt %.1f mm", s.tracks.size(), belt_mm);
    line(b, white, 0.62, 48);
    snprintf(b, sizeof b, "pick %llu   flap %llu   recheck %llu", (unsigned long long)s.st.decided_pick,
             (unsigned long long)s.st.decided_flap, (unsigned long long)s.st.decided_recheck);
    line(b, white);
    snprintf(b, sizeof b, "noise %llu   belt plan %s", (unsigned long long)s.st.noise, plans[s.plan.kind]);
    line(b, white);
    snprintf(b, sizeof b, "saved %llu  (images %llu)", (unsigned long long)s.logged, (unsigned long long)s.log_images);
    line(b, white, 0.62, 48);
        const int wrap_px = kPanelW - 2 * 18;
    auto fits = [&](const std::string &t) {
        int base = 0;
        return cv::getTextSize(t, cv::FONT_HERSHEY_SIMPLEX, 0.62, 1, &base).width <= wrap_px;
    };
    std::string word, cur;
    auto flush = [&] { if (!cur.empty()) line(cur, yellow, 0.62, 30); cur.clear(); };
    for (size_t i = 0; i <= run_line.size(); i++) {
        const char ch = i < run_line.size() ? run_line[i] : ' ';
        if (ch != ' ') { word += ch; continue; }
        if (word.empty()) continue;
        if (!cur.empty() && !fits(cur + " " + word)) flush();
        cur += (cur.empty() ? "" : " ") + word;
        word.clear();
    }
    flush();
    return panel;
}
void worker(Shared &sh, des_hw_t &hw, const AppConfig &cfg, Runner *runner, const LiveOptions &opt, UiInput *input)
{
    (void)pin_to_core(1);
    sched_param sp{};
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp);   // real-time priority stays with the capture loop
    Locator loc(cfg.locate);
    Tracker tr(cfg.tracker);
    TrackMeasurer meas(cfg.measure, cfg.px_to_table, cfg.px_to_part);
    PartLog plog;
    std::string e = plog.open(cfg.storage);
    if (!e.empty()) fprintf(stderr, "storage: %s (parts are NOT recorded)\n", e.c_str());
    else if (plog.is_open()) printf("storage: %s (images: %s)\n", plog.csv_path.c_str(), cfg.storage.images.c_str());
    std::unique_ptr<Collector> col;
    if (!opt.collect.label.empty()) {
        col.reset(new Collector(opt.collect, cfg));
        e = col->open();
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
        e = npu.start(cfg.npu.model, cfg.npu.core);
        if (!e.empty()) fprintf(stderr, "npu: %s (parts get no NPU class)\n", e.c_str());
        else printf("npu: %s, %s\n", cfg.npu.model.c_str(), npu.model().describe().c_str());
    }
        Hmi hmi(cfg, cfg.config_dir + "/ui_params.json");
    if (!hmi.fonts_ok()) fprintf(stderr, "hmi: Chinese font not found, ASCII fallback\n");
        ArmManual arm_manual(&hw, cfg.arm, cfg.arm_home_taught, cfg.config_dir + "/data/arm_teach.csv",
                         cfg.config_dir + "/arm_home.json");
    auto apply_params = [&](const HmiParams &p) {
        SizeRule &r = tr.config().size;
        if (const Recipe *rc = find_recipe(cfg.recipes, p.recipe)) rc->apply(r);   // then the operator's sizes
        r.enabled = p.size_rule;
        r.gear_od = p.gear_od; r.gear_od_tol = p.gear_od_tol; r.gear_bore = p.gear_bore; r.gear_bore_tol = p.gear_bore_tol;
        tr.config().min_conf = p.min_conf;
        plog.set_images(p.images == 0 ? "none" : p.images == 2 ? "all" : "ng");
        if (runner) runner->set_speed(p.belt_speed, "HMI");
        des_vis_config(&hw, p.pl_mode, 0, p.pl_thr, 200);   // PL display mode + threshold (no-op before v6)
    };
    struct PlMeas { uint32_t area = 0; float d = 0, w = 0, h = 0; };
    std::map<int, PlMeas> pl_meas;
    struct NpuStat { int n = 0; double npu_ms = 0, lat_ms = 0; };   // per track, for the record
    std::map<int, NpuStat> npu_stat;
    int roi_track = -1;
    uint32_t roi_seq = 0;
    const bool pl_vision = hw.version >= DES_VERSION_VISION;
    const Homography hinv = cfg.px_to_part.inverse();   // track positions -> image (PL ROI)
    apply_params(hmi.params());
    std::vector<std::shared_ptr<Snapshot>> snap_pool;
    auto acquire_snapshot = [&]() -> std::shared_ptr<Snapshot> {
        for (auto &p : snap_pool)
            if (p.use_count() == 1) return p;
        snap_pool.push_back(std::make_shared<Snapshot>());
        return snap_pool.back();
    };
    cv::Mat hmi_panel;
    std::string run_line;
    double last_hmi = 0, prev_work_ms = 0;
        struct { bool live = false, have_ref = false; int32_t first = 0, ref = 0; double ref_t = 0, mm_s = 0; } enc;
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
        const double now = mono_s();
        const double t0 = now;
        auto fresh = [&] { return sh.cap_seq.load() - seq < max_age; };   // DMA has not reused the buffer yet
        auto s = acquire_snapshot();
        FrameObs obs;
        obs.frame = f.pl_frame;
        const double b0 = f.step_pos * mmps, b1 = f.step_pos_end * mmps;
        obs.belt_mm = 0.5 * (b0 + b1);
        LocateTiming tm;
        obs.dets = loc.run(f.bgra, f.width, f.height, f.stride, cfg.px_to_part, &tm);   // where parts stand
        // the DMA rewrites a buffer buffers-1 frames after it was handed out: drop results read from a reused one
        if (!fresh()) { stale++; continue; }
        const auto &blobs = loc.rects_full();
        for (Detection &d : obs.dets) d.p.x -= float((b1 - b0) * (d.img_v - 0.5f));   // rolling-shutter row timing
        tr.update(obs);
        if (col)   // training crops; dropped if the DMA reused the buffer while they were copied
            col->step(tr, f.bgra, f.width, f.height, f.stride, obs.belt_mm, f.pl_frame, obs.dets, blobs, fresh);
        // measurement reads the camera buffer again: drop the results if the DMA reused it (the cycle goes on)
        auto mres = meas.measure(tr, f.bgra, f.width, f.height, f.stride, obs.belt_mm, b1 - b0, f.pl_frame);
        if (!mres.empty() && fresh()) {
            TrackMeasurer::apply(tr, mres);
            for (MeasureResult &r : mres)
                if (!r.crop.empty()) {
                    crops[r.id] = r.crop;
                    npu.submit(r.id, r.crop, f.t_us);   // classified on the NPU thread, voted in a later cycle
                }
        }
        NpuResult v;
        while (npu.poll(v))   // NPU soft votes (before decisions)
            if (tr.vote(v.id, v.probs, v.n_probs)) {
                NpuStat &ns = npu_stat[v.id];
                ns.n++; ns.npu_ms += v.npu_ms; ns.lat_ms += v.lat_ms;
            }
        Picker::Output out;
        if (runner) {
            runner->cycle(tr, pk);   // state machine, belt, arm, CAN
        } else {
            Picker::Input in = picker_input(nullptr, &hw, cfg.belt);
            out = pk.tick(tr, in);
            if (out.have_job) pk.job_done(tr, out.job.track_id, true);   // dry: pretend the pick succeeded
        }
        processed++;
        const uint8_t rec_recipe = uint8_t(hmi.params().recipe);
        const Track *roi_best = nullptr;
        float roi_best_d = 1e9f;
        const Vec2 axis = cfg.px_to_table.map({0.5f * f.width, 0.5f * f.height});
        s->tracks.clear();
        for (const Track &t : tr.tracks()) {   // one pass: records, snapshot view, PL ROI candidate
            // a Flap part's record waits until its gate is decided for good (else it may still go to recheck)
            const bool gate_pending = !cfg.picker.gates.empty() && t.disp == Disposition::Flap && !t.flap_final;
            if (t.state != TrackState::New && t.state != TrackState::Tracking && t.disp != Disposition::None &&
                !gate_pending && logged.insert(t.id).second) {
                PartRecord rec;
                rec.wall_ms = wall_now_ms();   // + offset from the PLC's TIME frame / Modbus (no RTC on the board)
                rec.t_s = plog.now_s();
                rec.track_id = t.id;
                rec.recipe = rec_recipe;
                rec.frame_first = t.first_frame;
                rec.frame_last = t.last_frame;
                rec.cls = int16_t(t.cls);
                rec.conf = t.cls_conf;
                rec.disp = t.disp;
                rec.label = t.cls >= 0 && size_t(t.cls) < cfg.tracker.rules.size() ? cfg.tracker.rules[t.cls].label.c_str()
                                                                                  : "unclassified";
                const std::vector<std::string> &names = t.disp == Disposition::Pick ? cfg.bins : cfg.outlets;
                if (t.disp == Disposition::Recheck) rec.dest = cfg.outlets.empty() ? "recheck" : cfg.outlets[0].c_str();
                else if (t.dest >= 0 && size_t(t.dest) < names.size()) rec.dest = names[size_t(t.dest)].c_str();
                else rec.dest = dest_fallback(t.dest);
                csv_sanitize(t.why, rec.why, sizeof rec.why);
                rec.meas = t.meas;
                rec.s = t.s; rec.y = t.y; rec.axis = t.axis; rec.len = t.len; rec.wid = t.wid;
                auto it = crops.find(t.id);
                auto pm = pl_meas.find(t.id);
                if (pm != pl_meas.end()) {
                    rec.pl_area = pm->second.area; rec.pl_d_mm = pm->second.d; rec.pl_w_mm = pm->second.w;
                    rec.pl_h_mm = pm->second.h; pl_meas.erase(pm);
                }
                auto ns = npu_stat.find(t.id);
                if (ns != npu_stat.end()) {
                    rec.votes = int16_t(ns->second.n);
                    rec.npu_ms = float(ns->second.npu_ms / ns->second.n);
                    rec.lat_ms = float(ns->second.lat_ms / ns->second.n);
                    npu_stat.erase(ns);
                }
                const cv::Mat &crop = it != crops.end() ? it->second : cv::Mat();
                hmi.add_part(rec, crop, now);
                plog.add(std::move(rec), crop);
                if (it != crops.end()) crops.erase(it);
            }
            if (t.state != TrackState::Done) {
                TrackView tv;
                tv.id = t.id; tv.st = t.state; tv.disp = t.disp;
                tv.s = t.s; tv.y = t.y; tv.len = t.len; tv.wid = t.wid; tv.axis = t.axis;
                // z = spec tooth count from the pitch (a 30-tooth gear with 2 teeth gone stays z30); the defects found
                // are listed after it. Off the axis the teeth are not judged and the count is not shown (it reads
                // 28..30 at random there, 2026-10-24: users took z29 -> z30 near the centre for a vanishing defect).
                if (t.meas.valid && t.meas.kind == 'g' && t.meas.teeth_judged) {
                    int n = snprintf(tv.meas, sizeof tv.meas, "OD%.2f z%d", t.meas.size, t.meas.teeth);
                    if (t.meas.missing_teeth && n < int(sizeof tv.meas))
                        n += snprintf(tv.meas + n, sizeof tv.meas - n, " miss%d", t.meas.missing_teeth);
                    if (t.meas.short_teeth && n < int(sizeof tv.meas))
                        n += snprintf(tv.meas + n, sizeof tv.meas - n, " short%d", t.meas.short_teeth);
                    if (t.meas.flash_gaps && n < int(sizeof tv.meas))
                        snprintf(tv.meas + n, sizeof tv.meas - n, " flash%d", t.meas.flash_gaps);
                } else if (t.meas.valid && t.meas.kind == 'g')
                    snprintf(tv.meas, sizeof tv.meas, "OD%.2f off-axis", t.meas.size);
                else if (t.meas.valid)
                    snprintf(tv.meas, sizeof tv.meas, "L%.2f p%.3f%s", t.meas.size, t.meas.pitch, t.meas.teeth_judged ? "" : "?");
                s->tracks.push_back(tv);
            }
            if (pl_vision && (t.state == TrackState::Tracking || t.state == TrackState::New) && !t.merged && t.n_geom > 0) {
                const float d = norm(Vec2{t.x_at(obs.belt_mm), t.y} - axis);
                if (d < roi_best_d) { roi_best_d = d; roi_best = &t; }
            }
        }
        if (processed % 120 == 0) {   // forget crops of tracks that vanished undecided, and old ids
            for (auto it = crops.begin(); it != crops.end();) it = tr.find(it->first) ? std::next(it) : crops.erase(it);
            for (auto it = npu_stat.begin(); it != npu_stat.end();)
                it = tr.find(it->first) ? std::next(it) : npu_stat.erase(it);
            while (logged.size() > 5000) logged.erase(logged.begin());
        }
        if (pl_vision) {
            des_vis_t vs;
            des_vis_read(&hw, &vs);
            if (roi_track >= 0 && vs.seq >= roi_seq + 2 && vs.area > 0 && vs.x0 <= vs.x1) {
                const Vec2 c = cfg.px_to_table.map({float(vs.cx), float(vs.cy)});
                const Vec2 cx1 = cfg.px_to_table.map({float(vs.cx) + 1, float(vs.cy)});
                const float mm_px = norm(cx1 - c);
                PlMeas m;
                m.area = vs.area; m.d = 2 * std::sqrt(vs.area / kPi) * mm_px;   // area-equivalent diameter
                m.w = norm(cfg.px_to_table.map({float(vs.x1), float(vs.y0)}) - cfg.px_to_table.map({float(vs.x0), float(vs.y0)}));
                m.h = norm(cfg.px_to_table.map({float(vs.x0), float(vs.y1)}) - cfg.px_to_table.map({float(vs.x0), float(vs.y0)}));
                if (tr.find(roi_track)) pl_meas[roi_track] = m;
            }
            if (roi_best && roi_best->id != roi_track) {
                float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
                for (const Vec2 &v : roi_best->footprint(obs.belt_mm, 4.f).corners()) {
                    const Vec2 p = hinv.map(v);
                    x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); y1 = std::max(y1, p.y);
                }
                des_vis_roi(&hw, std::max(0, int(x0)), std::max(0, int(y0)), std::min(f.width - 1, int(x1)),
                            std::min(f.height - 1, int(y1)));
                roi_track = roi_best->id;
                roi_seq = vs.seq;
            }
            if (pl_meas.size() > 200) pl_meas.erase(pl_meas.begin());
        }
        {
            HmiStatus hs;
            hs.run_state = runner ? int(runner->state()) : -1; hs.belt_mm_s = des_step_speed(&hw) * cfg.belt.mm_per_step;
            des_io_t io;
            if (hw.version >= DES_VERSION_IO && des_io_read(&hw, &io) == DES_OK) {
                if (!enc.have_ref) { enc.have_ref = true; enc.first = enc.ref = io.enc; enc.ref_t = now; }
                else if (now - enc.ref_t >= 0.5) {
                    enc.mm_s = std::abs(double(io.enc) - enc.ref) * cfg.belt.enc_scale() / (now - enc.ref_t);
                    enc.ref = io.enc;
                    enc.ref_t = now;
                }
                if (std::abs(double(io.enc) - enc.first) > 20) enc.live = true;   // it has counted: wired
                hs.enc_live = enc.live;
                hs.belt_enc_mm_s = enc.mm_s;
            }
            hs.worker_ms = float(prev_work_ms);
            hs.npu_ms = npu.last_npu_ms; hs.npu = npu.running(); hs.pl_vision = pl_vision;
            hs.clock_synced = wall_synced();
            hs.inputs = input ? input->devices() : 0; hs.jog = runner ? runner->jog_dir() : 0; hs.logged = plog.written;
            {   // manual arm commands only while the line is not running and no pick is being executed
                const int st = runner ? int(runner->state()) : -1;
                const char *why = st == CANP_ST_RUN ? "产线运行中" : st == CANP_ST_ESTOP ? "急停中"
                                : runner && runner->arm_busy() ? "机械臂正在抓取" : nullptr;
                arm_manual.context(hmi.arm_page(), why == nullptr, why);
                hs.arm = arm_manual.view();
            }
            hmi.status(hs, now);
            if (input)
                for (const HmiAction &a : hmi.handle(input->take())) {
                    if (a.kind == HmiAction::Params) apply_params(hmi.params());
                    else if (a.kind == HmiAction::Arm) arm_manual.request(a.arm);
                    else if (a.kind == HmiAction::Recipe) {
                        if (runner) runner->select_recipe(a.cmd, "HMI");   // applied below, same path as the PLC's
                        else if (hmi.set_recipe(a.cmd)) apply_params(hmi.params());
                    } else if (a.kind == HmiAction::Jog) {
                        if (runner) runner->jog(a.cmd, "HMI");
                    } else if (runner) runner->command(uint8_t(a.cmd), "HMI");
                }
            {   // ready pose re-taught on the arm page: the production picks use it from the next one
                std::array<int, 6> home;
                if (arm_manual.take_home(home) && runner) runner->set_home(home);
            }
            if (runner) {   // recipe accepted from the PLC (CAN RECIPE) or the HMI under the run rules
                const int rc = runner->take_recipe();
                if (rc > 0 && hmi.set_recipe(rc)) apply_params(hmi.params());
                runner->set_active_recipe(hmi.params().recipe);
            }
            if (now - last_hmi >= 0.12) { hmi_panel = hmi.render(); last_hmi = now; }
        }
        s->blobs.clear();   // raw outlines: only upstream of the decision line (past it, tracked parts have their box and
        for (size_t i = 0; i < blobs.size() && i < obs.dets.size(); i++)   // anything new there is clutter)
            if (obs.dets[i].p.x < cfg.tracker.decide_x) s->blobs.push_back(blobs[i]);
        s->tm = tm;
        s->meas_ms = mres.empty() ? 0 : meas.last_ms;
        s->measured = meas.measured; s->meas_failed = meas.failed;
        s->npu = npu.running(); s->npu_ms = npu.last_npu_ms; s->npu_runs = npu.runs;
        s->logged = plog.written; s->log_images = plog.images; s->plan = out.belt; s->st = tr.stats;
        s->processed = processed; s->stale = stale;
        tr.retire(des_step_pos(&hw) * mmps);
        const double end = mono_s();
        s->work_ms = (end - t0) * 1e3;
        prev_work_ms = s->work_ms;
        s->hmi = hmi_panel;
        if (end - last_panel >= 0.1) {   // after the snapshot is complete (tracks, timing)
            if (runner) run_line = runner->status_line();
            else if (col) run_line = col->status();
            else run_line.clear();
            panel = render_panel(*s, sh, f.pl_frame, sh.cap_seq.load() - seq, obs.belt_mm, run_line);
            last_panel = end;
        }
        s->panel = panel;   // reference-counted: the main thread only reads it
        std::lock_guard<std::mutex> lk(sh.m);
        sh.snap = s;
    }
    if (col) { col->finish(); printf("%s -> %s\n", col->status().c_str(), col->dir.c_str()); }
}
cv::Scalar color_of(const TrackView &t)
{
    if (t.st == TrackState::New) return cv::Scalar(170, 170, 170);
    return t.disp == Disposition::Pick ? cv::Scalar(0, 210, 0) : t.disp == Disposition::Flap ? cv::Scalar(40, 40, 255)
         : t.disp == Disposition::Recheck ? cv::Scalar(0, 165, 255) : cv::Scalar(0, 220, 220);   // tracking
}
cv::Point to_screen(float x, float y) { return cv::Point(int(x * kVidScale + 0.5), int((y - kCropY) * kVidScale + 0.5)); }
void line_at_x(cv::Mat &vid, const Homography &hinv, float x, const char *label, cv::Scalar col)
{
    Vec2 a = hinv.map({x, -400.f}), b = hinv.map({x, 400.f});
    cv::line(vid, to_screen(a.x, a.y), to_screen(b.x, b.y), col, 2, cv::LINE_8);
    const cv::Point pm = to_screen(hinv.map({x, 0.f}).x, hinv.map({x, 0.f}).y);
    if (pm.x > 0 && pm.x < vid.cols - 90)
        cv::putText(vid, label, cv::Point(pm.x + 6, vid.rows - 16), cv::FONT_HERSHEY_SIMPLEX, 0.6, col, 1);
}
struct BufState {
    cv::Mat panel, hmi;
    cv::Rect cursor;
};
void draw(cv::Mat &img, const Snapshot &s, const Homography &hinv, double belt_mm, const AppConfig &cfg, BufState &b)
{
    cv::Mat vid = img(cv::Rect(0, 0, kVidW, kVidH));
    for (const auto &r : s.blobs) {
        cv::Point pts[4];
        for (int i = 0; i < 4; i++) pts[i] = to_screen(r[2 * i], r[2 * i + 1]);
        for (int i = 0; i < 4; i++) cv::line(vid, pts[i], pts[(i + 1) & 3], cv::Scalar(200, 200, 200), 1, cv::LINE_8);
    }
    char text[96];
    for (const TrackView &t : s.tracks) {   // tracks, re-projected to this frame's belt position
        ORect r{{float(t.s + belt_mm), t.y}, t.len / 2, t.wid / 2, t.axis};
        const auto c = r.corners();
        cv::Point poly[4];
        int top = 1 << 30, left = 1 << 30, bottom = -(1 << 30), right = -(1 << 30);
        for (int i = 0; i < 4; i++) {
            const Vec2 p = hinv.map(c[i]);
            poly[i] = to_screen(p.x, p.y);
            top = std::min(top, poly[i].y);
            left = std::min(left, poly[i].x);
            bottom = std::max(bottom, poly[i].y);
            right = std::max(right, poly[i].x);
        }
        // tracks ride on to the belt end (tail_x) long after leaving the picture: drawing them anyway cost ~0.1 ms
        // each (10-23: 36 tracks, overlay 4.9 ms, display down to 40 fps)
        if (right < 0 || left >= kVidW || bottom < 0 || top >= kVidH) continue;
        cv::Scalar col = color_of(t);
        for (int i = 0; i < 4; i++) cv::line(vid, poly[i], poly[(i + 1) & 3], col, 2, cv::LINE_8);
        snprintf(text, sizeof text, "#%d %s %s", t.id,
                 t.st == TrackState::Decided || t.st == TrackState::Picking ? to_str(t.disp) : to_str(t.st), t.meas);
        // thickness 1: Hershey text at thickness 2 is drawn as many filled thick segments (~1 ms per label)
        cv::putText(vid, text, cv::Point(left, std::max(top - 8, 18)), cv::FONT_HERSHEY_SIMPLEX, 0.6, col, 1);
    }
    line_at_x(vid, hinv, cfg.tracker.decide_x, "decide", cv::Scalar(255, 255, 255));
    line_at_x(vid, hinv, cfg.picker.pick_x, "pick", cv::Scalar(0, 210, 0));
    // Panel + band, rendered by the worker (~10 / 8 Hz): copy only when the buffer holds an older one (saves ~3 ms).
    const bool new_panel = !s.panel.empty() && s.panel.cols == kPanelW && s.panel.data != b.panel.data;
    const bool new_hmi = !s.hmi.empty() && s.hmi.cols == img.cols && s.hmi.data != b.hmi.data;
    if (new_panel) {
        for (int r = 0; r < s.panel.rows && r < kPanelH; r++)
            memcpy(img.ptr(r) + kPanelX * 3, s.panel.ptr(r), size_t(s.panel.cols) * 3);
        b.panel = s.panel;
    }
    if (new_hmi) {
        for (int r = 0; r < s.hmi.rows; r++) memcpy(img.ptr(Hmi::Y0 + r), s.hmi.ptr(r), size_t(s.hmi.cols) * 3);
        b.hmi = s.hmi;
    }
    // erase the old cursor where it lies over panel / band that were not just copied (the picture is redrawn anyway)
    const cv::Rect c = b.cursor & cv::Rect(0, 0, img.cols, img.rows);
    for (int r = c.y; r < c.y + c.height; r++) {
        if (r < kPanelH && !new_panel && !b.panel.empty()) {
            const int x0 = std::max(c.x, kPanelX), x1 = c.x + c.width;
            if (x1 > x0) memcpy(img.ptr(r) + x0 * 3, b.panel.ptr(r) + (x0 - kPanelX) * 3, size_t(x1 - x0) * 3);
        } else if (r >= Hmi::Y0 && r - Hmi::Y0 < b.hmi.rows && !new_hmi)
            memcpy(img.ptr(r) + c.x * 3, b.hmi.ptr(r - Hmi::Y0) + c.x * 3, size_t(c.width) * 3);
    }
    b.cursor = cv::Rect();
}
cv::Rect draw_cursor(cv::Mat &img, const UiInput &in)   // returns the area drawn over (empty when hidden)
{
    int x, y;
    bool vis;
    in.cursor(x, y, vis);
    if (!vis) return cv::Rect();
    const cv::Point pts[] = {{x, y}, {x, y + 22}, {x + 6, y + 17}, {x + 11, y + 27}, {x + 15, y + 25}, {x + 10, y + 15},
                             {x + 17, y + 15}};
    cv::fillConvexPoly(img, pts, 3, cv::Scalar(255, 255, 255));
    for (int i = 0; i < 7; i++) cv::line(img, pts[i], pts[(i + 1) % 7], cv::Scalar(0, 0, 0), 1, cv::LINE_8);
    cv::line(img, pts[0], pts[1], cv::Scalar(255, 255, 255), 2);
    cv::line(img, pts[0], pts[6], cv::Scalar(255, 255, 255), 2);
    return cv::Rect(x - 2, y - 2, 22, 32);   // shape 18 x 28 plus the 2 px lines
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
    const Homography hinv = cfg.px_to_part.inverse();   // overlay of track positions
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
    (void)pin_to_core(0);
    static const Snapshot kEmptySnapshot;   // before the worker has published anything
    uint64_t frames = 0, skipped = 0, sec_frames = 0;
    double t_start = mono_s(), t_sec = t_start, fps = 0, sum_draw = 0, max_draw = 0, sum_conv = 0, sum_ovl = 0;
    const bool no_worker = getenv("LIVE_NO_WORKER") != nullptr, no_overlay = getenv("LIVE_NO_OVERLAY") != nullptr;
    CapturedFrame f;
    std::shared_ptr<const Snapshot> snap;
    BufState bufs[2];   // per display buffer
    while (!stop && (opt.seconds <= 0 || mono_s() - t_start < opt.seconds)) {
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
            sh.job = f;         // always the newest frame; an unprocessed older one is simply replaced
            sh.job_seq = seq;
            sh.has_job = true;
            snap = sh.snap;     // immutable: a pointer copy, no data
        }
        if (!no_worker) sh.cv.notify_one();
        const double t0 = mono_s();
        cv::Mat src(f.height, f.width, CV_8UC4, const_cast<uint8_t *>(f.bgra));
        cv::Mat dst = hdmi.back();
        // camera picture scaled to 3/4 into its rectangle (aspect kept, kCropY rows cut at top and bottom)
        if (f.height < kCropY + kSrcH ||
            !scale34_bgra_to_bgr(f.bgra + size_t(kCropY) * f.width * 4, f.width, kSrcH, f.width * 4, dst.data,
                                 int(dst.step))) {
            cv::Mat small;   // other sensor sizes: slow fallback
            cv::resize(src, small, cv::Size(kVidW, kVidH), 0, 0, cv::INTER_NEAREST);
            cv::Mat vid = dst(cv::Rect(0, 0, kVidW, kVidH));
            cv::cvtColor(small, vid, cv::COLOR_BGRA2BGR);
        }
        const double tc = mono_s();
        sh.disp_fps.store(float(fps)); sh.disp_skipped.store(skipped);
        BufState &bs = bufs[hdmi.back_index()];
        if (!no_overlay) draw(dst, snap ? *snap : kEmptySnapshot, hinv, 0.5 * (f.step_pos + f.step_pos_end) * mmps, cfg, bs);
        bs.cursor = draw_cursor(dst, input);
        const double td = mono_s();
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
        const double t = mono_s();
        const double dt = t - t0;
        sum_draw += dt; max_draw = std::max(max_draw, dt); sum_conv += tc - t0; sum_ovl += td - tc;
        sec_frames++;
        if (t - t_sec >= 1.0) {
            fps = sec_frames / (t - t_sec);
            printf("%5.1f fps shown | display %.1f ms (convert %.1f, overlay %.1f) | worker: %llu frames, locate %.1f ms, "
                   "%zu tracks, stale %llu, measured %llu (failed %llu)\n",
                   fps, sum_draw / frames * 1e3, sum_conv / frames * 1e3, sum_ovl / frames * 1e3,
                   (unsigned long long)(snap ? snap->processed : 0),
                   (snap ? snap->tm.gray_us + snap->tm.thresh_us + snap->tm.contour_us : 0) * 1e-3,
                   snap ? snap->tracks.size() : size_t(0), (unsigned long long)(snap ? snap->stale : 0),
                   (unsigned long long)(snap ? snap->measured : 0), (unsigned long long)(snap ? snap->meas_failed : 0));
            if (snap)
                for (const TrackView &t : snap->tracks)
                    if (t.meas[0]) printf("    track #%d %s: %s\n", t.id, to_str(t.st), t.meas);
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
    double el = mono_s() - t_start;
    printf("\n== live: %llu frames in %.2f s = %.2f fps shown, skipped %llu; display avg %.2f ms max %.2f ms; "
           "worker processed %llu (%.1f%%), stale %llu\n",
           (unsigned long long)frames, el, frames / el, (unsigned long long)skipped,
           frames ? sum_draw / frames * 1e3 : 0.0, max_draw * 1e3, (unsigned long long)(snap ? snap->processed : 0),
           frames ? 100.0 * (snap ? snap->processed : 0) / frames : 0.0, (unsigned long long)(snap ? snap->stale : 0));
    return 0;
}
}  // namespace des
