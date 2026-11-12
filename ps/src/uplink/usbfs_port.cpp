// usbfs_port.cpp — see usbfs_port.hpp
#include "usbfs_port.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <linux/usbdevice_fs.h>
#include <poll.h>
#include <sstream>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

namespace des {
namespace {

std::string slurp(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
std::string first_line(const std::string &path)
{
    std::string s = slurp(path);
    const size_t e = s.find_first_of("\r\n");
    if (e != std::string::npos) s.resize(e);
    return s;
}

}  // namespace

std::string UsbDev::node() const
{
    char b[64];
    snprintf(b, sizeof b, "/dev/bus/usb/%03d/%03d", bus, dev);
    return b;
}

// device descriptor (18 bytes) followed by the configuration descriptors, as sysfs "descriptors" and usbfs give them.
// Only the first configuration and alternate setting 0 are used.
bool usb_parse_descriptors(const uint8_t *d, size_t n, UsbDev &out)
{
    if (n < 18 || d[0] != 18 || d[1] != 1) return false;
    out.vid = uint16_t(d[8] | (d[9] << 8));
    out.pid = uint16_t(d[10] | (d[11] << 8));
    out.ifs.clear();
    size_t off = 18;
    int configs = 0;
    UsbIface *cur = nullptr;
    while (off + 2 <= n) {
        const size_t len = d[off];
        const int type = d[off + 1];
        if (len < 2 || off + len > n) break;
        if (type == 2) {                               // configuration
            if (++configs > 1) break;
            cur = nullptr;
        } else if (type == 4 && len >= 9) {            // interface
            cur = nullptr;
            if (d[off + 3] == 0) {                     // alternate setting 0
                UsbIface i;
                i.num = d[off + 2]; i.cls = d[off + 5]; i.sub = d[off + 6]; i.proto = d[off + 7];
                out.ifs.push_back(i);
                cur = &out.ifs.back();
            }
        } else if (type == 5 && len >= 7 && cur) {     // endpoint
            const int addr = d[off + 2], attr = d[off + 3] & 3, maxp = (d[off + 4] | (d[off + 5] << 8)) & 0x7FF;
            if (attr == 2) {                           // bulk
                if ((addr & 0x80) && !cur->ep_in) cur->ep_in = addr;
                if (!(addr & 0x80) && !cur->ep_out) { cur->ep_out = addr; cur->max_out = maxp; }
            } else if (attr == 3 && (addr & 0x80) && !cur->ep_int) {
                cur->ep_int = addr;
            }
        }
        off += len;
    }
    return true;
}

std::vector<UsbDev> usb_scan(uint16_t vid, const std::string &sys_root)
{
    std::vector<UsbDev> out;
    DIR *dir = opendir(sys_root.c_str());
    if (!dir) return out;
    while (dirent *e = readdir(dir)) {
        const std::string name = e->d_name;
        if (name[0] == '.' || name.find(':') != std::string::npos) continue;   // "1-1:1.0" = an interface
        const std::string base = sys_root + "/" + name;
        const std::string desc = slurp(base + "/descriptors");
        UsbDev d;
        if (!usb_parse_descriptors(reinterpret_cast<const uint8_t *>(desc.data()), desc.size(), d)) continue;
        if (vid && d.vid != vid) continue;
        d.sys = name;
        d.bus = atoi(first_line(base + "/busnum").c_str());
        d.dev = atoi(first_line(base + "/devnum").c_str());
        d.product = first_line(base + "/product");
        sscanf(first_line(base + "/dev").c_str(), "%d:%d", &d.major, &d.minor);
        out.push_back(d);
    }
    closedir(dir);
    return out;
}

std::string UsbfsPort::open(const UsbDev &d, const UsbIface &i)
{
    close();
    if (!i.ep_in || !i.ep_out) return "interface has no bulk IN / OUT pair";
    std::string node = d.node();
    fd_ = ::open(node.c_str(), O_RDWR | O_CLOEXEC);
    if (fd_ < 0 && errno == ENOENT && d.major > 0) {   // no devtmpfs node: make our own from sysfs major:minor
        node = "/tmp/des_4g_usb_" + d.sys;
        unlink(node.c_str());
        if (mknod(node.c_str(), S_IFCHR | 0600, makedev(unsigned(d.major), unsigned(d.minor))) == 0)
            fd_ = ::open(node.c_str(), O_RDWR | O_CLOEXEC);
    }
    if (fd_ < 0) return "open " + node + ": " + strerror(errno);
    int ifnum = i.num;
    if (ioctl(fd_, USBDEVFS_CLAIMINTERFACE, &ifnum) < 0) {
        const std::string e = std::string("claim interface ") + std::to_string(i.num) + ": " + strerror(errno) +
                              (errno == EBUSY ? " (a kernel driver owns it: use its tty instead)" : "");
        ::close(fd_); fd_ = -1;
        return e;
    }
    ifnum_ = i.num; ep_in_ = i.ep_in; ep_out_ = i.ep_out; max_out_ = i.max_out > 0 ? i.max_out : 64;
    // what the kernel's serial driver does when a port is opened: raise DTR + RTS (CDC SET_CONTROL_LINE_STATE);
    // some firmware stays silent on a port whose DTR is low. Not fatal when the interface ignores it.
    usbdevfs_ctrltransfer ct{};
    ct.bRequestType = 0x21; ct.bRequest = 0x22; ct.wValue = 0x0003; ct.wIndex = uint16_t(i.num);
    ct.wLength = 0; ct.timeout = 1000; ct.data = nullptr;
    ioctl(fd_, USBDEVFS_CONTROL, &ct);
    urb_.assign(sizeof(usbdevfs_urb), 0);
    in_buf_.assign(4096, 0);
    rx_.clear();
    in_pending_ = false;
    return submit_in() ? "" : std::string("submit bulk IN: ") + strerror(errno);
}

void UsbfsPort::close()
{
    if (fd_ < 0) return;
    if (in_pending_) {
        ioctl(fd_, USBDEVFS_DISCARDURB, urb_.data());
        void *done = nullptr;
        pollfd p{fd_, POLLOUT, 0};
        if (poll(&p, 1, 200) > 0) ioctl(fd_, USBDEVFS_REAPURBNDELAY, &done);
        in_pending_ = false;
    }
    int ifnum = ifnum_;
    ioctl(fd_, USBDEVFS_RELEASEINTERFACE, &ifnum);
    ::close(fd_);
    fd_ = -1;
}

bool UsbfsPort::submit_in()
{
    auto *u = reinterpret_cast<usbdevfs_urb *>(urb_.data());
    memset(u, 0, sizeof *u);
    u->type = USBDEVFS_URB_TYPE_BULK;
    u->endpoint = static_cast<unsigned char>(ep_in_);
    u->buffer = in_buf_.data();
    u->buffer_length = int(in_buf_.size());
    if (ioctl(fd_, USBDEVFS_SUBMITURB, u) < 0) return false;
    in_pending_ = true;
    return true;
}

bool UsbfsPort::write_all(const char *data, size_t n)
{
    if (fd_ < 0) return false;
    size_t off = 0;
    while (off < n) {
        const size_t chunk = n - off > 2048 ? 2048 : n - off;
        usbdevfs_bulktransfer bt{};
        bt.ep = unsigned(ep_out_); bt.len = unsigned(chunk); bt.timeout = 2000;
        bt.data = const_cast<char *>(data + off);
        const int r = ioctl(fd_, USBDEVFS_BULK, &bt);
        if (r < 0) return false;
        off += size_t(r);
    }
    if (n && n % size_t(max_out_) == 0) {   // a transfer that ends on a full packet needs a zero-length one after it
        usbdevfs_bulktransfer bt{};
        bt.ep = unsigned(ep_out_); bt.len = 0; bt.timeout = 1000; bt.data = nullptr;
        ioctl(fd_, USBDEVFS_BULK, &bt);
    }
    return true;
}

int UsbfsPort::read_some(char *buf, size_t cap, int timeout_ms)
{
    if (fd_ < 0) return -1;
    if (rx_.empty()) {
        if (!in_pending_ && !submit_in()) return -1;
        pollfd p{fd_, POLLOUT, 0};                       // usbfs: POLLOUT = a finished request can be reaped
        const int r = poll(&p, 1, timeout_ms);
        if (r < 0) return errno == EINTR ? 0 : -1;
        if (r == 0) return 0;
        if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) return -1;   // unplugged / module reset
        void *done = nullptr;
        if (ioctl(fd_, USBDEVFS_REAPURBNDELAY, &done) < 0) return errno == EAGAIN ? 0 : -1;
        in_pending_ = false;
        const auto *u = reinterpret_cast<const usbdevfs_urb *>(urb_.data());
        if (u->actual_length > 0) rx_.assign(in_buf_.begin(), in_buf_.begin() + u->actual_length);
        const int st = u->status;
        if (st == -ENODEV || st == -ESHUTDOWN || st == -EPROTO) return rx_.empty() ? -1 : 0;
        if (!submit_in() && rx_.empty()) return -1;
        if (rx_.empty()) return 0;
    }
    const size_t n = rx_.size() < cap ? rx_.size() : cap;
    memcpy(buf, rx_.data(), n);
    rx_.erase(rx_.begin(), rx_.begin() + long(n));
    return int(n);
}

}  // namespace des
