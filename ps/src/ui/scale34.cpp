// scale34.cpp — see scale34.hpp
#include "scale34.hpp"

#include <stddef.h>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace des {

static constexpr int kMaxOut = 1440;   // output pixels per row (1920 source)

#if defined(__aarch64__)
namespace {

// Byte indices for packing 12 BGRA pixels held as {o0, o1, o2} (output pixel p = lane p/3 of o[p%3]) into 36 BGR
// bytes: two full 16-byte lookups and a 4-byte tail.
struct PackIdx {
    alignas(16) uint8_t i[48];
    PackIdx()
    {
        for (int k = 0; k < 48; k++) {
            const int p = k / 3, c = k % 3;
            i[k] = k < 36 ? uint8_t(16 * (p % 3) + 4 * (p / 3) + c) : 0;
        }
    }
};
const PackIdx kPack;

// one source row (or, kTwo, the average of two) -> one packed BGR output row, horizontally 4 -> 3
// (16 source pixels per step)
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
    // Each group of 4 source rows gives 3 output rows, each written straight into the display buffer: row 0,
    // the average of rows 1 and 2 (averaged before the horizontal pass, so 3 horizontal passes per group, not 4),
    // row 3. Measured per frame (tests/bench_scale): 9.7 ms when rows 1 and 2 were each scaled and then averaged.
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
