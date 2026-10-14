// hdmi_out.hpp — 1080p HDMI output by page flipping the vendor scan-out VDMA (docs/软件与UI.md "显示通路").
//
// The scan-out VDMA at 0x8020_0000 reads 24 bpp BGR (H_SIZE / stride 5760) from START_1 (0x5C), normally the
// fb0 memory. We keep two CMA BGR buffers: draw into back(), then flip() flushes it and points START_1 at it.
// close() (also run by the destructor) restores the fb0 address so the console comes back.
// Needs CMA initialised first (CameraRig::open does it).
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
    void flip();
    void close();
    double last_flush_ms() const { return flush_ms_; }

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
    double flush_ms_ = 0;
};

}  // namespace des
