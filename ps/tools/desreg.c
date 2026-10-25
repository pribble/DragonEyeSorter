/* desreg: DragonEyeSorter PL register tool (/dev/mem), built on hw/des_hw.
 *   desreg                       status;  r <off> | w <off> <val>   raw access in the PL_BASE window;  led on|off|auto
 *   lat [n]                      PS->PL read interval (PL free-running counter)
 *   cmdlat [n]                   PS issues a 6-servo SYNC WRITE (ids 100..105, nothing moves) -> first start bit on the bus
 *   steplat [n]                  PS issues a 10-step move -> first step pulse (polls STEPS)
 *   steptgt                      stepper target rewrite while moving: TARGET = POS stops at once (v9+); behind ramps down past
 *   ping <id> | sread <id> <addr> <n> | swrite <id> <addr> <b..> | sync <pos> <ms> <id...>   servo bus (READ 0x38 2 = pos)
 *   svsync [n]                   read-back checked SYNC WRITE of servos 1..6 to where they are (nothing moves), n times:
 *                                re-sends / unconfirmed / read retries (bus robustness under stepper noise)
 *   env DES_SB_RETRIES=<n>       read retries (default 2; 0 = raw single attempts, for error-rate measurements)
 *   step status | vel <steps/s> [acc] | move <target> [vmax] [acc] | stop | halt | off | zero
 *   vis [mode [thr] [edge_thr] [full|x0 y0 x1 y1]]   PL vision statistics / settings (v6; modes 0 camera .. 5 edges)
 *   pwm <ch> <us> | pwm off      flap servo pulse width (v7);   io [estop-clr|enc-zero|ir1-clr|watch [sec]]  (v11)
 *   485 [cfg <baud> [N|E|O] [timeout_ms] | tx <hex..> | bc <hex..> | soak <n> <hex..> | listen [sec] | clr]
 *                                RS485 Modbus-RTU (v16): PDU bytes in hex without CRC (the PL adds / checks it)
 * Do not use the write commands while des_app is running (single-master servo bus / stepper).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "des_hw.h"

static des_hw_t hw;
static uint32_t rd(uint32_t off) { return des_rd(&hw, off); }

/* min / avg / max of a FREE_CNT delta series (the three latency subcommands use the same shape) */
typedef struct { uint32_t min, max; double sum; } stat_t;
static void stat_add(stat_t *s, uint32_t v)
{
    if (v < s->min) s->min = v;
    if (v > s->max) s->max = v;
    s->sum += v;
}

