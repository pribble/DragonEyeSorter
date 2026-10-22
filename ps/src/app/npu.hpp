// npu.hpp — the NPU part classifier and its worker thread (赛题硬性项②「NPU 目标检测 / 分类」).
//
// NpuClassifier: one part crop (roi_crop.hpp) -> class probabilities. Model = train/export_npu.py output
// (<name>_uint8.onnx + <name>.json: class names, input mean / scale) through the vendor ArmNN executor on Alnpu /
// CpuAcc / CpuRef. The ONNX parser folds the input QuantizeLinear into the tensor, so the input is written as uint8
// with the tensor's scale / zero point (float inputs handled too); softmax runs here (cut off the exported graph).
// AsyncClassifier: the same classifier on its own thread (core 1); the live worker submits crops and collects the
// probabilities a few frames later. Small bounded queue: a full queue drops the crop (other frames re-submit).
#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <opencv2/core.hpp>
#include "bg_queue.hpp"
class NPUExecutor;
class CmaResourceZone;
namespace des {
class NpuClassifier {
public:
    NpuClassifier();
    ~NpuClassifier();
    std::string load(const std::string &json_path);   // "" on success
    // crop: BGR (resized if not the input size). Returns softmax probabilities (empty on failure);
    // pre_ms / npu_ms = input preparation / network run.
    std::vector<float> classify(const cv::Mat &crop_bgr, double *pre_ms = nullptr, double *npu_ms = nullptr);
    std::vector<float> last_logits;                     // dequantized network output of the last call
    const std::vector<std::string> &classes() const { return classes_; }
    std::string describe() const;                       // input / output tensor types, for logs
private:
    std::unique_ptr<CmaResourceZone> zone_;
    std::unique_ptr<NPUExecutor> ex_;
    std::vector<std::string> classes_;
    int w_ = 0, h_ = 0;
    float mean_[3] = {0, 0, 0}, scale_[3] = {1 / 255.f, 1 / 255.f, 1 / 255.f};
    bool in_u8_ = false, out_u8_ = false;
    float in_scale_ = 1, out_scale_ = 1;
    int in_zero_ = 0, out_zero_ = 0;
    size_t out_n_ = 0;
};
// One finished classification, copied to the worker with no heap traffic (8 slots = the class count of
// models/label_list.json; a larger model is truncated).
struct NpuResult {
    int id = 0;
    float probs[8] = {0};        // softmax probabilities, n_probs valid
    uint8_t n_probs = 0;
    uint64_t frame_us = 0;       // CLOCK_MONOTONIC when the camera frame of the crop was complete
    float pre_ms = 0, npu_ms = 0;   // input preparation, inference
    double lat_ms = 0;           // frame complete -> probabilities available (test report: AI latency)
};
class AsyncClassifier {
public:
    ~AsyncClassifier() { stop(); }
    std::string start(const std::string &json_path, int core = 1);   // loads the model, then starts the thread
    bool running() const { return th_.joinable(); }
    bool submit(int id, const cv::Mat &crop, uint64_t frame_us = 0);   // false = queue full, dropped
    bool poll(NpuResult &out);                                         // next result, false = none
    void stop();
    const NpuClassifier &model() const { return npu_; }
    std::atomic<uint64_t> runs{0};
    std::atomic<float> last_npu_ms{0};
private:
    struct Job {
        int id = 0;
        cv::Mat crop;
        uint64_t frame_us = 0;
    };
    void loop(int core);
    NpuClassifier npu_;
    std::thread th_;
    BgQueue<Job> q_{4};             // crops waiting for the NPU (a full queue drops)
    std::mutex out_m_;
    std::deque<NpuResult> out_;     // finished, not yet polled
};
}  // namespace des
