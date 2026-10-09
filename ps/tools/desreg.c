/* desreg: DragonEyeSorter PL register tool (/dev/mem).
 *   desreg                         status
 *   desreg r <off> | w <off> <val> raw register access (offset inside PL_BASE window)
 *   desreg led on|off|auto
 *   desreg lat [n]                 PS->PL read interval, measured with the PL free-running counter
 *   desreg ping <id>               servo PING
 *   desreg sread <id> <addr> <n>   servo READ DATA (e.g. 0x38 2 = position, 0x2E 2 = current, 0x3F 1 = temp)
 *   desreg swrite <id> <addr> <b..> servo WRITE DATA (bytes)
 *   desreg sync <pos> <ms> <id...> servo SYNC WRITE target position / time to several ids
 *   desreg step status | vel <steps/s> | move <target> [vmax] [acc] | stop | halt | off | zero
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "../common/des_regs.h"

static volatile uint32_t *regs;

static uint32_t rd(uint32_t off) { return regs[off / 4]; }
static void wr(uint32_t off, uint32_t v) { regs[off / 4] = v; }

static void sleep_us(long us)
{
    struct timespec ts = { us / 1000000, (us % 1000000) * 1000 };
    nanosleep(&ts, NULL);
}

static int status(void)
{
    uint32_t id = rd(DES_REG_ID);
    printf("ID           0x%08x %s\n", id, id == DES_ID_VALUE ? "(ok)" : "(UNEXPECTED: bitstream lacks des_regs)");
    if (id != DES_ID_VALUE) return 2;
    uint32_t ver = rd(DES_REG_VERSION);
    printf("VERSION      0x%08x\n", ver);
    printf("SCRATCH      0x%08x\n", rd(DES_REG_SCRATCH));
    printf("FREE_CNT     %u\n", rd(DES_REG_FREE_CNT));
    printf("LED_CTRL     0x%08x\n", rd(DES_REG_LED_CTRL));
    if (ver >= 2) {
        uint32_t p = rd(DES_REG_FRAME_PERIOD);
        printf("FRAME_CNT    %u\n", rd(DES_REG_FRAME_CNT));
        printf("FRAME_PERIOD %u ticks = %.3f ms = %.2f fps\n", p, p * 1e3 / DES_FREE_CNT_HZ,
               p ? (double)DES_FREE_CNT_HZ / p : 0.0);
        printf("SERVO frames %u, timeouts %u, rx errors %u\n",
               rd(DES_SB_CNT_FRAMES), rd(DES_SB_CNT_TIMEOUT), rd(DES_SB_CNT_RX_ERR));
        uint32_t s = rd(DES_ST_STATUS);
        printf("STEPPER      pos %d, status 0x%x%s%s%s\n", (int32_t)rd(DES_ST_POS), s,
               s & 1 ? " moving" : "", s & 8 ? " halted" : "", s & 16 ? " enabled" : "");
    }
    return 0;
}

static int latency(int n)
{
    uint32_t prev = rd(DES_REG_FREE_CNT), min = 0xffffffffu, max = 0;
    double sum = 0;
    for (int i = 0; i < n; i++) {
        uint32_t now = rd(DES_REG_FREE_CNT);
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

/* ---------------- servo bus ---------------- */
static int sb_xfer(const uint8_t *body, int n, int expect_reply)
{
    for (int w = 0; w < (n + 3) / 4; w++) {
        uint32_t v = 0;
        for (int b = 0; b < 4 && 4 * w + b < n; b++) v |= (uint32_t)body[4 * w + b] << (8 * b);
        wr(DES_SB_TX_BUF + 4 * w, v);
    }
    wr(DES_SB_CTRL, ((uint32_t)n << 8) | (expect_reply ? 2u : 0u) | 1u);
    uint32_t st;
    int guard = 0;
    do { sleep_us(200); st = rd(DES_SB_STATUS); } while (((st & 1) || !(st & 2)) && ++guard < 500);

    uint32_t t0 = rd(DES_SB_TX_T0), t1 = rd(DES_SB_TX_T1), rt = rd(DES_SB_RX_T);
    printf("tx %d body bytes, on wire %.1f us", n, (t1 - t0) * 1e6 / DES_FREE_CNT_HZ);
    if (!expect_reply) { printf("\n"); return 0; }
    if (st & 4) { printf(", TIMEOUT (no reply)\n"); return 3; }
    int cnt = (st >> 8) & 0x7f;
    printf(", reply after %.1f us, %d bytes:", (rt - t1) * 1e6 / DES_FREE_CNT_HZ, cnt);
    for (int i = 0; i < cnt && i < 32; i++) printf(" %02x", (rd(DES_SB_RX_BUF + (i & ~3)) >> (8 * (i & 3))) & 0xff);
    printf("%s, servo status 0x%02x%s%s%s\n", st & 8 ? " CHECKSUM/FRAME ERROR" : "", (st >> 16) & 0xff,
           (st >> 16) & 0x20 ? " overload" : "", (st >> 16) & 0x04 ? " over-temp" : "", (st >> 16) & 0x01 ? " voltage" : "");
    return st & 16 ? 0 : 4;
}

static uint8_t rx_byte(int i) { return (rd(DES_SB_RX_BUF + (i & ~3)) >> (8 * (i & 3))) & 0xff; }

