// hdmi_out.cpp — see hdmi_out.hpp
#include "hdmi_out.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "flow_common_types.hpp"

namespace des {

static constexpr uint32_t kVdmaBase = 0x80200000u;
static constexpr uint32_t kVSize = 0x50, kHSize = 0x54, kStart1 = 0x5C;
static constexpr int kW = 1920, kH = 1080;

struct HdmiOut::Impl {
    int fd = -1;
    volatile uint32_t *vdma = nullptr;
    uint32_t start_orig = 0;
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
    p.start_orig = p.vdma[kStart1 / 4];
    for (auto &b : p.buf) {
        b = CmaBuf(kW * kH * 3);
        if (!b || !b->phys_addr || (b->phys_addr >> 32)) return "CMA alloc for display buffers failed";
    }
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
    p.vdma[kStart1 / 4] = uint32_t(p.buf[p.k]->phys_addr);
    clock_gettime(CLOCK_MONOTONIC, &b);
    flush_ms_ = (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6;
    p.k ^= 1;
}

void HdmiOut::close()
{
    Impl &p = *p_;
    if (p.vdma) {
        if (p.open) p.vdma[kStart1 / 4] = p.start_orig;   // console back on fb0
        munmap(const_cast<uint32_t *>(p.vdma), 0x1000);
        p.vdma = nullptr;
    }
    if (p.fd >= 0) ::close(p.fd);
    p.fd = -1;
    p.open = false;
}

}  // namespace des
