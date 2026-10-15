// hdmi_out.cpp — see hdmi_out.hpp
#include "hdmi_out.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "flow_common_types.hpp"

namespace des {

// Scan-out VDMA (vendor IP, Xilinx-compatible register map, NUM_FSTORES = 3, park mode, internal frame sync).
// ONE display buffer, no page flipping: every register write while it scans can blank the whole HDMI picture
// (2026-10-16, sync stays, pixels black): committing START + VSIZE per frame blanked ~1 s about once a minute,
// switching PARK_PTR per frame (frame-boundary switch) blanked every 1~3 s, i.e. at the beat between the camera
// (60.02 fps) and the display (60.6 Hz). So open() commits the addresses once (store 0 = our buffer, store 2 = the
// console it replaces) and parks on 2; the first flip() parks on 0; after that the VDMA is never touched and each
// frame is written straight into the buffer being scanned (a tear line is possible where the scan passes the
// write, hardly visible at belt speeds). close() parks back on 2 and restores the registers.
static constexpr uint32_t kVdmaBase = 0x80200000u;
static constexpr uint32_t kPark = 0x28, kVSize = 0x50, kHSize = 0x54, kStart = 0x5C;   // kStart + 4 * store
static constexpr int kW = 1920, kH = 1080;
static constexpr int kConsoleStore = 2;

struct HdmiOut::Impl {
    int fd = -1;
    volatile uint32_t *vdma = nullptr;
    uint32_t start_orig[3] = {0, 0, 0}, park_orig = 0;
    CmaBuf buf;
    bool shown = false;   // parked on our buffer
    bool open = false;
};

HdmiOut::HdmiOut() : p_(new Impl) {}
HdmiOut::~HdmiOut() { close(); }

std::string HdmiOut::open()
{
    Impl &p = *p_;
    p.fd = ::open("/dev/mem", O_RDWR | O_SYNC);
    if (p.fd < 0) return "cannot open /dev/mem";
    void *v = mmap(nullptr, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, p.fd, kVdmaBase);
    if (v == MAP_FAILED) return "cannot map scan-out VDMA";
    p.vdma = static_cast<volatile uint32_t *>(v);
    if (p.vdma[kHSize / 4] != kW * 3 || p.vdma[kVSize / 4] != kH) return "scan-out is not 1920x1080 at 24 bpp";
    for (int i = 0; i < 3; i++) p.start_orig[i] = p.vdma[kStart / 4 + i];
    p.park_orig = p.vdma[kPark / 4];
    p.buf = CmaBuf(kW * kH * 3);
    if (!p.buf || !p.buf->phys_addr || (p.buf->phys_addr >> 32)) return "CMA alloc for the display buffer failed";
    const uint32_t console = p.start_orig[(p.park_orig & 0x1F) < 3 ? (p.park_orig & 0x1F) : 0];
    p.vdma[kStart / 4 + 0] = uint32_t(p.buf->phys_addr);
    p.vdma[kStart / 4 + 1] = uint32_t(p.buf->phys_addr);
    p.vdma[kStart / 4 + kConsoleStore] = console;
    p.vdma[kPark / 4] = (p.park_orig & ~0x1Fu) | kConsoleStore;
    p.vdma[kVSize / 4] = kH;   // the one commit of the addresses
    p.shown = false;
    p.open = true;
    return "";
}

cv::Mat HdmiOut::back() { return cv::Mat(kH, kW, CV_8UC3, p_->buf->virt_addr); }

void HdmiOut::flip()
{
    Impl &p = *p_;
    if (!p.open) return;
    timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    p.buf.sync_device();
    if (!p.shown) {   // first frame drawn: switch from the console once, never again
        p.vdma[kPark / 4] = (p.vdma[kPark / 4] & ~0x1Fu) | 0u;
        p.shown = true;
    }
    clock_gettime(CLOCK_MONOTONIC, &b);
    flush_ms_ = (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6;
}

void HdmiOut::close()
{
    Impl &p = *p_;
    if (p.vdma) {
        if (p.open) {   // back to the console: park on store 2 (its old address), then restore the registers
            p.vdma[kPark / 4] = (p.vdma[kPark / 4] & ~0x1Fu) | kConsoleStore;
            usleep(40000);   // let the switch happen before our buffer goes away
            for (int i = 0; i < 3; i++) p.vdma[kStart / 4 + i] = p.start_orig[i];
            p.vdma[kPark / 4] = p.park_orig;
            p.vdma[kVSize / 4] = kH;
        }
        munmap(const_cast<uint32_t *>(p.vdma), 0x1000);
        p.vdma = nullptr;
    }
    if (p.fd >= 0) ::close(p.fd);
    p.fd = -1;
    p.open = false;
}

}  // namespace des
