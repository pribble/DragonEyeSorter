// hmi.hpp — the HDMI operator interface (赛题硬性项② / 评分「HDMI 可视化曲线 / 参数配置界面」).
//
// A 1920 x 300 band at the bottom of the screen, below the 3/4-scaled camera picture. Five pages, mouse / keyboard through evdev (ui_input.hpp), Chinese text (text_ft.hpp):
//   总览  counts, yield, throughput, run state, parts per class, last parts
//   曲线  gear OD and rack length trends with tolerance lines + mean / sigma / Cpk; worker / NPU time, belt speed
//   参数  recipe (配方), belt speed, size tolerances, size rule on / off, confidence threshold, image saving; run / stop.
//         Applied at once by the worker and saved to <config dir>/ui_params.json (loaded at start)
//   记录  last parts with reason, sizes and the part crop
//   机械臂 manual jogging of the six servos, torque on / off, teach (app/arm_manual.hpp)
// Everything runs on the worker thread (core 1): render() (~8 Hz) draws into a BGR image that the capture thread only
// copies into the display buffer, plus the cursor.
#pragma once

#include <deque>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "arm_manual.hpp"
#include "config.hpp"
#include "part_log.hpp"
#include "text_ft.hpp"
#include "ui_input.hpp"

namespace des {

struct HmiParams {            // operator-editable, persisted in ui_params.json
    double belt_speed = 50;   // mm/s (run mode)
    bool size_rule = false;
    float gear_od = 32, gear_od_tol = 0.15f, gear_bore = 6, gear_bore_tol = 0.3f;
    float min_conf = 0.5f;
    int images = 1;           // 0 none, 1 NG + recheck, 2 all
    int pl_mode = 0;          // des_vision display: 0 camera 1 gray 2 Gaussian 3 binary 4 Sobel 5 edges
    int pl_thr = 128;         // des_vision binary threshold
    int recipe = 1;           // active recipe id (recipes.json); switching it also resets the four size values above
};

struct HmiStatus {            // from the worker, every cycle
    std::string mode = "live";
    int run_state = -1;       // CANP_ST_* in run mode, -1 otherwise
    double belt_mm_s = 0;     // commanded (stepper)
    double belt_enc_mm_s = 0; // measured by the measuring-wheel encoder (valid when enc_live)
    bool enc_live = false;    // the encoder has counted since start (wired)
    float worker_ms = 0, npu_ms = 0;
    bool npu = false, clock_synced = false;
    bool pl_vision = false;   // bitstream has des_vision (v6)
    int inputs = 0, tracks = 0;
    int jog = 0;              // manual belt jog: +1 forward, -1 reverse, 0 off
    uint64_t logged = 0, log_images = 0;
    ArmManualView arm;        // manual arm page
};

struct HmiAction {
    enum Kind { Params, Command, Recipe, Arm, Jog } kind = Params;
    int cmd = 0;              // CANP_CMD_* for Command, recipe id for Recipe, +1 / -1 / 0 for Jog (the worker applies the run rules first)
    ArmRequest arm;           // Arm: request for the manual arm thread
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
    cv::Mat render(double now);
    bool fonts_ok() const { return txt_.ok(); }
    bool arm_page() const { return page_ == 4; }   // the manual arm page is shown (it polls the servos)

private:
    struct Part {
        PartRecord r;
        cv::Mat crop;
        double t = 0;
    };
    struct Hit {
        cv::Rect box;
        int action;           // see hmi.cpp
        int arg;
    };
    void save_params() const;
    void load_params();
    void page_overview(cv::Mat &img, double now);
    void page_curves(cv::Mat &img);
    void page_params(cv::Mat &img);
    void page_records(cv::Mat &img);
    void page_arm(cv::Mat &img);
    void button(cv::Mat &img, const cv::Rect &r, const std::string &text, bool on, int action, int arg, int px = 22);
    void text(cv::Mat &img, const std::string &s, int x, int y, int px, const cv::Scalar &c);

    const AppConfig &cfg_;
    std::string params_path_;
    HmiParams p_;
    HmiStatus st_;
    TextFT txt_;
    int page_ = 0, selected_ = -1;
    int arm_step_ = 1, arm_ms_ = 1;   // indexes into the step / move-time choices of the arm page
    std::vector<Hit> hits_;
    std::deque<Part> parts_;                     // newest first, <= 40
    std::deque<std::pair<float, bool>> od_, bore_;  // measured gear OD / bore (value, NG) for the trends, <= 80
    std::deque<double> part_times_;              // for parts per minute
    struct Sample {
        float worker, npu, belt;
        float belt_enc;   // encoder-measured belt speed, < 0 = no encoder
    };
    std::deque<Sample> samples_;                 // 2 Hz, 60 s
    double last_sample_ = 0;
    std::vector<uint64_t> per_class_;            // index = class id, last = unclassified
    uint64_t total_ = 0, ok_ = 0, ng_ = 0, recheck_ = 0;
    std::vector<std::string> class_cn_;
};

}  // namespace des
