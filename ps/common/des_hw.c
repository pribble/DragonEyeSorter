/* des_hw: see des_hw.h */
#include "des_hw.h"

#include <fcntl.h>
#include <stddef.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

int des_hw_open(des_hw_t *h)
{
    h->fd = -1;
    h->regs = NULL;
    h->version = 0;
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) return DES_E_OPEN;
    void *p = mmap(NULL, DES_PL_SPAN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, DES_PL_BASE);
    if (p == MAP_FAILED) {
        close(fd);
        return DES_E_OPEN;
    }
    h->fd = fd;
    h->regs = (volatile uint32_t *)p;
    if (des_rd(h, DES_REG_ID) != DES_ID_VALUE) return DES_E_ID;
    h->version = des_rd(h, DES_REG_VERSION);
    return DES_OK;
}

void des_hw_close(des_hw_t *h)
{
    if (h->regs) munmap((void *)h->regs, DES_PL_SPAN);
    if (h->fd >= 0) close(h->fd);
    h->regs = NULL;
    h->fd = -1;
}

const char *des_strerror(int rc)
{
    switch (rc) {
    case DES_OK: return "ok";
    case DES_E_OPEN: return "cannot map /dev/mem";
    case DES_E_ID: return "DES_ID mismatch (bitstream lacks des_regs)";
    case DES_E_TIMEOUT: return "servo reply timeout";
    case DES_E_RXERR: return "servo reply checksum/framing error";
    case DES_E_BUSY: return "servo_bus stuck busy";
    case DES_E_ARG: return "bad argument";
    default: return "unknown error";
    }
}

static void sleep_us(long us)
{
    struct timespec ts = { us / 1000000, (us % 1000000) * 1000 };
    nanosleep(&ts, NULL);
}

void des_frame_read(const des_hw_t *h, des_frame_t *f)
{
    /* FRAME_TS can change between the reads; re-read until the count is stable */
    uint32_t c;
    do {
        c = des_rd(h, DES_REG_FRAME_CNT);
        f->ts = des_rd(h, DES_REG_FRAME_TS);
        f->period = des_rd(h, DES_REG_FRAME_PERIOD);
    } while (c != des_rd(h, DES_REG_FRAME_CNT));
    f->cnt = c;
}

int des_capture_read(const des_hw_t *h, des_capture_t *c)
{
    if (h->version < DES_VERSION_CAPTURE) return DES_E_ARG;
    /* the set only changes on a done interrupt; re-read if one landed in between */
    uint32_t s;
    do {
        s = des_rd(h, DES_REG_CAP_SEQ);
        c->frame = des_rd(h, DES_REG_CAP_FCNT);
        c->t0 = des_rd(h, DES_REG_CAP_T0);
        c->t1 = des_rd(h, DES_REG_CAP_T1);
        c->pos0 = (int32_t)des_rd(h, DES_REG_CAP_POS0);
        c->pos1 = (int32_t)des_rd(h, DES_REG_CAP_POS1);
    } while (s != des_rd(h, DES_REG_CAP_SEQ));
    c->seq = s;
    return DES_OK;
}

/* ---------------- PL vision path ---------------- */

int des_vis_config(des_hw_t *h, int mode, int full, int thr, int edge_thr)
{
    if (h->version < DES_VERSION_VISION) return DES_E_ARG;
    des_wr(h, DES_VIS_CTRL, (uint32_t)(mode & 7) | (full ? 0x100u : 0u));
    des_wr(h, DES_VIS_THR, (uint32_t)(thr & 0xFF) | ((uint32_t)(edge_thr & 0x7FF) << 16));
    des_wr(h, DES_VIS_APPLY, 1);
    return DES_OK;
}

int des_vis_roi(des_hw_t *h, int x0, int y0, int x1, int y1)
{
    if (h->version < DES_VERSION_VISION) return DES_E_ARG;
    des_wr(h, DES_VIS_ROI_X, (uint32_t)(x0 & 0x7FF) | ((uint32_t)(x1 & 0x7FF) << 16));
    des_wr(h, DES_VIS_ROI_Y, (uint32_t)(y0 & 0x7FF) | ((uint32_t)(y1 & 0x7FF) << 16));
    des_wr(h, DES_VIS_APPLY, 1);
    return DES_OK;
}

