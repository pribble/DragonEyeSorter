// modbus_tcp.cpp — see modbus_tcp.hpp
#include "modbus_tcp.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

namespace des {

std::string ModbusTcp::start(int port)
{
    stop();
    lfd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (lfd_ < 0) return std::string("socket: ") + strerror(errno);
    int one = 1;
    setsockopt(lfd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(uint16_t(port));
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(lfd_, reinterpret_cast<sockaddr *>(&a), sizeof a) < 0 || listen(lfd_, 4) < 0) {
        std::string e = "port " + std::to_string(port) + ": " + strerror(errno);
        close(lfd_);
        lfd_ = -1;
        return e;
    }
    socklen_t al = sizeof a;
    getsockname(lfd_, reinterpret_cast<sockaddr *>(&a), &al);
    port_ = ntohs(a.sin_port);
    if (pipe2(wake_, O_NONBLOCK | O_CLOEXEC) < 0) wake_[0] = wake_[1] = -1;
    quit_.store(false);
    th_ = std::thread(&ModbusTcp::loop, this);
    return "";
}

void ModbusTcp::stop()
{
    if (th_.joinable()) {
        quit_.store(true);
        if (wake_[1] >= 0) (void)!write(wake_[1], "q", 1);
        th_.join();
    }
    if (lfd_ >= 0) close(lfd_);
    lfd_ = -1;
    for (int &w : wake_) {
        if (w >= 0) close(w);
        w = -1;
    }
}

void ModbusTcp::set_input(size_t addr, uint16_t v)
{
    std::lock_guard<std::mutex> lk(m_);
    if (addr < ir_.size()) ir_[addr] = v;
}

void ModbusTcp::set_inputs(size_t addr, const std::vector<uint16_t> &v)
{
    std::lock_guard<std::mutex> lk(m_);
    for (size_t i = 0; i < v.size() && addr + i < ir_.size(); i++) ir_[addr + i] = v[i];
}

void ModbusTcp::set_holding(size_t addr, uint16_t v)
{
    std::lock_guard<std::mutex> lk(m_);
    if (addr < hr_.size()) hr_[addr] = v;
}

std::vector<ModbusTcp::Write> ModbusTcp::take_writes()
{
    std::lock_guard<std::mutex> lk(m_);
    std::vector<Write> w(writes_.begin(), writes_.end());
    writes_.clear();
    return w;
}

size_t ModbusTcp::take_writes(Write *out, size_t cap)
{
    std::lock_guard<std::mutex> lk(m_);
    const size_t n = writes_.size() < cap ? writes_.size() : cap;
    for (size_t i = 0; i < n; i++) { out[i] = writes_.front(); writes_.pop_front(); }
    return n;
}

namespace {
uint16_t be16(const char *p) { return uint16_t((uint8_t(p[0]) << 8) | uint8_t(p[1])); }
void put16(char *p, uint16_t v) { p[0] = char(v >> 8); p[1] = char(v & 0xFF); }
bool send_all(int fd, const char *s, size_t n)
{
    size_t off = 0;
    for (int tries = 0; off < n && tries < 50; tries++) {
        ssize_t w = send(fd, s + off, n - off, MSG_NOSIGNAL);
        if (w > 0) off += size_t(w);
        else if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return false;
        else {
            pollfd p{fd, POLLOUT, 0};
            poll(&p, 1, 20);
        }
    }
    return off == n;
}
}  // namespace

// MBAP framing around pdu_[0..n); exc != 0 turns the PDU into the exception reply
bool ModbusTcp::reply(Conn &c, uint16_t tid, uint8_t unit, uint8_t fc, size_t n, uint8_t exc)
{
    if (exc) { pdu_[0] = char(fc | 0x80); pdu_[1] = char(exc); n = 2; }
    put16(out_, tid); put16(out_ + 2, 0); put16(out_ + 4, uint16_t(n + 1));
    out_[6] = char(unit);
    memcpy(out_ + 7, pdu_, n);
    return send_all(c.fd, out_, 7 + n);
}

// c.in holds the bytes received so far on this connection; consume complete frames, reply to each
bool ModbusTcp::handle(Conn &c)
{
    size_t off = 0;
    while (c.len - off >= 7) {
        const char *in = c.in + off;
        const uint16_t tid = be16(in), proto = be16(in + 2), len = be16(in + 4);
        if (proto != 0 || len < 2 || len > 254) return false;   // not Modbus: drop the connection
        if (c.len - off < size_t(6 + len)) break;               // wait for the rest
        const uint8_t unit = uint8_t(in[6]), fc = uint8_t(in[7]);
        const char *pdu = in + 8;
        const size_t pn = len - 2;                              // PDU bytes after the function code
        size_t n = 0;
        uint8_t exc = 0;
        {
            std::lock_guard<std::mutex> lk(m_);
            requests_.fetch_add(1, std::memory_order_relaxed);
            const uint16_t a = pn >= 2 ? be16(pdu) : 0, q = pn >= 4 ? be16(pdu + 2) : 0;
            if (fc == 0x03 || fc == 0x04) {
                const std::vector<uint16_t> &t = fc == 0x03 ? hr_ : ir_;
                if (pn != 4 || q < 1 || q > 125) exc = 3;
                else if (size_t(a) + q > t.size()) exc = 2;
                else {
                    pdu_[n++] = char(fc); pdu_[n++] = char(q * 2);
                    for (uint16_t i = 0; i < q; i++) { put16(pdu_ + n, t[a + i]); n += 2; }
                }
            } else if (fc == 0x06) {
                if (pn != 4) exc = 3;
                else if (a >= hr_.size()) exc = 2;
                else { writes_.push_back({a, q}); memcpy(pdu_, in + 7, 5); n = 5; }   // echo fc / address / value
            } else if (fc == 0x10) {
                const uint8_t nb = pn >= 5 ? uint8_t(pdu[4]) : 0;
                if (pn < 5 || q < 1 || q > 123 || nb != q * 2 || pn != size_t(5 + nb)) exc = 3;
                else if (size_t(a) + q > hr_.size()) exc = 2;
                else {
                    for (uint16_t i = 0; i < q; i++) writes_.push_back({uint16_t(a + i), be16(pdu + 5 + 2 * i)});
                    pdu_[n++] = char(fc); put16(pdu_ + n, a); n += 2; put16(pdu_ + n, q); n += 2;
                }
            } else {
                exc = 1;
            }
        }
        if (!reply(c, tid, unit, fc, n, exc)) return false;
        off += 6 + len;
    }
    if (off) {
        if (off >= c.len) c.len = 0;
        else { memmove(c.in, c.in + off, c.len - off); c.len -= off; }
    }
    return true;
}

void ModbusTcp::loop()
{
    Conn conns[kMaxClients];
    int n = 0;
    while (!quit_.load(std::memory_order_relaxed)) {
        pollfd pf[2 + kMaxClients];
        pf[0] = {lfd_, POLLIN, 0};
        pf[1] = {wake_[0], POLLIN, 0};
        for (int i = 0; i < n; i++) pf[i + 2] = {conns[i].fd, POLLIN, 0};
        poll(pf, nfds_t(n + 2), 500);
        if (pf[1].revents & POLLIN) {
            char b[16];
            while (read(wake_[0], b, sizeof b) > 0) {}
        }
        for (int i = 0; i < n; i++) {
            const short rev = pf[i + 2].revents;
            bool dead = rev & (POLLERR | POLLHUP | POLLNVAL);
            if (!dead && (rev & POLLIN)) {
                if (conns[i].len >= kInSize) dead = true;   // a client that never completes a frame
                else {
                    const ssize_t r = recv(conns[i].fd, conns[i].in + conns[i].len, kInSize - conns[i].len, 0);
                    if (r > 0) { conns[i].len += size_t(r); dead = !handle(conns[i]); }
                    else dead = r == 0 || errno != EAGAIN;   // EWOULDBLOCK == EAGAIN on Linux
                }
            }
            if (dead) {
                close(conns[i].fd);
                for (int j = i; j + 1 < n; j++) { conns[j] = conns[j + 1]; pf[j + 2] = pf[j + 3]; }
                n--;
                i--;
            }
        }
        if (pf[0].revents & POLLIN)
            for (;;) {
                const int fd = accept4(lfd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
                if (fd < 0) break;
                if (n >= int(kMaxClients)) { close(fd); continue; }
                int one = 1;
                setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
                conns[n++] = {fd, 0, {}};
            }
        clients_.store(n, std::memory_order_relaxed);
    }
    for (int i = 0; i < n; i++) close(conns[i].fd);
    clients_.store(0, std::memory_order_relaxed);
}

}  // namespace des
