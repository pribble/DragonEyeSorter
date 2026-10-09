/* DragonEyeSorter PL register map. Keep in sync with docs/03-PL设计.md (same commit). */
#ifndef DES_REGS_H
#define DES_REGS_H

#include <stdint.h>

#define DES_PL_BASE      0x80301000u   /* fvdma_ctrl AXI-Lite window + 0x1000 */
#define DES_PL_SPAN      0x1000u

#define DES_REG_ID       0x000u        /* R  : 0x44455301 */
#define DES_REG_BUILD    0x004u        /* R  : build date, e.g. 0x20261010 */
#define DES_REG_SCRATCH  0x008u        /* RW */
#define DES_REG_FREE_CNT 0x00Cu        /* R  : 75 MHz free-running counter */
#define DES_REG_LED_CTRL 0x010u        /* RW : b0 override LED1, b1 LED1 value */

#define DES_ID_VALUE     0x44455301u
#define DES_FREE_CNT_HZ  75000000u

#endif
