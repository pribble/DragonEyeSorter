// board_link.cpp — see board_link.hpp
#include "board_link.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "json.hpp"

namespace des {
namespace {

double now_s()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
std::vector<std::string> split(const std::string &s, char sep)
{
    std::vector<std::string> out;
    size_t a = 0;
    for (;;) {
        const size_t b = s.find(sep, a);
        out.push_back(s.substr(a, b == std::string::npos ? b : b - a));
        if (b == std::string::npos) return out;
        a = b + 1;
    }
}

}  // namespace

// header: 'D' 'F' type 0 name_len(2, big endian) payload_len(4, big endian), then the name, then the payload
bool FeedParser::feed(const char *data, size_t n, std::vector<Frame> &out)
{
    size_t off = 0;
    for (;;) {
        if (skip_) {                                    // an image: count it down without keeping it
            if (off >= n) break;
            const size_t k = n - off < skip_ ? n - off : skip_;
            off += k; skip_ -= k;
            if (!skip_) { out.push_back(cur_); stage_ = 0; }
            continue;
        }
        const size_t want = stage_ == 0 ? 10 : stage_ == 1 ? name_len_ : pay_len_;
        if (buf_.size() < want) {                       // (a zero-length name / payload needs no more bytes)
            if (off >= n) break;
            const size_t k = n - off < want - buf_.size() ? n - off : want - buf_.size();
            buf_.append(data + off, k);
            off += k;
            if (buf_.size() < want) break;
        }
        if (stage_ == 0) {
            const auto *h = reinterpret_cast<const unsigned char *>(buf_.data());
            if (h[0] != 'D' || h[1] != 'F') return false;
            cur_ = Frame();
            cur_.type = char(h[2]);
            name_len_ = size_t(h[4]) << 8 | h[5];
            pay_len_ = size_t(h[6]) << 24 | size_t(h[7]) << 16 | size_t(h[8]) << 8 | h[9];
            stage_ = 1;
        } else if (stage_ == 1) {
            cur_.name = buf_;
            stage_ = 2;
            if (cur_.type == 'I' || pay_len_ > kMaxPayload) { skip_ = pay_len_; if (!skip_) { out.push_back(cur_); stage_ = 0; } }
        } else {
            cur_.payload = buf_;
            out.push_back(cur_);
            stage_ = 0;
        }
        buf_.clear();
    }
    return true;
}

void BoardLink::start(const std::string &host, int port)
{
    stop();
    quit_ = false;
    th_ = std::thread(&BoardLink::loop, this, host, port);
}
void BoardLink::stop()
{
    quit_ = true;
    if (th_.joinable()) th_.join();
}

BoardSnapshot BoardLink::snapshot()
{
    std::lock_guard<std::mutex> lk(m_);
    s_.quiet_s = now_s() - last_rx_;
    return s_;
}
bool BoardLink::next_order(std::string &json)
{
    std::lock_guard<std::mutex> lk(m_);
    if (changes_.empty()) return false;
    json = changes_.front();
    changes_.pop_front();
    return true;
}

void BoardLink::on_connect(bool up)
{
    std::lock_guard<std::mutex> lk(m_);
    if (s_.connected != up) { s_.connected = up; s_.seq++; }
    if (up) last_rx_ = now_s();
}

void BoardLink::recount()
{
    s_.wo_active = s_.wo_unacked = s_.wo_level = s_.wo_top_id = 0;
    s_.wo_top_title.clear();
    for (const auto &kv : orders_) {
        const Order &o = kv.second;
        const bool active = o.status == "open" || o.status == "acked";
        if (o.status == "open" || o.status == "recovered") s_.wo_unacked++;
        if (!active) continue;
        s_.wo_active++;
        if (o.level >= s_.wo_level) { s_.wo_level = o.level; s_.wo_top_id = kv.first; s_.wo_top_title = o.title; }
    }
}

void BoardLink::apply_order(const std::string &json, bool queue)
{
    const nlohmann::json j = nlohmann::json::parse(json, nullptr, false);
    if (!j.is_object() || !j.contains("id")) return;
    Order o;
    o.level = j.value("level", 0);
    o.status = j.value("status", std::string());
    o.title = j.value("title", std::string());
    orders_[j.value("id", 0)] = o;
    if (queue) {
        if (changes_.size() >= 32) changes_.pop_front();   // 4G is slow: keep the newest
        changes_.push_back(json);
    }
}

void BoardLink::on_frame(const FeedParser::Frame &f)
{
    std::lock_guard<std::mutex> lk(m_);
    last_rx_ = now_s();
    switch (f.type) {
    case 'H':   // a new des_app session: its CSV header; counts start over
        if (s_.session != f.name) { s_.total = s_.ok = s_.ng = s_.recheck = 0; orders_.clear(); recount(); }
        s_.session = f.name;
        cols_ = split(f.payload, ',');
        s_.seq++;
        break;
    case 'w': {   // the order list as of now
        const nlohmann::json a = nlohmann::json::parse(f.payload, nullptr, false);
        orders_.clear();
        if (a.is_array())
            for (const auto &o : a) apply_order(o.dump(), false);
        recount();
        s_.seq++;
        break;
    }
    case 'W':
        apply_order(f.payload, true);
        recount();
        s_.seq++;
        break;
    case 'R': {
        const std::vector<std::string> v = split(f.payload, ',');
        auto col = [&](const char *name) -> std::string {
            for (size_t i = 0; i < cols_.size() && i < v.size(); i++)
                if (cols_[i] == name) return v[i];
            return "";
        };
        s_.last_label = col("label"); s_.last_disp = col("disp"); s_.last_dest = col("dest");
        s_.last_conf = atof(col("conf").c_str()); s_.last_size_mm = atof(col("size_mm").c_str());
        s_.total++;
        if (s_.last_disp == "pick") s_.ok++;
        else if (s_.last_disp == "recheck") s_.recheck++;
        else s_.ng++;
        s_.seq++;
        break;
    }
    default: break;   // 'S' statistics, 'I' image: only the time stamp above
    }
}

void BoardLink::loop(std::string host, int port)
{
    FeedParser parser;
    while (!quit_) {
        const int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET; a.sin_port = htons(uint16_t(port));
        inet_pton(AF_INET, host.c_str(), &a.sin_addr);
        if (fd < 0 || connect(fd, reinterpret_cast<sockaddr *>(&a), sizeof a) < 0) {
            if (fd >= 0) close(fd);
            for (int i = 0; i < 20 && !quit_; i++) usleep(100000);   // des_app not up yet: try again in 2 s
            continue;
        }
        parser.reset();
        on_connect(true);
        std::vector<FeedParser::Frame> frames;
        char buf[8192];
        while (!quit_) {
            pollfd p{fd, POLLIN, 0};
            const int r = poll(&p, 1, 200);
            if (r < 0 && errno != EINTR) break;
            if (r <= 0) continue;
            const ssize_t n = recv(fd, buf, sizeof buf, 0);
            if (n <= 0) break;
            frames.clear();
            if (!parser.feed(buf, size_t(n), frames)) break;
            for (const auto &f : frames) on_frame(f);
        }
        close(fd);
        on_connect(false);
    }
}

}  // namespace des
