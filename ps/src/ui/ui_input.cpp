// ui_input.cpp — see ui_input.hpp. The device set (and its pollfd/name arrays) is rebuilt only on the 2 s hot-plug
// scan or when a device disappears, never per wake-up; FIFO protocol, event content and 200 ms poll timeout kept.
#include "ui_input.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>

#include "util.hpp"

namespace des {
namespace {
struct Dev {
    int fd = -1;
    bool abs = false;
    input_absinfo ax{}, ay{};
};
}  // namespace
void UiInput::start()
{
    if (th_.joinable()) return;
    quit_ = false;
    th_ = std::thread(&UiInput::loop, this);
}
void UiInput::stop()
{
    quit_ = true;
    if (th_.joinable()) th_.join();
}
std::vector<UiEvent> UiInput::take()
{
    std::lock_guard<std::mutex> lk(m_);
    std::vector<UiEvent> r;
    r.swap(ev_);
    return r;
}
void UiInput::cursor(int &x, int &y, bool &visible) const
{
    x = x_;
    y = y_;
    visible = mono_s() - last_move_ < 10.0;
}
// Remote / scripted input: text lines written to the FIFO /tmp/des_ui act like a pointer:
//   "click X Y", "move X Y", "wheel N", "key CODE" (linux key code). For tests and demo scripts over SSH
//   (echo "click 75 860" > /tmp/des_ui); the board has no uinput.
void UiInput::fifo_line(const std::string &l)
{
    char cmd[16] = "";
    int a = 0, b = 0;
    const int n = sscanf(l.c_str(), "%15s %d %d", cmd, &a, &b);
    const std::string c = cmd;
    if (c == "snap") {
        snap_ = true;
        return;
    }
    if (n < 2) return;
    std::lock_guard<std::mutex> lk(m_);
    if ((c == "click" || c == "move") && n == 3) {
        x_ = std::clamp(a, 0, w_ - 1);
        y_ = std::clamp(b, 0, h_ - 1);
        last_move_ = mono_s();
        if (c == "click") ev_.push_back({UiEvent::Click, x_, y_, 0});
    } else if (c == "wheel") {
        ev_.push_back({UiEvent::Wheel, x_, y_, a > 0 ? 1 : -1});
    } else if (c == "key") {
        ev_.push_back({UiEvent::Key, x_, y_, a});
    }
}
void UiInput::loop()
{
    std::map<std::string, Dev> devs;
    std::vector<pollfd> pf;
    std::vector<std::string> names;   // names[i] belongs to pf[i]; empty = the FIFO entry
    double last_scan = -1e9;
    static const char *kFifo = "/tmp/des_ui";
    mkfifo(kFifo, 0666);
    const int fifo = open(kFifo, O_RDONLY | O_NONBLOCK);
    const int fifo_keep = open(kFifo, O_WRONLY | O_NONBLOCK);   // our own writer: no POLLHUP when scripts close
    std::string fifo_buf;
    auto rebuild = [&]() {
        pf.clear();
        names.clear();
        for (auto &kv : devs) {
            pf.push_back({kv.second.fd, POLLIN, 0});
            names.push_back(kv.first);
        }
        if (fifo >= 0) pf.push_back({fifo, POLLIN, 0});
    };
    rebuild();
    while (!quit_) {
        if (mono_s() - last_scan > 2.0) {   // hot plug: scan the directory, reopen only new devices
            last_scan = mono_s();
            if (DIR *d = opendir("/dev/input")) {
                while (dirent *e = readdir(d)) {
                    if (strncmp(e->d_name, "event", 5) != 0) continue;
                    std::string p = std::string("/dev/input/") + e->d_name;
                    if (devs.count(p)) continue;
                    int fd = open(p.c_str(), O_RDONLY | O_NONBLOCK);
                    if (fd < 0) continue;
                    Dev dv;
                    dv.fd = fd;
                    dv.abs = ioctl(fd, EVIOCGABS(ABS_X), &dv.ax) == 0 && ioctl(fd, EVIOCGABS(ABS_Y), &dv.ay) == 0 &&
                             dv.ax.maximum > dv.ax.minimum && dv.ay.maximum > dv.ay.minimum;
                    devs[p] = dv;
                }
                closedir(d);
                ndev_ = int(devs.size());
                rebuild();
            }
        }
        if (pf.empty()) {
            usleep(200000);
            continue;
        }
        if (poll(pf.data(), pf.size(), 200) <= 0) continue;
        const size_t nd = names.size();   // devices first, the FIFO entry (if any) last
        if (fifo >= 0 && (pf[nd].revents & POLLIN)) {
            char buf[256];
            ssize_t n = read(fifo, buf, sizeof buf);
            if (n > 0) fifo_buf.append(buf, size_t(n));
            for (size_t nl; (nl = fifo_buf.find('\n')) != std::string::npos;) {
                fifo_line(fifo_buf.substr(0, nl));
                fifo_buf.erase(0, nl + 1);
            }
            if (fifo_buf.size() > 1024) fifo_buf.clear();
        }
        bool gone = false;
        for (size_t i = 0; i < nd; i++) {
            if (pf[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {   // unplugged
                close(pf[i].fd);
                devs.erase(names[i]);
                gone = true;
                continue;
            }
            if (!(pf[i].revents & POLLIN)) continue;
            Dev &dv = devs[names[i]];
            input_event ev[64];
            ssize_t n = read(dv.fd, ev, sizeof ev);
            if (n <= 0) continue;
            for (size_t k = 0; k < size_t(n) / sizeof(input_event); k++) {
                const input_event &e = ev[k];
                if (e.type == EV_REL) {
                    if (e.code == REL_X) x_ = std::clamp(x_ + e.value, 0, w_ - 1);
                    else if (e.code == REL_Y) y_ = std::clamp(y_ + e.value, 0, h_ - 1);
                    else if (e.code == REL_WHEEL) {
                        std::lock_guard<std::mutex> lk(m_);
                        ev_.push_back({UiEvent::Wheel, x_, y_, e.value > 0 ? 1 : -1});
                    }
                    last_move_ = mono_s();
                } else if (e.type == EV_ABS && dv.abs) {
                    if (e.code == ABS_X)
                        x_ = int(int64_t(e.value - dv.ax.minimum) * (w_ - 1) / (dv.ax.maximum - dv.ax.minimum));
                    else if (e.code == ABS_Y)
                        y_ = int(int64_t(e.value - dv.ay.minimum) * (h_ - 1) / (dv.ay.maximum - dv.ay.minimum));
                    last_move_ = mono_s();
                } else if (e.type == EV_KEY && e.value == 1) {   // press
                    std::lock_guard<std::mutex> lk(m_);
                    if (e.code == BTN_LEFT || e.code == BTN_TOUCH) {
                        ev_.push_back({UiEvent::Click, x_, y_, 0});
                        last_move_ = mono_s();
                    } else {
                        ev_.push_back({UiEvent::Key, x_, y_, int(e.code)});
                    }
                }
            }
        }
        if (gone) rebuild();   // close()/erase already done; ndev_ refreshes on the next 2 s scan (as before)
    }
    for (auto &kv : devs) close(kv.second.fd);
    if (fifo >= 0) close(fifo);
    if (fifo_keep >= 0) close(fifo_keep);
}
}  // namespace des
