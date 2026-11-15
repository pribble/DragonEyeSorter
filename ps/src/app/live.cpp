// live.cpp — see live.hpp.
// Core 0 (SCHED_FIFO, caller's thread): capture every frame, hand the newest to the worker, convert it to BGR into
// the HDMI back buffer, draw the latest snapshot re-projected to this frame's belt travel, flip. Core 1 (worker,
// SCHED_OTHER): locate -> tracker -> picker on the newest frame it can get (frames arriving while it is busy are
// skipped by the worker only, never by the display). The snapshot is published as an immutable shared_ptr: the
// capture thread copies the pointer (no strings / vectors / cv::Mat data) and reads it for one frame.
#include "live.hpp"
#include <pthread.h>
#include <sched.h>
#include <sys/statvfs.h>
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
#include "flap_manual.hpp"
#include "hmi.hpp"
#include "locate.hpp"
#include "npu.hpp"
#include "ops.hpp"
#include "part_log.hpp"
#include "picker.hpp"
#include "track_measure.hpp"
#include "tracker.hpp"
#include "ui_input.hpp"
#include "util.hpp"
#include "vib_mon.hpp"
namespace des {
namespace {
struct TrackView {
    int id = 0;
    TrackState st = TrackState::New;
    Disposition disp = Disposition::None;
    float s = 0, y = 0, len = 0, wid = 0, axis = 0;
    char meas[96] = {0};   // measurement summary for the label (empty = not measured)
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
    bool plan_on = true;                        // run mode: false while the line is not running (no plan)
    AdaptMode adapt = AdaptMode::Base;          // belt speed adaptation (run mode): fast / slowed / base
    float adapt_v = 0;                          // the cruise speed it set
    Tracker::Stats st;
    uint64_t processed = 0, stale = 0;
    int8_t beam = -1;                           // through-beam: -1 off, 0 clear, 1 blocked (overlay line colour)
    VibView vib;                                // belt motor vibration (off unless des_app.json "vib" enabled)
    float vib_warn_g = 0, vib_fault_g = 0;
    int env_link = -1;                          // RS485 temperature / humidity transmitter: -1 none, 0 quiet, 1 ok
    bool env_valid = false, env_bad = false;    // a reading / outside the ops limits
    float env_t = 0, env_h = 0;
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
    WorkOrders ops;                             // 运维工单: health rules in the worker, camera stall in the capture loop
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
static_assert(kVidW == Hmi::kPanelX, "status panel must start where the picture ends");
constexpr int kPanelX = Hmi::kPanelX, kPanelW = Hmi::kPanelW, kPanelH = Hmi::kPanelH;
const char *plan_cn(BeltPlan::Kind k)
{
    return k == BeltPlan::Run ? "连续运行（无待抓件）" : k == BeltPlan::StopAt ? "停到抓取线" : "停带（机械臂作业中）";
}
// Status panel right of the picture: Chinese (FreeType) on the light theme of the HMI band (2026-10-30, user). Rendered
// here on core 1 (~10 Hz), never on the capture core, which only blits it. Rows 0..Hmi::kStripBottom are reserved for
// the run-control strip (Hmi::render_panel_strip draws it right after this, same thread). Below it, status is a
// label/value grid on one rhythm (2026-10-31 user: the run-on lines read as clumps) — DIM labels on the left, values
// aligned in one column, thin rules between the display / processing / production groups; the runner line (connections,
// arm, last command) closes it, wrapped, in DIM.
cv::Mat render_panel(const Snapshot &s, const Shared &sh, uint32_t pl_frame, uint64_t lag, double belt_mm,
                     const std::string &run_line, const WorkOrders::Summary &os, TextFT &txt)
{
    using namespace ui;
    cv::Mat panel(kPanelH, kPanelW, CV_8UC3, BG);
    cv::line(panel, {0, 0}, {0, kPanelH - 1}, LINE, 2);
    int y = 16;
    txt.draw(panel, std::string("DragonEyeSorter  ") + (run_line.empty() ? "演示模式" : "产线模式"), 18, y, 26, ACCENT);
    y = Hmi::kStripBottom + 6;   // the run-control strip (state + start/stop/... buttons) lives here
    const int vx = 162;          // value column: labels stay left, values align here
    auto row = [&](const char *k, const std::string &v, const cv::Scalar &vc = TEXT) {
        txt.draw(panel, k, 18, y + 1, 16, DIM);
        txt.draw(panel, v, vx, y, 18, vc);
        y += 30;
    };
    auto rule = [&] { cv::line(panel, {18, y - 8}, {kPanelW - 18, y - 8}, LINE, 1); };
    row("显示帧率", fmt("%.1f 帧/秒 · 跳帧 %llu", double(sh.disp_fps.load()),
                       (unsigned long long)sh.disp_skipped.load()));
    row("PL 帧号", fmt("%u · 处理落后 %llu 帧", pl_frame, (unsigned long long)lag), DIM);
    rule();
    row("定位", fmt("%.1f ms", (s.tm.gray_us + s.tm.thresh_us + s.tm.contour_us) * 1e-3));
    row("单帧处理", fmt("%.1f ms", s.work_ms));
    row("测量", fmt("%.1f ms · 成功 %llu / 共 %llu", s.meas_ms, (unsigned long long)s.measured,
                   (unsigned long long)(s.measured + s.meas_failed)));
    row("NPU", s.npu ? fmt("%.1f ms · 已推理 %llu 次", s.npu_ms, (unsigned long long)s.npu_runs)
                     : std::string("未加载"), s.npu ? TEXT : DIM);
    rule();
    row("跟踪中", fmt("%zu 件", s.tracks.size()));
    row("皮带走过", fmt("%.1f mm", belt_mm));
    row("抓取/挡板/复检", fmt("%llu / %llu / %llu", (unsigned long long)s.st.decided_pick,
                             (unsigned long long)s.st.decided_flap, (unsigned long long)s.st.decided_recheck));
    row("噪点", fmt("%llu", (unsigned long long)s.st.noise));
    std::string plan = s.plan_on ? std::string(plan_cn(s.plan.kind)) : std::string("—");
    if (s.plan_on && s.adapt == AdaptMode::Fast) plan += fmt(" · 快档 %.0f", double(s.adapt_v));
    else if (s.plan_on && s.adapt == AdaptMode::Slow) plan += fmt(" · 减速 %.0f", double(s.adapt_v));
    row("皮带计划", plan);
    row("已记录", fmt("%llu 件 · 存图 %llu 张", (unsigned long long)s.logged, (unsigned long long)s.log_images));
    {   // work orders (app/ops.hpp): red while a fault is active, orange while something waits for an acknowledgement
        std::string v = os.unacked == 0 ? std::string("无待处理") : fmt("%d 条待处理", os.unacked);
        if (!os.last_title.empty() && os.unacked > 0) v += " · " + os.last_title;
        const int vw = kPanelW - 162 - 12;
        if (txt.width(v, 18) > vw) {   // cut at a code point, with an ellipsis
            while (v.size() > 3 && txt.width(v + "…", 18) > vw) {
                size_t k = v.size() - 1;
                while (k > 0 && (static_cast<unsigned char>(v[k]) & 0xC0) == 0x80) k--;
                v.erase(k);
            }
            v += "…";
        }
        row("运维工单", v, os.faults ? RED : os.unacked ? ORANGE : DIM);
    }
    if (s.env_link >= 0)   // RS485 temperature / humidity transmitter
        row("环境温湿度", !s.env_valid ? std::string("等待数据")
                          : fmt("%.1f ℃ · %.1f %%RH%s", double(s.env_t), double(s.env_h), s.env_link ? "" : " · 掉线"),
            !s.env_valid || !s.env_link ? DIM : s.env_bad ? ORANGE : TEXT);
    if (s.vib.on) {   // belt motor vibration: AC RMS (PL window statistics) + spectrum peak (PS FFT)
        const float r = s.vib.rms_g_total;
        const bool hi = r >= s.vib_fault_g, warn = r >= s.vib_warn_g;
        row("电机振动", r < 0 ? std::string("等待数据")
                              : fmt("%.3f g · 主频 %.0f Hz · %s", double(r), double(s.vib.peak_hz),
                                    hi ? "故障" : warn ? "预警" : "正常"),
            r < 0 ? DIM : hi ? RED : warn ? ORANGE : TEXT);
    }
    // the runner's line, wrapped at its spaces (Chinese words have none inside); DIM so it reads as one quiet block
    const int wrap_px = kPanelW - 2 * 18;
    std::string word, cur;
    auto flush = [&] {
        if (!cur.empty()) { txt.draw(panel, cur, 18, y, 18, DIM); y += 28; }
        cur.clear();
    };
    for (size_t i = 0; i <= run_line.size(); i++) {
        const char ch = i < run_line.size() ? run_line[i] : ' ';
        if (ch != ' ') { word += ch; continue; }
        if (word.empty()) continue;
        if (!cur.empty() && txt.width(cur + " " + word, 18) > wrap_px) flush();
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
    std::unique_ptr<BeamMonitor> beam;   // through-beam on IR1 (sched/beam.hpp)
    std::vector<BeamResult> beam_out;
    if (cfg.beam.active() && hw.version < DES_VERSION_IO) {
        fprintf(stderr, "beam: bitstream VERSION %u has no IR1 input (needs >= %u)\n", hw.version, DES_VERSION_IO);
    } else if (cfg.beam.active()) {
        beam.reset(new BeamMonitor(cfg.beam));
        printf("beam: x %.1f mm (match +-%.0f, unclaimed after %.0f, missed after %.0f mm)\n", cfg.beam.x_mm,
               cfg.beam.match_mm, cfg.beam.expire_mm, cfg.beam.miss_mm);
        if (cfg.beam.x_mm > cfg.tracker.decide_x - 20)   // records are written at the decision: the beam must come first
            printf("beam: WARNING x %.0f is not upstream of decide_x %.0f - 20: part records will say \"waiting\"\n",
                   cfg.beam.x_mm, cfg.tracker.decide_x);
    } else if (cfg.beam.enabled) {
        fprintf(stderr, "beam: enabled but no position (des_tool calib-beam writes calib.json \"beam\")\n");
    }
    FILE *env_csv = nullptr;           // env.csv next to the part records: temperature / humidity every 10 s
    double last_env_log = -1e9;
    std::unique_ptr<VibMonitor> vib;   // belt motor vibration (des_vib, v17 batch 2): only when configured
    if (cfg.vib.enabled) {
        std::string vdir;
        if (plog.is_open()) {
            const size_t k = plog.csv_path.find_last_of('/');
            vdir = k == std::string::npos ? std::string(".") : plog.csv_path.substr(0, k);
        }
        vib.reset(new VibMonitor);
        const std::string ve = vib->open(hw, cfg.vib, vdir);
        if (!ve.empty()) { fprintf(stderr, "vib: %s (vibration monitor off)\n", ve.c_str()); vib.reset(); }
        else printf("vib: ADXL345 +-%d g, rate code 0x%02X, window %d, FFT %d\n", 2 << cfg.vib.range, cfg.vib.rate,
                    cfg.vib.window, cfg.vib.fft);
    }
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
    std::string npu_err;
    if (cfg.npu.enabled) {
        e = npu.start(cfg.npu.model, cfg.npu.core);
        npu_err = e;
        if (!e.empty()) fprintf(stderr, "npu: %s (parts get no NPU class)\n", e.c_str());
        else printf("npu: %s, %s\n", cfg.npu.model.c_str(), npu.model().describe().c_str());
    }
        Hmi hmi(cfg, cfg.config_dir + "/ui_params.json");
    if (!hmi.fonts_ok()) fprintf(stderr, "hmi: Chinese font not found, ASCII fallback\n");
    TextFT panel_txt;   // status panel right of the picture (this thread renders it)
    panel_txt.open();
    FlapManual flap_manual(&hw);   // 挡板 page: the flap servos by hand
        ArmManual arm_manual(&hw, cfg.arm, cfg.arm_home_taught, cfg.config_dir + "/data/arm_teach.csv",
                         cfg.config_dir + "/arm_home.json");
    auto apply_params = [&](const HmiParams &p) {
        SizeRule &r = tr.config().size;
        if (const Recipe *rc = find_recipe(cfg.recipes, p.recipe)) rc->apply(r);   // then the operator's sizes
        r.enabled = p.size_rule;
        r.gear_od = p.gear_od; r.gear_od_tol = p.gear_od_tol; r.gear_bore = p.gear_bore; r.gear_bore_tol = p.gear_bore_tol;
        tr.config().min_conf = p.min_conf;
        plog.set_images(p.images == 0 ? "none" : p.images == 2 ? "all" : "ng");
        if (runner) runner->set_speed(p.belt_speed, "界面");
        if (runner) runner->set_pid_enabled(p.belt_pid);   // 参数页「带速 PI」开关（运行时，不持久化）
        des_vis_config(&hw, p.pl_mode, 0, p.pl_thr, p.pl_edge_thr);   // PL display mode + thresholds (no-op before v6)
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
        struct { bool live = false, have_ref = false, fresh = false; int32_t first = 0, ref = 0; double ref_t = 0, mm_s = 0; } enc;
    // 运维 (app/ops.hpp): health rules -> work orders, logged next to the part records, pushed every cycle below
    WorkOrders &ops = sh.ops;
    if (plog.is_open()) ops.open_log(plog.day_dir() + "/workorders_" + plog.session + ".jsonl", plog.session);
    HealthMonitor health(cfg.ops);
    WorkOrders::Summary ops_sum;
    bool ops_dirty = true;
    double last_df = -1e9, free_mb = -1;
    int beam_nt = 0, beam_ms = 0;   // through-beam events of this cycle (no track / missed)
    if (cfg.ops.enabled) printf("ops: fault detection on, work orders -> %s\n", plog.is_open() ? "workorders_<session>.jsonl" : "(no log)");
    std::map<int, cv::Mat> crops;   // latest part crop per track (from its best measurement frame)
    std::set<int> logged;
    Picker pk(cfg.picker);
    const double mmps = cfg.belt.mm_per_step * cfg.belt.dir;
    const uint64_t max_age = uint64_t(cfg.camera.buffers > 2 ? cfg.camera.buffers - 1 : 1);
    uint64_t processed = 0, stale = 0;
    cv::Mat panel;
    double last_panel = 0;
    // PL POS is a 32-bit step counter (wraps after ~10 days of belt motion): the PS keeps the accumulated travel.
    // Frame latches chain wrap-safely here (PosAccum, belt_ctl.hpp); "now" reads (beam / retire / dry-run picker
    // input) are referred to the current frame's end latch via peek(). Same anchor as BeltDriver::belt_mm: the PL
    // counter zero — restarts without an FPGA reconfiguration keep the raw POS.
    PosAccum accum(mmps);
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
        const double b0 = accum.feed(f.step_pos), b1 = accum.feed(f.step_pos_end);
        obs.belt_mm = 0.5 * (b0 + b1);
        LocateTiming tm;
        obs.dets = loc.run(f.bgra, f.width, f.height, f.stride, cfg.px_to_part, &tm);   // where parts stand
        // the DMA rewrites a buffer buffers-1 frames after it was handed out: drop results read from a reused one
        if (!fresh()) { stale++; continue; }
        const auto &blobs = loc.rects_full();
        for (Detection &d : obs.dets) d.p.x -= float((b1 - b0) * (d.img_v - 0.5f));   // rolling-shutter row timing
        tr.update(obs);
        des_io_t io{};   // field inputs once per cycle: the beam here, the encoder speed for the HMI below
        const bool io_ok = des_io_read(&hw, &io) == DES_OK;   // < DES_VERSION_IO bitstreams: DES_E_ARG -> io_ok false
        if (beam && io_ok) {   // before the records below: a part decided this cycle carries its beam result
            beam_out.clear();
            beam_nt = beam_ms = 0;
            beam->poll(io.ir1_cnt, accum.peek(io.ir1_pos), int32_t(io.ir1_enc), beam_out);
            beam->update(tr.tracks(), accum.peek(des_step_pos(&hw)), beam_out);
            const double ts = plog.now_s();
            for (const BeamResult &r : beam_out) {
                plog.add_beam(r, ts);
                if (r.kind == BeamKind::NoTrack) beam_nt++;
                else if (r.kind == BeamKind::Missed) beam_ms++;
                if (r.kind == BeamKind::NoTrack || r.kind == BeamKind::Missed || r.kind == BeamKind::Lost)
                    printf("[beam] %s at belt %.1f mm (track %d)\n", to_str(r.kind), r.belt_mm, r.track);
            }
        }
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
        Picker::Output out;   // belt plan for the panel (runner branch) — the dry branch binds the picker's member directly
        bool plan_on = true;
        AdaptMode adapt = AdaptMode::Base;
        float adapt_v = 0;
        if (runner) {
            runner->set_encoder(io.enc, io_ok);   // optional belt PI: count window in Runner
            runner->cycle(tr, pk);   // state machine, belt, arm, CAN
            plan_on = runner->state() == CANP_ST_RUN; out.belt = runner->belt_plan();   // the plan Runner applied
            adapt = runner->adapt_mode(); adapt_v = float(runner->adapt_speed_set());
        } else {
            Picker::Input in = picker_input(nullptr, &hw, cfg.belt);
            in.belt_mm = accum.peek(des_step_pos(&hw));   // the accumulated frame (same origin as obs.belt_mm)
            const Picker::Output &po = pk.tick(tr, in);   // ref into the picker: no per-cycle flaps copy
            if (po.have_job) pk.job_done(tr, po.job.track_id, true);   // dry: pretend the pick succeeded
            out.belt = po.belt;
        }
        processed++;
        const uint8_t rec_recipe = uint8_t(hmi.params().recipe);
        const Track *roi_best = nullptr;
        float roi_best_d = 1e9f;
        const Vec2 axis = cfg.px_to_table.map({0.5f * f.width, 0.5f * f.height});
        s->tracks.clear();
        for (const Track &t : tr.tracks()) {   // one pass: records, snapshot view, PL ROI candidate
            // a Flap part's record waits until its gate is decided for good (else it may still go to recheck)
            const bool gate_pending = cfg.picker.gates_active() && t.disp == Disposition::Flap && !t.flap_final;
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
                snprintf(rec.why, sizeof rec.why, "%s", t.why);   // display copy; the CSV writer sanitizes at its edge
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
                if (beam) {
                    rec.beam = t.beam;
                    if (t.beam == 2) { rec.beam_err = beam_track_err(t, cfg.beam.x_mm); beam->finalize(t); }
                }
                const cv::Mat &crop = it != crops.end() ? it->second : cv::Mat();
                hmi.add_part(rec, crop, now);
                health.on_part(rec.disp == Disposition::Pick ? 0 : rec.disp == Disposition::Flap ? 1 : 2,
                               rec.meas.valid && rec.meas.kind == 'g', rec.meas.size, hmi.params().gear_od,
                               hmi.params().gear_od_tol, now, rec.wall_ms, ops);
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
                    int n = snprintf(tv.meas, sizeof tv.meas, "外径%.2f %d齿", t.meas.size, t.meas.teeth);
                    if (t.meas.missing_teeth && n < int(sizeof tv.meas))
                        n += snprintf(tv.meas + n, sizeof tv.meas - n, " 缺齿%d", t.meas.missing_teeth);
                    if (t.meas.short_teeth && n < int(sizeof tv.meas))
                        n += snprintf(tv.meas + n, sizeof tv.meas - n, " 缺料%d", t.meas.short_teeth);
                    if (t.meas.flash_gaps && n < int(sizeof tv.meas))
                        snprintf(tv.meas + n, sizeof tv.meas - n, " 飞边%d", t.meas.flash_gaps);
                } else if (t.meas.valid && t.meas.kind == 'g')
                    snprintf(tv.meas, sizeof tv.meas, "外径%.2f 离轴未判齿", t.meas.size);
                else if (t.meas.valid)
                    snprintf(tv.meas, sizeof tv.meas, "%.2f mm", t.meas.size);
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
            if (io_ok) {
                if (!enc.have_ref) { enc.have_ref = true; enc.first = enc.ref = io.enc; enc.ref_t = now; }
                else if (now - enc.ref_t >= 0.5) {
                    enc.mm_s = std::abs(double(io.enc) - enc.ref) * cfg.belt.enc_scale() / (now - enc.ref_t);
                    enc.ref = io.enc;
                    enc.ref_t = now;
                    enc.fresh = true;
                }
                if (std::abs(double(io.enc) - enc.first) > 20) enc.live = true;   // it has counted: wired
                hs.enc_live = enc.live;
                hs.belt_enc_mm_s = enc.mm_s;
            }
            hs.worker_ms = float(prev_work_ms);
            hs.npu_ms = npu.last_npu_ms; hs.npu = npu.running(); hs.pl_vision = pl_vision;
            hs.clock_synced = wall_synced();
            hs.inputs = input ? input->devices() : 0; hs.jog = runner ? runner->jog_dir() : 0; hs.logged = plog.written;
            hs.ops = &ops;
            {   // manual arm commands only while the line is not running and no pick is being executed
                const int st = runner ? int(runner->state()) : -1;
                const char *why = st == CANP_ST_RUN ? "产线运行中" : st == CANP_ST_ESTOP ? "急停中"
                                : runner && runner->arm_busy() ? "机械臂正在抓取" : nullptr;
                arm_manual.context(hmi.arm_page(), why == nullptr, why);
                hs.arm = arm_manual.view();
            }
            {   // flap servos by hand only with the line stopped; the runner leaves the gates to the page meanwhile
                const int st = runner ? int(runner->state()) : -1;
                const char *why = st == CANP_ST_RUN ? "产线运行中" : st == CANP_ST_PAUSED ? "产线暂停中"
                                : st == CANP_ST_ESTOP ? "急停中" : st == CANP_ST_FAULT ? "故障中" : nullptr;
                flap_manual.context(hmi.flap_page(), why == nullptr, why);
                hs.flap = flap_manual.view();
            }
            hmi.status(hs, now);
            if (input)
                for (const HmiAction &a : hmi.handle(input->take())) {
                    if (a.kind == HmiAction::Params) apply_params(hmi.params());
                    else if (a.kind == HmiAction::Arm) arm_manual.request(a.arm);
                    else if (a.kind == HmiAction::Flap) flap_manual.request(a.flap);
                    else if (a.kind == HmiAction::Ops && a.cmd == 0) {   // 发送测试工单: raised and gone at once
                        ops.raise(OPS_TEST, "运维页手动触发，用来验证工单推送链路", now, wall_now_ms());
                        ops.clear(OPS_TEST, now, wall_now_ms());
                    } else if (a.kind == HmiAction::Ops) ops.ack(uint16_t(a.cmd), "界面", wall_now_ms());
                    else if (a.kind == HmiAction::Recipe) {
                        if (runner) runner->select_recipe(a.cmd, "界面");   // applied below, same path as the PLC's
                        else if (hmi.set_recipe(a.cmd)) apply_params(hmi.params());
                    } else if (a.kind == HmiAction::Jog) {
                        if (runner) runner->jog(a.cmd, "界面");
                    } else if (runner) runner->command(uint8_t(a.cmd), "界面");
                }
            flap_manual.tick(now);   // slow moves of the flap page, one step per cycle
            if (runner) runner->set_flap_manual(flap_manual.active());
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
        {   // 运维: health rules -> work orders -> pushers (feed 'W', CAN ALARM, FAULT stop, Modbus, tower light)
            HealthInput hin;
            hin.now = now;
            hin.wall_ms = wall_now_ms();
            hin.run_state = runner ? int(runner->state()) : -1;
            hin.enc_live = enc.live;
            hin.enc_new = enc.fresh;
            enc.fresh = false;
            hin.belt_cmd_mm_s = des_step_speed(&hw) * cfg.belt.mm_per_step;
            hin.belt_enc_mm_s = enc.mm_s;
            if (processed % 6 == 0) hin.gray = loc.gray_mean();   // ~10 Hz is plenty
            hin.pl_mode = hmi.params().pl_mode;
            hin.processed = processed;
            hin.beam_no_track = beam_nt;
            hin.beam_missed = beam_ms;
            beam_nt = beam_ms = 0;
            if (runner) {
                const Runner::PickStats &ps = runner->pick_stats();
                hin.picks_ok = ps.ok; hin.picks_failed = ps.failed; hin.servo_errors = ps.servo; hin.pick_why = ps.last_why;
                hin.rs485 = runner->rs485_link();
                hin.plc_lost = runner->plc_lost();
            }
            hin.npu_expected = cfg.npu.enabled;
            hin.npu_running = npu.running();
            hin.npu_error = npu_err;
            if (now - last_df >= 30) {   // SD card free space
                struct statvfs fs{};
                if (statvfs(cfg.storage.dir.c_str(), &fs) == 0) free_mb = double(fs.f_bavail) * fs.f_frsize / 1048576.0;
                hin.free_mb = free_mb;
                last_df = now;
            }
            hin.log_errors = plog.errors;
            hin.log_dropped = plog.dropped;
            if (runner) {   // RS485 temperature / humidity transmitter (rs485.th_addr)
                float et = 0, eh = 0;
                hin.env_link = runner->env_link();
                hin.env_valid = runner->env_reading(et, eh);
                hin.env_temp_c = et; hin.env_hum_rh = eh;
                if (hin.env_valid && hin.env_link == 1 && now - last_env_log >= 10 && plog.is_open()) {   // 10 s rows
                    if (!env_csv) {
                        const size_t k = plog.csv_path.find_last_of('/');
                        const std::string dir = k == std::string::npos ? std::string(".") : plog.csv_path.substr(0, k);
                        env_csv = fopen((dir + "/env.csv").c_str(), "a");
                        if (env_csv && ftell(env_csv) == 0) fprintf(env_csv, "wall_ms,temp_c,hum_rh\n");
                    }
                    if (env_csv) { fprintf(env_csv, "%lld,%.1f,%.1f\n", (long long)hin.wall_ms, double(et), double(eh)); fflush(env_csv); }
                    last_env_log = now;
                }
            }
            if (vib) {
                vib->poll(now);
                hin.vib_rms_g = vib->view().rms_g_total;
                hin.vib_peak_hz = vib->view().peak_hz;
            }
            health.update(hin, ops);
            for (const WorkOrders::Change &c : ops.take_changes()) {
                const OpsCodeInfo &info = ops_code_info(c.w.code);
                plog.push_ops(ops.json(c.w));
                ops_dirty = true;
                if (c.is_new || (c.raised && c.w.count > 1 && !c.w.acked) || !c.w.active)
                    printf("[ops] #%u %s %s（%s）%s: %s\n", unsigned(c.w.id), ops_level_cn(c.w.level), info.title,
                           info.device, c.w.status_cn(), c.w.detail.c_str());
                if (!runner || !c.raised) continue;
                if (c.is_new) runner->ops_alarm(c.w.code, uint8_t(c.w.level), c.w.id);
                if (info.stop) runner->ops_fault(c.w.code, (std::string("故障停机：") + info.title).c_str());
            }
            uint16_t aid;
            while (runner && runner->take_ops_ack(aid)) ops.ack(aid, "远程（Modbus）", wall_now_ms());
            if (ops_dirty) {   // what a remote station gets when it connects
                plog.set_ops_snapshot(ops.snapshot_json(60));
                ops_dirty = false;
            }
            ops_sum = ops.summary();
            if (runner) {
                Runner::OpsRegs r;
                r.active = uint16_t(ops_sum.active); r.unacked = uint16_t(ops_sum.unacked); r.level = ops_sum.top;
                r.last_id = ops_sum.last_id; r.last_code = ops_sum.last_code; r.seq = ops_sum.seq;
                r.warn = ops_sum.unacked_warning;
                runner->set_ops(r);
            }
        }
        s->blobs.clear();   // raw outlines: only upstream of the decision line (past it, tracked parts have their box and
        for (size_t i = 0; i < blobs.size() && i < obs.dets.size(); i++)   // anything new there is clutter)
            if (obs.dets[i].p.x < cfg.tracker.decide_x) s->blobs.push_back(blobs[i]);
        s->tm = tm;
        s->meas_ms = mres.empty() ? 0 : meas.last_ms;
        s->measured = meas.measured; s->meas_failed = meas.failed;
        s->npu = npu.running(); s->npu_ms = npu.last_npu_ms; s->npu_runs = npu.runs;
        s->logged = plog.written; s->log_images = plog.images; s->plan = out.belt; s->plan_on = plan_on; s->st = tr.stats;
        s->adapt = adapt; s->adapt_v = adapt_v;
        s->processed = processed; s->stale = stale;
        s->beam = !beam ? -1 : io_ok && (io.status & DES_IO_ST_IR1_BLOCKED) ? 1 : 0;
        if (vib) { s->vib = vib->view(); s->vib_warn_g = cfg.ops.vib_warn_g; s->vib_fault_g = cfg.ops.vib_fault_g; }
        if (runner) {
            s->env_link = runner->env_link();
            s->env_valid = runner->env_reading(s->env_t, s->env_h);
            s->env_bad = s->env_valid && (s->env_t < cfg.ops.env_temp_lo || s->env_t > cfg.ops.env_temp_hi ||
                                          s->env_h > cfg.ops.env_hum_hi);
        }
        tr.retire(accum.peek(des_step_pos(&hw)));
        const double end = mono_s();
        s->work_ms = (end - t0) * 1e3;
        prev_work_ms = s->work_ms;
        s->hmi = hmi_panel;
        if (end - last_panel >= 0.1) {   // after the snapshot is complete (tracks, timing)
            if (runner) run_line = runner->status_line();
            else if (col) run_line = col->status();
            else run_line.clear();
            panel = render_panel(*s, sh, f.pl_frame, sh.cap_seq.load() - seq, obs.belt_mm, run_line, ops_sum, panel_txt);
            hmi.render_panel_strip(panel);   // run control on top of the panel, every page, ~10 Hz
            last_panel = end;
        }
        s->panel = panel;   // reference-counted: the main thread only reads it
        std::lock_guard<std::mutex> lk(sh.m);
        sh.snap = s;
    }
    if (col) { col->finish(); printf("%s -> %s\n", col->status().c_str(), col->dir.c_str()); }
    if (beam) {
        const BeamMonitor::Stats &b = beam->stats;
        printf("== beam: %llu edges: matched %llu, repeat %llu, no track %llu (vision missed them?), lost %llu; "
               "tracks past the beam without an edge %llu\n", (unsigned long long)b.edges, (unsigned long long)b.matched,
               (unsigned long long)b.repeats, (unsigned long long)b.no_track, (unsigned long long)b.lost,
               (unsigned long long)b.missed);
        if (b.n_err)
            printf("   tracking error at the beam (predicted leading edge - beam): mean %+.2f mm, sd %.2f, min %+.2f, "
                   "max %+.2f over %llu parts\n", b.mean(), b.sd(), b.err_min, b.err_max, (unsigned long long)b.n_err);
    }
}
cv::Scalar color_of(const TrackView &t)
{
    if (t.st == TrackState::New) return cv::Scalar(170, 170, 170);
    return t.disp == Disposition::Pick ? cv::Scalar(0, 210, 0) : t.disp == Disposition::Flap ? cv::Scalar(40, 40, 255)
         : t.disp == Disposition::Recheck ? cv::Scalar(0, 165, 255) : cv::Scalar(0, 220, 220);   // tracking
}
cv::Point to_screen(float x, float y) { return cv::Point(int(x * kVidScale + 0.5), int((y - kCropY) * kVidScale + 0.5)); }
const char *track_cn(const TrackView &t)
{
    if (t.st == TrackState::New) return "新";
    if (t.st == TrackState::Tracking) return "跟踪中";
    if (t.st == TrackState::Picking) return "抓取中";
    return t.disp == Disposition::Pick ? "抓取" : t.disp == Disposition::Flap ? "挡板" : t.disp == Disposition::Recheck ? "复检" : "—";
}
void line_at_x(cv::Mat &vid, const Homography &hinv, float x, const char *label, cv::Scalar col, TextFT &txt)
{
    Vec2 a = hinv.map({x, -400.f}), b = hinv.map({x, 400.f});
    cv::line(vid, to_screen(a.x, a.y), to_screen(b.x, b.y), col, 2, cv::LINE_8);
    const cv::Point pm = to_screen(hinv.map({x, 0.f}).x, hinv.map({x, 0.f}).y);
    if (pm.x > 0 && pm.x < vid.cols - 90)
        txt.draw_solid(vid, label, pm.x + 6, vid.rows - 34, 20, col);
}
struct BufState {
    cv::Mat panel, hmi;
    cv::Rect cursor;
};
void draw(cv::Mat &img, const Snapshot &s, const Homography &hinv, double belt_mm, const AppConfig &cfg, BufState &b,
          TextFT &txt)
{
    cv::Mat vid = img(cv::Rect(0, 0, kVidW, kVidH));
    for (const auto &r : s.blobs) {
        cv::Point pts[4];
        for (int i = 0; i < 4; i++) pts[i] = to_screen(r[2 * i], r[2 * i + 1]);
        for (int i = 0; i < 4; i++) cv::line(vid, pts[i], pts[(i + 1) & 3], cv::Scalar(200, 200, 200), 1, cv::LINE_8);
    }
    char text[160];
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
        snprintf(text, sizeof text, "#%d %s %s", t.id, track_cn(t), t.meas);
        // write-only glyphs: the display buffer is uncached, a blended (read-modify-write) label would cost ms
        txt.draw_solid(vid, text, left, std::max(top - 26, 0), 20, col);
    }
    line_at_x(vid, hinv, cfg.tracker.decide_x, "判定线", cv::Scalar(255, 255, 255), txt);
    line_at_x(vid, hinv, cfg.picker.pick_x, "抓取线", cv::Scalar(0, 210, 0), txt);
    if (s.beam >= 0)   // through-beam: red while something blocks it
        line_at_x(vid, hinv, cfg.beam.x_mm, "光束", s.beam ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 200, 255), txt);
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
    TextFT overlay_txt;   // labels on the picture (this thread; its own glyph cache)
    overlay_txt.open();
    // overlay belt position: the same wrap-safe chaining as the worker's tracker feed (PosAccum, same PL-zero anchor)
    PosAccum disp(mmps);
    while (!stop && (opt.seconds <= 0 || mono_s() - t_start < opt.seconds)) {
        err = cam.next(f, true);
        if (!err.empty()) {
            fprintf(stderr, "%s\n", err.c_str());
            // the program ends here and the service restarts it: leave the work order in the log first
            if (cfg.ops.enabled) sh.ops.raise(OPS_CAMERA, "取图超时：" + err, mono_s(), wall_now_ms());
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
        const double belt_frame = 0.5 * (disp.feed(f.step_pos) + disp.feed(f.step_pos_end));
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
        if (!no_overlay) draw(dst, snap ? *snap : kEmptySnapshot, hinv, belt_frame, cfg, bs, overlay_txt);
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
    if (opt.belt_mm_s != 0 && !opt.run) des_step_stop_release(&hw, 3000);   // ramp down, then ENA off
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