int des_vis_read(const des_hw_t *h, des_vis_t *v)
{
    if (h->version < DES_VERSION_VISION) return DES_E_ARG;
    uint32_t s, bx, by, sx, sy, act;
    do {   /* a new frame may publish while we read: retry until the sequence number is stable */
        s = des_rd(h, DES_VIS_SEQ);
        v->area = des_rd(h, DES_VIS_AREA);
        bx = des_rd(h, DES_VIS_BBOX_X);
        by = des_rd(h, DES_VIS_BBOX_Y);
        sx = des_rd(h, DES_VIS_SUMX);
        sy = des_rd(h, DES_VIS_SUMY);
        v->edges = des_rd(h, DES_VIS_EDGES);
        v->cycles = des_rd(h, DES_VIS_CYCLES);
        act = des_rd(h, DES_VIS_ACTIVE);
    } while (s != des_rd(h, DES_VIS_SEQ));
    v->seq = s;
    v->x0 = (int)(bx & 0x7FF); v->x1 = (int)((bx >> 16) & 0x7FF);
    v->y0 = (int)(by & 0x7FF); v->y1 = (int)((by >> 16) & 0x7FF);
    v->cx = v->area ? (double)sx / v->area : 0.0;
    v->cy = v->area ? (double)sy / v->area : 0.0;
    v->mode = (int)(act & 7); v->full = (int)((act >> 8) & 1); v->thr = (int)((act >> 16) & 0xFF);
    v->lanes = (int)((act >> 24) & 0xF);
    if (v->lanes != 1 && v->lanes != 2) v->lanes = 4;   /* first v6 builds: field absent (0) = every pixel */
    v->area *= (uint32_t)(4 / v->lanes);                /* counts of processed pixels -> full resolution */
    v->edges *= (uint32_t)(4 / v->lanes);
    return DES_OK;
}

/* ---------------- flaps + light ---------------- */

int des_pwm_set(des_hw_t *h, int ch, int us)
{
    if (h->version < DES_VERSION_PWM || ch < 0 || ch > 1) return DES_E_ARG;
    des_wr(h, ch ? DES_PWM_CH1 : DES_PWM_CH0, (uint32_t)(us < 0 ? 0 : us));
    return DES_OK;
}

int des_pwm_enable(des_hw_t *h, unsigned ch_mask, int lamp)
{
    if (h->version < DES_VERSION_PWM) return DES_E_ARG;
    des_wr(h, DES_PWM_CTRL, (ch_mask & 3u) | (lamp ? 0x100u : 0u));
    return DES_OK;
}

/* ---------------- servo bus ---------------- */

static uint8_t rx_byte(const des_hw_t *h, int i)
{
    return (des_rd(h, DES_SB_RX_BUF + (i & ~3)) >> (8 * (i & 3))) & 0xff;
}

int des_sb_xfer(des_hw_t *h, const uint8_t *body, int n, int expect_reply, des_sb_result_t *r)
{
    des_sb_result_t tmp;
    if (!r) r = &tmp;
    r->rc = DES_OK;
    r->status = 0;
    r->n = 0;
    if (n < 3 || n > 64) return r->rc = DES_E_ARG;

    for (int w = 0; w < (n + 3) / 4; w++) {
        uint32_t v = 0;
        for (int b = 0; b < 4 && 4 * w + b < n; b++) v |= (uint32_t)body[4 * w + b] << (8 * b);
        des_wr(h, DES_SB_TX_BUF + 4 * w, v);
    }
    des_wr(h, DES_SB_CTRL, ((uint32_t)n << 8) | (expect_reply ? 2u : 0u) | 1u);

    /* on-wire time is (n + 3) * 86.8 us; poll at 100 us, give up after 100 ms */
    uint32_t st;
    int guard = 0;
    do {
        sleep_us(100);
        st = des_rd(h, DES_SB_STATUS);
    } while (((st & 1) || !(st & 2)) && ++guard < 1000);

    r->t_tx0 = des_rd(h, DES_SB_TX_T0);
    r->t_tx1 = des_rd(h, DES_SB_TX_T1);
    r->t_rx = des_rd(h, DES_SB_RX_T);
    if ((st & 1) || !(st & 2)) return r->rc = DES_E_BUSY;
    if (!expect_reply) return r->rc = DES_OK;
    if (st & 4) return r->rc = DES_E_TIMEOUT;

    int cnt = (st >> 8) & 0x7f;
    if (cnt > 32) cnt = 32;
    r->n = (uint8_t)cnt;
    for (int i = 0; i < cnt; i++) r->raw[i] = rx_byte(h, i);
    r->status = (st >> 16) & 0xff;
    if ((st & 8) || !(st & 16)) return r->rc = DES_E_RXERR;
    return r->rc = DES_OK;
}

int des_servo_ping(des_hw_t *h, uint8_t id, des_sb_result_t *r)
{
    uint8_t b[3] = { id, 2, 0x01 };
    return des_sb_xfer(h, b, 3, 1, r);
}

int des_servo_read(des_hw_t *h, uint8_t id, uint8_t addr, uint8_t n, uint8_t *out, des_sb_result_t *r)
{
    des_sb_result_t tmp;
    if (!r) r = &tmp;
    uint8_t b[5] = { id, 4, 0x02, addr, n };
    int rc = des_sb_xfer(h, b, 5, 1, r);
    if (rc) return rc;
    /* reply: ID LEN STATUS data[n] CHK */
    if (r->n < 4 + n || r->raw[0] != id) return r->rc = DES_E_RXERR;
    for (int i = 0; i < n; i++) out[i] = r->raw[3 + i];
    return DES_OK;
}

int des_servo_read_u16(des_hw_t *h, uint8_t id, uint8_t addr, uint16_t *v)
{
    uint8_t d[2];
    int rc = des_servo_read(h, id, addr, 2, d, NULL);
    if (rc == DES_OK) *v = (uint16_t)((d[0] << 8) | d[1]);   /* big-endian */
    return rc;
}

