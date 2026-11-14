/* des_hw: see des_hw.h */
#include "des_hw.h"
#include <math.h>

#include <fcntl.h>
#include <pthread.h>
#include <stddef.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

int des_hw_open(des_hw_t *h)
{
    h->regs = NULL;
    h->fd = -1;
    h->version = 0;
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) return DES_E_OPEN;
    void *p = mmap(NULL, DES_PL_SPAN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, DES_PL_BASE);
    if (p == MAP_FAILED) { close(fd); return DES_E_OPEN; }
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
    static const char *const s[] = {"ok", "cannot map /dev/mem", "DES_ID mismatch (bitstream lacks des_regs)",
                                    "servo reply timeout", "servo reply checksum/framing error",
                                    "servo_bus stuck busy", "bad argument", "RS485 frame CRC error",
                                    "servo never confirmed the command"};
    return rc <= 0 && rc >= -8 ? s[-rc] : "unknown error";
}

static void sleep_us(long us)
{
    struct timespec ts = { us / 1000000, (us % 1000000) * 1000 };
    nanosleep(&ts, NULL);
}
void des_frame_read(const des_hw_t *h, des_frame_t *f)
{
    /* FRAME_PERIOD can change between the reads; re-read until the count is stable (3 loads in the common case) */
    uint32_t c;
    do { c = des_rd(h, DES_REG_FRAME_CNT); f->period = des_rd(h, DES_REG_FRAME_PERIOD); }
    while (c != des_rd(h, DES_REG_FRAME_CNT));
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
        if (h->version >= DES_VERSION_CAPTURE_ENC) {
            c->enc0 = (int32_t)des_rd(h, DES_REG_CAP_ENC0);
            c->enc1 = (int32_t)des_rd(h, DES_REG_CAP_ENC1);
        } else c->enc0 = c->enc1 = 0;
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
    uint32_t s, bx, by, sx = 0, sy = 0;
    int guard = 0;
    const uint32_t act = des_rd(h, DES_VIS_ACTIVE);   /* settings in use: only change on APPLY, read once */
    do {   /* a new frame may publish while we read: retry until the sequence number is stable */
        s = des_rd(h, DES_VIS_SEQ);
        v->area = des_rd(h, DES_VIS_AREA);
        bx = des_rd(h, DES_VIS_BBOX_X);
        by = des_rd(h, DES_VIS_BBOX_Y);
        if (v->area) {   /* centroid sums: 2 MMIO loads less on an empty ROI (cx = cy = 0 either way) */
            sx = des_rd(h, DES_VIS_SUMX);
            sy = des_rd(h, DES_VIS_SUMY);
        } else {
            sx = sy = 0;
        }
        v->edges = des_rd(h, DES_VIS_EDGES);
        v->cycles = des_rd(h, DES_VIS_CYCLES);
    } while (s != des_rd(h, DES_VIS_SEQ) && ++guard < 100);
    if (guard >= 100) return DES_E_BUSY;   /* SEQ never stable: frame starts every few us = broken pixel clock domain */
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
/* ---------------- belt-end flaps / field inputs ---------------- */
int des_pwm_set(des_hw_t *h, int ch, int us)
{
    if (h->version < DES_VERSION_PWM || ch < 0 || ch > 1) return DES_E_ARG;
    des_wr(h, ch ? DES_PWM_CH1 : DES_PWM_CH0, (uint32_t)(us < 0 ? 0 : us));
    return DES_OK;
}
int des_pwm_enable(des_hw_t *h, unsigned ch_mask)
{
    if (h->version < DES_VERSION_PWM) return DES_E_ARG;
    des_wr(h, DES_PWM_CTRL, ch_mask & 3u);
    return DES_OK;
}
/* ---- v17 ---- */
int des_pwm_slew(des_hw_t *h, int us_per_period)
{
    if (h->version < DES_VERSION_V17 || us_per_period < 0 || us_per_period > 4095) return DES_E_ARG;
    des_wr(h, DES_PWM_SLEW, (uint32_t)us_per_period);
    return des_rd(h, DES_PWM_SLEW) == (uint32_t)us_per_period ? DES_OK : DES_E_ARG;   /* a build without SLEW reads 0 */
}
int des_bpi_present(const des_hw_t *h) { return h->version >= DES_VERSION_V17 && des_rd(h, DES_BPI_ID) == DES_BPI_ID_VALUE; }
int des_bpi_config(des_hw_t *h, const des_bpi_cfg_t *c)
{
    if (!des_bpi_present(h) || !c) return DES_E_ARG;
    if (!(c->kp >= 0 && c->kp < 16.0) || !(c->ki >= 0 && c->ki < 1.0) || !(c->limit >= 0 && c->limit <= 0.25) ||
        !(c->deadband >= 0 && c->deadband < 1.0) || c->period_ms < 1 || c->period_ms > 200 ||
        c->timeout_ms < c->period_ms || c->timeout_ms > 200 || c->settle < 0 || c->settle > 255)
        return DES_E_ARG;
    des_wr(h, DES_BPI_KP, (uint32_t)(c->kp * 4096.0 + 0.5));            /* Q4.12 */
    des_wr(h, DES_BPI_KI, (uint32_t)(c->ki * 65536.0 + 0.5));           /* Q0.16 */
    des_wr(h, DES_BPI_SMAX, (uint32_t)(c->limit * 65536.0 + 0.5));
    des_wr(h, DES_BPI_DB, (uint32_t)(c->deadband * 65536.0 + 0.5));
    des_wr(h, DES_BPI_PERIOD, c->period_ms * (DES_FREE_CNT_HZ / 1000u));
    des_wr(h, DES_BPI_TIMEOUT, c->timeout_ms * (DES_FREE_CNT_HZ / 1000u));
    des_wr(h, DES_BPI_SETTLE, (uint32_t)c->settle);
    return DES_OK;
}
int des_bpi_set(des_hw_t *h, double counts_per_s)
{
    if (!des_bpi_present(h) || !(counts_per_s > -292000.0 && counts_per_s < 292000.0)) return DES_E_ARG;   /* 24 bits */
    des_wr(h, DES_BPI_SET, des_enc_inc(counts_per_s));
    return DES_OK;
}
int des_bpi_enable(des_hw_t *h, int on)
{
    if (!des_bpi_present(h)) return DES_E_ARG;
    des_wr(h, DES_BPI_CTRL, on ? 1u : 0u);
    return DES_OK;
}
int des_bpi_clear(des_hw_t *h)
{
    if (!des_bpi_present(h)) return DES_E_ARG;
    des_wr(h, DES_BPI_CTRL, (des_rd(h, DES_BPI_CTRL) & 1u) | 2u);
    return DES_OK;
}
int des_bpi_read(const des_hw_t *h, des_bpi_t *b)
{
    if (!des_bpi_present(h) || !b) return DES_E_ARG;
    b->status = des_rd(h, DES_BPI_STATUS);
    b->set_cps = des_enc_cps(des_rd(h, DES_BPI_SET));
    b->meas_cps = des_enc_cps(des_rd(h, DES_BPI_MEAS));
    b->err = (int32_t)des_rd(h, DES_BPI_ERR) / 65536.0;
    b->scale = (int32_t)des_rd(h, DES_BPI_SCALE) / 65536.0;
    b->integ = (int32_t)des_rd(h, DES_BPI_INTEG) / 65536.0;
    b->meas_cnt = des_rd(h, DES_BPI_MEAS_CNT);
    b->step_cnt = des_rd(h, DES_BPI_STEP_CNT);
    return DES_OK;
}
int des_ptrig_present(const des_hw_t *h) { return h->version >= DES_VERSION_V17 && des_rd(h, DES_PTG_ID) == DES_PTG_ID_VALUE; }
int des_ptrig_arm(des_hw_t *h, int i, int32_t thr, const des_ptrig_cfg_t *c)
{
    if (!des_ptrig_present(h) || !c || i < 0 || i >= DES_PTG_N || c->action < 0 || c->action > DES_PTG_ACT_PWM1 ||
        c->pwm_us < 0 || c->pwm_us > 2550)
        return DES_E_ARG;
    des_wr(h, DES_PTG_THR(i), (uint32_t)thr);
    des_wr(h, DES_PTG_CFG(i), des_ptrig_cfg_word(c, 1));
    return DES_OK;
}
int des_ptrig_disarm(des_hw_t *h, int i)
{
    if (!des_ptrig_present(h) || i >= DES_PTG_N) return DES_E_ARG;
    if (i < 0) des_wr(h, DES_PTG_CTRL, 1u);
    else des_wr(h, DES_PTG_CFG(i), des_rd(h, DES_PTG_CFG(i)) & ~1u);
    return DES_OK;
}
int des_ptrig_read(const des_hw_t *h, des_ptrig_t *t)
{
    if (!des_ptrig_present(h) || !t) return DES_E_ARG;
    const uint32_t s = des_rd(h, DES_PTG_STATUS);
    t->fire_cnt = des_rd(h, DES_PTG_FIRE_CNT);
    for (int i = 0; i < DES_PTG_N; i++) {
        t->e[i].armed = (s >> i) & 1u;
        t->e[i].fired = (s >> (8 + i)) & 1u;
        t->e[i].late = (s >> (16 + i)) & 1u;
        t->e[i].ts = des_rd(h, DES_PTG_TS(i));
    }
    return DES_OK;
}
/* ---- v17 batch 2: des_vib ---- */
int des_vib_present(const des_hw_t *h) { return h->version >= DES_VERSION_V17 && des_rd(h, DES_VIB_ID) == DES_VIB_ID_VALUE; }
int des_vib_xfer(des_hw_t *h, int read, unsigned reg, unsigned data, uint8_t *out)
{
    if (!des_vib_present(h) || reg > 0x3Fu || data > 0xFFu) return DES_E_ARG;
    for (int k = 0; (des_rd(h, DES_VIB_XFER) & 0x100u) != 0; k++) {   /* a transfer still running */
        if (k > 1000) return DES_E_BUSY;
        usleep(10);
    }
    des_wr(h, DES_VIB_XFER, (read ? 0x8000u : 0u) | (reg << 8) | (read ? 0u : data));
    /* 16 bits at 3.75 MHz + set-up / hold = about 5 us; a sample read can be in front of it: about 20 us more */
    for (int k = 0;; k++) {
        const uint32_t v = des_rd(h, DES_VIB_XFER);
        if (!(v & 0x100u)) {
            if (out) *out = (uint8_t)v;
            return DES_OK;
        }
        if (k > 1000) return DES_E_BUSY;
        usleep(10);
    }
}
int des_vib_init(des_hw_t *h, int range, int rate_code)
{
    if (!des_vib_present(h) || range < 0 || range > 3 || rate_code < 0x06 || rate_code > 0x0F) return DES_E_ARG;
    uint8_t id = 0;
    int rc;
    des_wr(h, DES_VIB_CTRL, 0u);                                          /* not streaming while set up */
    if ((rc = des_vib_xfer(h, 0, 0x2D, 0x00, NULL)) != DES_OK) return rc;  /* POWER_CTL: standby */
    if ((rc = des_vib_xfer(h, 0, 0x31, 0x48u | (unsigned)range, NULL)) != DES_OK) return rc;   /* 3-wire, FULL_RES */
    if ((rc = des_vib_xfer(h, 1, 0x00, 0, &id)) != DES_OK) return rc;
    if (id != 0xE5) return DES_E_ID;
    if ((rc = des_vib_xfer(h, 0, 0x2C, (unsigned)rate_code, NULL)) != DES_OK) return rc;        /* BW_RATE */
    if ((rc = des_vib_xfer(h, 0, 0x38, 0x00, NULL)) != DES_OK) return rc;                       /* FIFO_CTL: bypass */
    if ((rc = des_vib_xfer(h, 0, 0x2F, 0x00, NULL)) != DES_OK) return rc;                       /* INT_MAP: all INT1 */
    if ((rc = des_vib_xfer(h, 0, 0x2E, 0x80, NULL)) != DES_OK) return rc;                       /* INT_ENABLE: DATA_READY */
    return des_vib_xfer(h, 0, 0x2D, 0x08, NULL);                                                 /* POWER_CTL: measure */
}
int des_vib_stream(des_hw_t *h, int on, unsigned window)
{
    if (!des_vib_present(h) || window < 1 || window > 2048) return DES_E_ARG;
    des_wr(h, DES_VIB_WIN, window & 0x7FFu);   /* 2048 is written as 0 */
    des_wr(h, DES_VIB_CTRL, (on ? 1u : 0u) | 2u);
    return DES_OK;
}
int des_vib_drain(des_hw_t *h, des_vib_sample_t *buf, int max)
{
    if (!des_vib_present(h) || !buf || max < 0) return DES_E_ARG;
    int n = (int)((des_rd(h, DES_VIB_STATUS) >> 16) & 0x3FFu);
    if (n > max) n = max;
    for (int i = 0; i < n; i++) {
        const uint32_t a = des_rd(h, DES_VIB_HEAD0), b = des_rd(h, DES_VIB_HEAD1);
        des_wr(h, DES_VIB_HEAD0, 0u);              /* pop */
        buf[i].x = (int16_t)(a & 0xFFFFu); buf[i].y = (int16_t)(a >> 16);
        buf[i].z = (int16_t)(b & 0xFFFFu); buf[i].ts16 = (uint16_t)(b >> 16);
    }
    return n;
}
int des_vib_stats(const des_hw_t *h, des_vib_stats_t *st)
{
    if (!des_vib_present(h) || !st) return DES_E_ARG;
    for (int tries = 0; tries < 5; tries++) {
        const uint32_t w0 = des_rd(h, DES_VIB_WIN_CNT);
        uint32_t sum[3], sq[3], mm[3];
        for (int a = 0; a < 3; a++) {
            sum[a] = des_rd(h, DES_VIB_SUM0 + 4u * a); sq[a] = des_rd(h, DES_VIB_SQ0 + 4u * a); mm[a] = des_rd(h, DES_VIB_MM0 + 4u * a);
        }
        if (des_rd(h, DES_VIB_WIN_CNT) != w0) continue;   /* a window closed while reading */
        const unsigned win = des_rd(h, DES_VIB_WIN) & 0x7FFu;
        st->windows = w0;
        st->n = win ? win : 2048u;
        for (int a = 0; a < 3; a++) {
            const double mean = (int32_t)sum[a] / (double)st->n;
            const double ms = sq[a] * 1024.0 / st->n;      /* mean square, counts^2 (low 10 bits of the sum dropped) */
            const double var = ms - mean * mean;
            st->mean_g[a] = mean * DES_VIB_G_PER_LSB;
            st->rms_g[a] = (var > 0 ? sqrt(var) : 0.0) * DES_VIB_G_PER_LSB;
            st->min_g[a] = (int16_t)(mm[a] & 0xFFFFu) * DES_VIB_G_PER_LSB;
            st->max_g[a] = (int16_t)(mm[a] >> 16) * DES_VIB_G_PER_LSB;
        }
        return DES_OK;
    }
    return DES_E_BUSY;
}
int des_io_read(const des_hw_t *h, des_io_t *io)
{
    if (h->version < DES_VERSION_IO) return DES_E_ARG;
    io->status = des_rd(h, DES_IO_STATUS);
    io->ir1_cnt = des_rd(h, DES_IO_IR1_CNT);
    io->ir1_pos = (int32_t)des_rd(h, DES_IO_IR1_POS);
    io->ir1_enc = des_rd(h, DES_IO_IR1_ENC);
    io->enc = (int32_t)des_rd(h, DES_IO_ENC_CNT);
    io->enc_err = des_rd(h, DES_IO_ENC_ERR);
    io->enc_zpos = (int32_t)des_rd(h, DES_IO_ENC_ZPOS);
    io->enc_speed = (int32_t)des_rd(h, DES_IO_ENC_SPEED);
    io->enc_zcnt = des_rd(h, DES_IO_ENC_ZCNT);
    return DES_OK;
}
int des_estop_latched(const des_hw_t *h)
{
    if (h->version < DES_VERSION_IO) return DES_E_ARG;
    return (des_rd(h, DES_IO_STATUS) & DES_IO_ST_ESTOP_LATCHED) ? 1 : 0;
}
int des_estop_clear(des_hw_t *h)
{
    if (h->version < DES_VERSION_IO) return DES_E_ARG;
    des_wr(h, DES_IO_CTRL, 1u);
    return des_estop_latched(h);
}
int des_enc_zero(des_hw_t *h)
{
    if (h->version < DES_VERSION_IO) return DES_E_ARG;
    des_wr(h, DES_IO_CTRL, 2u);
    return DES_OK;
}
/* ---------------- servo bus ---------------- */
/* The 15 kg body servos (DS-SY15A, ID 1..5) ignore a frame that arrives within ~10..15 ms after their own reply
 * (2026-10-17, first servos on the bus, tests/test_servo_gap.c: strict ok / timeout alternation up to 10 ms of idle
 * after the reply, none from 15 ms; no RX errors - they simply do not answer or act; the gripper servo has no such
 * window). So every frame waits until each servo it addresses has been quiet this long since its last reply: a
 * second READ to the same servo, or a SYNC WRITE right after reading a position, would otherwise be dropped. Frames
 * to other servos are not delayed. Two sources for "last reply": this process's own table (per id), and the PL's
 * record of the last reply on the bus (RX_T + id in RX_BUF) for frames sent by another process just before. */
static unsigned g_quiet_us = 20000;
static unsigned g_timings = 1;     /* des_sb_result_t t_tx0/t_tx1/t_rx: on by default, app may switch off */
static uint64_t g_reply_us[256];   /* CLOCK_MONOTONIC us of the last reply seen from each id (0 = none) */
void des_servo_set_quiet_us(unsigned us) { g_quiet_us = us; }
void des_sb_timings(int on) { g_timings = on ? 1u : 0u; }
static uint64_t mono_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}
/* does this frame reach servo id? */
static int addresses(const uint8_t *body, int n, uint8_t id)
{
    if (body[0] != DES_SERVO_BROADCAST) return body[0] == id;
    if (n >= 5 && body[2] == 0x83) {   /* SYNC WRITE: FE LEN 83 addr L {id, L data bytes}... */
        for (int k = 5; k < n; k += body[4] + 1)
            if (body[k] == id) return 1;
        return 0;
    }
    return 1;                          /* other broadcasts reach every servo */
}
static void wait_quiet(const des_hw_t *h, const uint8_t *body, int n)
{
    if (!g_quiet_us) return;
    const uint64_t now = mono_us();
    uint64_t wait = 0;
    if (body[0] != DES_SERVO_BROADCAST) {
        /* unicast: only the addressed servo matters, skip the 255-entry scan */
        const uint64_t last = g_reply_us[body[0]];
        if (last && now - last < g_quiet_us) wait = g_quiet_us - (now - last);
    } else {
        for (int id = 0; id < 255; id++) {
            if (!g_reply_us[id] || !addresses(body, n, (uint8_t)id)) continue;
            const uint64_t since = now - g_reply_us[id];
            if (since < g_quiet_us && g_quiet_us - since > wait) wait = g_quiet_us - since;
        }
    }
    const uint8_t last_id = (uint8_t)(des_rd(h, DES_SB_RX_BUF) & 0xff);   /* last reply on the bus, whoever sent it */
    if (addresses(body, n, last_id)) {
        const double since = des_ticks_us(des_now(h) - des_rd(h, DES_SB_RX_T));
        if (since < g_quiet_us && g_quiet_us - since > wait) wait = (uint64_t)(g_quiet_us - since);
    }
    if (wait) sleep_us((long)wait + 100);
}
static int sb_xfer(des_hw_t *h, const uint8_t *body, int n, int expect_reply, des_sb_result_t *r)
{
    des_sb_result_t tmp;
    if (!r) r = &tmp;
    r->status = 0;
    r->n = 0;
    if (n < 3 || n > 64) return DES_E_ARG;
    wait_quiet(h, body, n);
    for (int w = 0; w < (n + 3) / 4; w++) {
        uint32_t v = 0;
        for (int b = 0; b < 4 && 4 * w + b < n; b++) v |= (uint32_t)body[4 * w + b] << (8 * b);
        des_wr(h, DES_SB_TX_BUF + 4 * w, v);
    }
    des_wr(h, DES_SB_CTRL, ((uint32_t)n << 8) | (expect_reply ? 2u : 0u) | 1u);
    /* on-wire time is (n + 3) * 86.8 us; poll at 100 us, give up after 100 ms; the first poll is immediate */
    uint32_t st = des_rd(h, DES_SB_STATUS);
    for (int guard = 0; ((st & 1) || !(st & 2)) && guard < 1000; guard++) {
        sleep_us(100);
        st = des_rd(h, DES_SB_STATUS);
    }
    if (g_timings) {
        r->t_tx0 = des_rd(h, DES_SB_TX_T0);
        r->t_tx1 = des_rd(h, DES_SB_TX_T1);
        r->t_rx = des_rd(h, DES_SB_RX_T);
    } else {
        r->t_tx0 = r->t_tx1 = r->t_rx = 0;
    }
    if ((st & 1) || !(st & 2)) return DES_E_BUSY;
    if (!expect_reply) return DES_OK;
    if (st & 4) return DES_E_TIMEOUT;
    int cnt = (st >> 8) & 0x7f;
    if (cnt > 32) cnt = 32;
    r->n = (uint8_t)cnt;
    /* unpack RX_BUF word by word: 1 MMIO load per 4 reply bytes instead of 1 per byte */
    for (int w = 0; w < (cnt + 3) / 4; w++) {
        const uint32_t v = des_rd(h, DES_SB_RX_BUF + 4 * w);
        for (int b = 0; b < 4 && 4 * w + b < cnt; b++) r->raw[4 * w + b] = (uint8_t)(v >> (8 * b));
    }
    if (cnt > 0) g_reply_us[r->raw[0]] = mono_us();   /* reply ended at most one poll (100 us) ago */
    r->status = (st >> 16) & 0xff;
    if ((st & 8) || !(st & 16)) return DES_E_RXERR;
    return DES_OK;
}
/* one frame at a time: the production arm thread (Runner) and the manual arm page (ArmManual) share the bus */
static pthread_mutex_t g_sb_lock = PTHREAD_MUTEX_INITIALIZER;
int des_sb_xfer(des_hw_t *h, const uint8_t *body, int n, int expect_reply, des_sb_result_t *r)
{
    pthread_mutex_lock(&g_sb_lock);
    const int rc = sb_xfer(h, body, n, expect_reply, r);
    pthread_mutex_unlock(&g_sb_lock);
    return rc;
}
/* ---- noisy-bus robustness (see des_hw.h): retried reads, read-back checked writes ---- */
static unsigned g_retries = 2;
static des_sb_stats_t g_stats;
void des_servo_set_retries(unsigned n) { g_retries = n > 10 ? 10 : n; }
void des_sb_stats(des_sb_stats_t *s)
{
    pthread_mutex_lock(&g_sb_lock);
    *s = g_stats;
    pthread_mutex_unlock(&g_sb_lock);
}
static void stats_add(uint32_t *field, uint32_t n)
{
    pthread_mutex_lock(&g_sb_lock);
    *field += n;
    pthread_mutex_unlock(&g_sb_lock);
}
/* a failed exchange: the servo may have heard the request (only the reply got lost or damaged - and a damaged reply
   can carry a wrong ID, so sb_xfer may have stamped another servo), so it gets its quiet window before the retry */
