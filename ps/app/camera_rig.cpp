// camera_rig.cpp — see camera_rig.hpp
#include "camera_rig.hpp"

#include <time.h>

#include <vector>

#include "flow_common_types.hpp"
#include "mipi_camera.hpp"

namespace des {

static constexpr uint8_t kImx415 = 0x1a;

static uint64_t now_us()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000000u + ts.tv_nsec / 1000;
}

struct CameraRig::Impl {
    std::unique_ptr<CmaResourceZone> zone;
    std::shared_ptr<camera_t> cam;
    std::vector<CmaBuf> bufs;
    int cur = 0;
    bool have_prev = false;
    uint32_t prev_frame = 0;
};

CameraRig::CameraRig(des_hw_t *hw) : p_(new Impl), hw_(hw) {}
CameraRig::~CameraRig() = default;

int CameraRig::sensor_write(uint16_t reg, uint8_t val)
{
    uint8_t b[3] = {uint8_t(reg >> 8), uint8_t(reg & 0xff), val};
    p_->cam->iic.Write(kImx415, b);    // the vendor helper's return value is meaningless: verify by read-back
    uint8_t v = 0;
    if (sensor_read(reg, v) || v != val) return -1;
    return 0;
}

int CameraRig::sensor_read(uint16_t reg, uint8_t &val)
{
    uint8_t out[1] = {0};
    uint16_t wire = uint16_t((reg >> 8) | (reg << 8));   // helper sends the register bytes in memory order
    p_->cam->iic.Read(kImx415, wire, out);
    val = out[0];
    return 0;
}

std::string CameraRig::open(int buffers, int vmax, int exposure_us, int gain_db10)
{
    try {
        p_->zone.reset(new CmaResourceZone);
    } catch (const std::exception &e) {
        return std::string("CMA init: ") + e.what();
    }
    p_->cam = std::make_shared<camera_t>();
    p_->cam->open("/dev/mipicam");          // finds the sensor, writes the vendor register set, maps the DMA regs
    if (!p_->cam->is_open()) return "camera open failed (/dev/mipicam, mipicam.ko loaded? sensor on I2C?)";

    // read back the VMAX the driver configured
    uint8_t v0 = 0, v1 = 0, v2 = 0;
    sensor_read(0x3024, v0); sensor_read(0x3025, v1); sensor_read(0x3026, v2);
    vmax_ = v0 | (v1 << 8) | ((v2 & 0x0f) << 16);

    if (vmax > 0 || exposure_us > 0 || gain_db10 > 0) {
        int err = 0;
        err |= sensor_write(0x3001, 1);                      // REGHOLD: apply everything on the same frame
        if (vmax > 0) {
            err |= sensor_write(0x3024, vmax & 0xff);
            err |= sensor_write(0x3025, (vmax >> 8) & 0xff);
            err |= sensor_write(0x3026, (vmax >> 16) & 0x0f);
            vmax_ = vmax;
        }
        if (exposure_us > 0) {
            // 1H = 1/(60 fps * VMAX) when INCK matches the register set (PL v4+); exposure = (VMAX - SHR0) lines
            double line_us = 1e6 / 60.0 / vmax_;
            int lines = int(exposure_us / line_us + 0.5);
            int shr0 = vmax_ - lines;
            if (shr0 < 8) shr0 = 8;
            if (shr0 > vmax_ - 4) shr0 = vmax_ - 4;
            err |= sensor_write(0x3050, shr0 & 0xff);
            err |= sensor_write(0x3051, (shr0 >> 8) & 0xff);
            err |= sensor_write(0x3052, (shr0 >> 16) & 0x0f);
        }
        if (gain_db10 > 0) {
            int g = gain_db10 / 3;                            // GAIN register unit = 0.3 dB
            err |= sensor_write(0x3090, g & 0xff);
            err |= sensor_write(0x3091, (g >> 8) & 0x01);
        }
        err |= sensor_write(0x3001, 0);
        if (err) return "sensor register write/read-back failed";
    }

    if (buffers < 2) buffers = 2;
    size_t size = p_->cam->buffer_size();
    for (int i = 0; i < buffers; i++) {
        CmaBuf b(size);
        if (!b || b->phys_addr == 0 || !b->virt_addr) return "CMA alloc failed";
        p_->bufs.push_back(b);
    }
    p_->cur = 0;
    p_->have_prev = false;
    p_->cam->SubmitAndStart(p_->bufs[0]->phys_addr, p_->bufs[0]->virt_addr);
    return "";
}

std::string CameraRig::next(CapturedFrame &f, bool need_pixels)
{
    Impl &p = *p_;
    uint64_t t0 = now_us();
    if (p.cam->WaitFinish()) return "frame timeout (no camera interrupt within 1 s)";
    uint64_t t1 = now_us();

    // re-arm first: the next frame start may be less than a millisecond away
    int nxt = (p.cur + 1) % int(p.bufs.size());
    p.cam->SubmitAndStart(p.bufs[nxt]->phys_addr, p.bufs[nxt]->virt_addr);
    uint64_t t2 = now_us();

    des_frame_t fr;
    des_frame_read(hw_, &fr);
    f.pl_frame = fr.cnt;
    f.pl_ts = des_now(hw_);
    f.step_pos = des_step_pos(hw_);
    f.pl_gap = p.have_prev ? fr.cnt - p.prev_frame : 1;
    p.prev_frame = fr.cnt;
    p.have_prev = true;

    f.buf = p.cur;
    f.width = p.cam->width();
    f.height = p.cam->height();
    f.stride = f.width * p.cam->channel();
    f.bgra = static_cast<const uint8_t *>(p.bufs[p.cur]->virt_addr);
    f.t_us = t1;
    f.wait_us = double(t1 - t0);
    f.rearm_us = double(t2 - t1);
    f.sync_us = 0;
    if (need_pixels) {
        p.bufs[p.cur].sync_cpu();
        f.sync_us = double(now_us() - t2);
    }
    p.cur = nxt;
    return "";
}

}  // namespace des
