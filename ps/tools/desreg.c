/* desreg: read/write DragonEyeSorter PL registers through /dev/mem.
 *   desreg                 status
 *   desreg r <off>         read register
 *   desreg w <off> <val>   write register
 *   desreg led on|off|auto
 *   desreg lat [n]         PS->PL read latency, measured with the PL free-running counter
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "../common/des_regs.h"

static volatile uint32_t *regs;

static uint32_t rd(uint32_t off) { return regs[off / 4]; }
static void wr(uint32_t off, uint32_t v) { regs[off / 4] = v; }

static int status(void)
{
    uint32_t id = rd(DES_REG_ID);
    printf("ID       0x%08x %s\n", id, id == DES_ID_VALUE ? "(ok)" : "(UNEXPECTED: old bitstream?)");
    printf("BUILD    0x%08x\n", rd(DES_REG_BUILD));
    printf("SCRATCH  0x%08x\n", rd(DES_REG_SCRATCH));
    printf("FREE_CNT %u\n", rd(DES_REG_FREE_CNT));
    printf("LED_CTRL 0x%08x\n", rd(DES_REG_LED_CTRL));
    return id == DES_ID_VALUE ? 0 : 2;
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

int main(int argc, char **argv)
{
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { perror("open /dev/mem"); return 1; }
    void *p = mmap(NULL, DES_PL_SPAN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, DES_PL_BASE);
    if (p == MAP_FAILED) { perror("mmap"); return 1; }
    regs = (volatile uint32_t *)p;

    int rc = 0;
    int writes = argc > 1 && (!strcmp(argv[1], "w") || !strcmp(argv[1], "led"));
    if (writes && rd(DES_REG_ID) != DES_ID_VALUE) {
        /* on a bitstream without des_regs this window aliases the camera DMA registers */
        fprintf(stderr, "refusing to write: DES_ID mismatch (0x%08x), bitstream lacks des_regs\n", rd(DES_REG_ID));
        rc = 2;
    } else if (argc == 1) {
        rc = status();
    } else if (!strcmp(argv[1], "r") && argc == 3) {
        uint32_t off = strtoul(argv[2], NULL, 0);
        printf("0x%08x\n", rd(off));
    } else if (!strcmp(argv[1], "w") && argc == 4) {
        uint32_t off = strtoul(argv[2], NULL, 0), v = strtoul(argv[3], NULL, 0);
        wr(off, v);
        printf("0x%03x <- 0x%08x, readback 0x%08x\n", off, v, rd(off));
    } else if (!strcmp(argv[1], "led") && argc == 3) {
        uint32_t v = !strcmp(argv[2], "on") ? 3 : !strcmp(argv[2], "off") ? 1 : 0;
        wr(DES_REG_LED_CTRL, v);
        printf("LED_CTRL <- %u\n", v);
    } else if (!strcmp(argv[1], "lat")) {
        rc = latency(argc > 2 ? atoi(argv[2]) : 100000);
    } else {
        fprintf(stderr, "usage: desreg | r <off> | w <off> <val> | led on|off|auto | lat [n]\n");
        rc = 1;
    }
    munmap(p, DES_PL_SPAN);
    close(fd);
    return rc;
}
