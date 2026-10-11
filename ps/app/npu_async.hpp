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

class AsyncClassifier {
public:
    ~AsyncClassifier() { stop(); }
    std::string start(const std::string &json_path, int core = 1);   // loads the model, then starts the thread
    bool running() const { return th_.joinable(); }
    bool submit(int id, const cv::Mat &crop);                         // false = queue full, dropped
    std::vector<std::pair<int, std::vector<float>>> poll();           // finished (track id, probabilities)
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
    std::deque<std::pair<int, cv::Mat>> in_;
    std::vector<std::pair<int, std::vector<float>>> out_;
    bool quit_ = false;
    int max_queue_ = 4;
};

}  // namespace des