int des_servo_write(des_hw_t *h, uint8_t id, uint8_t addr, const uint8_t *data, int n)
{
    if (n < 1 || n > 58) return DES_E_ARG;
    uint8_t b[64];
    b[0] = id;
    b[1] = (uint8_t)(n + 3);          /* LEN = params (addr + data) + 2 */
    b[2] = 0x03;
    b[3] = addr;
    for (int i = 0; i < n; i++) b[4 + i] = data[i];
    return des_sb_xfer(h, b, 4 + n, id != DES_SERVO_BROADCAST, NULL);
}

int des_servo_torque(des_hw_t *h, uint8_t id, int on)
{
    uint8_t v = on ? 1 : 0;
    return des_servo_write(h, id, 0x28, &v, 1);
}

int des_servo_sync_pos(des_hw_t *h, const uint8_t *ids, const uint16_t *pos, const uint16_t *ms, int n)
{
    if (n < 1 || n > 11) return DES_E_ARG;
    uint8_t b[64];
    int k = 0;
    b[k++] = DES_SERVO_BROADCAST;
    b[k++] = (uint8_t)(2 + 2 + 5 * n);   /* params = addr, len-per-servo, n * (id + 4 data) */
    b[k++] = 0x83;
    b[k++] = 0x2A;
    b[k++] = 4;
    for (int i = 0; i < n; i++) {
        uint16_t p = pos[i] > 4095 ? 4095 : pos[i];
        b[k++] = ids[i];
        b[k++] = p >> 8;
        b[k++] = p & 0xff;
        b[k++] = ms[i] >> 8;
        b[k++] = ms[i] & 0xff;
    }
    return des_sb_xfer(h, b, k, 0, NULL);
}

/* ---------------- stepper ---------------- */

#define ST_EN    0x01u
#define ST_START 0x02u
#define ST_STOP  0x04u
#define ST_HALT  0x08u
#define ST_POS   0x10u
#define ST_ZERO  0x20u
#define ST_NEG   0x40u

static void step_params(des_hw_t *h, double vmax, double acc)
{
    if (vmax < 0) vmax = -vmax;
    des_wr(h, DES_ST_VMAX_INC, des_step_inc(vmax));
    des_wr(h, DES_ST_VMIN_INC, des_step_inc(vmax < 500 ? vmax : 500));
    des_wr(h, DES_ST_ACC_INC, des_step_inc(acc / DES_ST_RAMP_HZ));
}

void des_step_enable(des_hw_t *h, int on)
{
    if (on) des_wr(h, DES_ST_CTRL, ST_EN | (des_rd(h, DES_ST_CTRL) & (ST_POS | ST_NEG)));
    else des_wr(h, DES_ST_CTRL, 0);
}

void des_step_vel(des_hw_t *h, double steps_per_s, double acc)
{
    step_params(h, steps_per_s, acc);
    des_wr(h, DES_ST_CTRL, ST_EN | ST_START | (steps_per_s < 0 ? ST_NEG : 0));
}

void des_step_move(des_hw_t *h, int32_t target, double vmax, double acc)
{
    step_params(h, vmax, acc);
    des_wr(h, DES_ST_TARGET, (uint32_t)target);
    des_wr(h, DES_ST_CTRL, ST_EN | ST_START | ST_POS);
}

void des_step_stop(des_hw_t *h)
{
    des_wr(h, DES_ST_CTRL, ST_EN | ST_STOP | (des_rd(h, DES_ST_CTRL) & (ST_POS | ST_NEG)));
}

void des_step_halt(des_hw_t *h)
{
    des_wr(h, DES_ST_CTRL, ST_EN | ST_HALT | (des_rd(h, DES_ST_CTRL) & (ST_POS | ST_NEG)));
}

void des_step_zero(des_hw_t *h)
{
    des_wr(h, DES_ST_CTRL, (des_rd(h, DES_ST_CTRL) & (ST_EN | ST_POS | ST_NEG)) | ST_ZERO);
}

int32_t des_step_pos(const des_hw_t *h) { return (int32_t)des_rd(h, DES_ST_POS); }
uint32_t des_step_status(const des_hw_t *h) { return des_rd(h, DES_ST_STATUS); }

double des_step_speed(const des_hw_t *h)
{
    return des_rd(h, DES_ST_CUR_INC) * (double)DES_FREE_CNT_HZ / 4294967296.0;
}

void des_step_set_target(des_hw_t *h, int32_t target) { des_wr(h, DES_ST_TARGET, (uint32_t)target); }
void des_step_set_vmax(des_hw_t *h, double steps_per_s)
{
    des_wr(h, DES_ST_VMAX_INC, des_step_inc(steps_per_s < 0 ? -steps_per_s : steps_per_s));
}
int32_t des_step_target(const des_hw_t *h) { return (int32_t)des_rd(h, DES_ST_TARGET); }

int32_t des_step_brake_steps(const des_hw_t *h)
{
    return h->version >= DES_VERSION_CAPTURE ? (int32_t)des_rd(h, DES_ST_BRAKE) : -1;
}
