/****************************************************************
 * Copyright (c) 2023 Anlogic, Inc.  All rights reserved.
 * SPDX-License-Identifier: MIT
 ****************************************************************/

#include "mipi_camera.hpp"
#include "logger_forward.hpp"
#include "mipicam_module.h"
#include <cstring>
#include <thread>
#include <stdexcept>
#define CAMERA_BASE_ADDR 0x000
#define CAMERA_START 0x004
#define CAMERA_CLEAR_INTR 0x008

#define try_throw(expr, str)           \
    if (!(expr)) {                     \
        throw std::runtime_error(str); \
    }

namespace {

// IMX415 power-on register set (30 fps, H/V 2/2-line binning): {reg, value, delay_ms after the write}.
// Order and values are the datasheet-derived set, frozen: only the two settling waits are non-zero.
struct RegOp { uint16_t reg; uint8_t val; uint16_t delay_ms; };
constexpr RegOp kInitRegs[] = {
    {0x3000, 0x01, 0}, {0x3001, 0x00, 0}, {0x3002, 0x01, 0}, {0x3003, 0x00, 0},
    {0x3008, 0x5D, 0}, {0x3009, 0x00, 0}, {0x300A, 0x42, 0}, {0x300B, 0xA0, 0},
    {0x301C, 0x00, 0}, {0x301D, 0x08, 0}, {0x3020, 0x01, 0}, {0x3021, 0x01, 0},
    {0x3022, 0x01, 0}, {0x3023, 0x01, 0}, {0x3024, 0xEB, 0}, {0x3025, 0x08, 0},
    {0x3026, 0x00, 0}, {0x3028, 0x1E, 0}, {0x3029, 0x02, 0}, {0x302C, 0x00, 0},
    {0x302D, 0x00, 0}, {0x3030, 0x00, 0}, {0x3031, 0x00, 0}, {0x3032, 0x00, 0},
    {0x3033, 0x05, 0}, {0x3040, 0x00, 0}, {0x3041, 0x00, 0}, {0x3042, 0x18, 0},
    {0x3043, 0x0F, 0}, {0x3044, 0x00, 0}, {0x3045, 0x00, 0}, {0x3046, 0x20, 0},
    {0x3047, 0x11, 0}, {0x3050, 0x08, 0}, {0x3051, 0x00, 0}, {0x3052, 0x00, 0},
    {0x3054, 0x19, 0}, {0x3055, 0x00, 0}, {0x3056, 0x00, 0}, {0x3058, 0x3E, 0},
    {0x3059, 0x00, 0}, {0x305A, 0x00, 0}, {0x305C, 0x66, 0}, {0x305D, 0x00, 0},
    {0x305E, 0x00, 0}, {0x3060, 0x25, 0}, {0x3061, 0x00, 0}, {0x3062, 0x00, 0},
    {0x3064, 0x4A, 0}, {0x3065, 0x00, 0}, {0x3066, 0x00, 0}, {0x3090, 0x32, 0},
    {0x3091, 0x00, 0}, {0x3092, 0x32, 0}, {0x3093, 0x00, 0}, {0x3094, 0x32, 0},
    {0x3095, 0x00, 0}, {0x3096, 0x32, 0}, {0x3097, 0x00, 0}, {0x30C0, 0x2A, 0},
    {0x30C1, 0x00, 0}, {0x30CC, 0x00, 0}, {0x30CD, 0x00, 0}, {0x30CF, 0x00, 0},
    {0x30D9, 0x02, 0}, {0x30DA, 0x01, 0}, {0x30E2, 0x32, 0}, {0x30E3, 0x00, 0},
    // clock / PLL dividers (INCKSEL*)
    {0x3115, 0x00, 0}, {0x3116, 0x23, 0}, {0x3118, 0xC6, 0}, {0x3119, 0x00, 0},
    {0x311A, 0xE7, 0}, {0x311B, 0x00, 0}, {0x311E, 0x23, 0},
    // analogue gain and fixed settings
    {0x3260, 0x01, 0}, {0x32C8, 0x01, 0}, {0x32D4, 0x21, 0}, {0x32EC, 0xA1, 0},
    {0x344C, 0x2B, 0}, {0x344D, 0x01, 0}, {0x344E, 0xED, 0}, {0x344F, 0x01, 0},
    {0x3450, 0xF6, 0}, {0x3451, 0x02, 0}, {0x3452, 0x7F, 0}, {0x3453, 0x03, 0},
    {0x358A, 0x04, 0}, {0x35A1, 0x02, 0}, {0x35EC, 0x27, 0}, {0x35EE, 0x8D, 0},
    {0x35F0, 0x8D, 0}, {0x35F2, 0x29, 0}, {0x36BC, 0x0C, 0}, {0x36CC, 0x53, 0},
    {0x36CD, 0x00, 0}, {0x36CE, 0x3C, 0}, {0x36D0, 0x8C, 0}, {0x36D1, 0x00, 0},
    {0x36D2, 0x71, 0}, {0x36D4, 0x3C, 0}, {0x36D6, 0x53, 0}, {0x36D7, 0x00, 0},
    {0x36D8, 0x71, 0}, {0x36DA, 0x8C, 0}, {0x36DB, 0x00, 0}, {0x3701, 0x00, 0},
    {0x3720, 0x00, 0}, {0x3724, 0x02, 0}, {0x3726, 0x02, 0}, {0x3732, 0x02, 0},
    {0x3734, 0x03, 0}, {0x3736, 0x03, 0}, {0x3742, 0x03, 0}, {0x3862, 0xE0, 0},
    {0x38CC, 0x30, 0}, {0x38CD, 0x2F, 0}, {0x395C, 0x0C, 0}, {0x39A4, 0x07, 0},
    {0x39A8, 0x32, 0}, {0x39AA, 0x32, 0}, {0x39AC, 0x32, 0}, {0x39AE, 0x32, 0},
    {0x39B0, 0x32, 0}, {0x39B2, 0x2F, 0}, {0x39B4, 0x2D, 0}, {0x39B6, 0x28, 0},
    {0x39B8, 0x30, 0}, {0x39BA, 0x30, 0}, {0x39BC, 0x30, 0}, {0x39BE, 0x30, 0},
    {0x39C0, 0x30, 0}, {0x39C2, 0x2E, 0}, {0x39C4, 0x2B, 0}, {0x39C6, 0x25, 0},
    {0x3A42, 0xD1, 0}, {0x3A4C, 0x77, 0}, {0x3AE0, 0x02, 0}, {0x3AEC, 0x0C, 0},
    {0x3B00, 0x2E, 0}, {0x3B06, 0x29, 0}, {0x3B98, 0x25, 0}, {0x3B99, 0x21, 0},
    {0x3B9B, 0x13, 0}, {0x3B9C, 0x13, 0}, {0x3B9D, 0x13, 0}, {0x3B9E, 0x13, 0},
    {0x3BA1, 0x00, 0}, {0x3BA2, 0x06, 0}, {0x3BA3, 0x0B, 0}, {0x3BA4, 0x10, 0},
    {0x3BA5, 0x14, 0}, {0x3BA6, 0x18, 0}, {0x3BA7, 0x1A, 0}, {0x3BA8, 0x1A, 0},
    {0x3BA9, 0x1A, 0}, {0x3BAC, 0xED, 0}, {0x3BAD, 0x01, 0}, {0x3BAE, 0xF6, 0},
    {0x3BAF, 0x02, 0}, {0x3BB0, 0xA2, 0}, {0x3BB1, 0x03, 0}, {0x3BB2, 0xE0, 0},
    {0x3BB3, 0x03, 0}, {0x3BB4, 0xE0, 0}, {0x3BB5, 0x03, 0}, {0x3BB6, 0xE0, 0},
    {0x3BB7, 0x03, 0}, {0x3BB8, 0xE0, 0}, {0x3BBA, 0xE0, 0}, {0x3BBC, 0xDA, 0},
    {0x3BBE, 0x88, 0}, {0x3BC0, 0x44, 0}, {0x3BC2, 0x7B, 0}, {0x3BC4, 0xA2, 0},
    {0x3BC8, 0xBD, 0}, {0x3BCA, 0xBD, 0},
    // MIPI CSI-2 transmitter timing (TCLK* / THS* / TLPX*)
    {0x4000, 0x10, 0}, {0x4001, 0x03, 0}, {0x4004, 0xC0, 0}, {0x4005, 0x06, 0},
    {0x400C, 0x00, 0}, {0x4018, 0x7F, 0}, {0x4019, 0x00, 0}, {0x401A, 0x37, 0},
    {0x401B, 0x00, 0}, {0x401C, 0x37, 0}, {0x401D, 0x00, 0}, {0x401E, 0xF7, 0},
    {0x401F, 0x00, 0}, {0x4020, 0x3F, 0}, {0x4021, 0x00, 0}, {0x4022, 0x6F, 0},
    {0x4023, 0x00, 0}, {0x4024, 0x3F, 0}, {0x4025, 0x00, 0}, {0x4026, 0x5F, 0},
    {0x4027, 0x00, 0}, {0x4028, 0x2F, 0}, {0x4029, 0x00, 0}, {0x4074, 0x01, 0},
    // Standby cancel (18 ms), then XMSTA cancel (320 ms): the two settling waits
    {0x3000, 0x00, 18}, {0x3002, 0x00, 320},
};

}  // namespace

