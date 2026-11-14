// vib_mon.cpp — see vib_mon.hpp
#include "vib_mon.hpp"
#include <cmath>
#include <complex>
#include <ctime>
namespace des {
namespace {
constexpr double kPi = 3.14159265358979323846;
// in-place iterative radix-2 FFT, n a power of two
void fft(std::vector<std::complex<float>> &a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * float(kPi) / float(len);
        const std::complex<float> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1, 0);
            for (size_t k = 0; k < len / 2; k++) {
                const std::complex<float> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}
}  // namespace

VibAnalyzer::VibAnalyzer(const VibConfig &c) : c_(c)
{
    const size_t cap = size_t(c.fft);
    for (auto &v : x_) v.assign(cap, 0);
    t_.assign(cap, 0);
}
void VibAnalyzer::push(const des_vib_sample_t &s)
{
    if (n_ == 0) now_ = uint64_t(s.ts16) * 16u;
    else now_ += uint64_t(uint16_t(s.ts16 - last16_)) * 16u;   // samples are < 14 ms apart: one wrap at most
    last16_ = s.ts16;
    x_[0][head_] = s.x; x_[1][head_] = s.y; x_[2][head_] = s.z; t_[head_] = now_;
    head_ = (head_ + 1) % t_.size();
    if (n_ < t_.size()) n_++;
}
double VibAnalyzer::rate_hz() const
{
    if (n_ < 16) return 0;
    const size_t cap = t_.size(), newest = (head_ + cap - 1) % cap, oldest = (head_ + cap - n_) % cap;
    const uint64_t dt = t_[newest] - t_[oldest];
    return dt ? double(n_ - 1) * DES_FREE_CNT_HZ / double(dt) : 0.0;
}
int VibAnalyzer::busiest_axis() const
{
    int best = -1;
    double best_v = -1;
    const size_t cap = t_.size();
    for (int a = 0; a < 3; a++) {
        double s = 0, s2 = 0;
        for (size_t i = 0; i < n_; i++) {
            const double v = x_[a][(head_ + cap - n_ + i) % cap];
            s += v; s2 += v * v;
        }
        const double var = n_ ? s2 / n_ - (s / n_) * (s / n_) : 0;
        if (var > best_v) { best_v = var; best = a; }
    }
    return best;
}
bool VibAnalyzer::spectrum(int axis, float &peak_hz, float &peak_g, float band_g[3]) const
{
    const size_t n = t_.size();
    const double fs = rate_hz();
    if (axis < 0 || axis > 2 || n_ < n || fs <= 0) return false;
    std::vector<std::complex<float>> a(n);
    double mean = 0;
    for (size_t i = 0; i < n; i++) mean += x_[axis][(head_ + i) % n];
    mean /= double(n);
    for (size_t i = 0; i < n; i++) {   // oldest first (head_ is the oldest once the ring is full), Hann window
        const double w = 0.5 - 0.5 * std::cos(2 * kPi * double(i) / double(n));
        a[i] = std::complex<float>(float((x_[axis][(head_ + i) % n] - mean) * w), 0.0f);
    }
    fft(a);
    // one-sided amplitude of a sine: 2 |X| / sum(w) = 4 |X| / n for the Hann window
    const double df = fs / double(n), scale = 4.0 / double(n) * DES_VIB_G_PER_LSB;
    size_t kmin = size_t(std::ceil(c_.fmin_hz / df));
    if (kmin < 1) kmin = 1;
    size_t kp = 0;
    double best = -1;
    double e[3] = {0, 0, 0};
    for (size_t k = kmin; k < n / 2; k++) {
        const double m = std::abs(a[k]);
        if (m > best) { best = m; kp = k; }
        const double f = k * df;
        // Parseval with the Hann window (power gain 3/8): power of bin k of a one-sided spectrum
        const double p = 2.0 * m * m / (double(n) * double(n) * 0.375);
        e[f < c_.band_hz[0] ? 0 : f < c_.band_hz[1] ? 1 : 2] += p;
    }
    if (kp == 0) return false;
    double d = 0;   // parabolic interpolation on the log magnitudes around the peak
    if (kp > kmin && kp + 1 < n / 2) {
        const double l = std::log(std::abs(a[kp - 1]) + 1e-9), c = std::log(best + 1e-9),
                     r = std::log(std::abs(a[kp + 1]) + 1e-9);
        const double den = l - 2 * c + r;
        if (den < 0) d = 0.5 * (l - r) / den;
    }
    peak_hz = float((double(kp) + d) * df);
    peak_g = float(best * scale);
    for (int b = 0; b < 3; b++) band_g[b] = float(std::sqrt(e[b]) * DES_VIB_G_PER_LSB);
    return true;
}

std::string VibMonitor::open(des_hw_t &hw, const VibConfig &c, const std::string &log_dir)
{
    close();
    if (!des_vib_present(&hw)) return "bitstream without des_vib (v17 batch 2)";
    if (c.fft < 64 || c.fft > 4096 || (c.fft & (c.fft - 1))) return "vib.fft must be a power of two 64..4096";
    const int rc = des_vib_init(&hw, c.range, c.rate);
    if (rc == DES_E_ID) return "ADXL345 not answering (DEVID != 0xE5): wiring of J12 PIN22~25?";
    if (rc != DES_OK) return std::string("sensor set-up failed: ") + des_strerror(rc);
    if (des_vib_stream(&hw, 1, unsigned(c.window)) != DES_OK) return "vib.window must be 1..2048";
    hw_ = &hw;
    c_ = c;
    an_ = new VibAnalyzer(c);
    buf_.resize(DES_VIB_FIFO_DEPTH);
    v_ = VibView();
    v_.on = true;
    if (c.log && !log_dir.empty()) {
        char name[64];
        const time_t t = time(nullptr);
        strftime(name, sizeof name, "/vib_%Y%m%d_%H%M%S.csv", gmtime(&t));
        csv_ = fopen((log_dir + name).c_str(), "w");
        if (csv_) fprintf(csv_, "t_s,rate_hz,rms_x_g,rms_y_g,rms_z_g,rms_g,mean_z_g,peak_axis,peak_hz,peak_g,"
                                "band_low_g,band_mid_g,band_high_g,samples,dropped\n");
    }
    return "";
}
void VibMonitor::poll(double now)
{
    if (!hw_) return;
    if (t0_ < 0) t0_ = now;
    int n;
    while ((n = des_vib_drain(hw_, buf_.data(), int(buf_.size()))) > 0)
        for (int i = 0; i < n; i++) an_->push(buf_[size_t(i)]);
    const uint32_t cnt = des_rd(hw_, DES_VIB_COUNT);
    v_.samples = cnt & 0xFFFFu;
    v_.dropped = cnt >> 16;
    des_vib_stats_t st;
    if (des_vib_stats(hw_, &st) == DES_OK && st.windows != last_win_) {   // PL feature extraction: RMS per axis
        last_win_ = st.windows;
        double t2 = 0;
        for (int a = 0; a < 3; a++) {
            v_.rms_g[a] = float(st.rms_g[a]); v_.mean_g[a] = float(st.mean_g[a]);
            t2 += st.rms_g[a] * st.rms_g[a];
        }
        v_.rms_g_total = float(std::sqrt(t2));
    }
    if (now - last_spec_ >= c_.spectrum_s) {
        last_spec_ = now;
        v_.rate_hz = float(an_->rate_hz());
        const int ax = an_->busiest_axis();
        if (an_->spectrum(ax, v_.peak_hz, v_.peak_g, v_.band_g)) {
            v_.peak_axis = ax;
            if (csv_) {
                fprintf(csv_, "%.2f,%.1f,%.5f,%.5f,%.5f,%.5f,%.4f,%d,%.1f,%.5f,%.5f,%.5f,%.5f,%u,%u\n", now - t0_,
                        double(v_.rate_hz), double(v_.rms_g[0]), double(v_.rms_g[1]), double(v_.rms_g[2]),
                        double(v_.rms_g_total), double(v_.mean_g[2]), ax, double(v_.peak_hz), double(v_.peak_g),
                        double(v_.band_g[0]), double(v_.band_g[1]), double(v_.band_g[2]), v_.samples, v_.dropped);
                fflush(csv_);
            }
        }
    }
}
void VibMonitor::close()
{
    if (hw_) des_vib_stream(hw_, 0, unsigned(c_.window));
    hw_ = nullptr;
    delete an_;
    an_ = nullptr;
    if (csv_) fclose(csv_);
    csv_ = nullptr;
    v_.on = false;
}
}  // namespace des