static int status(void)
{
    uint32_t id = rd(DES_REG_ID);
    printf("ID           0x%08x %s\n", id, id == DES_ID_VALUE ? "(ok)" : "(UNEXPECTED: bitstream lacks des_regs)");
    if (id != DES_ID_VALUE) return 2;
    uint32_t ver = rd(DES_REG_VERSION);
    int integrated = ver >= 2 && ver < 0x10000;     /* v1 held a build date (0x20261010) here */
    printf("VERSION      0x%08x%s\n", ver, integrated ? "" : " (v1: register channel only)");
    printf("SCRATCH      0x%08x\n", rd(DES_REG_SCRATCH));
    printf("FREE_CNT     %u\n", rd(DES_REG_FREE_CNT));
    printf("LED_CTRL     0x%08x\n", rd(DES_REG_LED_CTRL));
    if (!integrated) return 0;
    des_frame_t f;
    des_frame_read(&hw, &f);
    printf("FRAME_CNT    %u\n", f.cnt);
    printf("FRAME_PERIOD %u ticks = %.3f ms = %.2f fps\n", f.period, f.period * 1e3 / DES_FREE_CNT_HZ,
           f.period ? (double)DES_FREE_CNT_HZ / f.period : 0.0);
    printf("SERVO frames %u, timeouts %u, rx errors %u\n", rd(DES_SB_CNT_FRAMES), rd(DES_SB_CNT_TIMEOUT),
           rd(DES_SB_CNT_RX_ERR));
    des_capture_t c;
    if (des_capture_read(&hw, &c) == DES_OK)
        printf("CAPTURE      seq %u, frame %u, start->done %.3f ms, belt pos %d -> %d%s", c.seq, c.frame,
               (c.t1 - c.t0) * 1e3 / DES_FREE_CNT_HZ, c.pos0, c.pos1, ver >= DES_VERSION_CAPTURE_ENC ? "" : "\n");
    if (ver >= DES_VERSION_CAPTURE_ENC) printf(", encoder %d -> %d\n", c.enc0, c.enc1);
    if (ver >= DES_VERSION_485 && rd(DES_485_ID) == DES_485_ID_VALUE)
        printf("RS485        %u baud, parity %c | sent %u, ok %u, timeouts %u, CRC errors %u, frame errors %u, "
               "listen %u\n", DES_FREE_CNT_HZ / rd(DES_485_DIV), "NEO?"[rd(DES_485_CFG) & 3], rd(DES_485_CNT_TX),
               rd(DES_485_CNT_OK), rd(DES_485_CNT_TIMEOUT), rd(DES_485_CNT_CRC), rd(DES_485_CNT_FERR),
               rd(DES_485_CNT_LISTEN));
    uint32_t s = des_step_status(&hw);
    printf("STEPPER      pos %d, status 0x%x%s%s%s", des_step_pos(&hw), s, s & DES_ST_STATUS_MOVING ? " moving" : "",
           s & DES_ST_STATUS_HALTED ? " halted" : "", s & DES_ST_STATUS_ENABLED ? " enabled" : "");
    if (des_step_brake_steps(&hw) >= 0) printf(", brake %d steps", des_step_brake_steps(&hw));
    printf("\n");
    return 0;
}

static int latency(int n)
{
    uint32_t prev = des_now(&hw);
    stat_t st = { 0xffffffffu, 0, 0 };
    for (int i = 0; i < n; i++) {
        uint32_t now = des_now(&hw);
        stat_add(&st, now - prev);
        prev = now;
    }
    double ns = 1e9 / DES_FREE_CNT_HZ;
    printf("back-to-back read interval over %d reads: min %.0f ns, avg %.0f ns, max %.0f ns\n", n, st.min * ns,
           st.sum / n * ns, st.max * ns);
    return 0;
}

/* command response: PS decides to send (FREE_CNT) -> first start bit of a 6-servo SYNC WRITE on the bus (PL TX_T0),
 * including the frame writes; servo ids 100..105 (nobody answers / moves). */
static int cmdlat(int n)
{
    const uint8_t ids[6] = { 100, 101, 102, 103, 104, 105 };
    uint16_t pos[6], ms[6];
    for (int i = 0; i < 6; i++) pos[i] = 2048, ms[i] = 500;
    stat_t st = { 0xffffffffu, 0, 0 };
    double wire = 0;
    for (int i = 0; i < n; i++) {
        uint32_t t0 = des_now(&hw);
        int rc = des_servo_sync_pos(&hw, ids, pos, ms, 6);
        if (rc) { printf("SYNC WRITE failed: %s\n", des_strerror(rc)); return 5; }
        stat_add(&st, rd(DES_SB_TX_T0) - t0);
        wire += rd(DES_SB_TX_T1) - rd(DES_SB_TX_T0);
    }
    printf("command -> first start bit on the bus, %d x 6-servo SYNC WRITE: min %.2f us, avg %.2f us, max %.2f us; "
           "frame on the wire %.1f us\n", n, des_ticks_us(st.min), des_ticks_us((uint32_t)(st.sum / n)),
           des_ticks_us(st.max), des_ticks_us((uint32_t)(wire / n)));
    return 0;
}

