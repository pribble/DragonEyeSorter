// vib_mon.hpp — 振动监测：PL des_vib 采样（带数据就绪时间戳）+ PL 窗口统计 → 本模块算频谱主频与频段能量 → 运维分级
// （赛题拓展 1「频谱分析」、拓展 2「振动特征提取」、拓展 4「故障分级」; v17 batch 2, 2026-11-15）.
//
// VibAnalyzer is pure computation (host-testable: tests/test_vib.cpp): it keeps the newest samples, rebuilds their
// times from the 16-bit time stamps (FREE_CNT[19:4]), measures the real sample rate and computes a Hann-windowed FFT of
// the axis with the most AC energy: peak frequency (parabolic interpolation), its amplitude and three band RMS values.
// VibMonitor owns the sensor in des_app: set-up, draining the PL FIFO every cycle, the PL window statistics (RMS per
// axis), and a CSV next to the part records. Thresholds and work orders live in ops (HealthMonitor), as for every
// other fault.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "des_hw.h"
namespace des {
struct VibConfig {
    bool enabled = false;        // ADXL345 on J12 PIN22~25 and a bitstream with des_vib (v17 batch 2)
    int range = 1;               // 0 = +-2 g, 1 = +-4 g, 2 = +-8 g, 3 = +-16 g (full resolution: 3.9 mg per count in all)
    int rate = 0x0F;             // ADXL345 BW_RATE code: 0x0F 3200 Hz, 0x0E 1600, 0x0D 800, 0x0C 400
    int window = 1024;           // PL statistics window in samples (1..2048)
    int fft = 1024;              // FFT length, power of two 64..4096 (1024 at 3200 Hz = 0.32 s, 3.1 Hz per bin)
    float fmin_hz = 5;           // the peak search starts here (below: drift, belt start / stop)
    float spectrum_s = 0.5f;     // spectrum refresh period
    float band_hz[2] = {100, 400};   // band edges: [fmin, b0), [b0, b1), [b1, fs/2)
    bool log = true;             // vib_<session>.csv next to the part records, one row per spectrum
};
struct VibView {
    bool on = false;
    float rate_hz = 0;           // measured from the time stamps
    float rms_g[3] = {0, 0, 0};  // AC RMS per axis (PL window)
    float rms_g_total = -1;      // sqrt of the sum of squares of the three; < 0 = no window yet
    float mean_g[3] = {0, 0, 0};
    float peak_hz = 0, peak_g = 0;   // spectrum peak (amplitude of that sine, g)
    int peak_axis = -1;
    float band_g[3] = {0, 0, 0};
    uint32_t samples = 0, dropped = 0;   // PL counters (16-bit, wrap)
};
class VibAnalyzer {
public:
    explicit VibAnalyzer(const VibConfig &c);
    void push(const des_vib_sample_t &s);     // in arrival order
    size_t size() const { return n_; }
    double rate_hz() const;                   // 0 until 16 samples
    // spectrum of `axis` over the newest fft samples; false while not enough samples or no rate yet
    bool spectrum(int axis, float &peak_hz, float &peak_g, float band_g[3]) const;
    int busiest_axis() const;                 // the axis with the largest AC RMS over the newest fft samples
private:
    VibConfig c_;
    std::vector<int16_t> x_[3];
    std::vector<uint64_t> t_;                 // absolute time in 75 MHz clocks (unwrapped)
    size_t head_ = 0, n_ = 0;
    uint16_t last16_ = 0;
    uint64_t now_ = 0;
};
class VibMonitor {
public:
    ~VibMonitor() { close(); }
    // set-up and start; log_dir empty = no CSV. Returns "" or what went wrong
    std::string open(des_hw_t &hw, const VibConfig &c, const std::string &log_dir);
    void poll(double now);                    // every control cycle
    const VibView &view() const { return v_; }
    void close();
private:
    des_hw_t *hw_ = nullptr;
    VibConfig c_;
    VibAnalyzer *an_ = nullptr;
    VibView v_;
    uint32_t last_win_ = 0;
    double last_spec_ = -1, t0_ = -1;
    FILE *csv_ = nullptr;
    std::vector<des_vib_sample_t> buf_;
};
}  // namespace des
