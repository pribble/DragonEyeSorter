/* des_hw: DragonEyeSorter PL access layer over /dev/mem (des_regs / servo_bus / stepper_ctrl).
 * Register map: des_regs.h (same commit as docs/PL设计.md).
 * Ownership: the servo bus and the stepper are single-master resources. In the running system only the application
 * touches them; do not run `desreg` write commands while des_app is running.
 */
#ifndef DES_HW_H
#define DES_HW_H
#include <stdint.h>
#include "des_regs.h"
#ifdef __cplusplus
extern "C" {
#endif

enum {
    DES_OK = 0,
    DES_E_OPEN = -1,       /* /dev/mem open or mmap failed */
    DES_E_ID = -2,         /* DES_ID mismatch: bitstream lacks des_regs (window aliases camera DMA regs) */
    DES_E_TIMEOUT = -3,    /* servo did not answer within TIMEOUT_US */
    DES_E_RXERR = -4,      /* reply framing / checksum error */
    DES_E_BUSY = -5,       /* servo_bus never reported done (PL stuck?) */
    DES_E_ARG = -6,
    DES_E_CRC = -7,        /* RS485 frame with a bad CRC (v16) */
    DES_E_VERIFY = -8,     /* a servo never confirmed a command (read-back still wrong after the re-sends) */
};

typedef struct { volatile uint32_t *regs; int fd; uint32_t version; } des_hw_t;   /* version 0 when ID check failed */

int  des_hw_open(des_hw_t *h);           /* maps the window; returns DES_E_ID but stays mapped on ID mismatch */
void des_hw_close(des_hw_t *h);
const char *des_strerror(int rc);

static inline uint32_t des_rd(const des_hw_t *h, uint32_t off) { return h->regs[off / 4]; }
static inline void des_wr(des_hw_t *h, uint32_t off, uint32_t v) { h->regs[off / 4] = v; }
static inline uint32_t des_now(const des_hw_t *h) { return des_rd(h, DES_REG_FREE_CNT); }
static inline double des_ticks_us(uint32_t ticks) { return ticks * 1e6 / DES_FREE_CNT_HZ; }

/* ---- camera frame counters (ISP start of frame) ---- */
typedef struct { uint32_t cnt; uint32_t period; } des_frame_t;   /* SOF count; ticks between the last two starts */
void des_frame_read(const des_hw_t *h, des_frame_t *f);

/* last completed DMA capture, latched by the PL (VERSION >= 5); DES_E_ARG on older bitstreams */
typedef struct {
    uint32_t seq, frame, t0, t1;
    int32_t pos0, pos1;    /* belt position (steps) at writer start / done */
    int32_t enc0, enc1;    /* belt encoder count at writer start / done, same clock edges (v16; 0 before) */
} des_capture_t;
#define DES_VERSION_CAPTURE_ENC 16u
#define DES_VERSION_CAPTURE 5u
int des_capture_read(const des_hw_t *h, des_capture_t *c);

/* ---- PL vision path (des_vision, VERSION >= 6) ---- */
enum { DES_VIS_CAMERA = 0, DES_VIS_GRAY, DES_VIS_GAUSS, DES_VIS_BINARY, DES_VIS_SOBEL, DES_VIS_EDGE };
typedef struct {
    uint32_t seq;          /* frames with published statistics */
    uint32_t area, edges;  /* bright / edge pixels in the ROI of that frame (full-resolution equivalent) */
    int x0, x1, y0, y1;    /* bounding box of the bright pixels (x0 > x1: none) */
    double cx, cy;         /* centroid (px), 0 without bright pixels */
    uint32_t cycles;       /* pixel clocks per frame */
    int mode, full, thr;   /* settings in use */
    int lanes;             /* pixels filtered per beat (4 = every pixel, 2 = every other: counts scaled by 2) */
} des_vis_t;
#define DES_VERSION_VISION 6u
/* settings take effect at the next frame start; DES_E_ARG on bitstreams before v6 */
int des_vis_config(des_hw_t *h, int mode, int full, int thr, int edge_thr);
int des_vis_roi(des_hw_t *h, int x0, int y0, int x1, int y1);   /* pixels, inclusive; also applies */
int des_vis_read(const des_hw_t *h, des_vis_t *v);

/* ---- belt-end flaps (des_pwm, VERSION >= 7; the lamp output was dropped in v10) ---- */
#define DES_VERSION_PWM 7u
int des_pwm_set(des_hw_t *h, int ch, int us);       /* pulse width in us (0 = no pulses), next period */
int des_pwm_enable(des_hw_t *h, unsigned ch_mask);  /* bit 0 = flap 1, bit 1 = flap 2 */

/* ---- field inputs (des_io, VERSION >= 11): e-stop latch, IR1 / LIMIT1, belt encoder ---- */
#define DES_VERSION_IO 11u
typedef struct {
    uint32_t status;       /* DES_IO_ST_* bits */
    uint32_t ir1_cnt, ir1_enc;
    int32_t  ir1_pos;
    int32_t  enc, enc_zpos, enc_speed;   /* counts; speed per 10 ms */
    uint32_t enc_err, enc_zcnt;
} des_io_t;
int des_io_read(const des_hw_t *h, des_io_t *io);   /* DES_E_ARG before v11 */
int des_estop_latched(const des_hw_t *h);          /* 1 latched, 0 not, DES_E_ARG before v11 */
int des_estop_clear(des_hw_t *h);                  /* clears the latch unless the button is still pressed; returns
                                                      1 if it is still latched afterwards */
int des_enc_zero(des_hw_t *h);

/* ---- RS485 Modbus-RTU (des_485, VERSION >= 16): PL adds / checks the CRC16, drives DE, frames by t3.5 ---- */
#define DES_VERSION_485 16u
typedef struct {
    uint8_t  n;            /* reply bytes in raw[]: CRC removed on success; on DES_E_CRC / DES_E_RXERR the bytes as
                              received (CRC included, at most 64) for debugging */
    uint8_t  raw[64];      /* [addr, func, data...] */
    uint32_t status;       /* DES_485_STATUS as read at the end (debug) */
    uint32_t t_tx0, t_tx1; /* FREE_CNT: first start bit / end of the last stop bit of the request */
    uint32_t t_rx0, t_rx1; /* FREE_CNT: first start bit / last byte of the reply; latency = t_rx0 - t_tx1 */
} des_485_result_t;
/* baud (1200..4000000), parity 0 none / 1 even / 2 odd, reply timeout (us, 0 = keep). Sets t3.5 = 3.5 x 11 bit times
   (fixed 1750 us above 19200 baud, Modbus spec). DES_E_ARG before v16 or out of range. */
int des_485_config(des_hw_t *h, uint32_t baud, int parity, uint32_t timeout_us);
/* pdu = [addr, func, data...] without CRC, n = 1..62; want_reply = 0 for broadcasts (addr 0). Blocks until the
   reply frame ended (t3.5 silence) or the timeout. DES_OK / DES_E_TIMEOUT / DES_E_CRC / DES_E_RXERR (parity / stop
   error, or a reply longer than 64 bytes) / DES_E_BUSY (aborted: bus never quiet, e.g. A/B swapped) / DES_E_ARG.
   Modbus exception replies (func | 0x80) are returned as DES_OK: the caller decodes the PDU. Thread safe. */
int des_485_xfer(des_hw_t *h, const uint8_t *pdu, int n, int want_reply, des_485_result_t *r);
/* frame received while idle (another master on the bus): 1 and *r filled (then acknowledged), 0 none,
   DES_E_CRC / DES_E_RXERR for a damaged one (still acknowledged), DES_E_ARG before v16. Non-blocking. */
int des_485_listen(des_hw_t *h, des_485_result_t *r);

/* ---- v17: flap PWM slew, belt speed PI (des_bpi), belt position compare trigger (des_ptrig) ----
 * All three are off after reset and the bitstream then behaves as v16. Every call returns DES_E_ARG on a bitstream
 * without the block, so callers can probe with the *_present functions and fall back. */
#define DES_VERSION_V17 17u
int des_pwm_slew(des_hw_t *h, int us_per_period);   /* output width walks to CHx by this much per period; 0 = at once */

/* encoder speed [counts/s] <-> Q0.32 counts per 75 MHz clock (the format of DES_BPI_SET / MEAS) */
static inline uint32_t des_enc_inc(double counts_per_s)
{
    return (uint32_t)((counts_per_s < 0 ? -counts_per_s : counts_per_s) * 4294967296.0 / DES_FREE_CNT_HZ + 0.5);
}
static inline double des_enc_cps(uint32_t inc) { return inc * (double)DES_FREE_CNT_HZ / 4294967296.0; }

#define DES_BPI_ST_ENABLED 0x01u
#define DES_BPI_ST_STEPPED 0x02u   /* the last window moved the output */
#define DES_BPI_ST_STALL   0x04u   /* no encoder event for TIMEOUT: belt standing or blocked */
#define DES_BPI_ST_SAT     0x08u   /* output at the limit */
#define DES_BPI_ST_REF     0x10u   /* a reference edge is held (measuring) */
#define DES_BPI_ST_CRUISE  0x20u   /* stepper in cruise right now */
#define DES_BPI_ST_DOWN    0x40u   /* the encoder count went down over the last window */
typedef struct {
    double kp;            /* output per unit of relative error, 0 .. 15.99 */
    double ki;            /* integral gain per window, 0 .. 0.9999 */
    double limit;         /* output limit as a fraction of the speed, 0 .. 0.25 (PL caps it there) */
    double deadband;      /* relative error left alone, 0 .. 0.9999 */
    unsigned period_ms;   /* shortest measuring window, 1 .. 200 (24-bit clock count in the PL) */
    unsigned timeout_ms;  /* no encoder event for this long = standing, period_ms .. 200 */
    int settle;           /* cruise windows skipped after the cruise speed is reached, 0 .. 255 */
} des_bpi_cfg_t;
typedef struct {
    uint32_t status;              /* DES_BPI_ST_* */
    double set_cps, meas_cps;     /* set-point / measured speed, encoder counts per second */
    double err, scale, integ;     /* fractions: err = (set - meas) / set after the dead band, scale = output */
    uint32_t meas_cnt, step_cnt;  /* windows measured / PI steps taken, 16-bit counters that wrap */
} des_bpi_t;
int des_bpi_present(const des_hw_t *h);                       /* 1 = the bitstream has des_bpi */
int des_bpi_config(des_hw_t *h, const des_bpi_cfg_t *c);     /* DES_E_ARG when a field is out of range (nothing written) */
int des_bpi_set(des_hw_t *h, double counts_per_s);           /* set-point; 0 = nothing to control; DES_E_ARG above 292000 */
int des_bpi_enable(des_hw_t *h, int on);                     /* off: output 0 at once, integrator cleared */
int des_bpi_clear(des_hw_t *h);                              /* integrator and output to 0, stays enabled */
int des_bpi_read(const des_hw_t *h, des_bpi_t *b);

#define DES_PTG_N 2
enum { DES_PTG_ACT_NONE = 0, DES_PTG_ACT_PWM0 = 1, DES_PTG_ACT_PWM1 = 2 };
typedef struct {
    int falling;          /* 0: fire when source >= threshold, 1: when source <= threshold (wrapping 24-bit difference:
                             the threshold must lie within 8 million counts of the source) */
    int use_enc;          /* 0: stepper POS, 1: belt encoder count */
    int action;           /* DES_PTG_ACT_* */
    int pwm_us;           /* width loaded by the PWM actions, 0 .. 2550 */
    int pulse_out;        /* also pulse the TRIG_OUT pin */
} des_ptrig_cfg_t;
typedef struct {
    uint32_t fire_cnt;
    struct { uint8_t armed, fired, late; uint32_t ts; } e[DES_PTG_N];   /* ts = FREE_CNT at the fire */
} des_ptrig_t;
/* the CFG word of an entry (exposed for the unit test) */
static inline uint32_t des_ptrig_cfg_word(const des_ptrig_cfg_t *c, int arm)
{
    return ((uint32_t)(c->pwm_us & 0xFFF) << 16) | (c->pulse_out ? 0x80u : 0u) | ((uint32_t)(c->action & 3) << 4) |
           (c->use_enc ? 4u : 0u) | (c->falling ? 2u : 0u) | (arm ? 1u : 0u);
}
int des_ptrig_present(const des_hw_t *h);
/* threshold first, then the CFG word with the arm bit: an entry already past its threshold fires at once (LATE) */
int des_ptrig_arm(des_hw_t *h, int i, int32_t thr, const des_ptrig_cfg_t *c);
int des_ptrig_disarm(des_hw_t *h, int i);                    /* i < 0: every entry, flags cleared */
int des_ptrig_read(const des_hw_t *h, des_ptrig_t *t);

/* ---- des_vib (v17 batch 2): ADXL345 vibration acquisition ---- */
#define DES_VIB_G_PER_LSB 0.0039   /* ADXL345 full resolution: 3.9 mg per count in every range */
typedef struct { int16_t x, y, z; uint16_t ts16; } des_vib_sample_t;   /* ts16 = FREE_CNT[19:4] at DATA_READY */
typedef struct {
    uint32_t windows;              /* windows completed (16-bit counter, wraps) */
    unsigned n;                    /* samples per window */
    double mean_g[3], rms_g[3];    /* per axis: mean and RMS about the mean (AC part), in g */
} des_vib_stats_t;
int des_vib_present(const des_hw_t *h);
/* one-byte register transfer with the sensor; read: *out gets the byte. DES_E_BUSY if the PL never finishes */
int des_vib_xfer(des_hw_t *h, int read, unsigned reg, unsigned data, uint8_t *out);
/* sensor set-up: 3-wire SPI + full resolution + range (0 = 2 g .. 3 = 16 g), rate code (0x0F = 3200 Hz,
 * 0x0E = 1600, 0x0D = 800, 0x0C = 400), DATA_READY on INT1, measuring. DES_E_ID if DEVID is not 0xE5 */
int des_vib_init(des_hw_t *h, int range, int rate_code);
int des_vib_stream(des_hw_t *h, int on, unsigned window);   /* window 1..2048 samples; clears FIFO and counters */
/* drain up to max samples from the FIFO (oldest first); returns the number read or a DES_E_* code */
int des_vib_drain(des_hw_t *h, des_vib_sample_t *buf, int max);
int des_vib_stats(const des_hw_t *h, des_vib_stats_t *st);  /* last completed window; DES_E_BUSY if it kept changing */

/* ---- servo bus (Desheng protocol v4.01; PL adds FF FF header and checksum) ---- */
typedef struct {
    uint8_t status;        /* servo status byte: b5 overload, b2 over-temp, b0 voltage */
    uint8_t n;             /* reply bytes in raw[] */
    uint8_t raw[32];       /* [ID, LEN, STATUS, params..., CHK] */
    uint32_t t_tx0, t_tx1, t_rx;   /* FREE_CNT: first start bit, last stop bit, reply complete (0 when timings off) */
} des_sb_result_t;
#define DES_SERVO_BROADCAST 0xFE

/* body = [ID, LEN, INSTR, params...]; blocks until the transaction ends (<= ~100 ms). */
int des_sb_xfer(des_hw_t *h, const uint8_t *body, int n, int expect_reply, des_sb_result_t *r);
/* idle time a servo gets after its reply before the next frame to it (default 20 ms, see des_hw.c); 0 = off */
void des_servo_set_quiet_us(unsigned us);
/* read the 3 transaction timestamp registers (default 1; the app turns it off, saving 3 MMIO loads per frame) */
void des_sb_timings(int on);

int des_servo_ping(des_hw_t *h, uint8_t id, des_sb_result_t *r);
int des_servo_read(des_hw_t *h, uint8_t id, uint8_t addr, uint8_t n, uint8_t *out, des_sb_result_t *r);
int des_servo_read_u16(des_hw_t *h, uint8_t id, uint8_t addr, uint16_t *v);
int des_servo_write(des_hw_t *h, uint8_t id, uint8_t addr, const uint8_t *data, int n);
int des_servo_torque(des_hw_t *h, uint8_t id, int on);
/* SYNC WRITE 0x83 to 0x2A: per servo target position (0..4095) and move time (ms). n <= 11. */
int des_servo_sync_pos(des_hw_t *h, const uint8_t *ids, const uint16_t *pos, const uint16_t *ms, int n);

/* ---- robustness against a noisy bus (2026-10-26: the belt's TB6600 chopper corrupts ~2..8 % of the frames while it
   is energised, both directions; writes are never acknowledged, so a lost one went unnoticed) ---- */
/* PING / READ are retried this many times after a timeout or a damaged reply (default 2; 0 = raw, for diagnostics).
   Before each retry the servo gets its quiet window: it may have heard the request and only the reply was lost. */
void des_servo_set_retries(unsigned n);
typedef struct {
    uint32_t retries;        /* PING / READ attempts repeated */
    uint32_t read_failures;  /* PING / READ that still failed after all retries */
    uint32_t resends;        /* servo commands sent again after a wrong read-back */
    uint32_t unconfirmed;    /* servo commands never confirmed (DES_E_VERIFY) */
} des_sb_stats_t;
void des_sb_stats(des_sb_stats_t *s);   /* this process, since start */
/* SYNC WRITE as above, then each servo's goal register (0x2A) is read back and the command is sent again to the
   servos that do not report it, up to `rounds` sends in total (<= 0: 3). DES_OK when all confirmed, DES_E_VERIFY when
   some never did (they keep their previous goal: the caller must not assume the move happened). Costs one READ per
   servo (~1.6 ms each) on top of the SYNC WRITE. */
int des_servo_sync_pos_checked(des_hw_t *h, const uint8_t *ids, const uint16_t *pos, const uint16_t *ms, int n,
                               int rounds);
/* torque on / off (broadcast), then 0x28 read back from each listed servo; re-sent while some disagree (3 rounds) */
int des_servo_torque_checked(des_hw_t *h, const uint8_t *ids, int n, int on);
/* ---- stepper (belt) ---- */
#define DES_ST_STATUS_MOVING    0x01u
#define DES_ST_STATUS_AT_TARGET 0x04u
#define DES_ST_STATUS_HALTED    0x08u
#define DES_ST_STATUS_ENABLED   0x10u
void     des_step_enable(des_hw_t *h, int on);                 /* off = ENA released, no torque */
void     des_step_vel(des_hw_t *h, double steps_per_s, double acc);   /* signed speed, acc in steps/s^2 */
void     des_step_move(des_hw_t *h, int32_t target, double vmax, double acc);  /* absolute position mode */
/* Rewrite TARGET while a position move is running (the deceleration point is recomputed in PL); a target behind the
 * direction of travel makes the move ramp down and stop past it (v5+). */
void     des_step_set_target(des_hw_t *h, int32_t target);
/* Change the cruise speed of the running move; PL ramps to it with the current acceleration. */
void     des_step_set_vmax(des_hw_t *h, double steps_per_s);
void     des_step_stop(des_hw_t *h);                           /* ramp down */
void     des_step_halt(des_hw_t *h);                           /* immediate, no more pulses */
/* For program exit: ramp down if moving (waits up to timeout_ms for standstill, else halts), then release the
 * driver (ENA off). des_step_stop / des_step_halt keep the driver enabled, so a program that ends with them leaves
 * holding current in the motor (TB6600 warm, PSU fan up) until something releases it. DES_OK, or DES_E_TIMEOUT
 * when it had to halt. */
int      des_step_stop_release(des_hw_t *h, unsigned timeout_ms);
void     des_step_zero(des_hw_t *h);
int32_t  des_step_pos(const des_hw_t *h);
uint32_t des_step_status(const des_hw_t *h);
double   des_step_speed(const des_hw_t *h);                    /* current |speed| in steps/s */
int32_t  des_step_brake_steps(const des_hw_t *h);              /* v5+: steps to ramp down; -1 before v5 */
#ifdef __cplusplus
}
#endif
#endif
