// mb_client.cpp — see mb_client.hpp
#include "mb_client.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace des {

void MbClient::close()
{
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
}

bool MbClient::ensure()
{
    if (fd_ >= 0) return true;
    const int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET; a.sin_port = htons(uint16_t(port_));
    inet_pton(AF_INET, host_.c_str(), &a.sin_addr);
    if (fd < 0 || connect(fd, reinterpret_cast<sockaddr *>(&a), sizeof a) < 0) {
        err_ = std::string("connect: ") + strerror(errno);
        if (fd >= 0) ::close(fd);
        return false;
    }
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    fd_ = fd;
    return true;
}

// one request / reply; reply = the PDU (function code first)
bool MbClient::xfer(const uint8_t *pdu, size_t n, uint8_t *reply, size_t cap, size_t &got)
{
    if (!ensure()) return false;
    uint8_t out[16];
    const uint16_t tid = ++tid_;
    out[0] = uint8_t(tid >> 8); out[1] = uint8_t(tid); out[2] = out[3] = 0;
    out[4] = uint8_t((n + 1) >> 8); out[5] = uint8_t(n + 1); out[6] = unit_;
    memcpy(out + 7, pdu, n);
    if (send(fd_, out, 7 + n, MSG_NOSIGNAL) != ssize_t(7 + n)) { err_ = "send failed"; close(); return false; }
    uint8_t in[300];
    size_t have = 0, need = 7;
    while (have < need) {
        pollfd p{fd_, POLLIN, 0};
        if (poll(&p, 1, timeout_ms_) <= 0) { err_ = "no reply"; close(); return false; }
        const ssize_t r = recv(fd_, in + have, sizeof in - have, 0);
        if (r <= 0) { err_ = "connection closed"; close(); return false; }
        have += size_t(r);
        if (have >= 7) {
            const size_t len = size_t(in[4]) << 8 | in[5];
            if (len < 2 || len > 254) { err_ = "bad frame"; close(); return false; }
            need = 6 + len;
        }
    }
    if ((uint16_t(in[0]) << 8 | in[1]) != tid) { err_ = "reply out of step"; close(); return false; }
    got = need - 7;
    if (got > cap) { err_ = "reply too long"; close(); return false; }
    memcpy(reply, in + 7, got);
    if (reply[0] & 0x80) { err_ = "exception " + std::to_string(got > 1 ? reply[1] : 0); return false; }
    return true;
}

bool MbClient::read(uint8_t fc, uint16_t addr, uint16_t count, uint16_t *out)
{
    if (count < 1 || count > 125) { err_ = "bad count"; return false; }
    const uint8_t pdu[5] = {fc, uint8_t(addr >> 8), uint8_t(addr), uint8_t(count >> 8), uint8_t(count)};
    uint8_t r[260];
    size_t n = 0;
    if (!xfer(pdu, sizeof pdu, r, sizeof r, n)) return false;
    if (n != size_t(2 + 2 * count) || r[0] != fc || r[1] != 2 * count) { err_ = "bad reply"; close(); return false; }
    for (uint16_t i = 0; i < count; i++) out[i] = uint16_t(r[2 + 2 * i] << 8 | r[3 + 2 * i]);
    return true;
}

bool MbClient::write(uint16_t addr, uint16_t value)
{
    const uint8_t pdu[5] = {0x06, uint8_t(addr >> 8), uint8_t(addr), uint8_t(value >> 8), uint8_t(value)};
    uint8_t r[16];
    size_t n = 0;
    if (!xfer(pdu, sizeof pdu, r, sizeof r, n)) return false;
    if (n != 5 || memcmp(r, pdu, 5) != 0) { err_ = "bad echo"; close(); return false; }
    return true;
}

}  // namespace des
