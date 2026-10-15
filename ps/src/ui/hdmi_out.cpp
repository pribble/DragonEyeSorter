// hdmi_out.cpp — see hdmi_out.hpp
#include "hdmi_out.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "flow_common_types.hpp"

namespace des {

// Scan-out VDMA (vendor IP, Xilinx-compatible register map, NUM_FSTORES = 3, park mode, internal frame sync).
// Page flipping goes through the park pointer: the three frame-store addresses are written once and committed with
// one VSIZE write at open(); each flip() only writes PARK_PTR.RdFrmPtrRef, which the VDMA applies at the next frame
// boundary. Do NOT commit addresses per frame (START + VSIZE): that restarts the fetch wherever the scan is, and as
// the camera (60.02 fps) drifts against HDMI (60.00 Hz) the write sweeps through a window that blanks the whole
// screen for ~1 s about once a minute (seen 2026-10-16; worse under load). PARK_PTR[20:16] reads back the store
// being scanned (checked on the board: follows RdFrmPtrRef within one frame).
static constexpr uint32_t kVdmaBase = 0x80200000u;
static constexpr uint32_t kPark = 0x28, kVSize = 0x50, kHSize = 0x54, kStart = 0x5C;   // kStart + 4 * store
static constexpr int kW = 1920, kH = 1080;
static constexpr int kConsoleStore = 2;   // frame store 2 keeps the console (fb0) address: shown before the first
                                          // flip and on close

struct HdmiOut::Impl {
    int fd = -1;
    volatile uint32_t *vdma = nullptr;
    uint32_t start_orig[3] = {0, 0, 0}, park_orig = 0;
    CmaBuf buf[2];
    int k = 0;
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
    for (auto &b : p.buf) {
        b = CmaBuf(kW * kH * 3);
        if (!b || !b->phys_addr || (b->phys_addr >> 32)) return "CMA alloc for display buffers failed";
    }
    // stores 0 / 1 = our buffers, 2 = what was on screen; park on 2 until the first frame is drawn
    p.vdma[kStart / 4 + 0] = uint32_t(p.buf[0]->phys_addr);
    p.vdma[kStart / 4 + 1] = uint32_t(p.buf[1]->phys_addr);
    p.vdma[kStart / 4 + kConsoleStore] = p.start_orig[(p.park_orig & 0x1F) < 3 ? (p.park_orig & 0x1F) : 0];
    p.vdma[kPark / 4] = (p.park_orig & ~0x1Fu) | kConsoleStore;
    p.vdma[kVSize / 4] = kH;   // the one commit of the addresses
    p.k = 0;
    p.open = true;
    return "";
}

cv::Mat HdmiOut::back() { return cv::Mat(kH, kW, CV_8UC3, p_->buf[p_->k]->virt_addr); }

void HdmiOut::flip()
{
    Impl &p = *p_;
    if (!p.open) return;
    timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    p.buf[p.k].sync_device();
    p.vdma[kPark / 4] = (p.vdma[kPark / 4] & ~0x1Fu) | uint32_t(p.k);   // switches at the next frame boundary
    clock_gettime(CLOCK_MONOTONIC, &b);
    flush_ms_ = (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6;
    p.k ^= 1;
}

void HdmiOut::close()
{
    Impl &p = *p_;
    if (p.vdma) {
        if (p.open) {   // back to the console: park on store 2 (its old address), then restore the registers
            p.vdma[kPark / 4] = (p.vdma[kPark / 4] & ~0x1Fu) | kConsoleStore;
            usleep(40000);   // let the switch happen before our buffers go away
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
