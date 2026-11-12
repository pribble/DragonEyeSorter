// at_port.hpp — the byte pipe under the AT command channel (uplink/at_chan) of the 4G uplink (tools/des_4g).
//
// Two real pipes and one for tests:
//   UsbfsPort (usbfs_port.hpp)  the modem's AT interface straight from user space through /dev/bus/usb — the board's
//                               kernel has no USB serial driver, so there is no /dev/ttyUSB* to open
//   FdPort (here)               any byte-stream file descriptor: a tty when a kernel serial driver exists, or one end
//                               of a socketpair with a scripted modem on the other end (tests/test_uplink)
#pragma once

#include <cerrno>
#include <cstddef>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace des {

struct AtPort {
    virtual ~AtPort() = default;
    virtual bool write_all(const char *data, size_t n) = 0;             // false: the port is gone
    virtual int read_some(char *buf, size_t cap, int timeout_ms) = 0;   // > 0 bytes, 0 nothing yet, < 0 port gone
};

class FdPort : public AtPort {
public:
    explicit FdPort(int fd = -1, bool own = true) : fd_(fd), own_(own) {}
    ~FdPort() override { if (own_ && fd_ >= 0) ::close(fd_); }
    int fd() const { return fd_; }
    bool write_all(const char *data, size_t n) override
    {
        size_t off = 0;
        while (off < n) {
            const ssize_t w = ::send(fd_, data + off, n - off, MSG_NOSIGNAL);
            const ssize_t w2 = w < 0 && errno == ENOTSOCK ? ::write(fd_, data + off, n - off) : w;   // a tty
            if (w2 < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    pollfd p{fd_, POLLOUT, 0};
                    if (poll(&p, 1, 2000) <= 0) return false;
                    continue;
                }
                return false;
            }
            off += size_t(w2);
        }
        return true;
    }
    int read_some(char *buf, size_t cap, int timeout_ms) override
    {
        pollfd p{fd_, POLLIN, 0};
        const int r = poll(&p, 1, timeout_ms);
        if (r < 0) return errno == EINTR ? 0 : -1;
        if (r == 0) return 0;
        if (p.revents & POLLIN) {
            const ssize_t n = ::read(fd_, buf, cap);
            if (n > 0) return int(n);
            return n == 0 ? -1 : (errno == EINTR || errno == EAGAIN ? 0 : -1);   // 0 = the other end closed
        }
        return -1;   // POLLERR / POLLHUP / POLLNVAL
    }

private:
    int fd_;
    bool own_;
};

}  // namespace des
