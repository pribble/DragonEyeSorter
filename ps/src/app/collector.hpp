// collector.hpp — training-data collection (`des_tool collect --cls <label>`, 总计划 §3-②「标注用自动框」).
//
// Put parts of ONE class on the belt and run it: every tracked part (fully in view, not merged) gets a crop
// (roi_crop.hpp, exactly the NPU input) every `every` processed frames, at most `max_per_part`, written to
//   <out>/<session>/<id>_<label>/<session>_<track>_<k>.jpg
// which is already the training layout (train/prepare_dataset.py merges sessions and splits by part). Optionally
// (full_every > 0) a full frame + YOLO box labels from the locator every N frames (detector fallback data):
//   <out>/<session>/full/<session>_<frame>.jpg / .txt
// Crops are taken at the tracked position, not the measured one: badly defective parts must be collected too.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "config.hpp"
#include "img_writer.hpp"
#include "roi_crop.hpp"
#include "tracker.hpp"

namespace des {

struct CollectOptions {
    std::string label;           // class label from models/label_list.json ("" = not collecting)
    int label_id = -1;
    std::string out = "data/collect";  // 相对值按 des_app.json 所在目录解析（绝对值原样）；des.env 的 DES_COLLECT 可覆盖
    int every = 6;               // processed frames between two crops of the same part
    int max_per_part = 10;
    int full_every = 0;          // 0 = no full frames
};

class Collector {
public:
    Collector(const CollectOptions &opt, const AppConfig &cfg);
    std::string open();          // creates the session folders; "" on success
    // after tracker.update() of a frame. still_valid(): false if the DMA reused the buffer while copying -> dropped.
    template <class F>
    void step(const Tracker &tr, const uint8_t *bgra, int w, int h, int stride, double belt_mm, uint32_t frame,
              const std::vector<Detection> &dets, const std::vector<std::array<float, 8>> &rects, F still_valid)
    {
        pending_.clear();
        prepare(tr, bgra, w, h, stride, belt_mm, frame, dets, rects);
        if (!pending_.empty() && still_valid()) commit();
    }
    void finish();               // flush, write session.json
    std::string status() const;  // one line for the HDMI panel
    std::string dir;             // <out>/<session>
    uint64_t crops = 0, parts = 0, frames = 0;

private:
    struct Pending {
        std::string path, text;
        cv::Mat img;
    };
    void prepare(const Tracker &tr, const uint8_t *bgra, int w, int h, int stride, double belt_mm, uint32_t frame,
                 const std::vector<Detection> &dets, const std::vector<std::array<float, 8>> &rects);
    void commit();

    CollectOptions opt_;
    const AppConfig &cfg_;
    Homography hinv_;
    CropSpec spec_;
    std::string session_, class_dir_;
    std::unique_ptr<AsyncWriter> w_;
    struct Count {
        uint64_t last = 0;
        int saved = 0;
    };
    std::map<int, Count> per_track_;
    uint64_t n_ = 0;             // processed frames
    std::vector<Pending> pending_;
    std::vector<std::pair<int, int>> pending_tracks_;   // (track id, saved index) committed with pending_
};

}  // namespace des
