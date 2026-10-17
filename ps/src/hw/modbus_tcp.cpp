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
    quit_ = false;
    th_ = std::thread(&ModbusTcp::loop, this);
    return "";
}

void ModbusTcp::stop()
{
    if (th_.joinable()) {
        {
            std::lock_guard<std::mutex> lk(m_);
            quit_ = true;
        }
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

namespace {
uint16_t be16(const char *p) { return uint16_t((uint8_t(p[0]) << 8) | uint8_t(p[1])); }
void put16(std::string &s, uint16_t v)
{
    s.push_back(char(v >> 8));
    s.push_back(char(v & 0xFF));
}
bool send_all(int fd, const std::string &s)
{
    size_t off = 0;
    for (int tries = 0; off < s.size() && tries < 50; tries++) {
        ssize_t w = send(fd, s.data() + off, s.size() - off, MSG_NOSIGNAL);
        if (w > 0) off += size_t(w);
        else if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return false;
        else {
            pollfd p{fd, POLLOUT, 0};
            poll(&p, 1, 20);
        }
    }
    return off == s.size();
}
}  // namespace

// in: bytes received so far on this connection; consumes complete frames, replies to each
bool ModbusTcp::handle(int fd, std::string &in)
{
    while (in.size() >= 7) {
        const uint16_t tid = be16(&in[0]), proto = be16(&in[2]), len = be16(&in[4]);
        if (proto != 0 || len < 2 || len > 254) return false;   // not Modbus: drop the connection
        if (in.size() < size_t(6 + len)) return true;            // wait for the rest
        const uint8_t unit = uint8_t(in[6]), fc = uint8_t(in[7]);
        const char *pdu = in.data() + 8;
        const size_t pn = len - 2;                              // PDU bytes after the function code
        std::string pdu_out;
        uint8_t exc = 0;
        {
            std::lock_guard<std::mutex> lk(m_);
            requests_++;
            if (fc == 0x03 || fc == 0x04) {
                const std::vector<uint16_t> &t = fc == 0x03 ? hr_ : ir_;
                if (pn != 4) exc = 3;
                else {
                    const uint16_t a = be16(pdu), q = be16(pdu + 2);
                    if (q < 1 || q > 125) exc = 3;
                    else if (size_t(a) + q > t.size()) exc = 2;
                    else {
                        pdu_out.push_back(char(fc));
                        pdu_out.push_back(char(q * 2));
                        for (uint16_t i = 0; i < q; i++) put16(pdu_out, t[a + i]);
                    }
                }
            } else if (fc == 0x06) {
                if (pn != 4) exc = 3;
                else {
                    const uint16_t a = be16(pdu), v = be16(pdu + 2);
                    if (a >= hr_.size()) exc = 2;
                    else {
                        writes_.push_back({a, v});
                        pdu_out.assign(in.data() + 7, 5);   // echo function code, address, value
                    }
                }
            } else if (fc == 0x10) {
                if (pn < 5) exc = 3;
                else {
                    const uint16_t a = be16(pdu), q = be16(pdu + 2);
                    const uint8_t nb = uint8_t(pdu[4]);
                    if (q < 1 || q > 123 || nb != q * 2 || pn != size_t(5 + nb)) exc = 3;
                    else if (size_t(a) + q > hr_.size()) exc = 2;
                    else {
                        for (uint16_t i = 0; i < q; i++) writes_.push_back({uint16_t(a + i), be16(pdu + 5 + 2 * i)});
                        pdu_out.push_back(char(fc));
                        put16(pdu_out, a);
                        put16(pdu_out, q);
                    }
                }
            } else {
                exc = 1;
            }
        }
        if (exc) {
            pdu_out.clear();
            pdu_out.push_back(char(fc | 0x80));
            pdu_out.push_back(char(exc));
        }
        std::string out;
        put16(out, tid);
        put16(out, 0);
        put16(out, uint16_t(pdu_out.size() + 1));
        out.push_back(char(unit));
        out += pdu_out;
        in.erase(0, 6 + len);
        if (!send_all(fd, out)) return false;
    }
    return true;
}

void ModbusTcp::loop()
{
    struct Conn {
        int fd;
        std::string in;
    };
    std::vector<Conn> conns;
    for (;;) {
        {
            std::lock_guard<std::mutex> lk(m_);
            if (quit_) break;
        }
        std::vector<pollfd> pf;
        pf.push_back({lfd_, POLLIN, 0});
        pf.push_back({wake_[0], POLLIN, 0});
        for (const Conn &c : conns) pf.push_back({c.fd, POLLIN, 0});
        poll(pf.data(), pf.size(), 500);
        if (pf[1].revents & POLLIN) {
            char b[16];
            while (read(wake_[0], b, sizeof b) > 0) {}
        }
        for (size_t i = 0; i < conns.size(); i++) {
            const short rev = pf[i + 2].revents;
            bool dead = rev & (POLLERR | POLLHUP | POLLNVAL);
            if (!dead && (rev & POLLIN)) {
                char b[512];
                ssize_t r = recv(conns[i].fd, b, sizeof b, 0);
                if (r > 0) {
                    conns[i].in.append(b, size_t(r));
                    dead = !handle(conns[i].fd, conns[i].in) || conns[i].in.size() > 4096;
                } else {
                    dead = r == 0 || (errno != EAGAIN && errno != EWOULDBLOCK);
                }
            }
            if (dead) {
                close(conns[i].fd);
                conns.erase(conns.begin() + long(i));
                pf.erase(pf.begin() + long(i) + 2);
                i--;
            }
        }
        if (pf[0].revents & POLLIN)
            for (;;) {
                int fd = accept4(lfd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
                if (fd < 0) break;
                if (conns.size() >= 4) { close(fd); continue; }
                int one = 1;
                setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
                conns.push_back({fd, std::string()});
            }
        std::lock_guard<std::mutex> lk(m_);
        clients_ = int(conns.size());
    }
    for (Conn &c : conns) close(c.fd);
    std::lock_guard<std::mutex> lk(m_);
    clients_ = 0;
}

}  // namespace des
