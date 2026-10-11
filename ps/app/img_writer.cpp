// img_writer.cpp — see img_writer.hpp
#include "img_writer.hpp"

#include <sys/stat.h>

#include <cerrno>
#include <cstdio>
#include <vector>

#include <opencv2/imgcodecs.hpp>

namespace des {

bool make_dirs(const std::string &path)
{
    for (size_t i = 1; i <= path.size(); i++)
        if (i == path.size() || path[i] == '/') {
            std::string p = path.substr(0, i);
            if (mkdir(p.c_str(), 0755) != 0 && errno != EEXIST) return false;
        }
    return true;
}

AsyncWriter::AsyncWriter(int max_queue, int jpeg_quality) : max_queue_(max_queue), quality_(jpeg_quality)
{
    th_ = std::thread(&AsyncWriter::loop, this);
}

AsyncWriter::~AsyncWriter()
{
    {
        std::lock_guard<std::mutex> lk(m_);
        quit_ = true;
    }
    cv_.notify_one();
    th_.join();
}

bool AsyncWriter::add_image(const std::string &path, const cv::Mat &img)
{
    std::lock_guard<std::mutex> lk(m_);
    if (int(q_.size()) >= max_queue_) {
        dropped++;
        return false;
    }
    q_.push_back({path, "", img});
    cv_.notify_one();
    return true;
}

bool AsyncWriter::add_text(const std::string &path, const std::string &text)
{
    std::lock_guard<std::mutex> lk(m_);
    if (int(q_.size()) >= max_queue_) {
        dropped++;
        return false;
    }
    q_.push_back({path, text, cv::Mat()});
    cv_.notify_one();
    return true;
}

void AsyncWriter::flush()
{
    std::unique_lock<std::mutex> lk(m_);
    idle_.wait(lk, [&] { return q_.empty() && !busy_; });
}

void AsyncWriter::loop()
{
    for (;;) {
        Item it;
        {
            std::unique_lock<std::mutex> lk(m_);
            busy_ = false;
            idle_.notify_all();
            cv_.wait(lk, [&] { return quit_ || !q_.empty(); });
            if (q_.empty()) return;
            it = std::move(q_.front());
            q_.pop_front();
            busy_ = true;
        }
        bool ok;
        if (!it.img.empty()) {
            std::vector<int> prm;
            if (it.path.size() > 4 && it.path.compare(it.path.size() - 4, 4, ".jpg") == 0)
                prm = {cv::IMWRITE_JPEG_QUALITY, quality_};
            ok = cv::imwrite(it.path, it.img, prm);
        } else {
            FILE *f = fopen(it.path.c_str(), "w");
            ok = f && fwrite(it.text.data(), 1, it.text.size(), f) == it.text.size();
            if (f) ok = fclose(f) == 0 && ok;
        }
        if (ok) written++;
        else errors++;
    }
}

}  // namespace des
