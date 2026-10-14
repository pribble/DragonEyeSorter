// npu_async.hpp — the NPU part classifier (npu_cls.hpp) on its own thread: the live worker submits part crops and
// collects the probabilities a few frames later, so it never waits while the NPU computes (the CPU only waits there).
// Small queue; when it is full a crop is dropped (the part gets other crops on later frames).
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <opencv2/core.hpp>

#include "npu_cls.hpp"

namespace des {

struct NpuResult {
    int id = 0;
    std::vector<float> probs;
    float pre_ms = 0, npu_ms = 0;   // input preparation, inference
    double lat_ms = 0;              // camera frame complete -> probabilities available (test report: AI latency)
};

class AsyncClassifier {
public:
    ~AsyncClassifier() { stop(); }
    std::string start(const std::string &json_path, int core = 1);   // loads the model, then starts the thread
    bool running() const { return th_.joinable(); }
    // frame_us: CLOCK_MONOTONIC when the camera frame of the crop was complete (CapturedFrame::t_us)
    bool submit(int id, const cv::Mat &crop, uint64_t frame_us = 0);  // false = queue full, dropped
    std::vector<NpuResult> poll();                                    // finished since the last poll
    void stop();
    const NpuClassifier &model() const { return npu_; }

    std::atomic<uint64_t> runs{0}, dropped{0}, failed{0};
    std::atomic<float> last_npu_ms{0}, last_pre_ms{0};

private:
    void loop(int core);
    NpuClassifier npu_;
    std::thread th_;
    std::mutex m_;
    std::condition_variable cv_;
    struct Job {
        int id;
        cv::Mat crop;
        uint64_t frame_us;
    };
    std::deque<Job> in_;
    std::vector<NpuResult> out_;
    bool quit_ = false;
    int max_queue_ = 4;
};

}  // namespace des
