// part_log.hpp — local storage of inspection results (赛题硬性项② 本地数据存储; 领域 3 中级「本地存储不良图像」).
//
// One CSV line per decided part, plus the part crop (roi_crop.hpp, the NPU input) for NG / recheck parts (or every
// part), under <dir>/<YYYY-MM-DD>/: parts_<session>.csv and img/<session>_<track>_<label>.jpg. A background thread
// does all file I/O, so the worker never waits for the SD card; when the queue is full records are dropped and
// counted (never blocks). Columns match the SQLite table planned for the UI history view (docs/软件与UI.md).
// ⚠ The board has no RTC: wall time is whatever the clock says (sync it with `date -s` before a test run); the
// monotonic t_s column is always right.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include <opencv2/core.hpp>

#include "config.hpp"
#include "tracker.hpp"

namespace des {

struct PartRecord {
    int64_t wall_ms = 0;         // CLOCK_REALTIME
    double t_s = 0;              // seconds since the log was opened (monotonic)
    int track_id = 0;
    uint32_t frame_first = 0, frame_last = 0;
    int cls = -1;
    std::string label;           // class label, "unclassified"
    float conf = 0;
    std::string disp, dest, why; // pick / flap / recheck, destination name, reason
    PartMeasure meas;
    float s = 0, y = 0, axis = 0, len = 0, wid = 0;   // belt coordinate / lateral position / blob geometry
    // PL coarse measurement (des_vision ROI statistics, v6): bright area, area-equivalent diameter, bounding box
    uint32_t pl_area = 0;
    float pl_d_mm = 0, pl_w_mm = 0, pl_h_mm = 0;
    // NPU: votes, mean inference time, mean latency camera frame complete -> probabilities (0 = no NPU result)
    int votes = 0;
    float npu_ms = 0, lat_ms = 0;
    int recipe = 0;              // active recipe id when the part was logged (recipes.json)
};

class PartLog {
public:
    ~PartLog() { close(); }
    std::string open(const StorageConfig &cfg);   // "" on success
    void add(PartRecord r, const cv::Mat &crop);  // non-blocking; crop may be empty
    void close();                                 // flush everything, stop the writer
    bool is_open() const { return th_.joinable(); }
    void set_images(const std::string &mode) { std::lock_guard<std::mutex> lk(m_); cfg_.images = mode; }   // HMI
    double now_s() const;                         // for PartRecord::t_s

    std::string session;                          // YYYYMMDD-HHMMSS of open()
    std::string csv_path;
    std::atomic<uint64_t> written{0}, images{0}, dropped{0}, errors{0};

private:
    struct Item {
        PartRecord r;
        cv::Mat crop;
    };
    void loop();
    void write(const Item &it);

    StorageConfig cfg_;
    std::string day_dir_;
    FILE *csv_ = nullptr;
    double t0_ = 0;
    std::thread th_;
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<Item> q_;
    bool quit_ = false;
};

const char *csv_header();

// Wall clock for the records: CLOCK_REALTIME + an offset learned from the PLC's TIME frame (CAN 0x104), because the
// board has no RTC. Thread-safe; 0 offset until a TIME frame arrives.
void set_wall_offset_ms(int64_t offset_ms);
int64_t wall_now_ms();
bool wall_synced();

}  // namespace des
