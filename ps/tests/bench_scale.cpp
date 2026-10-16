// bench_scale.cpp — what limits the display conversion: memory mapping of the camera / display CMA buffers.
// The vendor CMA allocator takes cache flags (0 cache_shared = what CmaBuf uses, 1 writecombine, 2 nocached).
// For each source (camera, BGRA 1920x1080) / destination (display, BGR 1920x1080) mapping this times: read alone,
// write alone, OpenCV cvtColor of the whole frame, scale34 (3/4 of 1920 x 1040) and the cache maintenance each mapping
// needs; first it checks scale34 against a plain scalar version of the same filter.
// 2026-10-16 (src / dst cached): read 4.66, sync_for_cpu 0.69, write 1.52, sync_for_device 0.51, cvtColor 4.58;
// a 2-tap linear 2/3 scaler (scale23, since removed) 11.16. Writecombine / nocached source: read 151 ms (no use).
// Run on the board with des_app stopped (des_service.sh stop): copy to /tmp and run it there
#include <arm_neon.h>
#include <time.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include <opencv2/imgproc.hpp>

#include "armnnALUtils/cma_mem_init.hpp"
#include "scale34.hpp"

static double now_ms()
{
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static constexpr int W = 1920, H = 1080, kReps = 6;

struct Buf {
    cma_buffer_t b;
    Buf(size_t size, uint32_t flags)
    {
        b.size = uint32_t(size);
        b.flags = flags;
        cma_mem_alloc(&b);
    }
    ~Buf()
    {
        if (b.phys_addr) cma_mem_free(&b);
    }
    uint8_t *p() const { return static_cast<uint8_t *>(b.virt_addr); }
};

static uint32_t read_all(const uint8_t *s, size_t n)   // 64-byte loads, front to back
{
    uint32x4_t acc = vdupq_n_u32(0);
    for (size_t i = 0; i < n; i += 64) {
        const uint8x16x4_t v = vld4q_u8(s + i);
        acc = vaddq_u32(acc, vreinterpretq_u32_u8(veorq_u8(veorq_u8(v.val[0], v.val[1]), veorq_u8(v.val[2], v.val[3]))));
    }
    return vgetq_lane_u32(acc, 0) ^ vgetq_lane_u32(acc, 3);
}

static void write_all(uint8_t *d, size_t n, uint8_t k)   // 48-byte stores, front to back
{
    uint8x16x3_t v;
    v.val[0] = vdupq_n_u8(k);
    v.val[1] = vdupq_n_u8(uint8_t(k + 1));
    v.val[2] = vdupq_n_u8(uint8_t(k + 2));
    for (size_t i = 0; i + 48 <= n; i += 48) vst3q_u8(d + i, v);
}

template <class F>
static double time_ms(F f)
{
    f();   // warm up (page faults)
    double t = now_ms();
    for (int i = 0; i < kReps; i++) f();
    return (now_ms() - t) / kReps;
}

static inline uint8_t rh(uint8_t a, uint8_t b) { return uint8_t((a + b + 1) >> 1); }

// scalar reference of scale34's filter; returns the number of differing bytes
static long check_scale34()
{
    const int sw = 1920, sh = 1040, ow = 1440, oh = 780;
    std::vector<uint8_t> src(size_t(sw) * sh * 4), out(size_t(1920) * oh * 3, 0xEE), ref(size_t(ow) * 3 * oh);
    uint32_t r = 12345;
    for (auto &v : src) v = uint8_t((r = r * 1103515245u + 12345u) >> 24);
    std::vector<uint8_t> v(size_t(sw) * 4 * oh);   // vertical pass, BGRA: row 0, avg(row 1, row 2), row 3 of each 4
    for (int g = 0; g < sh / 4; g++)
        for (int x = 0; x < sw * 4; x++) {
            const uint8_t *p = &src[size_t(4 * g) * sw * 4 + x];
            v[size_t(3 * g) * sw * 4 + x] = p[0];
            v[size_t(3 * g + 1) * sw * 4 + x] = rh(p[size_t(sw) * 4], p[size_t(sw) * 8]);
            v[size_t(3 * g + 2) * sw * 4 + x] = p[size_t(sw) * 12];
        }
    for (int y = 0; y < oh; y++)   // horizontal pass -> packed BGR
        for (int g = 0; g < sw / 4; g++)
            for (int c = 0; c < 3; c++) {
                const uint8_t *p = &v[(size_t(y) * sw + 4 * g) * 4 + c];
                const uint8_t s0 = p[0], s1 = p[4], s2 = p[8], s3 = p[12];
                uint8_t *o = &ref[(size_t(y) * ow + 3 * g) * 3 + c];
                o[0] = rh(s0, rh(s0, s1));
                o[3] = rh(s1, s2);
                o[6] = rh(s3, rh(s2, s3));
            }
    if (!des::scale34_bgra_to_bgr(src.data(), sw, sh, sw * 4, out.data(), 1920 * 3)) return -1;
    long bad = 0;
    for (int y = 0; y < oh; y++) {
        for (int x = 0; x < ow * 3; x++) bad += out[size_t(y) * 1920 * 3 + x] != ref[size_t(y) * ow * 3 + x];
        for (int x = ow * 3; x < 1920 * 3; x++) bad += out[size_t(y) * 1920 * 3 + x] != 0xEE;   // nothing past the row
    }
    return bad;
}

int main()
{
    setvbuf(stdout, nullptr, _IOLBF, 0);   // keep what was printed if a case crashes
    printf("scale34 vs scalar reference: %ld bytes differ\n", check_scale34());
    if (cma_mem_init() != 0) {
        fprintf(stderr, "cma_mem_init failed\n");
        return 1;
    }
    const char *names[3] = {"cached", "wcomb ", "nocache"};
    const size_t ssize = size_t(W) * H * 4, dsize = size_t(W) * H * 3;
    printf("per 1080p frame, ms (src = camera BGRA 8.3 MB, dst = display BGR 6.2 MB)\n");
    for (uint32_t sf = 0; sf < 3; sf++) {
        Buf src(ssize, sf);
        if (!src.b.phys_addr) {
            printf("src flags %u: alloc failed\n", sf);
            continue;
        }
        memset(src.p(), 0x5A, ssize);
        cma_mem_sync_for_device(src.b.phys_addr, ssize);
        volatile uint32_t sink = 0;
        const double rd = time_ms([&] { sink = sink + read_all(src.p(), ssize); });
        const double inv = time_ms([&] { cma_mem_sync_for_cpu(src.b.phys_addr, ssize); });
        printf("src %s: read %5.2f  sync_for_cpu %5.2f\n", names[sf], rd, inv);
        for (uint32_t df = 0; df < 3; df++) {
            Buf dst(dsize, df);
            if (!dst.b.phys_addr) {
                printf("  dst flags %u: alloc failed\n", df);
                continue;
            }
            const double wr = time_ms([&] { write_all(dst.p(), dsize, 7); });
            const double fl = time_ms([&] { cma_mem_sync_for_device(dst.b.phys_addr, dsize); });
            cv::Mat s(H, W, CV_8UC4, src.p()), d(H, W, CV_8UC3, dst.p());
            const double cvt = time_ms([&] { cv::cvtColor(s, d, cv::COLOR_BGRA2BGR); });
            // nocached = Device memory: scale34's 16-byte stores at 4-byte alignment take an alignment fault (SIGBUS)
            // there; the display buffers are CmaBuf = flags 0 (normal memory), so that case is not timed
            const double sc4 = df == 2 ? 0 : time_ms([&] {
                des::scale34_bgra_to_bgr(src.p() + 20 * W * 4, W, 1040, W * 4, dst.p(), W * 3);
            });
            printf("  dst %s: write %5.2f  sync_for_device %5.2f  cvtColor %5.2f  scale34 %5.2f\n", names[df], wr, fl,
                   cvt, sc4);
        }
    }
    cma_mem_deinit();
    return 0;
}