static void failed_exchange(uint8_t id)
{
    if (id == DES_SERVO_BROADCAST) return;
    pthread_mutex_lock(&g_sb_lock);
    g_reply_us[id] = mono_us();
    pthread_mutex_unlock(&g_sb_lock);
}
static int retryable(int rc) { return rc == DES_E_TIMEOUT || rc == DES_E_RXERR; }
int des_servo_ping(des_hw_t *h, uint8_t id, des_sb_result_t *r)
{
    uint8_t b[3] = { id, 2, 0x01 };
    int rc = DES_E_ARG;
    for (unsigned a = 0; a <= g_retries; a++) {
        if (a) stats_add(&g_stats.retries, 1);
        rc = des_sb_xfer(h, b, 3, 1, r);
        if (!retryable(rc)) return rc;
        failed_exchange(id);
    }
    stats_add(&g_stats.read_failures, 1);
    return rc;
}
int des_servo_read(des_hw_t *h, uint8_t id, uint8_t addr, uint8_t n, uint8_t *out, des_sb_result_t *r)
{
    des_sb_result_t tmp;
    if (!r) r = &tmp;
    uint8_t b[5] = { id, 4, 0x02, addr, n };
    int rc = DES_E_ARG;
    for (unsigned a = 0; a <= g_retries; a++) {
        if (a) stats_add(&g_stats.retries, 1);
        rc = des_sb_xfer(h, b, 5, 1, r);
        if (rc == DES_OK && (r->n < 4 + n || r->raw[0] != id)) rc = DES_E_RXERR;   /* reply: ID LEN STATUS data[n] CHK */
        if (rc == DES_OK) {
            for (int i = 0; i < n; i++) out[i] = r->raw[3 + i];
            return DES_OK;
        }
        if (!retryable(rc)) return rc;
        failed_exchange(id);
    }
    stats_add(&g_stats.read_failures, 1);
    return rc;
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
int des_servo_sync_pos_checked(des_hw_t *h, const uint8_t *ids, const uint16_t *pos, const uint16_t *ms, int n,
                               int rounds)
{
    if (n < 1 || n > 11) return DES_E_ARG;
    if (rounds <= 0) rounds = 3;
    uint8_t pid[11];
    uint16_t ppos[11], pms[11];
    int np = n;
    for (int i = 0; i < n; i++) { pid[i] = ids[i]; ppos[i] = pos[i] > 4095 ? 4095 : pos[i]; pms[i] = ms[i]; }
    for (int round = 0; round < rounds && np > 0; round++) {
        if (round) stats_add(&g_stats.resends, (uint32_t)np);
        const int rc = des_servo_sync_pos(h, pid, ppos, pms, np);
        if (rc != DES_OK) return rc;            /* bus stuck: nothing to check */
        int left = 0;
        for (int i = 0; i < np; i++) {          /* goal register = what the servo is now heading for */
            uint16_t g;
            if (des_servo_read_u16(h, pid[i], 0x2A, &g) == DES_OK && g == ppos[i]) continue;
            pid[left] = pid[i]; ppos[left] = ppos[i]; pms[left] = pms[i]; left++;
        }
        np = left;
    }
    if (!np) return DES_OK;
    stats_add(&g_stats.unconfirmed, (uint32_t)np);
    return DES_E_VERIFY;
}
int des_servo_torque_checked(des_hw_t *h, const uint8_t *ids, int n, int on)
{
    if (n < 1 || n > 32) return DES_E_ARG;
    uint8_t pid[32];
    int np = n;
    for (int i = 0; i < n; i++) pid[i] = ids[i];
    for (int round = 0; round < 3 && np > 0; round++) {
        if (round) stats_add(&g_stats.resends, (uint32_t)np);
        const int rc = des_servo_torque(h, DES_SERVO_BROADCAST, on);   /* broadcast: no reply */
        if (rc != DES_OK) return rc;
        int left = 0;
        for (int i = 0; i < np; i++) {
            uint8_t v;
            if (des_servo_read(h, pid[i], 0x28, 1, &v, NULL) == DES_OK && (v != 0) == (on != 0)) continue;
            pid[left++] = pid[i];
        }
        np = left;
    }
    if (!np) return DES_OK;
    stats_add(&g_stats.unconfirmed, (uint32_t)np);
    return DES_E_VERIFY;
}
/* ---------------- stepper ---------------- */
#define ST_EN 0x01u
#define ST_START 0x02u
#define ST_STOP 0x04u
#define ST_HALT 0x08u
#define ST_POS 0x10u
#define ST_ZERO 0x20u
#define ST_NEG 0x40u
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
void des_step_stop(des_hw_t *h) { des_wr(h, DES_ST_CTRL, ST_EN | ST_STOP | (des_rd(h, DES_ST_CTRL) & (ST_POS | ST_NEG))); }
void des_step_halt(des_hw_t *h) { des_wr(h, DES_ST_CTRL, ST_EN | ST_HALT | (des_rd(h, DES_ST_CTRL) & (ST_POS | ST_NEG))); }
int des_step_stop_release(des_hw_t *h, unsigned timeout_ms)
{
    int rc = DES_OK;
    if (des_step_status(h) & DES_ST_STATUS_MOVING) {
        des_step_stop(h);   /* ramp down with the driver enabled: releasing at speed lets the belt coast */
        const uint64_t t_end = mono_us() + (uint64_t)timeout_ms * 1000u;
        while ((des_step_status(h) & DES_ST_STATUS_MOVING) && mono_us() < t_end) sleep_us(5000);
        if (des_step_status(h) & DES_ST_STATUS_MOVING) {
            des_step_halt(h);
            sleep_us(1000);
            rc = DES_E_TIMEOUT;
        }
    }
    des_step_enable(h, 0);
    return rc;
}
void des_step_zero(des_hw_t *h) { des_wr(h, DES_ST_CTRL, (des_rd(h, DES_ST_CTRL) & (ST_EN | ST_POS | ST_NEG)) | ST_ZERO); }
void des_step_set_target(des_hw_t *h, int32_t target) { des_wr(h, DES_ST_TARGET, (uint32_t)target); }
int32_t des_step_pos(const des_hw_t *h) { return (int32_t)des_rd(h, DES_ST_POS); }
uint32_t des_step_status(const des_hw_t *h) { return des_rd(h, DES_ST_STATUS); }
double des_step_speed(const des_hw_t *h) { return des_rd(h, DES_ST_CUR_INC) * (double)DES_FREE_CNT_HZ / 4294967296.0; }
void des_step_set_vmax(des_hw_t *h, double steps_per_s)
{
    des_wr(h, DES_ST_VMAX_INC, des_step_inc(steps_per_s < 0 ? -steps_per_s : steps_per_s));
}
int32_t des_step_brake_steps(const des_hw_t *h)
{
    return h->version >= DES_VERSION_CAPTURE ? (int32_t)des_rd(h, DES_ST_BRAKE) : -1;
}

/* ---------------- RS485 Modbus-RTU (des_485) ---------------- */
#define ST485_BUSY   0x1u
#define ST485_DONE   0x2u
#define ST485_TMO    0x4u
#define ST485_CRC    0x8u
#define ST485_OK     0x10u
#define ST485_FERR   0x20u
#define ST485_OVF    0x40u
#define ST485_LISTEN 0x80u
#define ST485_LF_OK  (1u << 19)
#define ST485_LF_ERR (1u << 20)
static pthread_mutex_t g_485_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_485_char_us = 1146, g_485_t35_us = 4011, g_485_timeout_us = 100000;   /* PL reset values (9600) */

int des_485_config(des_hw_t *h, uint32_t baud, int parity, uint32_t timeout_us)
{
    if (h->version < DES_VERSION_485 || des_rd(h, DES_485_ID) != DES_485_ID_VALUE) return DES_E_ARG;
    if (baud < 1200 || baud > 4000000 || parity < 0 || parity > 2) return DES_E_ARG;
    const uint32_t div = (DES_FREE_CNT_HZ + baud / 2) / baud;
    const uint32_t char_us = (11u * 1000000u + baud - 1) / baud;            /* 11 bits: start + 8 + parity + stop */
    const uint32_t t35 = baud > 19200 ? 1750u : (uint32_t)(3.5 * 11e6 / baud + 0.5);   /* Modbus over serial line */
    pthread_mutex_lock(&g_485_lock);
    des_wr(h, DES_485_DIV, div);
    des_wr(h, DES_485_CFG, (uint32_t)parity);
    des_wr(h, DES_485_T35_US, t35);
    if (timeout_us) { des_wr(h, DES_485_TIMEOUT_US, timeout_us); g_485_timeout_us = timeout_us; }
    g_485_char_us = char_us; g_485_t35_us = t35;
    pthread_mutex_unlock(&g_485_lock);
    return DES_OK;
}
static void rd485_frame(const des_hw_t *h, uint32_t st, des_485_result_t *r)
{
    int len = (int)((st >> 8) & 0xFF);
    if (len > 64) len = 64;
    for (int w = 0; w < (len + 3) / 4; w++) {
        const uint32_t v = des_rd(h, DES_485_RX_BUF + 4u * (uint32_t)w);
        for (int b = 0; b < 4 && 4 * w + b < len; b++) r->raw[4 * w + b] = (uint8_t)(v >> (8 * b));
    }
    r->n = (uint8_t)len;
    r->t_rx0 = des_rd(h, DES_485_RX_T0);
    r->t_rx1 = des_rd(h, DES_485_RX_T1);
}
static int xfer485(des_hw_t *h, const uint8_t *pdu, int n, int want_reply, des_485_result_t *r)
{
    if (des_rd(h, DES_485_STATUS) & ST485_BUSY) des_wr(h, DES_485_CTRL, 0x10u);   /* left over by a killed process */
    for (int w = 0; w < (n + 3) / 4; w++) {
        uint32_t v = 0;
        for (int b = 0; b < 4 && 4 * w + b < n; b++) v |= (uint32_t)pdu[4 * w + b] << (8 * b);
        des_wr(h, DES_485_TX_BUF + 4u * (uint32_t)w, v);
    }
    des_wr(h, DES_485_CTRL, ((uint32_t)n << 8) | (want_reply ? 2u : 0u) | 1u);
    /* own deadline: quiet bus (t3.5, after at most one 256-byte frame of someone else) + request + reply window +
       a 64-byte reply + t3.5, then 20 ms slack. Past it the bus never went quiet (A/B swapped, stuck low): abort. */
    const uint64_t c = g_485_char_us;
    uint64_t limit = g_485_t35_us + 256 * c + (uint64_t)(n + 3) * c + 20000;
    if (want_reply) limit += g_485_timeout_us + 66 * c + g_485_t35_us;
    const long poll = c < 200 ? 100 : 200;
    uint64_t waited = 0;
    uint32_t st = des_rd(h, DES_485_STATUS);
    while ((st & ST485_BUSY) && waited < limit) {
        sleep_us(poll);
        waited += (uint64_t)poll;
        st = des_rd(h, DES_485_STATUS);
    }
    if (st & ST485_BUSY) { des_wr(h, DES_485_CTRL, 0x10u); r->status = des_rd(h, DES_485_STATUS); return DES_E_BUSY; }
    r->status = st;
    r->t_tx0 = des_rd(h, DES_485_TX_T0);
    r->t_tx1 = des_rd(h, DES_485_TX_T1);
    if (!want_reply) return (st & ST485_DONE) ? DES_OK : DES_E_BUSY;
    if (st & ST485_TMO) return DES_E_TIMEOUT;
    rd485_frame(h, st, r);
    if (st & (ST485_FERR | ST485_OVF)) return DES_E_RXERR;
    if (!(st & ST485_OK)) return DES_E_CRC;
    r->n = (uint8_t)(r->n - 2);   /* OK implies >= 4 bytes; drop the CRC */
    return DES_OK;
}
int des_485_xfer(des_hw_t *h, const uint8_t *pdu, int n, int want_reply, des_485_result_t *r)
{
    des_485_result_t tmp;
    if (!r) r = &tmp;
    r->n = 0; r->status = 0; r->t_tx0 = r->t_tx1 = r->t_rx0 = r->t_rx1 = 0;
    if (h->version < DES_VERSION_485 || !pdu || n < 1 || n > 62) return DES_E_ARG;
    pthread_mutex_lock(&g_485_lock);
    const int rc = xfer485(h, pdu, n, want_reply, r);
    pthread_mutex_unlock(&g_485_lock);
    return rc;
}
int des_485_listen(des_hw_t *h, des_485_result_t *r)
{
    if (h->version < DES_VERSION_485 || !r) return DES_E_ARG;
    pthread_mutex_lock(&g_485_lock);
    const uint32_t st = des_rd(h, DES_485_STATUS);
    if (!(st & ST485_LISTEN)) { pthread_mutex_unlock(&g_485_lock); return 0; }
    r->status = st; r->t_tx0 = r->t_tx1 = 0;
    rd485_frame(h, st, r);
    des_wr(h, DES_485_CTRL, 0x8u);   /* ack: the next idle frame can be reported */
    pthread_mutex_unlock(&g_485_lock);
    if (st & ST485_LF_ERR) return DES_E_RXERR;
    if (!(st & ST485_LF_OK)) return DES_E_CRC;
    r->n = (uint8_t)(r->n - 2);
    return 1;
}
