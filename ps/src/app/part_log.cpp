// part_log.cpp — see part_log.hpp
#include "part_log.hpp"

#include <sys/stat.h>
#include <time.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <vector>

#include <opencv2/imgcodecs.hpp>

namespace des {
namespace {

bool mkdirs(const std::string &path)
{
    for (size_t i = 1; i <= path.size(); i++)
        if (i == path.size() || path[i] == '/') {
            std::string p = path.substr(0, i);
            if (mkdir(p.c_str(), 0755) != 0 && errno != EEXIST) return false;
        }
    return true;
}

std::string csv_text(std::string s)   // no separators / line breaks inside a field
{
    for (char &c : s)
        if (c == ',' || c == '\n' || c == '\r') c = ';';
    return s;
}

}  // namespace

static std::atomic<int64_t> g_wall_offset_ms{0};
static std::atomic<bool> g_wall_synced{false};

void set_wall_offset_ms(int64_t offset_ms)
{
    g_wall_offset_ms = offset_ms;
    g_wall_synced = true;
}

int64_t wall_now_ms()
{
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return int64_t(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000 + g_wall_offset_ms.load();
}

bool wall_synced() { return g_wall_synced; }

const char *csv_header()
{
    return "wall_ms,t_s,track,frame_first,frame_last,cls,label,conf,disp,dest,why,"
           "meas_valid,kind,size_mm,bore_mm,ecc_mm,pitch_mm,teeth,short,missing,flash,teeth_judged,smear_mm,"
           "axis_dist_mm,meas_frame,meas_count,s_mm,y_mm,axis_deg,len_mm,wid_mm,pl_area_px,pl_d_mm,pl_w_mm,pl_h_mm,"
           "image,votes,npu_ms,lat_ms,recipe";
}

double PartLog::now_s() const
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9 - t0_;
}

std::string PartLog::open(const StorageConfig &cfg)
{
    close();
    cfg_ = cfg;
    if (!cfg.enabled) return "";
    time_t now = time(nullptr);
    tm lt{};
    localtime_r(&now, &lt);
    char day[16], ses[32];
    strftime(day, sizeof day, "%Y-%m-%d", &lt);
    strftime(ses, sizeof ses, "%Y%m%d-%H%M%S", &lt);
    session = ses;
    day_dir_ = cfg.dir + "/" + day;
    if (!mkdirs(day_dir_ + "/img")) return "cannot create " + day_dir_ + ": " + strerror(errno);
    csv_path = day_dir_ + "/parts_" + session + ".csv";
    csv_ = fopen(csv_path.c_str(), "w");
    if (!csv_) return "cannot open " + csv_path + ": " + strerror(errno);
    fprintf(csv_, "%s\n", csv_header());
    fflush(csv_);
    t0_ = 0;
    t0_ = now_s();
    quit_ = false;
    th_ = std::thread(&PartLog::loop, this);
    return "";
}

void PartLog::add(PartRecord r, const cv::Mat &crop)
{
    if (!th_.joinable()) return;
    std::lock_guard<std::mutex> lk(m_);
    if (int(q_.size()) >= cfg_.max_queue) {
        dropped++;
        return;
    }
    const bool want = cfg_.images == "all" || (cfg_.images == "ng" && r.disp != "pick");
    q_.push_back({std::move(r), want ? crop : cv::Mat()});   // cv::Mat is reference-counted: no copy here
    cv_.notify_one();
}

void PartLog::close()
{
    if (th_.joinable()) {
        {
            std::lock_guard<std::mutex> lk(m_);
            quit_ = true;
        }
        cv_.notify_one();
        th_.join();
    }
    if (csv_) {
        fclose(csv_);
        csv_ = nullptr;
    }
}

void PartLog::loop()
{
    for (;;) {
        Item it;
        {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait(lk, [&] { return quit_ || !q_.empty(); });
            if (q_.empty()) return;   // quit with everything written
            it = std::move(q_.front());
            q_.pop_front();
        }
        write(it);
    }
}

void PartLog::write(const Item &it)
{
    const PartRecord &r = it.r;
    std::string img;
    if (!it.crop.empty()) {
        img = "img/" + session + "_" + std::to_string(r.track_id) + "_" + csv_text(r.label) + ".jpg";
        std::vector<int> prm = {cv::IMWRITE_JPEG_QUALITY, cfg_.jpeg_quality};
        if (cv::imwrite(day_dir_ + "/" + img, it.crop, prm)) images++;
        else { errors++; img.clear(); }
    }
    const PartMeasure &m = r.meas;
    int n = fprintf(csv_,
                    "%lld,%.3f,%d,%u,%u,%d,%s,%.3f,%s,%s,%s,"
                    "%d,%c,%.3f,%.3f,%.3f,%.4f,%d,%d,%d,%d,%d,%.2f,"
                    "%.1f,%u,%d,%.2f,%.2f,%.1f,%.1f,%.1f,%u,%.2f,%.2f,%.2f,%s,%d,%.2f,%.1f,%d\n",
                    (long long)r.wall_ms, r.t_s, r.track_id, r.frame_first, r.frame_last, r.cls,
                    csv_text(r.label).c_str(), r.conf, csv_text(r.disp).c_str(), csv_text(r.dest).c_str(),
                    csv_text(r.why).c_str(), int(m.valid), m.kind ? m.kind : '-', m.size, m.bore, m.ecc, m.pitch,
                    m.teeth, m.short_teeth, m.missing_teeth, m.flash_gaps, int(m.teeth_judged), m.smear,
                    m.axis_dist < 1e8f ? m.axis_dist : -1.f, m.frame, m.count, r.s, r.y, r.axis * 57.29578f, r.len,
                    r.wid, r.pl_area, r.pl_d_mm, r.pl_w_mm, r.pl_h_mm, img.c_str(), r.votes, r.npu_ms, r.lat_ms, r.recipe);
    if (n < 0 || fflush(csv_) != 0) errors++;
    else written++;
}

}  // namespace des