void camera_t::selfTest() {
    try_throw(iic.IsOpen(), "i2c not open");

    unsigned char buff[16];
    memset(buff, 0, sizeof(buff));
    auto err_code = iic.Read(dev_addr, (uint16_t)0x30, buff);

    try_throw(err_code == 0, "camera.iic.Read return error code = " + std::to_string(err_code));

    // bool has_value = false;
    // for (int i = 0; i < 16; i++) {
    //     if (buff[i] != 0) {
    //         has_value = true;
    //     }
    // }
    // try_throw(has_value, "camera check failed");
}

int camera_t::init() {
    // 记录初始化开始时间
    	init_start_time = std::chrono::high_resolution_clock::now();
    /* Common Config */
        printf("Mipi Camera Config Start\r\n");
        for (const RegOp &op : kInitRegs) {
            configReg(op.reg, op.val);
            if (op.delay_ms != 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(op.delay_ms));
            }
        }
        printf("Mipi Camera Config End\r\n");
        printf("-----------------------------------------------\r\n");
        printf("    ReadoutMode   :     H/V 2/2-line binning   \r\n");
    
        auto init_end_time = std::chrono::high_resolution_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            init_end_time - init_start_time).count();
        double elapsed_s = elapsed_ms / 1000.0;
    
        printf("    Elapsed time  :     %.3lfs                 \r\n", elapsed_s);
        printf("-----------------------------------------------\r\n");

    return OpenDevice();
}

