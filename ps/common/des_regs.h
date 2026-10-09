/* DragonEyeSorter PL register map. Keep in sync with docs/03-PL设计.md (same commit). */
#ifndef DES_REGS_H
#define DES_REGS_H

#include <stdint.h>

#define DES_PL_BASE      0x80301000u   /* fvdma_ctrl AXI-Lite window + 0x1000 */
#define DES_PL_SPAN      0x1000u

#define DES_REG_ID           0x000u    /* R  : 0x44455301 */
#define DES_REG_VERSION      0x004u    /* R  : 1 register channel (v1 reads 0x20261010 here), 2 integrated */
#define DES_REG_SCRATCH      0x008u    /* RW */
#define DES_REG_FREE_CNT     0x00Cu    /* R  : 75 MHz free-running counter */
#define DES_REG_LED_CTRL     0x010u    /* RW : b0 override LED1, b1 LED1 value */
#define DES_REG_FRAME_CNT    0x014u    /* R  : camera start-of-frame count (v2) */
#define DES_REG_FRAME_TS     0x018u    /* R  : FREE_CNT at latest start of frame */
#define DES_REG_FRAME_PERIOD 0x01Cu    /* R  : FREE_CNT ticks between the last two frames */

/* servo_bus, PL_BASE + 0x100 */
#define DES_SB_CTRL          0x100u    /* W  : b0 go, b1 expect reply, b2 clear, b[14:8] body bytes */
#define DES_SB_STATUS        0x104u    /* R  : b0 busy b1 done b2 timeout b3 rx_err b4 rx_ok [14:8] rx_cnt [23:16] servo status */
#define DES_SB_TIMEOUT_US    0x108u
#define DES_SB_TX_T0         0x10Cu
#define DES_SB_TX_T1         0x110u
#define DES_SB_RX_T          0x114u
#define DES_SB_CNT_TIMEOUT   0x118u
#define DES_SB_CNT_RX_ERR    0x11Cu
#define DES_SB_CNT_FRAMES    0x120u
#define DES_SB_TX_BUF        0x140u    /* 64 bytes: [ID, LEN, INSTR, params...] */
#define DES_SB_RX_BUF        0x180u    /* 32 bytes: [ID, LEN, STATUS, params..., CHK] */

/* stepper_ctrl, PL_BASE + 0x200 */
#define DES_ST_CTRL          0x200u    /* b0 enable b1 start b2 stop b3 halt b4 mode(1=pos) b5 zero b6 vel dir */
#define DES_ST_STATUS        0x204u    /* b0 moving b1 dir b2 at_target b3 halted b4 enabled */
#define DES_ST_TARGET        0x208u
#define DES_ST_VMAX_INC      0x20Cu
#define DES_ST_ACC_INC       0x210u
#define DES_ST_VMIN_INC      0x214u
#define DES_ST_POS           0x218u
#define DES_ST_CUR_INC       0x21Cu
#define DES_ST_STEPS         0x220u
#define DES_ST_RAMP_HZ       10000u    /* ramp ticks per second */

#define DES_ID_VALUE     0x44455301u
#define DES_FREE_CNT_HZ  75000000u

/* speed in steps/s -> Q0.32 phase increment per 75 MHz clock */
static inline uint32_t des_step_inc(double steps_per_s)
{
    return (uint32_t)(steps_per_s * 4294967296.0 / DES_FREE_CNT_HZ + 0.5);
}

#endif
