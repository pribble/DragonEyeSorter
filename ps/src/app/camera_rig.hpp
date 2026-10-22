// camera_rig.hpp — frame capture from /dev/mipicam with PL frame metadata (frame count, belt position).
//
// The vendor frame writer captures on demand: a buffer is armed, the next frame start is written into it, then the done interrupt fires. To keep every frame at 60 fps the next buffer is re-armed immediately
// after the completion, before the caller touches the finished one (ring of `buffers`). pl_gap > 1 means frames were skipped (re-arm missed the next frame start).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include "des_hw.h"
namespace des {
// One captured frame. The DMA ring rewrites the pixels `buffers - 2` captures later (copy / finish with them first); the PL metadata latched at that capture stays valid.
struct CapturedFrame {
    const uint8_t *bgra = nullptr;   // 4 bytes per pixel
    int width = 0, height = 0, stride = 0;
    uint32_t pl_frame = 0;      // PL start-of-frame count of this frame
    int32_t step_pos = 0;       // belt position (steps) when the first row arrived
    int32_t step_pos_end = 0;   // belt position when the last row was written
    bool latched = false;       // true: PL-latched at the frame start / done (v5); false: read after completion
    uint64_t t_us = 0;          // CLOCK_MONOTONIC at completion
    uint32_t pl_gap = 0;        // PL frames since the previous capture (1 = nothing skipped)
    uint32_t wait_us = 0;       // blocked in WaitFinish
    uint32_t rearm_us = 0;      // completion seen -> next buffer armed
    uint32_t sync_us = 0;       // cache invalidate of this buffer
};
class CameraRig {
public:
    explicit CameraRig(des_hw_t *hw);
    ~CameraRig();
    // Opens the camera (sensor init by the vendor wrapper), applies vmax / exposure / gain when non-zero, allocates the DMA ring and arms the first buffer. Empty string on success.
    std::string open(int buffers, int vmax, int exposure_us, int gain_db10);
    // Blocks until the next frame. need_pixels = false skips the cache invalidate (timing-only use).
    std::string next(CapturedFrame &f, bool need_pixels = true);
    // IMX415 timing: 1H = frame period / VMAX. Exposure is (VMAX - SHR0) lines.
    int sensor_vmax() const { return vmax_; }
    int sensor_write(uint16_t reg, uint8_t val);
    int sensor_read(uint16_t reg, uint8_t &val);
private:
    struct Impl;
    std::unique_ptr<Impl> p_;   // created by open(): an unused rig (--image modes) allocates nothing
    des_hw_t *hw_;
    int vmax_ = 2283;
};
}  // namespace des
