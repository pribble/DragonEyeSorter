// display.cpp — see display.hpp (hdmi_out.cpp + scale34.cpp merged; behavior unchanged).
#include "display.hpp"

#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

#include "flow_common_types.hpp"

namespace des {
// ---- HDMI output (was hdmi_out.cpp) ----
// Scan-out VDMA (vendor IP, Xilinx-compatible map, NUM_FSTORES = 3, park mode, internal frame sync). Flips use the park
// pointer: the three store addresses are written once and committed with one VSIZE write at open(); flip() then only
// writes PARK_PTR.RdFrmPtrRef, applied at the next frame boundary (never half drawn; PARK_PTR[20:16] reads back the
// scanned store — checked on the board). The 2026-10-16 black flashes did not come from here (docs/log/2026-10-16-*.md).
static constexpr uint32_t kVdmaBase = 0x80200000u;
static constexpr uint32_t kPark = 0x28, kVSize = 0x50, kHSize = 0x54, kStart = 0x5C;   // kStart + 4 * store
static constexpr int kW = 1920, kH = 1080;
static constexpr int kConsoleStore = 2;   // store 2 keeps the console (fb0) address: shown before the first flip and
                                          // on close
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
        memset(b->virt_addr, 0, size_t(kW) * kH * 3);   // areas a mode never draws stay black, not stale memory
        b.sync_device();
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
int HdmiOut::back_index() const { return p_->k; }
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
// ---- scale34 (was scale34.cpp) ----
// The A35's NEON unit is 64 bits wide, so per-pixel arithmetic limits this: 1080p BGRA read 4.7 ms, OpenCV BGRA->BGR copy
// 4.6 ms, full 2-tap 2/3 scaler 11.1 ms (板上实测, docs/log/2026-10-16-*.md). 3/4 groups whole pixels by 4 (one vld4):
//   horizontal, each 4 source pixels -> 3: 3/4 s0 + 1/4 s1, (s1 + s2) / 2, 1/4 s2 + 3/4 s3 (rounding halving adds),
//              packed straight to BGR with table lookups;
//   vertical, each 4 source rows -> 3: row 0, (row 1 + row 2) / 2, row 3 — sample positions 0 / 1.5 / 3 instead of
//              the ideal 0.17 / 1.5 / 2.83, not visible; rows 1 and 2 are averaged before the horizontal pass.
static constexpr int kMaxOut = 1440;   // output pixels per row (1920 source)
#if defined(__aarch64__)
namespace {
// Byte indices packing 12 BGRA pixels held as {o0, o1, o2} (output pixel p = lane p/3 of o[p%3]) into 36 BGR bytes:
// two full 16-byte lookups and a 4-byte tail.
struct alignas(16) PackIdx {
    uint8_t i[48];
};
constexpr PackIdx make_pack_idx()
{
    PackIdx p{};
    for (int k = 0; k < 48; k++) {
        const int pix = k / 3, c = k % 3;
        p.i[k] = k < 36 ? uint8_t(16 * (pix % 3) + 4 * (pix / 3) + c) : 0;
    }
    return p;
}
constexpr PackIdx kPack = make_pack_idx();
// one source row (or, kTwo, the average of two) -> one packed BGR output row, horizontally 4 -> 3 (16 source px/step)
template <bool kTwo>
inline void row_h(const uint8_t *s, const uint8_t *t, int sw, uint8_t *d)
{
    const uint8x16_t i0 = vld1q_u8(kPack.i), i1 = vld1q_u8(kPack.i + 16);
    const uint8x8_t i2 = vld1_u8(kPack.i + 32);
    for (int x = 0; x < sw; x += 16, s += 64, t += 64, d += 36) {
        // vld4 on 32-bit lanes = whole pixels de-interleaved by 4: val[k] holds pixels k, k+4, k+8, k+12
        const uint32x4x4_t r = vld4q_u32(reinterpret_cast<const uint32_t *>(s));
        uint8x16_t s0 = vreinterpretq_u8_u32(r.val[0]), s1 = vreinterpretq_u8_u32(r.val[1]),
                   s2 = vreinterpretq_u8_u32(r.val[2]), s3 = vreinterpretq_u8_u32(r.val[3]);
        if (kTwo) {   // vertical average first: one horizontal pass instead of two
            const uint32x4x4_t q = vld4q_u32(reinterpret_cast<const uint32_t *>(t));
            s0 = vrhaddq_u8(s0, vreinterpretq_u8_u32(q.val[0]));
            s1 = vrhaddq_u8(s1, vreinterpretq_u8_u32(q.val[1]));
            s2 = vrhaddq_u8(s2, vreinterpretq_u8_u32(q.val[2]));
            s3 = vrhaddq_u8(s3, vreinterpretq_u8_u32(q.val[3]));
        }
        uint8x16x3_t o;
        o.val[0] = vrhaddq_u8(s0, vrhaddq_u8(s0, s1));   // 3/4 s0 + 1/4 s1
        o.val[1] = vrhaddq_u8(s1, s2);
        o.val[2] = vrhaddq_u8(s3, vrhaddq_u8(s2, s3));   // 1/4 s2 + 3/4 s3
        vst1q_u8(d, vqtbl3q_u8(o, i0));
        vst1q_u8(d + 16, vqtbl3q_u8(o, i1));
        // 4-byte tail as one 32-bit lane: never writes past the row (the display buffer continues with the panel)
        vst1_lane_u32(reinterpret_cast<uint32_t *>(d + 32), vreinterpret_u32_u8(vqtbl3_u8(o, i2)), 0);
    }
}
}  // namespace
#endif
bool scale34_bgra_to_bgr(const uint8_t *src, int sw, int sh, int sstride, uint8_t *dst, int dstride)
{
    if (sw <= 0 || sh <= 0 || sw % 16 || sh % 4 || sw / 4 * 3 > kMaxOut) return false;
#if defined(__aarch64__)
    // Each group of 4 source rows gives 3 output rows written straight into the display buffer: row 0, the average of
    // rows 1 and 2 (averaged before the horizontal pass: 3 passes per group, not 4), row 3.
    for (int y = 0, oy = 0; y < sh; y += 4, oy += 3) {
        const uint8_t *s = src + size_t(y) * sstride;
        uint8_t *d = dst + size_t(oy) * dstride;
        row_h<false>(s, s, sw, d);
        row_h<true>(s + sstride, s + 2 * size_t(sstride), sw, d + dstride);
        row_h<false>(s + 3 * size_t(sstride), s, sw, d + 2 * size_t(dstride));
    }
    return true;
#else
    (void)src;
    (void)sstride;
    (void)dst;
    (void)dstride;
    return false;
#endif
}
}  // namespace des
