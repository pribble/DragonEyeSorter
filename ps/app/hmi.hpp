// hmi.hpp — the HDMI operator interface (赛题硬性项② / 评分「HDMI 可视化曲线 / 参数配置界面」).
//
// A 1920 x 300 band at the bottom of the 60 fps video (parts run near the belt center line; the band covers the far
// side of the belt only). Four pages, mouse / keyboard through evdev (ui_input.hpp), Chinese text (text_ft.hpp):
//   总览  counts, yield, throughput, run state, parts per class, last parts
//   曲线  gear OD and rack length trends with tolerance lines + mean / sigma / Cpk; worker / NPU time, belt speed
//   参数  belt speed, size tolerances, size rule on / off, confidence threshold, image saving; run / stop buttons.
//         Applied at once by the worker and saved to <config dir>/ui_params.json (loaded at start)
//   记录  last parts with reason, sizes and the part crop
// Everything runs on the worker thread (core 1): render() (~8 Hz) draws into a BGR image that the capture thread only
// copies into the display buffer, plus the cursor.
#pragma once

#include <deque>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "config.hpp"
#include "part_log.hpp"
#include "text_ft.hpp"
#include "ui_input.hpp"

namespace des {

struct HmiParams {            // operator-editable, persisted in ui_params.json
    double belt_speed = 50;   // mm/s (run mode)
    bool size_rule = false;
    float gear_od = 32, gear_od_tol = 0.15f, rack_len = 50, rack_len_tol = 0.5f;
    float min_conf = 0.5f;
    int images = 1;           // 0 none, 1 NG + recheck, 2 all
    int pl_mode = 0;          // des_vision display: 0 camera 1 gray 2 Gaussian 3 binary 4 Sobel 5 edges
    int pl_thr = 128;         // des_vision binary threshold
};

struct HmiStatus {            // from the worker, every cycle
    std::string mode = "live";
    int run_state = -1;       // CANP_ST_* in run mode, -1 otherwise
    double belt_mm_s = 0;
    float worker_ms = 0, npu_ms = 0;
    bool npu = false, clock_synced = false;
    bool pl_vision = false;   // bitstream has des_vision (v6)
    int inputs = 0, tracks = 0;
    uint64_t logged = 0, log_images = 0;
};

struct HmiAction {
    enum Kind { Params, Command } kind = Params;
    int cmd = 0;              // CANP_CMD_* for Command
};

class Hmi {
public:
    static constexpr int W = 1920, H = 300, Y0 = 1080 - 300;
    Hmi(const AppConfig &cfg, const std::string &params_path);
    const HmiParams &params() const { return p_; }
    void add_part(const PartRecord &r, const cv::Mat &crop, double now);
    void status(const HmiStatus &s, double now);
    std::vector<HmiAction> handle(const std::vector<UiEvent> &ev);   // clicks / keys -> actions for the worker
    cv::Mat render(double now);
    bool fonts_ok() const { return txt_.ok(); }

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
    void button(cv::Mat &img, const cv::Rect &r, const std::string &text, bool on, int action, int arg, int px = 22);
    void text(cv::Mat &img, const std::string &s, int x, int y, int px, const cv::Scalar &c);

    const AppConfig &cfg_;
    std::string params_path_;
    HmiParams p_;
    HmiStatus st_;
    TextFT txt_;
    int page_ = 0, selected_ = -1;
    std::vector<Hit> hits_;
    std::deque<Part> parts_;                     // newest first, <= 40
    std::deque<std::pair<float, bool>> od_, len_;   // measured sizes (value, NG) for the trends, <= 80
    std::deque<double> part_times_;              // for parts per minute
    struct Sample {
        float worker, npu, belt;
    };
    std::deque<Sample> samples_;                 // 2 Hz, 60 s
    double last_sample_ = 0;
    std::vector<uint64_t> per_class_;            // index = class id, last = unclassified
    uint64_t total_ = 0, ok_ = 0, ng_ = 0, recheck_ = 0;
    std::vector<std::string> class_cn_;
};

}  // namespace des
