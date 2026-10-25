// hmi.hpp — the HDMI operator interface (赛题硬性项② / 评分「HDMI 可视化曲线 / 参数配置界面」).
//
// A 1920 x 300 band below the 3/4-scaled camera picture, five pages, evdev mouse/keyboard (ui_input.hpp), FreeType text
// + drawing helpers (ui_text.hpp): 总览 counts/yield/throughput/state/per class/last parts · 曲线 OD+bore trends +
// worker/NPU time + belt speed · 参数 recipe/speed/tolerances/size rule/confidence/images/run control/PL display ·
// 记录 last parts with reason/sizes/crop · 机械臂 manual jogging/torque/teach (app/arm_io.hpp). Everything runs on the
// worker thread (core 1): render() (~8 Hz) draws into a FRESH BGR image the capture thread only copies to the display
// (a reused buffer would alias snapshots still alive in live.cpp's pool: mid-blit tearing).
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
};

struct HmiAction {
    enum Kind { Params, Command, Recipe, Arm, Jog } kind = Params;
    int cmd = 0;              // CANP_CMD_* for Command, recipe id for Recipe, +1 / -1 / 0 for Jog (worker applies run rules)
    ArmRequest arm;           // Arm: request for the manual arm thread
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
    Hmi(const AppConfig &cfg, const std::string &params_path);
    const HmiParams &params() const { return p_; }
    bool set_recipe(int id);  // known id: switch, load its sizes into the params, save; false = unknown id
    void add_part(const PartRecord &r, const cv::Mat &crop, double now);
    void status(const HmiStatus &s, double now);
    std::vector<HmiAction> handle(const std::vector<UiEvent> &ev);   // clicks / keys -> actions for the worker
    cv::Mat render();                                               // fresh 1920 x 300 CV_8UC3 every call
    bool fonts_ok() const { return txt_.ok(); }
    bool arm_page() const { return page_ == 4; }   // the manual arm page is shown (it polls the servos)

private:
    struct Part {
        PartRecord r;
        cv::Mat crop;   // 224^2 part crop, kept only while the part is one of the 9 visible rows
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
    void refresh_pic();   // cached 260 x 260 INTER_AREA view of the selected part crop

    const AppConfig &cfg_;
    std::string params_path_;
    HmiParams p_;
    HmiStatus st_;
    TextFT txt_;
    int page_ = 0, selected_ = -1;
    int arm_step_ = 1, arm_ms_ = 1;   // indexes into the step / move-time choices of the arm page
    std::vector<ui::Hit> hits_;
    Ring<Part, 40> parts_;
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
