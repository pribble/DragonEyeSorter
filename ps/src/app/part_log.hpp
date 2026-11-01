// part_log.hpp — local storage of inspection results (赛题硬性项② 本地数据存储; 领域 3 中级「本地存储不良图像」).
//
// One CSV line per decided part, plus the part crop (roi_crop.hpp, the NPU input) for NG / recheck parts (or every
// part), under <dir>/<YYYY-MM-DD>/: parts_<session>.csv and img/<session>_<track>_<label>.jpg. A background thread
// does all file I/O, so the worker never waits for the SD card; when the queue is full records are dropped and
// counted (never blocks). Columns match the SQLite table planned for the UI history view (docs/软件与UI.md).
//
// The same lines / images are also streamed over Ethernet when StorageConfig::feed_port > 0 (赛题拓展 5「千兆网数据
// 上传」): the board listens on a TCP port and a PC client (pc/feed/des_feed.py) receives each record the log writes.
// That NetFeed is an implementation detail of part_log.cpp (own thread, bounded per-client queues, dead clients just
// close) — its wire frame is unchanged: 10-byte header, big-endian: 'D' 'F' <type> 0 <name_len:2> <payload_len:4>,
// then name, then payload; 'H' hello (session + CSV header), 'R' record, 'I' image, 'S' status every 2 s,
// 'W' one changed work order (app/ops.hpp, JSON) and 'w' the current work-order list (JSON array) right after the
// hello (2026-11-01: pc/ops/ops_station.py; des_feed.py skips both).
//
// ⚠ The board has no RTC: wall time is whatever the clock says (sync it with `date -s` before a test run, or let the
// PLC's TIME frame / Modbus time registers set the offset); the monotonic t_s column is always right.
#pragma once
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <opencv2/core.hpp>
#include "bg_queue.hpp"
#include "config.hpp"
#include "part_types.hpp"
namespace des {
class NetFeed;
// One decided part. Pointers stay valid for the whole process: label points into AppConfig::tracker.rules[i].label
// (or the literal "unclassified"), dest into AppConfig::bins / outlets.
struct PartRecord {
    int64_t wall_ms = 0;  double t_s = 0;
    int track_id = 0;     uint32_t frame_first = 0, frame_last = 0;
    int16_t cls = -1;     const char *label = "unclassified";   // 指向 config rules[i].label / 字面量
    float conf = 0;
    Disposition disp = Disposition::None;
    const char *dest = "";               // 指向 cfg.bins / cfg.outlets 的字符串
    char why[40] = {0};
    PartMeasure meas;
    float s = 0, y = 0, axis = 0, len = 0, wid = 0;
    uint32_t pl_area = 0; float pl_d_mm = 0, pl_w_mm = 0, pl_h_mm = 0;
    int16_t votes = 0;    float npu_ms = 0, lat_ms = 0;  uint8_t recipe = 0;
    int8_t beam = 0;      float beam_err = 0;   // through-beam (Track::beam, sched/beam.hpp); err only when beam == 2
};
const char *csv_header();   // the 44 column names, one line (part_stats.py / des_feed.py read them by name)
const char *beam_status(int8_t beam);   // CSV text of Track::beam: "" (off) / waiting / matched / missed / na
class PartLog {
public:
    PartLog();
    ~PartLog();
    std::string open(const StorageConfig &cfg);   // "" on success
    void add(PartRecord r, const cv::Mat &crop);  // non-blocking; crop may be empty
    // through-beam event (matched / repeat / no_track / missed / lost) -> beam_<session>.csv next to the parts CSV
    void add_beam(const BeamResult &b, double t_s);
    void close();                                 // flush everything, stop the writer
    bool is_open() const { return th_.joinable(); }
    void set_images(const std::string &mode);   // HMI: sampled per record by the writer's producer
    // 运维工单 to the remote station: one changed order ('W'); the list every new client gets after the hello ('w')
    void push_ops(const std::string &json);
    void set_ops_snapshot(const std::string &json_array);
    int feed_clients() const;                     // remote stations / feed clients connected now
    std::string day_dir() const { return day_dir_; }
    double now_s() const;                         // for PartRecord::t_s (monotonic - open())
    std::string session;                          // YYYYMMDD-HHMMSS of open()
    std::string csv_path;
    std::atomic<uint64_t> written{0}, images{0}, dropped{0}, errors{0};
private:
    struct Item {
        PartRecord r;
        cv::Mat crop;
        bool is_beam = false;   // a beam event instead of a part record
        BeamResult b;
        double b_t = 0;
    };
    void loop();
    void write(const Item &it);
    void write_beam(const Item &it);
    StorageConfig cfg_;
    mutable std::mutex m_;   // cfg_.images: set_images() vs add()
    std::string day_dir_;
    FILE *csv_ = nullptr;
    FILE *beam_ = nullptr;   // opened at the first beam event (no file when the beam is off)
    double t0_ = 0;
    std::thread th_;
    BgQueue<Item> q_;
    std::unique_ptr<NetFeed> feed_;
};
}  // namespace des
