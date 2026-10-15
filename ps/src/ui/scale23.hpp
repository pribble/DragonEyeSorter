// scale23.hpp — fast 2/3 downscale of a BGRA camera frame into a BGR display rectangle (1920 x 1080 -> 1280 x 720).
//
// OpenCV's resize is far too slow here (A35, single thread, uncached source / destination): 1920 x 1080 -> 1280 x 720
// INTER_LINEAR takes 55 ms even from cached memory (bench_cma, 2026-10-16). This NEON routine reads the camera buffer
// once, row by row, and writes every output row in 48-byte stores. Each group of 3 source pixels (and rows) becomes
// 2: out0 = 3/4 s0 + 1/4 s1, out1 = 1/4 s1 + 3/4 s2 (two rounding halving adds), i.e. linear interpolation at
// sample positions 0.25 / 0.75 of the group — visually the same as true 2/3 area resampling. Whole BGRA pixels are
// gathered by 3 with one vld3 on 32-bit lanes and blended four channels at a time; each source row is first scaled
// horizontally into a small cached row, then pairs of those rows are blended straight into the display buffer
// (12.7 ms per frame; the display loop then takes 18 ms -> 40 fps shown, see scale23.cpp for what was tried).
#pragma once

#include <cstdint>

namespace des {

// src: BGRA, sw x sh, stride sstride bytes; dst: BGR, (sw*2/3) x (sh*2/3), stride dstride bytes.
// sw must be a multiple of 24 and at most 1920, sh a multiple of 3; returns false (nothing written) otherwise.
bool scale23_bgra_to_bgr(const uint8_t *src, int sw, int sh, int sstride, uint8_t *dst, int dstride);

}  // namespace des
