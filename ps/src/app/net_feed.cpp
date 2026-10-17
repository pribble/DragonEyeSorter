// net_feed.cpp — see net_feed.hpp
#include "net_feed.hpp"

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

namespace des {

namespace {
double mono_s()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
}  // namespace

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
        std::string e = std::string("port ") + std::to_string(port) + ": " + strerror(errno);
        close(lfd_);
        lfd_ = -1;
        return e;
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
            std::lock_guard<std::mutex> lk(m_);
            quit_ = true;
        }
        if (wake_[1] >= 0) (void)!write(wake_[1], "q", 1);
        th_.join();
    }
    for (Client &c : cl_)
        if (c.fd >= 0) close(c.fd);
    cl_.clear();
    clients = 0;
    if (lfd_ >= 0) close(lfd_);
    lfd_ = -1;
    for (int &w : wake_) {
        if (w >= 0) close(w);
        w = -1;
    }
}

void NetFeed::push(char type, const std::string &name, const char *data, size_t n)
{
    if (!th_.joinable()) return;
    std::shared_ptr<const std::string> f;
    {
        std::lock_guard<std::mutex> lk(m_);
        if (cl_.empty()) return;   // nobody listening: nothing to keep
        f = frame(type, name, data, n);
        for (Client &c : cl_) {
            if (c.q.size() >= max_queue_) { dropped++; continue; }
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
        std::vector<pollfd> pf;
        bool quitting;
        {
            std::lock_guard<std::mutex> lk(m_);
            quitting = quit_;
            if (quitting && flush_until == 0) flush_until = mono_s() + 0.5;
            bool pending = false;
            pf.push_back({lfd_, short(quitting ? 0 : POLLIN), 0});
            pf.push_back({wake_[0], POLLIN, 0});
            for (Client &c : cl_) {
                pf.push_back({c.fd, short(c.q.empty() ? POLLIN : (POLLIN | POLLOUT)), 0});
                pending |= !c.q.empty();
            }
            if (quitting && (!pending || mono_s() > flush_until)) return;
        }
        poll(pf.data(), pf.size(), 200);
        if (pf[1].revents & POLLIN) {
            char b[64];
            while (read(wake_[0], b, sizeof b) > 0) {}
        }
        std::lock_guard<std::mutex> lk(m_);
        // new clients: hello first
        if (pf[0].revents & POLLIN) {
            for (;;) {
                int fd = accept4(lfd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
                if (fd < 0) break;
                int one = 1;
                setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
                Client c;
                c.fd = fd;
                c.q.push_back(frame('H', session_, header_.data(), header_.size()));
                cl_.push_back(std::move(c));
            }
        }
        // send / detect closed clients (the client never sends: readable = closed or garbage)
        for (size_t i = 0; i < cl_.size(); i++) {
            Client &c = cl_[i];
            const short rev = i + 2 < pf.size() && pf[i + 2].fd == c.fd ? pf[i + 2].revents : 0;
            bool dead = rev & (POLLERR | POLLHUP | POLLNVAL);
            if (!dead && (rev & POLLIN)) {
                char b[256];
                ssize_t r = recv(c.fd, b, sizeof b, 0);
                dead = r == 0 || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK);
            }
            while (!dead && !c.q.empty()) {
                const std::string &f = *c.q.front();
                ssize_t w = send(c.fd, f.data() + c.off, f.size() - c.off, MSG_NOSIGNAL);
                if (w < 0) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) dead = true;
                    break;
                }
                c.off += size_t(w);
                if (c.off < f.size()) break;
                c.q.erase(c.q.begin());
                c.off = 0;
            }
            if (dead) {
                close(c.fd);
                cl_.erase(cl_.begin() + long(i));
                i--;
            }
        }
        clients = cl_.size();
        if (mono_s() >= next_status) {
            next_status = mono_s() + 2;
            const std::string s = "records=" + std::to_string(records.load()) + " images=" + std::to_string(images.load()) +
                                  " dropped=" + std::to_string(dropped.load());
            auto f = frame('S', "", s.data(), s.size());
            for (Client &c : cl_)
                if (c.q.size() < max_queue_) c.q.push_back(f);
        }
    }
}

}  // namespace des
