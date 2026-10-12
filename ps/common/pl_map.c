/* DEPRECATED: Use des_hw.h instead. This module has known bugs:
 * - Line 16-17: mmap failure leaves s_fd open without cleanup
 * - pl_rd/pl_wr lack volatile semantics, unsafe for hardware polling
 * See ps/README.md section "约定" for migration guide.
 */
#include "pl_map.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>

static int      s_fd   = -1;
static uint32_t *s_va  = 0;
static size_t   s_len  = 0;
static uintptr_t s_base = 0;

int pl_map(uintptr_t phys, size_t len) {
    if (s_fd >= 0) return -1; /* 单映射够用，多区域时扩展 */
    s_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (s_fd < 0) return -1;
    s_va = mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, s_fd, (off_t)phys);
    if (s_va == MAP_FAILED) { close(s_fd); s_fd = -1; s_va = NULL; s_len = 0; return -1; }
    s_base = phys; s_len = len;
    return 0;
}

void pl_unmap(void) {
    if (s_va) { munmap(s_va, s_len); s_va = 0; }
    if (s_fd >= 0) { close(s_fd); s_fd = -1; }
}

uint32_t pl_rd(uint32_t off)  { return s_va[off >> 2]; }
void     pl_wr(uint32_t off, uint32_t v) { s_va[off >> 2] = v; }
