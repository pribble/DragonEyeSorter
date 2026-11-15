/* test_hw_v17: des_hw v17 calls (des_bpi belt speed PI, des_ptrig position trigger, des_pwm slew) against a fake
 * register window — the packing of every register, the range checks, and that nothing is written on a bitstream
 * without the blocks. Pure logic: host (gcc -Isrc/hw tests/test_hw_v17.c src/hw/des_hw.c) or the board. */
#include <stdio.h>
#include <string.h>
#include "des_hw.h"

static int fails, passes;
#define CHECK(cond, what) do { if (cond) passes++; else { fails++; printf("FAIL: %s\n", what); } } while (0)

static uint32_t regs[DES_PL_SPAN / 4];
#define R(off) regs[(off) / 4]

static void fake_v17(des_hw_t *h, uint32_t version)
{
    memset(regs, 0, sizeof regs);
    h->regs = regs; h->fd = -1; h->version = version;
    R(DES_BPI_ID) = DES_BPI_ID_VALUE;
    R(DES_PTG_ID) = DES_PTG_ID_VALUE;
    R(DES_VIB_ID) = DES_VIB_ID_VALUE;
}
static int near(double a, double b, double tol) { return a > b - tol && a < b + tol; }

int main(void)
{
    des_hw_t h;

    /* ---- a v16 bitstream: every call refuses, nothing written ---- */
    fake_v17(&h, 16);
    R(DES_BPI_ID) = R(DES_PTG_ID) = 0xDEADBEEF;     /* what the v16 read mux returns there */
    des_bpi_cfg_t cfg = { 0.5, 0.1, 0.08, 0.0, 10, 100, 5 };
    des_ptrig_cfg_t tc = { 0, 0, DES_PTG_ACT_PWM0, 1100, 1 };
    des_bpi_t b; des_ptrig_t t;
    CHECK(!des_bpi_present(&h) && !des_ptrig_present(&h), "v16: blocks reported absent");
    CHECK(des_bpi_config(&h, &cfg) == DES_E_ARG && des_bpi_set(&h, 1000) == DES_E_ARG && des_bpi_enable(&h, 1) == DES_E_ARG &&
          des_bpi_clear(&h) == DES_E_ARG && des_bpi_read(&h, &b) == DES_E_ARG, "v16: des_bpi calls refuse");
    CHECK(des_ptrig_arm(&h, 0, 100, &tc) == DES_E_ARG && des_ptrig_disarm(&h, -1) == DES_E_ARG &&
          des_ptrig_read(&h, &t) == DES_E_ARG, "v16: des_ptrig calls refuse");
    CHECK(des_pwm_slew(&h, 40) == DES_E_ARG, "v16: des_pwm_slew refuses");
    CHECK(R(DES_BPI_CTRL) == 0 && R(DES_BPI_SET) == 0 && R(DES_BPI_KP) == 0 && R(DES_PTG_CFG(0)) == 0 && R(DES_PTG_CTRL) == 0 &&
          R(DES_PWM_SLEW) == 0, "v16: nothing written");
    /* a v17 version number without the ID (window aliased): still absent */
    fake_v17(&h, 17); R(DES_BPI_ID) = 0;
    CHECK(!des_bpi_present(&h) && des_bpi_enable(&h, 1) == DES_E_ARG && R(DES_BPI_CTRL) == 0, "v17 without the des_bpi ID: absent");

    /* ---- des_bpi ---- */
    fake_v17(&h, 17);
    CHECK(des_bpi_present(&h) && des_ptrig_present(&h), "v17: both present");
    CHECK(des_bpi_config(&h, &cfg) == DES_OK, "config accepted");
    CHECK(R(DES_BPI_KP) == 2048 && R(DES_BPI_KI) == 6554, "KP 0.5 -> 2048 (Q4.12), KI 0.1 -> 6554 (Q0.16)");
    CHECK(R(DES_BPI_SMAX) == 5243 && R(DES_BPI_DB) == 0, "limit 8 % -> 5243, dead band 0");
    CHECK(R(DES_BPI_PERIOD) == 750000 && R(DES_BPI_TIMEOUT) == 7500000 && R(DES_BPI_SETTLE) == 5,
          "10 ms -> 750000 clocks, 100 ms -> 7500000, settle 5");
    /* 50 mm/s at 0.049227 mm per count = 1015.70 counts/s -> 1015.70 * 2^32 / 75e6 = 58165 */
    CHECK(des_bpi_set(&h, 50.0 / 0.049227) == DES_OK && R(DES_BPI_SET) == 58165, "set-point 50 mm/s -> 58165 (Q0.32 per clock)");
    CHECK(des_bpi_set(&h, -50.0 / 0.049227) == DES_OK && R(DES_BPI_SET) == 58165, "set-point takes the magnitude");
    CHECK(des_bpi_set(&h, 292000.0) == DES_E_ARG && des_bpi_set(&h, -3e5) == DES_E_ARG && R(DES_BPI_SET) == 58165 &&
          des_bpi_set(&h, 291999.0) == DES_OK && R(DES_BPI_SET) < (1u << 24), "set-point beyond the PL's 24 bits refused");
    des_bpi_set(&h, 50.0 / 0.049227);
    CHECK(near(des_enc_cps(58165), 1015.70, 0.02), "58165 reads back as 1015.70 counts/s");
    des_bpi_enable(&h, 1); CHECK(R(DES_BPI_CTRL) == 1, "enable -> CTRL 1");
    des_bpi_clear(&h);     CHECK(R(DES_BPI_CTRL) == 3, "clear keeps the enable bit and sets b1");
    des_bpi_enable(&h, 0); CHECK(R(DES_BPI_CTRL) == 0, "disable -> CTRL 0");
    /* out-of-range settings: refused as a whole, registers keep their values */
    {
        des_bpi_cfg_t bad[] = { { 16.0, 0.1, 0.08, 0, 10, 100, 5 }, { 0.5, 1.0, 0.08, 0, 10, 100, 5 }, { 0.5, 0.1, 0.26, 0, 10, 100, 5 },
                                { 0.5, 0.1, 0.08, 1.0, 10, 100, 5 }, { 0.5, 0.1, 0.08, 0, 0, 100, 5 }, { 0.5, 0.1, 0.08, 0, 10, 5, 5 },
                                { 0.5, 0.1, 0.08, 0, 10, 100, 256 }, { -0.1, 0.1, 0.08, 0, 10, 100, 5 },
                                { 0.5, 0.1, 0.08, 0, 201, 201, 5 }, { 0.5, 0.1, 0.08, 0, 10, 201, 5 } };
        int refused = 0;
        for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) refused += des_bpi_config(&h, &bad[i]) == DES_E_ARG;
        CHECK(refused == 10 && R(DES_BPI_KP) == 2048 && R(DES_BPI_SMAX) == 5243, "10 out-of-range configs refused, registers untouched");
        des_bpi_cfg_t edge = { 0.5, 0.1, 0.08, 0, 200, 200, 5 };
        CHECK(des_bpi_config(&h, &edge) == DES_OK && R(DES_BPI_PERIOD) == 15000000 && R(DES_BPI_TIMEOUT) < (1u << 24),
              "200 ms accepted: 15000000 clocks fit the PL's 24 bits");
    }
    /* read-back scaling, including the sign of the Q0.16 fields */
    R(DES_BPI_STATUS) = DES_BPI_ST_ENABLED | DES_BPI_ST_SAT | DES_BPI_ST_CRUISE | DES_BPI_ST_DOWN;
    R(DES_BPI_MEAS) = 55839;                 /* 96 % of the set-point */
    R(DES_BPI_ERR) = 2621;                   /* +4 % */
    R(DES_BPI_SCALE) = (uint32_t)-5243;      /* -8 % */
    R(DES_BPI_INTEG) = (uint32_t)-3277;      /* -5 % */
    R(DES_BPI_MEAS_CNT) = 77; R(DES_BPI_STEP_CNT) = 66;
    CHECK(des_bpi_read(&h, &b) == DES_OK, "read");
    CHECK(b.status == (DES_BPI_ST_ENABLED | DES_BPI_ST_SAT | DES_BPI_ST_CRUISE | DES_BPI_ST_DOWN) && near(b.set_cps, 1015.70, 0.02) &&
          near(b.meas_cps, 975.08, 0.02), "read: status, set 1015.70, meas 975.08 counts/s");
    CHECK(near(b.err, 0.04, 1e-4) && near(b.scale, -0.08, 1e-4) && near(b.integ, -0.05, 1e-4), "read: err +4 %, scale -8 %, integral -5 %");
    CHECK(b.meas_cnt == 77 && b.step_cnt == 66, "read: counters");

    /* ---- des_ptrig ---- */
    CHECK(des_ptrig_cfg_word(&tc, 1) == 0x044C0091u, "CFG word: 1100 us, TRIG_OUT, PWM ch0, stepper, rising, armed = 0x044C0091");
    CHECK(des_ptrig_arm(&h, 1, -123456, &tc) == DES_OK && R(DES_PTG_THR(1)) == (uint32_t)-123456 && R(DES_PTG_CFG(1)) == 0x044C0091u,
          "arm entry 1: THR at 0x850 (signed), CFG at 0x854");
    CHECK(DES_PTG_THR(1) == 0x850 && DES_PTG_CFG(0) == 0x844 && DES_PTG_TS(1) == 0x858, "entry offsets");
    {
        des_ptrig_cfg_t e = { 1, 1, DES_PTG_ACT_PWM1, 0, 0 };
        CHECK(des_ptrig_cfg_word(&e, 0) == 0x00000026u, "CFG word: falling, encoder, PWM ch1, not armed = 0x26");
        des_ptrig_cfg_t bad1 = { 0, 0, 3, 0, 0 }, bad2 = { 0, 0, DES_PTG_ACT_PWM1, 2551, 0 };
        CHECK(des_ptrig_arm(&h, 2, 0, &tc) == DES_E_ARG && des_ptrig_arm(&h, -1, 0, &tc) == DES_E_ARG &&
              des_ptrig_arm(&h, 0, 0, &bad1) == DES_E_ARG && des_ptrig_arm(&h, 0, 0, &bad2) == DES_E_ARG && R(DES_PTG_CFG(0)) == 0,
              "arm refuses entry 2 / -1, action 3, width 2551");
    }
    des_ptrig_disarm(&h, 1);
    CHECK(R(DES_PTG_CFG(1)) == 0x044C0090u, "disarm one entry: CFG rewritten without the arm bit");
    des_ptrig_disarm(&h, -1);
    CHECK(R(DES_PTG_CTRL) == 1, "disarm all: CTRL b0");
    R(DES_PTG_STATUS) = 0x00020201u;   /* armed 0, fired 1, late 1 */
    R(DES_PTG_FIRE_CNT) = 9; R(DES_PTG_TS(1)) = 1234567;
    CHECK(des_ptrig_read(&h, &t) == DES_OK && t.fire_cnt == 9, "read");
    CHECK(t.e[0].armed && !t.e[1].armed && !t.e[0].fired && t.e[1].fired && t.e[1].late && !t.e[0].late && t.e[1].ts == 1234567,
          "read: armed / fired / late bits per entry, time stamp");

    /* ---- des_pwm slew ---- */
    CHECK(des_pwm_slew(&h, 40) == DES_OK && R(DES_PWM_SLEW) == 40 && des_pwm_slew(&h, -1) == DES_E_ARG &&
          des_pwm_slew(&h, 4096) == DES_E_ARG && R(DES_PWM_SLEW) == 40, "PWM slew 40 written; -1 / 4096 refused");
    /* (a build without SLEW reads the register back as 0 and the call then reports DES_E_ARG: not reproducible on this
     * fake window, which stores whatever is written) */

    /* ---- des_vib ---- */
    {
        des_vib_sample_t sb[4]; des_vib_stats_t vs; uint8_t byte = 0;
        fake_v17(&h, 16); R(DES_VIB_ID) = 0xDEADBEEF;
        CHECK(!des_vib_present(&h) && des_vib_init(&h, 1, 0x0F) == DES_E_ARG && des_vib_stream(&h, 1, 64) == DES_E_ARG &&
              des_vib_drain(&h, sb, 4) == DES_E_ARG && des_vib_stats(&h, &vs) == DES_E_ARG && R(DES_VIB_CTRL) == 0,
              "v16: des_vib absent, calls refuse, nothing written");
        fake_v17(&h, 17);
        CHECK(des_vib_present(&h), "v17 with the VIB1 ID: present");
        /* the fake window keeps the word written at XFER and reads it back as the status: bit 8 (busy) must be 0 */
        CHECK(des_vib_xfer(&h, 0, 0x2C, 0x0F, NULL) == DES_OK && R(DES_VIB_XFER) == 0x2C0Fu, "write transfer word 0x2C0F");
        CHECK(des_vib_xfer(&h, 1, 0x00, 0, &byte) == DES_OK && R(DES_VIB_XFER) == 0x8000u && byte == 0x00,
              "read transfer word 0x8000, byte from [7:0]");
        CHECK(des_vib_xfer(&h, 0, 0x40, 0, NULL) == DES_E_ARG && des_vib_xfer(&h, 0, 0x2D, 0x100, NULL) == DES_E_ARG,
              "register 0x40 / data 0x100 refused");
        R(DES_VIB_XFER) = 0x100u;   /* PL reports busy forever */
        CHECK(des_vib_xfer(&h, 1, 0x00, 0, &byte) == DES_E_BUSY, "a transfer that never finishes: DES_E_BUSY");
        R(DES_VIB_XFER) = 0;
        /* on the fake window init stops at its first transfer (0x2D00 reads back with bit 8 = busy): it must fail
         * and leave streaming off */
        R(DES_VIB_CTRL) = 1;
        CHECK(des_vib_init(&h, 1, 0x0F) != DES_OK && R(DES_VIB_CTRL) == 0, "init that fails: error returned, streaming off");
        R(DES_VIB_XFER) = 0;
        CHECK(des_vib_init(&h, 4, 0x0F) == DES_E_ARG && des_vib_init(&h, 1, 0x10) == DES_E_ARG, "init: range 4 / rate 0x10 refused");
        CHECK(des_vib_stream(&h, 1, 2048) == DES_OK && R(DES_VIB_WIN) == 0 && R(DES_VIB_CTRL) == 3u, "stream on, window 2048 -> WIN 0, CTRL 3");
        CHECK(des_vib_stream(&h, 0, 64) == DES_OK && R(DES_VIB_WIN) == 64 && R(DES_VIB_CTRL) == 2u, "stream off, window 64, clear");
        CHECK(des_vib_stream(&h, 1, 0) == DES_E_ARG && des_vib_stream(&h, 1, 2049) == DES_E_ARG, "window 0 / 2049 refused");
        R(DES_VIB_STATUS) = 3u << 16;
        R(DES_VIB_HEAD0) = ((uint32_t)(uint16_t)-256 << 16) | 100u; R(DES_VIB_HEAD1) = (0xBEEFu << 16) | 256u;
        CHECK(des_vib_drain(&h, sb, 2) == 2 && sb[0].x == 100 && sb[0].y == -256 && sb[0].z == 256 && sb[0].ts16 == 0xBEEF,
              "drain: level 3, max 2 -> 2 samples, signed x / y / z, time stamp");
        /* window of 64: x 10 +- 4 (mean 10, rms 4 counts), y constant -20, z 256 */
        R(DES_VIB_WIN) = 64; R(DES_VIB_WIN_CNT) = 7;
        R(DES_VIB_SUM0) = 640; R(DES_VIB_SUM0 + 4) = (uint32_t)-1280; R(DES_VIB_SUM0 + 8) = 16384;
        R(DES_VIB_SQ0) = (64 * (100 + 16)) >> 10; R(DES_VIB_SQ0 + 4) = (64 * 400) >> 10; R(DES_VIB_SQ0 + 8) = (64 * 65536) >> 10;
        CHECK(des_vib_stats(&h, &vs) == DES_OK && vs.windows == 7 && vs.n == 64, "stats: window 7 of 64 samples");
        CHECK(near(vs.mean_g[0], 10 * 0.0039, 1e-6) && near(vs.mean_g[1], -20 * 0.0039, 1e-6) && near(vs.mean_g[2], 256 * 0.0039, 1e-6),
              "stats: means 0.039 / -0.078 / 0.9984 g");
        CHECK(vs.rms_g[1] < 1e-9 && vs.rms_g[2] < 1e-9 && vs.rms_g[0] > 0.0, "stats: constant axes have no AC part");
    }

    printf("test_hw_v17: %d passed, %d failed\n", passes, fails);
    return fails;
}