/* ---------------- stepper ---------------- */
static void step_params(double vmax, double acc)
{
    wr(DES_ST_VMAX_INC, des_step_inc(vmax));
    wr(DES_ST_VMIN_INC, des_step_inc(vmax < 500 ? vmax : 500));
    wr(DES_ST_ACC_INC, des_step_inc(acc / DES_ST_RAMP_HZ));
}

int main(int argc, char **argv)
{
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { perror("open /dev/mem"); return 1; }
    void *p = mmap(NULL, DES_PL_SPAN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, DES_PL_BASE);
    if (p == MAP_FAILED) { perror("mmap"); return 1; }
    regs = (volatile uint32_t *)p;

    const char *cmd = argc > 1 ? argv[1] : "";
    int rc = 0;
    int read_only = argc == 1 || !strcmp(cmd, "r") || !strcmp(cmd, "lat");
    if (!read_only && rd(DES_REG_ID) != DES_ID_VALUE) {
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
        wr(off, v);
        printf("0x%03x <- 0x%08x, readback 0x%08x\n", off, v, rd(off));
    } else if (!strcmp(cmd, "led") && argc == 3) {
        uint32_t v = !strcmp(argv[2], "on") ? 3 : !strcmp(argv[2], "off") ? 1 : 0;
        wr(DES_REG_LED_CTRL, v);
        printf("LED_CTRL <- %u\n", v);
    } else if (!strcmp(cmd, "lat")) {
        rc = latency(argc > 2 ? atoi(argv[2]) : 100000);
    } else if (!strcmp(cmd, "ping") && argc == 3) {
        uint8_t b[3] = { (uint8_t)strtoul(argv[2], NULL, 0), 2, 0x01 };
        rc = sb_xfer(b, 3, 1);
    } else if (!strcmp(cmd, "sread") && argc == 5) {
        uint8_t n = (uint8_t)strtoul(argv[4], NULL, 0);
        uint8_t b[5] = { (uint8_t)strtoul(argv[2], NULL, 0), 4, 0x02, (uint8_t)strtoul(argv[3], NULL, 0), n };
        rc = sb_xfer(b, 5, 1);
        if (rc == 0 && n == 2) printf("value = %u\n", (rx_byte(3) << 8) | rx_byte(4));
        if (rc == 0 && n == 1) printf("value = %u\n", rx_byte(3));
    } else if (!strcmp(cmd, "swrite") && argc >= 5) {
        uint8_t b[64];
        int np = argc - 3;                   /* addr + data bytes */
        b[0] = (uint8_t)strtoul(argv[2], NULL, 0);
        b[1] = (uint8_t)(np + 2);
        b[2] = 0x03;
        for (int i = 0; i < np && i < 60; i++) b[3 + i] = (uint8_t)strtoul(argv[3 + i], NULL, 0);
        rc = sb_xfer(b, 3 + np, b[0] != 0xFE);
    } else if (!strcmp(cmd, "sync") && argc >= 5) {
        unsigned pos = strtoul(argv[2], NULL, 0), ms = strtoul(argv[3], NULL, 0);
        int nid = argc - 4;
        if (nid > 11) nid = 11;
        uint8_t b[64];
        int k = 0;
        b[k++] = 0xFE; b[k++] = (uint8_t)(2 + 2 + 5 * nid); b[k++] = 0x83; b[k++] = 0x2A; b[k++] = 4;
        for (int i = 0; i < nid; i++) {
            b[k++] = (uint8_t)strtoul(argv[4 + i], NULL, 0);
            b[k++] = pos >> 8; b[k++] = pos & 0xff; b[k++] = ms >> 8; b[k++] = ms & 0xff;
        }
        rc = sb_xfer(b, k, 0);
    } else if (!strcmp(cmd, "step") && argc >= 3) {
        const char *s = argv[2];
        if (!strcmp(s, "vel") && argc >= 4) {
            double v = atof(argv[3]);
            step_params(v < 0 ? -v : v, argc > 4 ? atof(argv[4]) : 20000.0);
            wr(DES_ST_CTRL, 0x01 | 0x02 | (v < 0 ? 0x40 : 0));
        } else if (!strcmp(s, "move") && argc >= 4) {
            step_params(argc > 4 ? atof(argv[4]) : 8000.0, argc > 5 ? atof(argv[5]) : 20000.0);
            wr(DES_ST_TARGET, (uint32_t)strtol(argv[3], NULL, 0));
            wr(DES_ST_CTRL, 0x01 | 0x02 | 0x10);
        } else if (!strcmp(s, "stop")) {
            wr(DES_ST_CTRL, 0x01 | 0x04 | (rd(DES_ST_CTRL) & 0x50));
        } else if (!strcmp(s, "halt")) {
            wr(DES_ST_CTRL, 0x01 | 0x08 | (rd(DES_ST_CTRL) & 0x50));
        } else if (!strcmp(s, "off")) {
            wr(DES_ST_CTRL, 0);
        } else if (!strcmp(s, "zero")) {
            wr(DES_ST_CTRL, (rd(DES_ST_CTRL) & 0x51) | 0x20);
        }
        uint32_t st = rd(DES_ST_STATUS);
        printf("stepper pos %d, speed %.0f steps/s, steps %u, status 0x%x\n", (int32_t)rd(DES_ST_POS),
               rd(DES_ST_CUR_INC) * (double)DES_FREE_CNT_HZ / 4294967296.0, rd(DES_ST_STEPS), st);
    } else {
        fprintf(stderr, "usage: see header of desreg.c\n");
        rc = 1;
    }
    munmap(p, DES_PL_SPAN);
    close(fd);
    return rc;
}
