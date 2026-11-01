// hmi.hpp — the HDMI operator interface (赛题硬性项② / 评分「HDMI 可视化曲线 / 参数配置界面」).
//
// A 1920 x 300 band below the 3/4-scaled camera picture, six pages, evdev mouse/keyboard (ui_input.hpp), FreeType text
// + drawing helpers (ui_text.hpp, light theme): 总览 counts/yield/throughput/per class · 曲线 OD+bore
// trends + worker/NPU time + belt speed · 参数 recipe/speed/tolerances/size rule/confidence/images/PL display ·
// 记录 the last kRecords parts (wheel / buttons scroll) with reason/sizes/crop · 机械臂 manual jogging/torque/teach
// (app/arm_io.hpp) · 挡板 the two flap servos by hand (app/flap_manual.hpp). Run control (启动/停止/暂停/恢复
// + belt jog; 复位报警 only while 急停/故障) is top-level, so it is NOT on the params page: render_panel_strip() draws
// it at the top of the status panel right of the picture (kPanelX.., always visible on every page); live.cpp calls it
// right after its render_panel() (same worker thread) and handle() routes clicks there via panel_hits_.
// Everything runs on the worker thread (core 1): render() (~8 Hz) draws into a FRESH BGR image the capture thread
// only copies to the display (a reused buffer would alias snapshots still alive in live.cpp's pool: mid-blit tearing).
// Types stay light: fixed-capacity rings (no per-part heap churn), frozen POD PartRecord, uint8_t ui_params enums.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "arm_io.hpp"
#include "config.hpp"
#include "flap_manual.hpp"
#include "part_log.hpp"
#include "ui_input.hpp"
#include "ui_text.hpp"

