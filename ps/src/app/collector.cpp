// collector.cpp — see collector.hpp
#include "collector.hpp"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <thread>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include "bg_queue.hpp"
#include "util.hpp"
namespace des {
// Background file writer for images / small text files (was img_writer.*). One worker thread; the queue is bounded
// (bg_queue.hpp), a full queue drops the item and counts it, so the caller never waits for JPEG encoding or the SD card.
class Collector::Writer {
public:
    Writer(int max_queue, int jpeg_quality) : q_(size_t(max_queue)), quality_(jpeg_quality)
    {
        th_ = std::thread(&Writer::loop, this);
    }
    ~Writer()
    {
        q_.close();   // keep what is queued: the consumer drains it (same as the old quit flag)
        th_.join();
    }
    bool add_image(const std::string &path, const cv::Mat &img)
    {
        const bool jpeg = path.size() > 4 && path.compare(path.size() - 4, 4, ".jpg") == 0;
        return add({path, "", img, jpeg});
    }
    bool add_text(const std::string &path, const std::string &text) { return add({path, text, cv::Mat(), false}); }
    // Wait until everything queued has been written (Collector::finish writes session.json with the final counts).
    void flush()
    {
        std::unique_lock<std::mutex> lk(idle_m_); idle_.wait(lk, [&] { return inflight_.load() == 0; });
    }
    std::atomic<uint64_t> dropped{0}, errors{0}; private:
    struct Item {
        std::string path, text; cv::Mat img;
        bool jpeg = false;
    };
    bool add(Item it)
    {
        inflight_++;   // counted before the push, so flush() cannot see 0 with an item on its way in
        if (!q_.push(std::move(it))) {
            inflight_--; dropped++;
            return false;
        }
        return true;
    }
    void loop()
    {
        Item it;
        while (q_.pop(it)) {
            bool ok;
            if (!it.img.empty()) {
                ok = it.jpeg ? cv::imwrite(it.path, it.img, {cv::IMWRITE_JPEG_QUALITY, quality_})
                             : cv::imwrite(it.path, it.img);
            } else {
                FILE *f = fopen(it.path.c_str(), "w"); ok = f && fwrite(it.text.data(), 1, it.text.size(), f) == it.text.size();
                if (f) ok = fclose(f) == 0 && ok;
            }
            if (!ok) errors++;
            if (--inflight_ == 0) {
                std::lock_guard<std::mutex> lk(idle_m_); idle_.notify_all();
            }
        }
    }
    BgQueue<Item> q_; std::thread th_;
    std::mutex idle_m_; std::condition_variable idle_;
    std::atomic<int> inflight_{0}; int quality_;
};
Collector::Collector(const CollectOptions &opt, const AppConfig &cfg)
    : opt_(opt), cfg_(cfg), hinv_(cfg.px_to_part.inverse())
{
    // 相对的输出目录按 des_app.json 所在目录解析（与 storage.dir / label_list 同规矩）： 生产配置 → /root/des/data/collect；测试目录部署的副本 → 写自己的，不混进生产数据
    if (!opt_.out.empty() && opt_.out[0] != '/') opt_.out = cfg.config_dir + "/" + opt_.out;
    spec_.side_mm = cfg.measure.crop_side_mm; spec_.out_px = cfg.measure.crop_px > 0 ? cfg.measure.crop_px : 224;
    spec_.hinv = &hinv_;   // saves the per-crop matrix inversion (process-lifetime constant)
    spec_.flat = cfg.measure.flat.get();
}
Collector::~Collector() = default;
std::string Collector::open()
{
    time_t now = time(nullptr);
    tm lt{};
    localtime_r(&now, &lt);
    char ses[32];
    strftime(ses, sizeof ses, "%Y%m%d-%H%M%S", &lt);
    session_ = std::string(ses) + "_" + opt_.label;
    dir = opt_.out + "/" + session_;
    class_dir_ = dir + "/" + std::to_string(opt_.label_id) + "_" + opt_.label;
    if (!make_dirs(class_dir_)) return "cannot create " + class_dir_;
    if (opt_.full_every > 0 && !make_dirs(dir + "/full")) return "cannot create " + dir + "/full";
    w_.reset(new Writer(128, 95));
    return "";
}
void Collector::prepare(const Tracker &tr, const uint8_t *bgra, int w, int h, int stride, double belt_mm, uint32_t frame, const std::vector<Detection> &dets, const std::vector<std::array<float, 8>> &rects)
{
    n_++;
    pending_tracks_.clear();
    const cv::Mat src(h, w, CV_8UC4, const_cast<uint8_t *>(bgra), size_t(stride));
    for (const Track &t : tr.tracks()) {
        if (t.state != TrackState::Tracking || t.merged || t.n_geom == 0) continue;
        Count &c = per_track_[t.id];
        if (c.saved >= opt_.max_per_part || (c.saved > 0 && n_ - c.last < uint64_t(opt_.every))) continue;
        // whole crop square inside the image (a crop cut by the border would teach the model the border)
        ORect sq{{t.x_at(belt_mm), t.y}, spec_.side_mm / 2, spec_.side_mm / 2, t.axis};
        bool inside = true;
        for (const Vec2 &v : sq.corners()) {
            Vec2 p = hinv_.map(v);
            inside = inside && p.x >= 1 && p.y >= 1 && p.x <= w - 2 && p.y <= h - 2;
        }
        if (!inside) continue;
        Pending p;
        p.img = crop_part(src, cfg_.px_to_part, {t.x_at(belt_mm), t.y}, t.axis, spec_);
        p.path = fmt("%s/%s_%d_%d.jpg", class_dir_.c_str(), session_.c_str(), t.id, c.saved);
        pending_.push_back(std::move(p));
        pending_tracks_.push_back({t.id, c.saved});
    }
    if (opt_.full_every > 0 && n_ % uint64_t(opt_.full_every) == 0) {
        std::string lab;
        char line[96];
        for (size_t i = 0; i < dets.size() && i < rects.size(); i++) {
            if (dets[i].partial || dets[i].merged) continue;
            float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
            for (int k = 0; k < 4; k++) {
                x0 = std::min(x0, rects[i][2 * k]); x1 = std::max(x1, rects[i][2 * k]);
                y0 = std::min(y0, rects[i][2 * k + 1]); y1 = std::max(y1, rects[i][2 * k + 1]);
            }
            snprintf(line, sizeof line, "%d %.6f %.6f %.6f %.6f\n", opt_.label_id, 0.5f * (x0 + x1) / w, 0.5f * (y0 + y1) / h, (x1 - x0) / w, (y1 - y0) / h);
            lab += line;
        }
        if (!lab.empty()) {
            Pending img, txt;
            cv::cvtColor(src, img.img, cv::COLOR_BGRA2BGR);   // one pass over the (uncached) buffer
            const std::string base = fmt("%s/full/%s_%u", dir.c_str(), session_.c_str(), frame);
            img.path = base + ".jpg";
            img.full = true;
            txt.path = base + ".txt";
            txt.text = lab;
            pending_.push_back(std::move(img));
            pending_.push_back(std::move(txt));
        }
    }
}
void Collector::commit()
{
    for (Pending &p : pending_) {
        bool ok = p.img.empty() ? w_->add_text(p.path, p.text) : w_->add_image(p.path, p.img);
        if (ok && !p.img.empty()) {
            if (p.full) frames++;
            else crops++;
        }
    }
    for (const auto &pt : pending_tracks_) {
        Count &c = per_track_[pt.first];
        if (c.saved == 0) parts++;
        c.saved = pt.second + 1;
        c.last = n_;
    }
    pending_.clear();
    pending_tracks_.clear();
    if (per_track_.size() > 4000) per_track_.erase(per_track_.begin());
}
void Collector::finish()
{
    if (!w_) return;
    w_->flush();
    char js[1024];
    snprintf(js, sizeof js, "{\n \"session\": \"%s\",\n \"label\": \"%s\",\n \"label_id\": %d,\n \"crops\": %llu,\n \"parts\": %llu,\n" " \"full_frames\": %llu,\n \"dropped\": %llu,\n \"write_errors\": %llu,\n" " \"crop\": {\"side_mm\": %.1f, \"px\": %d},\n \"every\": %d,\n \"max_per_part\": %d,\n"
             " \"px_to_table\": [%g, %g, %g, %g, %g, %g, %g, %g, %g]\n}\n",
             session_.c_str(), opt_.label.c_str(), opt_.label_id, (unsigned long long)crops, (unsigned long long)parts,
             (unsigned long long)frames, (unsigned long long)w_->dropped.load(), (unsigned long long)w_->errors.load(),
             spec_.side_mm, spec_.out_px, opt_.every, opt_.max_per_part, cfg_.px_to_table.h[0], cfg_.px_to_table.h[1],
             cfg_.px_to_table.h[2], cfg_.px_to_table.h[3], cfg_.px_to_table.h[4], cfg_.px_to_table.h[5],
             cfg_.px_to_table.h[6], cfg_.px_to_table.h[7], cfg_.px_to_table.h[8]);
    FILE *f = fopen((dir + "/session.json").c_str(), "w");
    if (f) {
        fputs(js, f);
        fclose(f);
    }
}
std::string Collector::status() const
{
    char b[160];
    snprintf(b, sizeof b, "COLLECT %s: %llu crops from %llu parts, %llu full frames, dropped %llu", opt_.label.c_str(), (unsigned long long)crops, (unsigned long long)parts, (unsigned long long)frames, (unsigned long long)(w_ ? w_->dropped.load() : 0));
    return b;
}
}  // namespace des