/* stepper response: move command -> first step pulse (STEPS changes), polled back to back (~0.4 us); +-10 steps. */
static int steplat(int n)
{
    des_step_enable(&hw, 1);
    usleep(20000);
    stat_t st = { 0xffffffffu, 0, 0 };
    uint32_t lost = 0;
    for (int i = 0; i < n; i++) {
        int32_t p = des_step_pos(&hw);
        uint32_t s0 = rd(DES_ST_STEPS), t0 = des_now(&hw), t = t0;
        des_step_move(&hw, p + ((i & 1) ? -10 : 10), 2000, 20000);
        while (rd(DES_ST_STEPS) == s0) {
            t = des_now(&hw);
            if (t - t0 > DES_FREE_CNT_HZ / 10) break;   /* 100 ms: no step at all */
        }
        if (t - t0 > DES_FREE_CNT_HZ / 10) { lost++; continue; }
        stat_add(&st, t - t0);
        for (int g = 0; g < 200 && (des_step_status(&hw) & DES_ST_STATUS_MOVING); g++) usleep(1000);
    }
    if (lost == (uint32_t)n) { printf("no step pulse within 100 ms (halted? see desreg step status)\n"); return 5; }
    printf("move command -> first step pulse, %d runs: min %.2f us, avg %.2f us, max %.2f us%s\n", n, des_ticks_us(st.min),
           des_ticks_us((uint32_t)(st.sum / (n - lost))), des_ticks_us(st.max),
           lost ? " (some runs gave no step)" : "");
    return 0;
}

/* stepper TARGET rewrite while moving (belt "stop at"): slow move (100 steps/s, 10 ms per step) so a target written
 * microseconds after reading POS equals POS. v9+: stops at once with AT_TARGET; v8 and older stay MOVING. Then a
 * target behind the travel at speed: must ramp down past it (not stop abruptly). */