namespace des {

struct HmiParams {                        // operator-editable, persisted in ui_params.json (12 keys)
    double belt_speed = 50;               // mm/s (run mode)
    bool size_rule = false;
    float gear_od = 32, gear_od_tol = 0.15f, gear_bore = 6, gear_bore_tol = 0.3f;
    float min_conf = 0.5f;
    uint8_t images = 1;                   // 0 none, 1 NG + recheck, 2 all
    uint8_t pl_mode = 0;                  // des_vision display: 0 camera 1 gray 2 Gaussian 3 binary 4 Sobel 5 edges
    uint8_t pl_thr = 128;                 // des_vision binary threshold
    uint16_t pl_edge_thr = 200;           // des_vision Sobel / edge threshold (no HMI button: ui_params.json only)
    int recipe = 1;                       // active recipe id (recipes.json); switching it also resets the four sizes
    bool belt_pid = false;                // runtime switch for the optional PS-side belt PI (not persisted)
};

struct HmiStatus {                        // from the worker, every cycle
    int run_state = -1;                   // CANP_ST_* in run mode, -1 otherwise
    double belt_mm_s = 0;                 // commanded (stepper)
    double belt_enc_mm_s = 0;             // measured by the measuring-wheel encoder (valid when enc_live)
    bool enc_live = false;                // the encoder has counted since start (wired)
    float worker_ms = 0, npu_ms = 0;
    bool npu = false, clock_synced = false;
    bool pl_vision = false;               // bitstream has des_vision (v6)
    int inputs = 0;
    int jog = 0;                          // manual belt jog: +1 forward, -1 reverse, 0 off
    uint64_t logged = 0;
    ArmManualView arm;                    // manual arm page
    FlapManualView flap;                  // flap servo page
    const WorkOrders *ops = nullptr;      // 运维 page (app/ops.hpp; thread-safe, read at render)
    int ops_clients = 0;                  // remote stations on the network feed
};

struct HmiAction {
    enum Kind { Params, Command, Recipe, Arm, Jog, Flap, Ops } kind = Params;
    int cmd = 0;              // CANP_CMD_* for Command, recipe id for Recipe, +1 / -1 / 0 for Jog (worker applies run rules),
                              // work order id to acknowledge for Ops (0xFFFF = all, 0 = send a test order)
    ArmRequest arm;           // Arm: request for the manual arm thread
    FlapRequest flap;         // Flap: request for FlapManual (worker thread)
};

// Fixed-capacity ring, index 0 = oldest. Replaces the four std::deque history buffers: no allocation while running,
// and "not full yet" iterates exactly the entries the deque had.
template <class T, size_t N>
class Ring {
public:
    void push(const T &v)
    {
        a_[head_] = v;  head_ = (head_ + 1) % N;  if (n_ < N) n_++;
    }
    void pop_front() { if (n_) n_--; }
    size_t size() const { return n_; }
    bool empty() const { return n_ == 0; }
    const T &operator[](size_t i) const { return a_[(head_ + N - n_ + i) % N]; }
    T &operator[](size_t i) { return a_[(head_ + N - n_ + i) % N]; }
    const T &front() const { return (*this)[0]; }
    size_t copy(T *out, size_t cap) const   // oldest -> newest, returns how many were copied
    {
        const size_t m = n_ < cap ? n_ : cap;
        for (size_t i = 0; i < m; i++) out[i] = (*this)[i];
        return m;
    }

private:
    std::array<T, N> a_{};
    size_t head_ = 0, n_ = 0;
};

class Hmi {
public:
    static constexpr int W = 1920, H = 300, Y0 = 1080 - 300;
    static constexpr int kPanelX = 1440, kPanelW = 480, kPanelH = Y0;   // status panel right of the picture
    static constexpr int kStripBottom = 252;   // render_panel_strip occupies the panel down to this y (live.cpp reserves it)
    Hmi(const AppConfig &cfg, const std::string &params_path);
    const HmiParams &params() const { return p_; }
    bool set_recipe(int id);  // known id: switch, load its sizes into the params, save; false = unknown id
    void add_part(const PartRecord &r, const cv::Mat &crop, double now);
    void status(const HmiStatus &s, double now);
    std::vector<HmiAction> handle(const std::vector<UiEvent> &ev);   // clicks / keys -> actions for the worker
    cv::Mat render();                                               // fresh 1920 x 300 CV_8UC3 every call
    void render_panel_strip(cv::Mat &panel);                        // run control at the top of the status panel
    bool fonts_ok() const { return txt_.ok(); }
    bool arm_page() const { return page_ == 4; }    // the manual arm page is shown (it polls the servos)
    bool flap_page() const { return page_ == 5; }   // the flap servo page is shown
    bool ops_page() const { return page_ == 6; }    // the 运维 (work order) page is shown
    static constexpr int kRecords = 500;   // parts kept for the 记录 page (all of them are in the SD card CSV anyway)
    static constexpr int kCrops = 40;      // crops kept for the newest parts (224^2 BGR = 150 KB each, ~6 MB)
    static constexpr int kRows = 9;        // rows shown at a time

private:
    struct Part {
        PartRecord r;
        cv::Mat crop;   // 224^2 part crop, kept for the newest kCrops parts
    };
    const std::string &cn_of(int cls) const;      // Chinese class name (rules[cls].cn; "未分类")
    static const char *cn_disp(Disposition d);    // 抓取 / 挡板 / 复检
    static cv::Scalar disp_color(Disposition d);
    const Part &part(size_t i) const { return parts_[parts_.size() - 1 - i]; }   // 0 = newest
    Part &part(size_t i) { return parts_[parts_.size() - 1 - i]; }
    void save_params() const;
    void load_params();
    void page_overview(ui::Canvas &c);
    void page_curves(ui::Canvas &c);
    void page_params(ui::Canvas &c);
    void page_records(ui::Canvas &c);
    void page_arm(ui::Canvas &c);
    void page_flap(ui::Canvas &c);
    void page_ops(ui::Canvas &c);
    void scroll_records(int rows);   // + = towards older parts
    void refresh_pic();   // cached 260 x 260 INTER_AREA view of the selected part crop

    const AppConfig &cfg_;
    std::string params_path_;
    HmiParams p_;
    HmiStatus st_;
    TextFT txt_;
    int page_ = 0, selected_ = -1;
    int rec_top_ = 0;                 // 记录 page: first row shown (0 = newest part); kept on the same part as new ones come
    int arm_step_ = 1, arm_ms_ = 1;   // indexes into the step / move-time choices of the arm page
    int ops_top_ = 0;                 // 运维 page: first row shown (0 = newest order)
    int ops_sel_ = 0;                 // 运维 page: selected order id (0 = the newest)
    std::vector<ui::Hit> hits_;
    std::vector<ui::Hit> panel_hits_;   // buttons of the status-panel strip (rebuilt by render_panel_strip)
    Ring<Part, kRecords> parts_;
    Ring<ui::Trend, 80> od_, bore_;    // measured gear OD / bore (value, NG)
    Ring<double, 512> part_times_;     // for parts per minute (last 60 s)
    Ring<ui::Sample, 120> samples_;    // 2 Hz, 60 s
    double last_sample_ = 0;
    std::vector<uint64_t> per_class_;  // index = class id, last = unclassified
    uint64_t total_ = 0, ok_ = 0, ng_ = 0, recheck_ = 0;
    cv::Mat sel_pic_;                  // cached resize of the selected crop
    int sel_pic_for_ = -1;             // selected_ the cache belongs to (-1 = invalid)
};

}  // namespace des
