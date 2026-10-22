// display.hpp — HDMI output + fast camera downscale (was hdmi_out.* / scale34.*).
//
// HdmiOut: 1080p HDMI by page flipping the vendor scan-out VDMA (docs/软件与UI.md "显示通路"): 24 bpp BGR (H_SIZE /
// stride 5760) read from START_1 (0x5C), two CMA BGR buffers — draw into back(), flip() points START_1 at it after a
// cache flush, close() (also the destructor) restores the fb0 address so the console comes back. Needs CMA first
// (CameraRig::open does it). Never draw into the scanned buffer: overlays would roll (user, 2026-10-16).
//
// scale34_bgra_to_bgr: fast 3/4 downscale of a BGRA camera frame into a BGR display rectangle (1920x1040 -> 1440x780;
// NEON row pairs, 3 output rows per 4 source rows — see display.cpp for the measured reason).
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <opencv2/core.hpp>

namespace des {

class HdmiOut {
public:
    HdmiOut();
    ~HdmiOut();
    std::string open();
    cv::Mat back();           // 1080 x 1920 CV_8UC3 view of the buffer to draw next
    int back_index() const;   // which of the two buffers back() is (0 / 1): what it still holds from two frames ago
    void flip();
    void close();
    double last_flush_ms() const { return flush_ms_; }

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
    double flush_ms_ = 0;
};

// src: BGRA, sw x sh, stride sstride bytes; dst: BGR, (sw*3/4) x (sh*3/4), stride dstride bytes.
// sw must be a multiple of 16 and at most 1920, sh a multiple of 4; false (nothing written) otherwise.
// dst must be normal memory (cached / write-combining CMA, as CmaBuf allocates): rows are written with 16-byte stores
// at 4-byte alignment, which fault on Device (nocached) mappings.
bool scale34_bgra_to_bgr(const uint8_t *src, int sw, int sh, int sstride, uint8_t *dst, int dstride);

}  // namespace des