static int steptgt(void)
{
    des_step_enable(&hw, 1);
    usleep(20000);
    int32_t p0 = des_step_pos(&hw);
    des_step_move(&hw, p0 + 100000, 100, 100000);   /* reaches 100 steps/s within a ramp tick or two */
    usleep(300000);
    int32_t p = des_step_pos(&hw);
    des_step_set_target(&hw, p);
    uint32_t t0 = des_now(&hw), st;
    while (((st = des_step_status(&hw)) & DES_ST_STATUS_MOVING) && des_now(&hw) - t0 < DES_FREE_CNT_HZ / 20) {}
    const double us = des_ticks_us(des_now(&hw) - t0);
    const int32_t pe = des_step_pos(&hw);
    int rc = 0;
    if (st & DES_ST_STATUS_MOVING) {
        printf("TARGET = POS (%d) while moving: still MOVING after 50 ms (PL v8 and older)\n", p);
        des_step_halt(&hw);
        rc = 6;
    } else {
        printf("TARGET = POS (%d) while moving: stopped after %.1f us at %d, AT_TARGET %d\n", p, us, pe,
               !!(st & DES_ST_STATUS_AT_TARGET));
        if (!(st & DES_ST_STATUS_AT_TARGET) || pe != p) rc = 6;
    }
    /* overshoot: cruise at 4000 steps/s, then a target 10 steps behind */
    usleep(20000);
    des_step_enable(&hw, 1);   /* clears a HALT */
    p0 = des_step_pos(&hw);
    des_step_move(&hw, p0 + 1000000, 4000, 20000);
    usleep(400000);
    p = des_step_pos(&hw);
    des_step_set_target(&hw, p - 10);
    t0 = des_now(&hw);
    while ((des_step_status(&hw) & DES_ST_STATUS_MOVING) && des_now(&hw) - t0 < DES_FREE_CNT_HZ) {}
    const double ms = des_ticks_us(des_now(&hw) - t0) / 1000.0;
    const int32_t past = des_step_pos(&hw) - p;
    printf("target behind at 4000 steps/s: ramped down in %.1f ms, %d steps past\n", ms, past);
    if (ms < 50 || past < 50) rc = 6;   /* 4000 -> 0 at 20000 steps/s^2 = 200 ms, ~400 steps */
    return rc;
}
/* one servo transaction: print on-wire time / reply exactly like the original tool */
static int sb_print(const uint8_t *body, int n, int expect_reply, des_sb_result_t *out)
{
    des_sb_result_t r;
    int rc = des_sb_xfer(&hw, body, n, expect_reply, &r);
    if (out) *out = r;
    printf("tx %d body bytes, on wire %.1f us", n, des_ticks_us(r.t_tx1 - r.t_tx0));
    if (rc == DES_E_BUSY) { printf(", BUS STUCK BUSY\n"); return 5; }
    if (!expect_reply) { printf("\n"); return 0; }
    if (rc == DES_E_TIMEOUT) { printf(", TIMEOUT (no reply)\n"); return 3; }
    printf(", reply after %.1f us, %d bytes:", des_ticks_us(r.t_rx - r.t_tx1), r.n);
    for (int i = 0; i < r.n; i++) printf(" %02x", r.raw[i]);
    printf("%s, servo status 0x%02x%s%s%s\n", rc == DES_E_RXERR ? " CHECKSUM/FRAME ERROR" : "", r.status,
           r.status & 0x20 ? " overload" : "", r.status & 0x04 ? " over-temp" : "", r.status & 0x01 ? " voltage" : "");
    return rc == DES_OK ? 0 : 4;
}
/* ---- desreg 485 ... ---- */
static int hex_pdu(int argc, char **argv, uint8_t *pdu)
{
    if (argc < 1 || argc > 62) { fprintf(stderr, "485: 1..62 PDU bytes in hex (address, function, data; no CRC)\n"); return -1; }
    for (int i = 0; i < argc; i++) pdu[i] = (uint8_t)strtoul(argv[i], NULL, 16);
    return argc;
}
static const char *err485(int rc)   /* des_strerror() words its timeout / framing texts for the servo bus */
{
    return rc == DES_OK ? "ok" : rc == DES_E_TIMEOUT ? "timeout (no reply)" : rc == DES_E_CRC ? "CRC error" :
           rc == DES_E_RXERR ? "parity / stop-bit error or reply > 64 bytes" :
           rc == DES_E_BUSY ? "bus never quiet (aborted)" : des_strerror(rc);
}
static void print_485(int rc, const des_485_result_t *r, int want_reply)
{
    printf("%s", err485(rc));
    if (r->t_tx1) printf("  request %.3f ms", (r->t_tx1 - r->t_tx0) * 1e3 / DES_FREE_CNT_HZ);
    if (want_reply && (rc == DES_OK || rc == DES_E_CRC || rc == DES_E_RXERR)) {
        printf("  reply after %.3f ms:", (r->t_rx0 - r->t_tx1) * 1e3 / DES_FREE_CNT_HZ);
        for (int i = 0; i < r->n; i++) printf(" %02X", r->raw[i]);
        if (rc == DES_OK && r->n >= 2 && (r->raw[1] & 0x80)) printf("  (Modbus exception %u)", r->raw[2]);
    }
    printf("\n");
}
static int rs485_cmd(int argc, char **argv)
{
    if (hw.version < DES_VERSION_485 || rd(DES_485_ID) != DES_485_ID_VALUE) {
        fprintf(stderr, "bitstream before v16: no des_485\n");
        return 2;
    }
    uint8_t pdu[64];
    des_485_result_t r;
    if (argc == 0) {
        const uint32_t st = rd(DES_485_STATUS);
        printf("RS485 %u baud parity %c, t3.5 %u us, timeout %u us | status 0x%08x%s%s, quiet %u us\n",
               DES_FREE_CNT_HZ / rd(DES_485_DIV), "NEO?"[rd(DES_485_CFG) & 3], rd(DES_485_T35_US),
               rd(DES_485_TIMEOUT_US), st, st & 1 ? " busy" : "", st & 0x80 ? " listen-frame" : "", rd(DES_485_QUIET_US));
        printf("sent %u, ok %u, timeouts %u, CRC errors %u, frame errors %u, listen %u\n", rd(DES_485_CNT_TX),
               rd(DES_485_CNT_OK), rd(DES_485_CNT_TIMEOUT), rd(DES_485_CNT_CRC), rd(DES_485_CNT_FERR),
               rd(DES_485_CNT_LISTEN));
        return 0;
    }
    const char *s = argv[0];
    if (!strcmp(s, "cfg") && argc >= 2) {
        const char pc = argc > 2 ? argv[2][0] : 'N';
        const int par = (pc == 'E' || pc == 'e') ? 1 : (pc == 'O' || pc == 'o') ? 2 : 0;
        const int rc = des_485_config(&hw, (uint32_t)atoi(argv[1]), par, argc > 3 ? (uint32_t)(atof(argv[3]) * 1000) : 0);
        if (rc) fprintf(stderr, "485 cfg: %s\n", des_strerror(rc));
        return rc ? 1 : 0;
    }
    if (!strcmp(s, "clr")) { des_wr(&hw, DES_485_CTRL, 0x4u); return 0; }
    if ((!strcmp(s, "tx") || !strcmp(s, "bc")) && argc >= 2) {
        const int n = hex_pdu(argc - 1, argv + 1, pdu), want = !strcmp(s, "tx");
        if (n < 0) return 1;
        const int rc = des_485_xfer(&hw, pdu, n, want, &r);
        print_485(rc, &r, want);
        return rc == DES_OK ? 0 : 1;
    }
    if (!strcmp(s, "soak") && argc >= 3) {   /* n requests: success rate and reply latency for the test report */
        const int count = atoi(argv[1]), n = hex_pdu(argc - 2, argv + 2, pdu);
        if (n < 0 || count < 1) return 1;
        int ok = 0, tmo = 0, crc = 0, ferr = 0, other = 0;
        stat_t lat = {0xFFFFFFFFu, 0, 0};
        for (int k = 0; k < count; k++) {
            const int rc = des_485_xfer(&hw, pdu, n, 1, &r);
            if (rc == DES_OK) { ok++; stat_add(&lat, r.t_rx0 - r.t_tx1); }
            else if (rc == DES_E_TIMEOUT) tmo++;
            else if (rc == DES_E_CRC) crc++;
            else if (rc == DES_E_RXERR) ferr++;
            else other++;
        }
        printf("485 soak: %d requests, ok %d (%.2f%%), timeouts %d, CRC errors %d, frame errors %d, other %d", count, ok,
               100.0 * ok / count, tmo, crc, ferr, other);
        if (ok) printf(" | reply latency min %.3f / avg %.3f / max %.3f ms", lat.min * 1e3 / DES_FREE_CNT_HZ,
                       lat.sum / ok * 1e3 / DES_FREE_CNT_HZ, lat.max * 1e3 / DES_FREE_CNT_HZ);
        printf("\n");
        return ok == count ? 0 : 1;
    }
    if (!strcmp(s, "listen")) {   /* frames of another master (PC USB-RS485) while we are idle */
        const double sec = argc > 1 ? atof(argv[1]) : 10.0;
        for (int k = 0; k < (int)(sec * 100); k++) {
            const int rc = des_485_listen(&hw, &r);
            if (rc != 0) {
                printf("%10.3f  %s:", r.t_rx0 * 1e3 / DES_FREE_CNT_HZ, rc == 1 ? "frame" : err485(rc));
                for (int i = 0; i < r.n; i++) printf(" %02X", r.raw[i]);
                printf("\n");
                fflush(stdout);
            }
            usleep(10000);
        }
        return 0;
    }
    fprintf(stderr, "usage: desreg 485 [cfg <baud> [N|E|O] [timeout_ms] | tx <hex..> | bc <hex..> | soak <n> <hex..> | "
                    "listen [sec] | clr]\n");
    return 1;
}

