// test_vib: vibration analysis (app/vib_mon) and its work orders (app/ops) on synthetic sensor data — sample rate
// from the 16-bit time stamps (with their wraps), spectrum peak / amplitude / bands, axis choice, the low-frequency
// cut, and the warning / fault rules. No hardware: host (make -C ps build/test_vib) or the board.
#include <cmath>
#include <cstdio>
#include "ops.hpp"
#include "vib_mon.hpp"
using namespace des;
static int fails, passes;
#define CHECK(cond, what) do { if (cond) passes++; else { fails++; printf("FAIL: %s\n", what); } } while (0)
static bool near(double a, double b, double tol) { return a > b - tol && a < b + tol; }
constexpr double kPi = 3.14159265358979323846;

// n samples at fs (Hz) from t0 clocks: x = ax sin(2 pi fx t) + drift, y = ay sin(2 pi fy t), z = 1 g
static void feed(VibAnalyzer &an, int n, double fs, double ax, double fx, double ay, double fy, double drift_g = 0)
{
    const double clk = double(DES_FREE_CNT_HZ) / fs;
    for (int i = 0; i < n; i++) {
        const double t = i / fs;
        const uint64_t c = uint64_t(1000.0 + i * clk);
        des_vib_sample_t s;
        s.x = int16_t(std::lround((ax * std::sin(2 * kPi * fx * t) + drift_g * std::sin(2 * kPi * 2.0 * t)) / DES_VIB_G_PER_LSB));
        s.y = int16_t(std::lround(ay * std::sin(2 * kPi * fy * t) / DES_VIB_G_PER_LSB));
        s.z = int16_t(std::lround(1.0 / DES_VIB_G_PER_LSB));
        s.ts16 = uint16_t(c >> 4);
        an.push(s);
    }
}

int main()
{
    VibConfig c;   // fft 1024, fmin 5 Hz, bands 100 / 400 Hz
    {
        VibAnalyzer an(c);
        float pk = 0, pg = 0, bands[3];
        feed(an, 500, 3200, 0.3, 147, 0.05, 50);
        CHECK(!an.spectrum(0, pk, pg, bands), "fewer samples than the FFT length: no spectrum");
        feed(an, 2048, 3200, 0.3, 147, 0.05, 50);
        char m[160];
        snprintf(m, sizeof m, "sample rate from the 16-bit stamps (they wrap every 45 samples): %.2f Hz", an.rate_hz());
        CHECK(near(an.rate_hz(), 3200, 0.5), m);
        CHECK(an.busiest_axis() == 0, "busiest axis = x (0.3 g against 0.05 g and a constant z)");
        CHECK(an.spectrum(0, pk, pg, bands), "spectrum of x");
        snprintf(m, sizeof m, "peak %.2f Hz (147, bins of 3.1 Hz), amplitude %.4f g (0.3)", double(pk), double(pg));
        CHECK(near(pk, 147, 0.5) && near(pg, 0.3, 0.3 * 0.03), m);
        snprintf(m, sizeof m, "bands %.4f / %.4f / %.4f g: the sine's RMS (0.212) in 100..400 Hz", double(bands[0]),
                 double(bands[1]), double(bands[2]));
        CHECK(near(bands[1], 0.3 / std::sqrt(2.0), 0.212 * 0.03) && bands[0] < 0.01 && bands[2] < 0.01, m);
        CHECK(an.spectrum(1, pk, pg, bands) && near(pk, 50, 0.5) && near(pg, 0.05, 0.05 * 0.04) && bands[0] > bands[1],
              "y: 50 Hz, 0.05 g, in the low band");
    }
    {   // a large 2 Hz drift is below fmin: the peak is still the motor line
        VibAnalyzer an(c);
        float pk = 0, pg = 0, bands[3];
        feed(an, 1024, 1600, 0.1, 233, 0, 0, 0.5);
        CHECK(an.spectrum(0, pk, pg, bands) && near(pk, 233, 1.0) && near(an.rate_hz(), 1600, 0.3),
              "2 Hz drift of 0.5 g ignored below fmin: peak at 233 Hz (rate 1600)");
    }
    {   // work orders: warning after vib_s above vib_warn_g, fault above vib_fault_g, both recover 2 s below
        OpsConfig oc;   // 0.30 / 0.80 g, 2 s
        HealthMonitor hm(oc);
        WorkOrders wo;
        HealthInput in;
        in.vib_peak_hz = 150;
        auto run = [&](double t0, double t1, double g) {
            for (double t = t0; t < t1; t += 0.05) { in.now = t; in.vib_rms_g = g; hm.update(in, wo); }
        };
        run(0, 5, 0.1);
        CHECK(!wo.active(OPS_VIB) && !wo.active(OPS_VIB_HIGH), "0.1 g: no order");
        run(5, 6.5, 0.5);
        CHECK(!wo.active(OPS_VIB), "0.5 g for 1.5 s (< vib_s): no order yet");
        run(6.5, 8, 0.5);
        CHECK(wo.active(OPS_VIB) && !wo.active(OPS_VIB_HIGH), "0.5 g for 3 s: warning order, no fault");
        run(8, 11, 1.2);
        CHECK(wo.active(OPS_VIB_HIGH) && ops_code_info(OPS_VIB_HIGH).level == OpsLevel::Fault &&
              !ops_code_info(OPS_VIB_HIGH).stop, "1.2 g: fault order (fault level, does not stop the line)");
        run(11, 14, 0.1);
        CHECK(!wo.active(OPS_VIB) && !wo.active(OPS_VIB_HIGH), "back to 0.1 g for 3 s: both recovered");
        in.vib_rms_g = -1;
        CHECK(std::string(ops_code_info(OPS_VIB).key) == "vibration" && std::string(ops_code_info(OPS_TEST).key) == "test",
              "code table: vibration entries after the test entry, earlier numbers unchanged");
    }
    printf("test_vib: %d passed, %d failed\n", passes, fails);
    return fails;
}
