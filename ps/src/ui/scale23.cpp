// scale23.cpp — see scale23.hpp
#include "scale23.hpp"

#include <stddef.h>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace des {

static constexpr int kMaxOut = 1280;   // output pixels per row (1920 source)

#if defined(__aarch64__)
namespace {

// 3:2 on whole BGRA pixels: given the pixels of a group (a0 a1 a2), out0 = 3/4 a0 + 1/4 a1, out1 = 1/4 a1 + 3/4 a2
inline uint8x16_t first(uint8x16_t a, uint8x16_t b) { return vrhaddq_u8(a, vrhaddq_u8(a, b)); }
inline uint8x16_t second(uint8x16_t b, uint8x16_t c) { return vrhaddq_u8(c, vrhaddq_u8(b, c)); }

// 16 BGRA pixels (4 registers, pixel order) -> 48 bytes BGR: two unzip levels give the B / G / R / A planes
inline void store_bgr16(uint8_t *dst, uint8x16_t p0, uint8x16_t p1, uint8x16_t p2, uint8x16_t p3)
{
    const uint8x16x2_t u01 = vuzpq_u8(p0, p1), u23 = vuzpq_u8(p2, p3);   // [B R ...] / [G A ...]
    const uint8x16x2_t br = vuzpq_u8(u01.val[0], u23.val[0]);            // B plane / R plane
    const uint8x16x2_t ga = vuzpq_u8(u01.val[1], u23.val[1]);            // G plane / A plane
    uint8x16x3_t o;
    o.val[0] = br.val[0];
    o.val[1] = ga.val[0];
    o.val[2] = br.val[1];
    vst3q_u8(dst, o);
}

// 12 source pixels -> 8 horizontally scaled BGRA pixels (cached temp row). vld3 on 32-bit lanes = whole pixels
// de-interleaved by 3 (val[k] holds pixels k, k+3, k+6, k+9); the two outputs of each group are zipped back in order.
inline void h12(const uint32_t *s, uint32_t *t)
{
    const uint32x4x3_t r = vld3q_u32(s);
    const uint8x16_t a = vreinterpretq_u8_u32(r.val[0]), b = vreinterpretq_u8_u32(r.val[1]),
                     c = vreinterpretq_u8_u32(r.val[2]);
    const uint32x4x2_t z = vzipq_u32(vreinterpretq_u32_u8(first(a, b)), vreinterpretq_u32_u8(second(b, c)));
    vst1q_u32(t, z.val[0]);
    vst1q_u32(t + 4, z.val[1]);
}

// 16 pixels of two temp rows -> 16 BGR output pixels: 3/4 a + 1/4 b (upper output row of a group) or 1/4 a + 3/4 b
inline void v16(const uint32_t *a, const uint32_t *b, bool upper, uint8_t *dst)
{
    uint8x16_t p[4];
    for (int i = 0; i < 4; i++) {
        const uint8x16_t va = vreinterpretq_u8_u32(vld1q_u32(a + 4 * i)), vb = vreinterpretq_u8_u32(vld1q_u32(b + 4 * i));
        p[i] = upper ? first(va, vb) : second(va, vb);
    }
    store_bgr16(dst, p[0], p[1], p[2], p[3]);
}

}  // namespace
#endif

bool scale23_bgra_to_bgr(const uint8_t *src, int sw, int sh, int sstride, uint8_t *dst, int dstride)
{
    if (sw <= 0 || sh <= 0 || sw % 24 || sh % 3 || sw / 3 * 2 > kMaxOut) return false;
#if defined(__aarch64__)
    // Each source row is scaled horizontally into a cached temp row, then the group's two output rows are blended
    // from those and written. Measured per 1080p frame on the A35 (2026-10-16): 12.7 ms; loads alone take 6.4 ms.
    // Tried and slower: the three rows of a group read side by side (17.5 ms), reads and writes interleaved chunk by
    // chunk (20.2 ms), 8 groups read before writing (13.2 ms), per-channel table lookups instead of vld3 (16 ms).
    alignas(64) static thread_local uint32_t t[3][kMaxOut];
    const int ow = sw / 3 * 2;
    for (int y = 0; y < sh; y += 3) {
        for (int i = 0; i < 3; i++) {
            const uint32_t *s = reinterpret_cast<const uint32_t *>(src + size_t(y + i) * sstride);
            for (int x = 0, o = 0; x < sw; x += 12, o += 8) h12(s + x, t[i] + o);
        }
        uint8_t *d = dst + size_t(y / 3 * 2) * dstride;
        for (int o = 0; o < ow; o += 16) v16(t[0] + o, t[1] + o, true, d + 3 * o);
        d += dstride;
        for (int o = 0; o < ow; o += 16) v16(t[1] + o, t[2] + o, false, d + 3 * o);
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