int main(int argc, char **argv)
{
    int orc = des_hw_open(&hw);
    if (getenv("DES_SB_RETRIES")) des_servo_set_retries((unsigned)atoi(getenv("DES_SB_RETRIES")));
    if (orc == DES_E_OPEN) { perror("open/mmap /dev/mem"); return 1; }
    const char *cmd = argc > 1 ? argv[1] : "";
    int rc = 0;
    int read_only = argc == 1 || !strcmp(cmd, "r") || !strcmp(cmd, "lat") || (!strcmp(cmd, "vis") && argc == 2) ||
                    (!strcmp(cmd, "485") && argc == 2);
    if (!read_only && orc == DES_E_ID) {
        /* on a bitstream without des_regs this window aliases the camera DMA registers */
        fprintf(stderr, "refusing to write: DES_ID mismatch (0x%08x), bitstream lacks des_regs\n", rd(DES_REG_ID));
        rc = 2;
    } else if (argc == 1) {
        rc = status();
    } else if ((!strcmp(cmd, "r") && argc == 3) || (!strcmp(cmd, "w") && argc == 4)) {
        /* raw access: only the mapped window (DES_PL_SPAN), word aligned - anything else would fault or alias */
        uint32_t off = strtoul(argv[2], NULL, 0);
        if (off >= DES_PL_SPAN || (off & 3)) {
            fprintf(stderr, "offset 0x%x outside 0x000..0x%03x or not 4-byte aligned\n", off, DES_PL_SPAN - 4);
            rc = 1;
        } else if (cmd[0] == 'r') {
            printf("0x%08x\n", rd(off));
        } else {
        uint32_t v = strtoul(argv[3], NULL, 0);
        des_wr(&hw, off, v);
        printf("0x%03x <- 0x%08x, readback 0x%08x\n", off, v, rd(off));
        }
    } else if (!strcmp(cmd, "led") && argc == 3) {
        uint32_t v = !strcmp(argv[2], "on") ? 3 : !strcmp(argv[2], "off") ? 1 : 0;
        des_wr(&hw, DES_REG_LED_CTRL, v);
        printf("LED_CTRL <- %u\n", v);
    } else if (!strcmp(cmd, "lat")) rc = latency(argc > 2 ? atoi(argv[2]) : 100000);
    else if (!strcmp(cmd, "cmdlat")) rc = cmdlat(argc > 2 ? atoi(argv[2]) : 200);
    else if (!strcmp(cmd, "steplat")) rc = steplat(argc > 2 ? atoi(argv[2]) : 100);
    else if (!strcmp(cmd, "steptgt")) rc = steptgt();
    else if (!strcmp(cmd, "ping") && argc == 3) {
        uint8_t b[3] = { (uint8_t)strtoul(argv[2], NULL, 0), 2, 0x01 };
        rc = sb_print(b, 3, 1, NULL);
    } else if (!strcmp(cmd, "sread") && argc == 5) {
        uint8_t n = (uint8_t)strtoul(argv[4], NULL, 0);
        uint8_t b[5] = { (uint8_t)strtoul(argv[2], NULL, 0), 4, 0x02, (uint8_t)strtoul(argv[3], NULL, 0), n };
        des_sb_result_t r;
        rc = sb_print(b, 5, 1, &r);
        if (rc == 0 && n == 2 && r.n >= 6) printf("value = %u\n", (r.raw[3] << 8) | r.raw[4]);
        if (rc == 0 && n == 1 && r.n >= 5) printf("value = %u\n", r.raw[3]);
    } else if (!strcmp(cmd, "swrite") && argc >= 5) {
        uint8_t b[64];
        int np = argc - 3;                   /* addr + data bytes */
        if (np > 60) np = 60;
        b[0] = (uint8_t)strtoul(argv[2], NULL, 0);
        b[1] = (uint8_t)(np + 2);
        b[2] = 0x03;
        for (int i = 0; i < np; i++) b[3 + i] = (uint8_t)strtoul(argv[3 + i], NULL, 0);
        rc = sb_print(b, 3 + np, b[0] != DES_SERVO_BROADCAST, NULL);
    } else if (!strcmp(cmd, "svsync")) {
        /* goal = the position each servo reports, read twice and only used when both agree: nothing moves */
        const int rounds = argc > 2 ? atoi(argv[2]) : 50;
        uint8_t ids[6];
        uint16_t pos[6], ms[6];
        int n = 0;
        for (uint8_t id = 1; id <= 6; id++) {
            uint16_t a, b;
            if (des_servo_read_u16(&hw, id, 0x38, &a) != DES_OK || des_servo_read_u16(&hw, id, 0x38, &b) != DES_OK ||
                a != b) {
                printf("servo %u: position not read twice the same, left out\n", id);
                continue;
            }
            ids[n] = id; pos[n] = a; ms[n] = 300; n++;
        }
        int fail = 0;
        for (int k = 0; k < rounds && n > 0; k++)
            if (des_servo_sync_pos_checked(&hw, ids, pos, ms, n, 0) != DES_OK) fail++;
        des_sb_stats_t st;
        des_sb_stats(&st);
        printf("svsync: %d servos x %d commands: %d not confirmed | re-sent %u, unconfirmed %u, read retries %u, "
               "reads failed after retries %u\n", n, rounds, fail, st.resends, st.unconfirmed, st.retries,
               st.read_failures);
        rc = fail ? 1 : 0;
    } else if (!strcmp(cmd, "sync") && argc >= 5) {
        unsigned pos = strtoul(argv[2], NULL, 0), ms = strtoul(argv[3], NULL, 0);
        int nid = argc - 4;
        if (nid > 11) nid = 11;
        uint8_t ids[11];
        uint16_t p[11], t[11];
        for (int i = 0; i < nid; i++) ids[i] = (uint8_t)strtoul(argv[4 + i], NULL, 0), p[i] = pos, t[i] = ms;
        rc = des_servo_sync_pos(&hw, ids, p, t, nid);
        printf("sync %d servos -> pos %u in %u ms: %s, on wire %.1f us\n", nid, pos, ms, des_strerror(rc),
               des_ticks_us(rd(DES_SB_TX_T1) - rd(DES_SB_TX_T0)));
        rc = rc ? 5 : 0;
    } else if (!strcmp(cmd, "pwm") && argc >= 3) {
        int usage = 0;
        if (!strcmp(argv[2], "off")) rc = des_pwm_enable(&hw, 0) ? 2 : 0;
        else if (argc >= 4 && (!strcmp(argv[2], "0") || !strcmp(argv[2], "1"))) {   // PL has two flap channels
            int ch = atoi(argv[2]);
            rc = des_pwm_set(&hw, ch, atoi(argv[3])) ? 2 : 0;
            if (!rc) rc = des_pwm_enable(&hw, (rd(DES_PWM_CTRL) & 3u) | (1u << ch)) ? 2 : 0;
        } else {
            fprintf(stderr, "usage: desreg pwm <ch 0|1> <us> | desreg pwm off\n");
            usage = rc = 1;
        }
        if (rc && !usage) fprintf(stderr, "bitstream before v7: no des_pwm\n");
    } else if (!strcmp(cmd, "io")) {
        des_io_t io;
        if (des_io_read(&hw, &io) != DES_OK) {
            fprintf(stderr, "bitstream before v11: no des_io\n");
            rc = 2;
        } else if (argc >= 3 && !strcmp(argv[2], "estop-clr")) {
            int still = des_estop_clear(&hw);
            printf("e-stop %s\n", still ? "still latched (button still pressed?)" : "cleared");
            rc = still ? 1 : 0;
        } else if (argc >= 3 && !strcmp(argv[2], "enc-zero")) {
            des_enc_zero(&hw);
        } else if (argc >= 3 && !strcmp(argv[2], "ir1-clr")) {
            des_wr(&hw, DES_IO_CTRL, 4u);
        } else {
            double sec = (argc >= 4 && !strcmp(argv[2], "watch")) ? atof(argv[3]) : 0;
            uint32_t last = 0xFFFFFFFFu;
            for (int k = 0;; k++) {
                des_io_read(&hw, &io);
                const uint32_t s = io.status;
                if (k == 0 || s != last || sec > 0)
                    printf("estop %s%s  IR1 %s (%u edges, last pos %d enc %d)  LIMIT1 %s  enc %d (%+d /10ms, err %u, Z %u @ %d)  A%u B%u Z%u\n",
                           s & DES_IO_ST_ESTOP_LATCHED ? "LATCHED" : "ok", s & DES_IO_ST_ESTOP_NOW ? " (pressed)" : "",
                           s & DES_IO_ST_IR1_BLOCKED ? "blocked" : "clear", io.ir1_cnt, io.ir1_pos, (int)io.ir1_enc,
                           s & DES_IO_ST_LIMIT1 ? "active" : "off", io.enc, io.enc_speed, io.enc_err, io.enc_zcnt,
                           io.enc_zpos, (s >> 4) & 1, (s >> 5) & 1, (s >> 6) & 1);
                last = s;
                if (sec <= 0 || k >= (int)(sec * 5)) break;
                usleep(200000);
            }
        }
    } else if (!strcmp(cmd, "485")) {
        rc = rs485_cmd(argc - 2, argv + 2);
    } else if (!strcmp(cmd, "vis")) {
        if (argc >= 3) {
            int mode = atoi(argv[2]), thr = argc > 3 ? atoi(argv[3]) : 128, ethr = argc > 4 ? atoi(argv[4]) : 200;
            int full = argc <= 5 || !strcmp(argv[5], "full");
            if (!full && argc >= 9) des_vis_roi(&hw, atoi(argv[5]), atoi(argv[6]), atoi(argv[7]), atoi(argv[8]));
            rc = des_vis_config(&hw, mode, full, thr, ethr) == DES_OK ? 0 : 2;
            if (rc) fprintf(stderr, "bitstream before v6: no des_vision\n");
        } else {
            des_vis_t v;
            int e = des_vis_read(&hw, &v);
            if (e != DES_OK) {
                fprintf(stderr, "%s\n", e == DES_E_BUSY ? "des_vision statistics never stable (frame starts every few us?)"
                                                        : "bitstream before v6: no des_vision");
                rc = 2;
            } else printf("seq %u mode %d full %d thr %d lanes %d | area %u bbox x %d..%d y %d..%d centroid (%.1f, %.1f) "
                          "edges %u | %u pixel clocks/frame\n", v.seq, v.mode, v.full, v.thr, v.lanes, v.area, v.x0, v.x1,
                          v.y0, v.y1, v.cx, v.cy, v.edges, v.cycles);
        }
    } else if (!strcmp(cmd, "step") && argc >= 3) {
        const char *s = argv[2];
        if (!strcmp(s, "vel") && argc >= 4) des_step_vel(&hw, atof(argv[3]), argc > 4 ? atof(argv[4]) : 20000.0);
        else if (!strcmp(s, "move") && argc >= 4)
            des_step_move(&hw, (int32_t)strtol(argv[3], NULL, 0), argc > 4 ? atof(argv[4]) : 8000.0,
                          argc > 5 ? atof(argv[5]) : 20000.0);
        else if (!strcmp(s, "stop")) des_step_stop(&hw);
        else if (!strcmp(s, "halt")) des_step_halt(&hw);
        else if (!strcmp(s, "off")) des_step_enable(&hw, 0);
        else if (!strcmp(s, "zero")) des_step_zero(&hw);
        printf("stepper pos %d, speed %.0f steps/s, steps %u, status 0x%x\n", des_step_pos(&hw), des_step_speed(&hw),
               rd(DES_ST_STEPS), des_step_status(&hw));
    } else {
        fprintf(stderr, "usage: see header of desreg.c\n");
        rc = 1;
    }
    des_hw_close(&hw);
    return rc;
}
