// part_log.cpp — see part_log.hpp
#include "part_log.hpp"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <cstring>
#include <mutex>
#include <utility>
#include <vector>
#include <opencv2/imgcodecs.hpp>
#include "util.hpp"
namespace des {
namespace {
// mkdir -p is util.hpp's make_dirs (was a private copy here)
}  // namespace
const char *csv_header()
{
    return "wall_ms,t_s,track,frame_first,frame_last,cls,label,conf,disp,dest,why,"
           "meas_valid,kind,size_mm,bore_mm,ecc_mm,pitch_mm,teeth,short,missing,flash,teeth_judged,smear_mm,"
           "axis_dist_mm,meas_frame,meas_count,s_mm,y_mm,axis_deg,len_mm,wid_mm,pl_area_px,pl_d_mm,pl_w_mm,pl_h_mm,"
           "image,votes,npu_ms,lat_ms,recipe,hue_deg,sat,color,beam,beam_err_mm";
}
const char *beam_status(int8_t beam)
{
    switch (beam) {
    case 1: return "waiting";
    case 2: return "matched";
    case -1: return "missed";
    case -2: return "na";
    default: return "";
    }
}
// ------------------------------------------------------------------ NetFeed (was net_feed.hpp/.cpp; only used here)
class NetFeed {
public:
    ~NetFeed() { stop(); }
    std::string start(int port, const std::string &session, const std::string &csv_header, size_t max_queue = 64);
    void stop();                                    // flushes queued frames (up to ~0.5 s), closes everything
    void record(const std::string &csv_line);
    void image(const std::string &name, const std::vector<uint8_t> &jpeg);
    int port() const { return port_; }              // the bound port (start(0, ...) picks a free one)
    std::atomic<uint64_t> records{0}, images{0}, dropped{0};
    std::atomic<int> clients{0};
private:
    struct Client {
        int fd = -1; std::vector<std::shared_ptr<const std::string>> q;
        size_t off = 0;                             // bytes of q.front() already sent
    };
    void loop();
    void push(char type, const std::string &name, const char *data, size_t n);
    static std::shared_ptr<const std::string> frame(char type, const std::string &name, const char *data, size_t n);
    int lfd_ = -1, port_ = 0, wake_[2] = {-1, -1};
    std::string session_, header_;
    size_t max_queue_ = 64;
    std::mutex m_;
    std::vector<Client> cl_;
    bool quit_ = false;
    std::thread th_;
};
std::shared_ptr<const std::string> NetFeed::frame(char type, const std::string &name, const char *data, size_t n)
{
    auto f = std::make_shared<std::string>();
    f->reserve(10 + name.size() + n);
    const uint16_t nl = uint16_t(name.size() > 65535 ? 65535 : name.size());
    const uint32_t pl = uint32_t(n);
    const char hdr[10] = {'D', 'F', type, 0, char(nl >> 8), char(nl & 0xFF),
                          char(pl >> 24), char((pl >> 16) & 0xFF), char((pl >> 8) & 0xFF), char(pl & 0xFF)};
    f->append(hdr, 10);
    f->append(name, 0, nl);
    if (n) f->append(data, n);
    return f;
}
std::string NetFeed::start(int port, const std::string &session, const std::string &csv_header, size_t max_queue)
{
    stop();
    session_ = session;
    header_ = csv_header;
    max_queue_ = max_queue;
    lfd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (lfd_ < 0) return std::string("socket: ") + strerror(errno);
    int one = 1;
    setsockopt(lfd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(uint16_t(port));
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(lfd_, reinterpret_cast<sockaddr *>(&a), sizeof a) < 0 || listen(lfd_, 4) < 0) {
        std::string e = std::string("port ") + std::to_string(port) + ": " + strerror(errno); close(lfd_);
        lfd_ = -1; return e;
    }
    socklen_t al = sizeof a;
    getsockname(lfd_, reinterpret_cast<sockaddr *>(&a), &al);
    port_ = ntohs(a.sin_port);
    if (pipe2(wake_, O_NONBLOCK | O_CLOEXEC) < 0) wake_[0] = wake_[1] = -1;
    quit_ = false;
    th_ = std::thread(&NetFeed::loop, this);
    return "";
}
void NetFeed::stop()
{
    if (th_.joinable()) {
        {
            std::lock_guard<std::mutex> lk(m_); quit_ = true;
        }
        if (wake_[1] >= 0) { (void)!write(wake_[1], "q", 1); } th_.join();
    }
    for (Client &c : cl_)
        if (c.fd >= 0) close(c.fd);
    cl_.clear();
    clients = 0;
    if (lfd_ >= 0) close(lfd_);
    lfd_ = -1;
    for (int &w : wake_) {
        if (w >= 0) { close(w); } w = -1;
    }
}
void NetFeed::push(char type, const std::string &name, const char *data, size_t n)
{
    if (!th_.joinable()) return;
    std::shared_ptr<const std::string> f;
    {
        std::lock_guard<std::mutex> lk(m_); if (cl_.empty()) return;   // nobody listening: nothing to keep
        f = frame(type, name, data, n); for (Client &c : cl_) {
            if (c.q.size() >= max_queue_) {
                dropped++; continue;
            }
            c.q.push_back(f);
        }
    }
    if (wake_[1] >= 0) (void)!write(wake_[1], "w", 1);
}
void NetFeed::record(const std::string &line)
{
    records++;
    push('R', "", line.data(), line.size());
}
void NetFeed::image(const std::string &name, const std::vector<uint8_t> &jpeg)
{
    images++;
    push('I', name, reinterpret_cast<const char *>(jpeg.data()), jpeg.size());
}
void NetFeed::loop()
{
    double next_status = mono_s() + 2, flush_until = 0;
    for (;;) {
        std::vector<pollfd> pf; bool quitting;
        {
            std::lock_guard<std::mutex> lk(m_); quitting = quit_;
            if (quitting && flush_until == 0) { flush_until = mono_s() + 0.5; } bool pending = false;
            pf.push_back({lfd_, short(quitting ? 0 : POLLIN), 0}); pf.push_back({wake_[0], POLLIN, 0});
            for (Client &c : cl_) {
                pf.push_back({c.fd, short(c.q.empty() ? POLLIN : (POLLIN | POLLOUT)), 0}); pending |= !c.q.empty();
            }
            if (quitting && (!pending || mono_s() > flush_until)) return;
        }
        poll(pf.data(), pf.size(), 200); if (pf[1].revents & POLLIN) {
            char b[64]; while (read(wake_[0], b, sizeof b) > 0) {}
        }
        std::lock_guard<std::mutex> lk(m_);
        // new clients: hello first
        if (pf[0].revents & POLLIN) {
            for (;;) {
                int fd = accept4(lfd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC); if (fd < 0) break;
                int one = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
                Client c; c.fd = fd;
                c.q.push_back(frame('H', session_, header_.data(), header_.size())); cl_.push_back(std::move(c));
            }
        }
        // send / detect closed clients (the client never sends: readable = closed or garbage)
        for (size_t i = 0; i < cl_.size(); i++) {
            Client &c = cl_[i]; const short rev = i + 2 < pf.size() && pf[i + 2].fd == c.fd ? pf[i + 2].revents : 0;
            bool dead = rev & (POLLERR | POLLHUP | POLLNVAL); if (!dead && (rev & POLLIN)) {
                char b[256]; ssize_t r = recv(c.fd, b, sizeof b, 0);
                dead = r == 0 || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK);  }
            while (!dead && !c.q.empty()) {
                const std::string &f = *c.q.front(); ssize_t w = send(c.fd, f.data() + c.off, f.size() - c.off, MSG_NOSIGNAL);
                if (w < 0) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) { dead = true; } break;
                }
                c.off += size_t(w); if (c.off < f.size()) break;
                c.q.erase(c.q.begin()); c.off = 0;
            }
            if (dead) {
                close(c.fd); cl_.erase(cl_.begin() + long(i));
                i--;  }
        }
        if (mono_s() >= next_status) {
            next_status = mono_s() + 2; const std::string s = "records=" + std::to_string(records.load()) + " images=" + std::to_string(images.load()) +
                                  " dropped=" + std::to_string(dropped.load()); auto f = frame('S', "", s.data(), s.size());
            for (Client &c : cl_) {
                if (c.q.size() < max_queue_) c.q.push_back(f);
            }
            clients = int(cl_.size());
        }
    }
}
// ------------------------------------------------------------------ PartLog
PartLog::PartLog() = default;
PartLog::~PartLog() { close(); }
double PartLog::now_s() const { return mono_s() - t0_; }
void PartLog::set_images(const std::string &mode)
{
    std::lock_guard<std::mutex> lk(m_);
    cfg_.images = mode;
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
    if (!make_dirs(day_dir_ + "/img")) return "cannot create " + day_dir_ + ": " + strerror(errno);
    csv_path = day_dir_ + "/parts_" + session + ".csv";
    csv_ = fopen(csv_path.c_str(), "w");
    if (!csv_) return "cannot open " + csv_path + ": " + strerror(errno);
    fprintf(csv_, "%s\n", csv_header());
    fflush(csv_);
    if (cfg.feed_port > 0) {   // live upload to a PC client; a busy port only costs the feed
        auto feed = std::make_unique<NetFeed>(); const std::string e = feed->start(cfg.feed_port, session, csv_header());
        if (!e.empty()) fprintf(stderr, "net feed: %s (records still go to the SD card)\n", e.c_str());
        else feed_ = std::move(feed);
    }
    t0_ = mono_s();
    q_.set_cap(cfg.max_queue > 0 ? size_t(cfg.max_queue) : 0);   // <= 0: add() drops everything (old behaviour)
    q_.reopen();
    th_ = std::thread(&PartLog::loop, this);
    return "";
}
void PartLog::add(PartRecord r, const cv::Mat &crop)
{
    if (!th_.joinable()) return;
    bool want;
    {
        std::lock_guard<std::mutex> lk(m_);
        want = cfg_.images == "all" || (cfg_.images == "ng" && r.disp != Disposition::Pick);
    }
    Item it;
    it.r = std::move(r);
    if (want) it.crop = crop;   // Mat: no copy
    if (cfg_.max_queue <= 0 || !q_.push(std::move(it))) dropped++;
}
void PartLog::close()
{
    q_.close();   // writer drains what is queued
    if (th_.joinable()) th_.join();
    q_.close_and_clear();
    if (feed_) {
        feed_->stop(); feed_.reset();
    }
    if (csv_) {
        fclose(csv_); csv_ = nullptr;
    }
    if (beam_) {
        fclose(beam_); beam_ = nullptr;
    }
}
void PartLog::loop()
{
    Item it;
    while (q_.pop(it)) it.is_beam ? write_beam(it) : write(it);
}
void PartLog::add_beam(const BeamResult &b, double t_s)
{
    if (!th_.joinable()) return;
    Item it;
    it.is_beam = true; it.b = b; it.b_t = t_s;
    if (cfg_.max_queue <= 0 || !q_.push(std::move(it))) dropped++;
}
void PartLog::write_beam(const Item &it)
{
    if (!beam_) {
        const std::string p = day_dir_ + "/beam_" + session + ".csv";
        if (!(beam_ = fopen(p.c_str(), "w"))) { errors++; return; }
        fprintf(beam_, "t_s,kind,belt_mm,enc,track,err_mm,n\n");
    }
    const BeamResult &b = it.b;
    if (fprintf(beam_, "%.3f,%s,%.2f,%d,%d,%.2f,%u\n", it.b_t, to_str(b.kind), b.belt_mm, b.enc, b.track, b.err, b.n) < 0 ||
        fflush(beam_) != 0)
        errors++;
}
void PartLog::write(const Item &it)
{
    const PartRecord &r = it.r;
    char lab[256], dst[256], why[40];
    csv_sanitize(r.label, lab, sizeof lab);
    csv_sanitize(r.dest, dst, sizeof dst);
    csv_sanitize(r.why, why, sizeof why);
    std::string img;
    std::vector<uint8_t> jpeg;
    if (!it.crop.empty()) {
        img = fmt("img/%s_%d_%s.jpg", session.c_str(), r.track_id, lab); std::vector<int> prm = {cv::IMWRITE_JPEG_QUALITY, cfg_.jpeg_quality};
        FILE *f = nullptr; if (cv::imencode(".jpg", it.crop, jpeg, prm) && (f = fopen((day_dir_ + "/" + img).c_str(), "wb")) &&
            fwrite(jpeg.data(), 1, jpeg.size(), f) == jpeg.size()) {
            images++;
        } else {
            errors++; img.clear();
            jpeg.clear();
        }
        if (f) fclose(f);
    }
    const PartMeasure &m = r.meas;
    char line[2048];
    int n = snprintf(line, sizeof line,
                    "%lld,%.3f,%d,%u,%u,%d,%s,%.3f,%s,%s,%s,"
                    "%d,%c,%.3f,%.3f,%.3f,%.4f,%d,%d,%d,%d,%d,%.2f,"
                    "%.1f,%u,%d,%.2f,%.2f,%.1f,%.1f,%.1f,%u,%.2f,%.2f,%.2f,%s,%d,%.2f,%.1f,%d,%.0f,%.0f,%d,%s,%s",
                    (long long)r.wall_ms, r.t_s, r.track_id, r.frame_first, r.frame_last, r.cls, lab, r.conf,
                    to_str(r.disp), dst, why, int(m.valid), m.kind ? m.kind : '-', m.size, m.bore, m.ecc, m.pitch,
                    m.teeth, m.short_teeth, m.missing_teeth, m.flash_gaps, int(m.teeth_judged), m.smear,
                    m.axis_dist < 1e8f ? m.axis_dist : -1.f, m.frame, m.count, r.s, r.y, r.axis * 57.29578f, r.len,
                    r.wid, r.pl_area, r.pl_d_mm, r.pl_w_mm, r.pl_h_mm, img.c_str(), r.votes, r.npu_ms, r.lat_ms,
                    r.recipe, m.hue, m.sat, m.color, beam_status(r.beam), r.beam == 2 ? fmt("%.2f", r.beam_err).c_str() : "");
    if (n < 0 || size_t(n) >= sizeof line || fprintf(csv_, "%s\n", line) < 0 || fflush(csv_) != 0) {
        errors++; return;
    }
    written++;
    if (feed_) {
        if (!jpeg.empty()) feed_->image(img, jpeg);   // image first: the record names it
        feed_->record(line);
    }
}
}  // namespace des
