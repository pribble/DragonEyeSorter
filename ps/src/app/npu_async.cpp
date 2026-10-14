// npu_async.cpp — see npu_async.hpp
#include "npu_async.hpp"

#include <pthread.h>
#include <sched.h>
#include <time.h>

namespace des {

std::string AsyncClassifier::start(const std::string &json_path, int core)
{
    stop();
    std::string e = npu_.load(json_path);
    if (!e.empty()) return e;
    quit_ = false;
    th_ = std::thread(&AsyncClassifier::loop, this, core);
    return "";
}

static uint64_t mono_us()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000000u + uint64_t(ts.tv_nsec) / 1000u;
}

bool AsyncClassifier::submit(int id, const cv::Mat &crop, uint64_t frame_us)
{
    if (!th_.joinable() || crop.empty()) return false;
    std::lock_guard<std::mutex> lk(m_);
    if (int(in_.size()) >= max_queue_) {
        dropped++;
        return false;
    }
    in_.push_back({id, crop, frame_us ? frame_us : mono_us()});
    cv_.notify_one();
    return true;
}

std::vector<NpuResult> AsyncClassifier::poll()
{
    std::lock_guard<std::mutex> lk(m_);
    std::vector<NpuResult> r;
    r.swap(out_);
    return r;
}

void AsyncClassifier::stop()
{
    if (!th_.joinable()) return;
    {
        std::lock_guard<std::mutex> lk(m_);
        quit_ = true;
    }
    cv_.notify_one();
    th_.join();
}

void AsyncClassifier::loop(int core)
{
    if (core >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(core, &set);
        pthread_setaffinity_np(pthread_self(), sizeof set, &set);
    }
    sched_param sp{};
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp);
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait(lk, [&] { return quit_ || !in_.empty(); });
            if (quit_) return;
            job = std::move(in_.front());
            in_.pop_front();
        }
        double pre = 0, run = 0;
        std::vector<float> p = npu_.classify(job.crop, &pre, &run);
        last_pre_ms = float(pre);
        last_npu_ms = float(run);
        if (p.empty()) {
            failed++;
            continue;
        }
        runs++;
        std::lock_guard<std::mutex> lk(m_);
        NpuResult res;
        res.id = job.id;
        res.probs = std::move(p);
        res.pre_ms = float(pre);
        res.npu_ms = float(run);
        res.lat_ms = (mono_us() - job.frame_us) * 1e-3;
        out_.push_back(std::move(res));
    }
}

}  // namespace des
