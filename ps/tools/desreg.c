/* desreg: DragonEyeSorter PL register tool (/dev/mem), built on common/des_hw.
 *   desreg                         status
 *   desreg r <off> | w <off> <val> raw register access (offset inside PL_BASE window)
 *   desreg led on|off|auto
 *   desreg lat [n]                 PS->PL read interval, measured with the PL free-running counter
 *   desreg ping <id>               servo PING
 *   desreg sread <id> <addr> <n>   servo READ DATA (e.g. 0x38 2 = position, 0x2E 2 = current, 0x3F 1 = temp)
 *   desreg swrite <id> <addr> <b..> servo WRITE DATA (bytes)
 *   desreg sync <pos> <ms> <id...> servo SYNC WRITE target position / time to several ids
 *   desreg step status | vel <steps/s> [acc] | move <target> [vmax] [acc] | stop | halt | off | zero
 * Do not use the write commands while des_app is running (single-master servo bus / stepper).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/des_hw.h"

static des_hw_t hw;

static uint32_t rd(uint32_t off) { return des_rd(&hw, off); }

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
    if (integrated) {
        des_frame_t f;
        des_frame_read(&hw, &f);
        printf("FRAME_CNT    %u\n", f.cnt);
        printf("FRAME_PERIOD %u ticks = %.3f ms = %.2f fps\n", f.period, f.period * 1e3 / DES_FREE_CNT_HZ,
               f.period ? (double)DES_FREE_CNT_HZ / f.period : 0.0);
        printf("SERVO frames %u, timeouts %u, rx errors %u\n",
               rd(DES_SB_CNT_FRAMES), rd(DES_SB_CNT_TIMEOUT), rd(DES_SB_CNT_RX_ERR));
        des_capture_t c;
        if (des_capture_read(&hw, &c) == DES_OK)
            printf("CAPTURE      seq %u, frame %u, start->done %.3f ms, belt pos %d -> %d\n", c.seq, c.frame,
                   (c.t1 - c.t0) * 1e3 / DES_FREE_CNT_HZ, c.pos0, c.pos1);
        uint32_t s = des_step_status(&hw);
        printf("STEPPER      pos %d, status 0x%x%s%s%s", des_step_pos(&hw), s,
               s & DES_ST_STATUS_MOVING ? " moving" : "", s & DES_ST_STATUS_HALTED ? " halted" : "",
               s & DES_ST_STATUS_ENABLED ? " enabled" : "");
        if (des_step_brake_steps(&hw) >= 0) printf(", brake %d steps", des_step_brake_steps(&hw));
        printf("\n");
    }
    return 0;
}

static int latency(int n)
{
    uint32_t prev = des_now(&hw), min = 0xffffffffu, max = 0;
    double sum = 0;
    for (int i = 0; i < n; i++) {
        uint32_t now = des_now(&hw);
        uint32_t d = now - prev;
        prev = now;
        if (d < min) min = d;
        if (d > max) max = d;
        sum += d;
    }
    double ns = 1e9 / DES_FREE_CNT_HZ;
    printf("back-to-back read interval over %d reads: min %.0f ns, avg %.0f ns, max %.0f ns\n",
           n, min * ns, sum / n * ns, max * ns);
    return 0;
}

/* run one servo transaction and print timing / reply like the original tool */
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

int main(int argc, char **argv)
{
    int orc = des_hw_open(&hw);
    if (orc == DES_E_OPEN) { perror("open/mmap /dev/mem"); return 1; }

    const char *cmd = argc > 1 ? argv[1] : "";
    int rc = 0;
    int read_only = argc == 1 || !strcmp(cmd, "r") || !strcmp(cmd, "lat");
    if (!read_only && orc == DES_E_ID) {
        /* on a bitstream without des_regs this window aliases the camera DMA registers */
        fprintf(stderr, "refusing to write: DES_ID mismatch (0x%08x), bitstream lacks des_regs\n", rd(DES_REG_ID));
        rc = 2;
    } else if (argc == 1) {
        rc = status();
    } else if (!strcmp(cmd, "r") && argc == 3) {
        uint32_t off = strtoul(argv[2], NULL, 0);
        printf("0x%08x\n", rd(off));
    } else if (!strcmp(cmd, "w") && argc == 4) {
        uint32_t off = strtoul(argv[2], NULL, 0), v = strtoul(argv[3], NULL, 0);
        des_wr(&hw, off, v);
        printf("0x%03x <- 0x%08x, readback 0x%08x\n", off, v, rd(off));
    } else if (!strcmp(cmd, "led") && argc == 3) {
        uint32_t v = !strcmp(argv[2], "on") ? 3 : !strcmp(argv[2], "off") ? 1 : 0;
        des_wr(&hw, DES_REG_LED_CTRL, v);
        printf("LED_CTRL <- %u\n", v);
    } else if (!strcmp(cmd, "lat")) {
        rc = latency(argc > 2 ? atoi(argv[2]) : 100000);
    } else if (!strcmp(cmd, "ping") && argc == 3) {
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
    } else if (!strcmp(cmd, "sync") && argc >= 5) {
        unsigned pos = strtoul(argv[2], NULL, 0), ms = strtoul(argv[3], NULL, 0);
        int nid = argc - 4;
        if (nid > 11) nid = 11;
        uint8_t ids[11];
        uint16_t p[11], t[11];
        for (int i = 0; i < nid; i++) {
            ids[i] = (uint8_t)strtoul(argv[4 + i], NULL, 0);
            p[i] = (uint16_t)pos;
            t[i] = (uint16_t)ms;
        }
        rc = des_servo_sync_pos(&hw, ids, p, t, nid);
        printf("sync %d servos -> pos %u in %u ms: %s, on wire %.1f us\n", nid, pos, ms, des_strerror(rc),
               des_ticks_us(rd(DES_SB_TX_T1) - rd(DES_SB_TX_T0)));
        rc = rc ? 5 : 0;
    } else if (!strcmp(cmd, "step") && argc >= 3) {
        const char *s = argv[2];
        if (!strcmp(s, "vel") && argc >= 4) {
            des_step_vel(&hw, atof(argv[3]), argc > 4 ? atof(argv[4]) : 20000.0);
        } else if (!strcmp(s, "move") && argc >= 4) {
            des_step_move(&hw, (int32_t)strtol(argv[3], NULL, 0), argc > 4 ? atof(argv[4]) : 8000.0,
                          argc > 5 ? atof(argv[5]) : 20000.0);
        } else if (!strcmp(s, "stop")) {
            des_step_stop(&hw);
        } else if (!strcmp(s, "halt")) {
            des_step_halt(&hw);
        } else if (!strcmp(s, "off")) {
            des_step_enable(&hw, 0);
        } else if (!strcmp(s, "zero")) {
            des_step_zero(&hw);
        }
        printf("stepper pos %d, speed %.0f steps/s, steps %u, status 0x%x\n", des_step_pos(&hw),
               des_step_speed(&hw), rd(DES_ST_STEPS), des_step_status(&hw));
    } else {
        fprintf(stderr, "usage: see header of desreg.c\n");
        rc = 1;
    }
    des_hw_close(&hw);
    return rc;
}