int camera_t::OpenDevice() {
    fd = ::open("/dev/mipicam", O_RDWR);
    if (fd <= 0) {
        LOG_ERROR("Open /dev/mipicam Failed");
        return -1;
    }

    mipicam_reg_info_t reg_info;
    int err = ioctl(fd, MIPICAM_IOCTL_GET_REGISTER_INFO, &reg_info);
    if (err != 0) {
        close(fd);
        fd = 0;
        LOG_ERROR("MIPICAM_IOCTL_GET_REGISTER_INFO failed");
        return -1;
    }

    m_map_addr = mmap(NULL, reg_info.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, __off_t(reg_info.addr));
    m_map_size = reg_info.size;
    if (m_map_addr == MAP_FAILED) {
        close(fd);
        fd = 0;
        LOG_ERROR("mmap failed");
        return -1;
    }

    // 处理偏移
    // 这里要hardcode一下支持老设备
    uint32_t offset = reg_info.addr & ((1 << 12) - 1);
    if (reg_info.addr == 0x80100000 && reg_info.size == 0x00010000) {
        offset = 0xC00;
    }
    m_map_data_start = (char *)m_map_addr;
    m_map_data_start += offset;

    return 0;
}
void camera_t::configReg(uint16_t CfgAddr, uint8_t CfgValue) {
    uint8_t val_[3];
    val_[0] = (CfgAddr >> 8) & 0xFF;
    val_[1] = CfgAddr & 0xFF;
    val_[2] = CfgValue;
    try_throw(0 == iic.Write(dev_addr, val_), "i2c write failed");

    unsigned char buff[1];
    buff[0] = 0;
    iic.Read(dev_addr, *(uint16_t *)val_, buff);
    try_throw(buff[0] == val_[2], "i2c read-back failed");
}

int camera_t::WaitFinish() {
    mipicam_intr_info_t intr;
    intr.intr_flag = 0; // For Worst
    intr.timeout = 1000;
    int err = ioctl(fd, MIPICAM_IOCTL_WAIT_INTERRUPT, &intr);
    if (err) {
        printf("MIPICAM_IOCTL_WAIT_INTERRUPT failed\r\n");
        return -1;
    }
    return 0;
}

camera_t::~camera_t() {
    if (fd > 0) {
        munmap(m_map_addr, m_map_size);
        m_map_addr = nullptr;
        m_map_data_start = nullptr;
        close(fd);
    }
    fd = 0;
}
void camera_t::SubmitAndStart(uint64_t dma_rx_buf, void *) {
    *(uint32_t *)(m_map_data_start + CAMERA_BASE_ADDR) = dma_rx_buf;
    *(uint32_t *)(m_map_data_start + CAMERA_START) = 0x1;
}

// icamera::enumerate_device —— 上游定义在 icamera_enum.cpp，Makefile 不编那个文件，
// 照厂家 demo 调用会链接失败；这里补上定义，让 icamera.hpp 的声明有对应实现。
// 只支持 /dev/mipicam：上游还按 /dev/video 前缀建 usb_camera_t，USB 分支已随
// usb_camera.* 删除（需要时从厂家 demo 取回，并加进 ps/Makefile 的 VISION_SRC）。
std::shared_ptr<icamera> icamera::enumerate_device(const char *dev, const char *resolution) {
    (void)resolution; // camera_t 固定 1920x1080，忽略分辨率参数（与上游一致）
    if (strncmp(dev, "/dev/mipicam", 12) != 0) {
        fprintf(stderr, "only /dev/mipicam is supported, but got %s\n", dev);
        return nullptr;
    }
    auto cam = std::make_shared<camera_t>();
    cam->open(dev);
    if (!cam->is_open()) return nullptr;
    return cam;
}
