// bg_queue.hpp — one bounded blocking queue for the app layer's background threads.
//
// Replaces the hand-rolled mutex + condition_variable + deque + quit-flag harness copies (Runner arm hand-off,
// PartLog writer, NetFeed, ArmManual commands, AsyncClassifier jobs, collector AsyncWriter). The owner starts / joins
// its own std::thread. close() keeps queued items for the consumer to drain, close_and_clear() drops them.
#pragma once
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>
#include <vector>
namespace des {
template <class T>
class BgQueue {
public:
    explicit BgQueue(size_t cap = 0) : cap_(cap) {}
    void set_cap(size_t cap) { std::lock_guard<std::mutex> lk(m_); cap_ = cap; }   // 0 = unbounded
    bool push(T v)   // false = closed or full: the caller counts the drop
    {
        std::lock_guard<std::mutex> lk(m_);
        if (closed_ || (cap_ != 0 && q_.size() >= cap_)) return false;
        q_.push_back(std::move(v));
        cv_.notify_one();
        return true;
    }
    void push_force(T v)   // keep the newest: a full capped queue drops its oldest item
    {
        std::lock_guard<std::mutex> lk(m_);
        if (closed_) return;
        if (cap_ != 0 && q_.size() >= cap_) q_.pop_front();
        q_.push_back(std::move(v));
        cv_.notify_one();
    }
    bool pop(T &out)   // blocks; false = closed and drained
    {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [&] { return closed_ || !q_.empty(); });
        if (q_.empty()) return false;
        out = std::move(q_.front());
        q_.pop_front();
        return true;
    }
    bool pop_for(T &out, int timeout_ms)   // false = timeout or closed and drained
    {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&] { return closed_ || !q_.empty(); });
        if (q_.empty()) return false;
        out = std::move(q_.front());
        q_.pop_front();
        return true;
    }
    bool drain(std::vector<T> &out)   // everything queued now, no waiting; the caller reuses out between cycles
    {
        std::lock_guard<std::mutex> lk(m_);
        if (q_.empty()) return false;
        out.reserve(out.size() + q_.size());
        for (T &v : q_) out.push_back(std::move(v));
        q_.clear();
        return true;
    }
    void close() { shutdown(false); }            // stop after the queued items (the consumer drains them)
    void close_and_clear() { shutdown(true); }   // stop now, drop what is queued
    void reopen() { std::lock_guard<std::mutex> lk(m_); closed_ = false; }
    bool closed() const { std::lock_guard<std::mutex> lk(m_); return closed_; }
private:
    void shutdown(bool drop)
    {
        {
            std::lock_guard<std::mutex> lk(m_);
            closed_ = true;
            if (drop) q_.clear();
        }
        cv_.notify_all();
    }
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::deque<T> q_;
    size_t cap_ = 0;
    bool closed_ = false;
};
}  // namespace des
