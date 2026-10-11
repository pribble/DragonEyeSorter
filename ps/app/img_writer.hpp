// img_writer.hpp — background file writer for images / small text files (des_app collect), so the worker thread
// never waits for JPEG encoding or the SD card. Never blocks: when the queue is full the item is dropped and counted.
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include <opencv2/core.hpp>

namespace des {

class AsyncWriter {
public:
    explicit AsyncWriter(int max_queue = 64, int jpeg_quality = 95);
    ~AsyncWriter();
    bool add_image(const std::string &path, const cv::Mat &img);   // false = dropped (queue full)
    bool add_text(const std::string &path, const std::string &text);
    void flush();   // wait until the queue is empty

    std::atomic<uint64_t> written{0}, dropped{0}, errors{0};

private:
    struct Item {
        std::string path, text;
        cv::Mat img;
    };
    void loop();

    int max_queue_, quality_;
    std::thread th_;
    std::mutex m_;
    std::condition_variable cv_, idle_;
    std::deque<Item> q_;
    bool quit_ = false, busy_ = false;
};

bool make_dirs(const std::string &path);

}  // namespace des
