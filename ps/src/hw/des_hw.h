/* des_hw: DragonEyeSorter PL access layer over /dev/mem (des_regs / servo_bus / stepper_ctrl).
 * Register map: des_regs.h (same commit as docs/PL设计.md).
 *
 * Ownership: the servo bus and the stepper are single-master resources. In the running system only
 * the application touches them; do not run `desreg` write commands while des_app is running.
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
};

typedef struct {
    int fd;
    volatile uint32_t *regs;
    uint32_t version;      /* DES_REG_VERSION; 0 when ID check failed */
} des_hw_t;

int  des_hw_open(des_hw_t *h);           /* maps the window; returns DES_E_ID but stays mapped on ID mismatch */
void des_hw_close(des_hw_t *h);
const char *des_strerror(int rc);

static inline uint32_t des_rd(const des_hw_t *h, uint32_t off) { return h->regs[off / 4]; }
static inline void des_wr(des_hw_t *h, uint32_t off, uint32_t v) { h->regs[off / 4] = v; }
static inline uint32_t des_now(const des_hw_t *h) { return des_rd(h, DES_REG_FREE_CNT); }
static inline double des_ticks_us(uint32_t ticks) { return ticks * 1e6 / DES_FREE_CNT_HZ; }

/* ---- camera frame counters (ISP start of frame) ---- */
typedef struct {
    uint32_t cnt;          /* start-of-frame count */
    uint32_t ts;           /* FREE_CNT at the latest start of frame */
    uint32_t period;       /* ticks between the last two starts of frame */
} des_frame_t;

void des_frame_read(const des_hw_t *h, des_frame_t *f);

/* last completed DMA capture, latched by the PL (VERSION >= 5) */
typedef struct {
    uint32_t seq;          /* completed captures */
    uint32_t frame;        /* FRAME_CNT at the writer start */
    uint32_t t0, t1;       /* FREE_CNT at writer start / done */
    int32_t pos0, pos1;    /* belt position (steps) at writer start / done */
} des_capture_t;

#define DES_VERSION_CAPTURE 5u
int des_capture_read(const des_hw_t *h, des_capture_t *c);   /* DES_E_ARG on bitstreams before v5 */

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

/* ---- servo bus (Desheng protocol v4.01; PL adds FF FF header and checksum) ---- */
typedef struct {
    int rc;                /* DES_OK / DES_E_* */
    uint8_t status;        /* servo status byte: b5 overload, b2 over-temp, b0 voltage */
    uint8_t n;             /* reply bytes in raw[] */
    uint8_t raw[32];       /* [ID, LEN, STATUS, params..., CHK] */
    uint32_t t_tx0, t_tx1, t_rx;   /* FREE_CNT: first start bit, last stop bit, reply complete */
} des_sb_result_t;

#define DES_SERVO_BROADCAST 0xFE

/* body = [ID, LEN, INSTR, params...]; blocks until the transaction ends (<= ~100 ms). */
int des_sb_xfer(des_hw_t *h, const uint8_t *body, int n, int expect_reply, des_sb_result_t *r);

int des_servo_ping(des_hw_t *h, uint8_t id, des_sb_result_t *r);
int des_servo_read(des_hw_t *h, uint8_t id, uint8_t addr, uint8_t n, uint8_t *out, des_sb_result_t *r);
int des_servo_read_u16(des_hw_t *h, uint8_t id, uint8_t addr, uint16_t *v);
int des_servo_write(des_hw_t *h, uint8_t id, uint8_t addr, const uint8_t *data, int n);
int des_servo_torque(des_hw_t *h, uint8_t id, int on);
/* SYNC WRITE 0x83 to 0x2A: per servo target position (0..4095) and move time (ms). n <= 11. */
int des_servo_sync_pos(des_hw_t *h, const uint8_t *ids, const uint16_t *pos, const uint16_t *ms, int n);

/* ---- stepper (belt) ---- */
#define DES_ST_STATUS_MOVING    0x01u
#define DES_ST_STATUS_DIR       0x02u
#define DES_ST_STATUS_AT_TARGET 0x04u
#define DES_ST_STATUS_HALTED    0x08u
#define DES_ST_STATUS_ENABLED   0x10u

void     des_step_enable(des_hw_t *h, int on);                 /* off = ENA released, no torque */
void     des_step_vel(des_hw_t *h, double steps_per_s, double acc);   /* signed speed, acc in steps/s^2 */
void     des_step_move(des_hw_t *h, int32_t target, double vmax, double acc);  /* absolute position mode */
/* Rewrite TARGET while a position move is running (the deceleration point is recomputed in PL). A target behind
 * the direction of travel makes the move ramp down and stop past it (v5+). */
void     des_step_set_target(des_hw_t *h, int32_t target);
/* Change the cruise speed of the running move; PL ramps to it with the current acceleration. */
void     des_step_set_vmax(des_hw_t *h, double steps_per_s);
int32_t  des_step_target(const des_hw_t *h);
void     des_step_stop(des_hw_t *h);                           /* ramp down */
void     des_step_halt(des_hw_t *h);                           /* immediate, no more pulses */
void     des_step_zero(des_hw_t *h);
int32_t  des_step_pos(const des_hw_t *h);
uint32_t des_step_status(const des_hw_t *h);
double   des_step_speed(const des_hw_t *h);                    /* current |speed| in steps/s */
int32_t  des_step_brake_steps(const des_hw_t *h);              /* v5+: steps to ramp down; -1 before v5 */

#ifdef __cplusplus
}
#endif

#endif
