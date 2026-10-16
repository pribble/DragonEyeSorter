// scale34.hpp — fast 3/4 downscale of a BGRA camera frame into a BGR display rectangle (1920 x 1040 -> 1440 x 780).
//
// The A35's NEON unit is 64 bits wide, so per-pixel arithmetic, not memory, limits this on the board: reading a
// 1080p BGRA frame takes 4.7 ms and OpenCV's plain BGRA->BGR copy 4.6 ms (writes to the display buffer are almost
// free), while a full 2-tap linear 2/3 scaler took 11.1 ms (tests/bench_scale.cpp, 2026-10-16). 3/4 lets whole
// pixels be grouped by 4 with one vld4 and keeps the arithmetic small:
//   horizontal, each 4 source pixels -> 3: 3/4 s0 + 1/4 s1, (s1 + s2) / 2, 1/4 s2 + 3/4 s3 (rounding halving adds),
//              packed straight to BGR with table lookups;
//   vertical, each 4 source rows -> 3: row 0, (row 1 + row 2) / 2, row 3 — sample positions 0 / 1.5 / 3 instead of
//              the ideal 0.17 / 1.5 / 2.83, not visible; rows 1 and 2 are averaged before the horizontal pass.
#pragma once

#include <cstdint>

namespace des {

// src: BGRA, sw x sh, stride sstride bytes; dst: BGR, (sw*3/4) x (sh*3/4), stride dstride bytes.
// sw must be a multiple of 16 and at most 1920, sh a multiple of 4; returns false (nothing written) otherwise.
// dst must be normal memory (cached or write-combining CMA, as CmaBuf allocates): output rows are written with 16-byte
// stores at 4-byte alignment, which fault on Device (nocached) mappings.
bool scale34_bgra_to_bgr(const uint8_t *src, int sw, int sh, int sstride, uint8_t *dst, int dstride);

}  // namespace des
