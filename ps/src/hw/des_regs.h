/* DragonEyeSorter PL register map. Keep in sync with docs/PL设计.md (same commit). */
#ifndef DES_REGS_H
#define DES_REGS_H

#include <stdint.h>

#define DES_PL_BASE      0x80301000u   /* fvdma_ctrl AXI-Lite window + 0x1000 */
#define DES_PL_SPAN      0x1000u

#define DES_REG_ID           0x000u    /* R  : 0x44455301 */
#define DES_REG_VERSION      0x004u    /* R  : 1 register channel (v1 reads 0x20261010 here), 2 integrated,
                                          3 SOF fix, 4 INCK 27 MHz (60 fps), 5 capture latch, 6 des_vision,
                                          7 des_pwm + AWB divider, 8 read mux / clk_mipi, 9 stepper TARGET == POS,
                                          10 des_pwm w/o lamp, 11 des_io, 12 PS CAN0 pins (no register change) */
#define DES_REG_SCRATCH      0x008u    /* RW */
#define DES_REG_FREE_CNT     0x00Cu    /* R  : 75 MHz free-running counter */
#define DES_REG_LED_CTRL     0x010u    /* RW : b0 override LED1, b1 LED1 value */
#define DES_REG_FRAME_CNT    0x014u    /* R  : camera start-of-frame count (v2) */
#define DES_REG_FRAME_TS     0x018u    /* R  : FREE_CNT at latest start of frame */
#define DES_REG_FRAME_PERIOD 0x01Cu    /* R  : FREE_CNT ticks between the last two frames */
/* last completed DMA capture (v5): updated on the done interrupt, stable for one frame period */
#define DES_REG_CAP_SEQ      0x020u    /* R  : completed captures */
#define DES_REG_CAP_FCNT     0x024u    /* R  : FRAME_CNT when the writer started the frame */
#define DES_REG_CAP_T0       0x028u    /* R  : FREE_CNT at writer start */
#define DES_REG_CAP_T1       0x02Cu    /* R  : FREE_CNT at done */
#define DES_REG_CAP_POS0     0x030u    /* R  : belt position (stepper POS) at writer start */
#define DES_REG_CAP_POS1     0x034u    /* R  : belt position at done */

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
#define DES_ST_BRAKE         0x224u    /* R  : steps needed to ramp down from the current speed (v5) */
#define DES_ST_RAMP_HZ       10000u    /* ramp ticks per second */

/* des_vision, PL_BASE + 0x300 (v6): PL image path between the ISP and the frame writer (pl/soc_base/src/des/des_vision.v) */
#define DES_VIS_CTRL         0x300u    /* RW [2:0] mode 0 camera 1 gray 2 Gaussian 3 binary 4 Sobel 5 edges, [8] full frame */
#define DES_VIS_THR          0x304u    /* RW [7:0] binary threshold, [26:16] edge threshold (|Gx|+|Gy|) */
#define DES_VIS_ROI_X        0x308u    /* RW [10:0] x0, [26:16] x1 */
#define DES_VIS_ROI_Y        0x30Cu    /* RW [10:0] y0, [26:16] y1 */
#define DES_VIS_APPLY        0x310u    /* W  settings take effect at the next frame start */
#define DES_VIS_ID           0x314u    /* R  0x56495331 */
#define DES_VIS_SEQ          0x320u    /* R  frames with published statistics */
#define DES_VIS_AREA         0x324u
#define DES_VIS_BBOX_X       0x328u    /* R  [10:0] min x, [26:16] max x */
#define DES_VIS_BBOX_Y       0x32Cu
#define DES_VIS_SUMX         0x330u
#define DES_VIS_SUMY         0x334u
#define DES_VIS_EDGES        0x338u
#define DES_VIS_CYCLES       0x33Cu    /* R  pixel clocks per frame */
#define DES_VIS_ACTIVE       0x340u    /* R  settings in use: [2:0] mode [8] full [23:16] threshold [27:24] lanes (0 = 4) */
#define DES_VIS_ID_VALUE     0x56495331u

/* des_pwm, PL_BASE + 0x400 (v7): belt-end flap servos (2 x 50 Hz) (pl/soc_base/src/des/des_pwm.v);
 * v10 dropped the light output, so CTRL is [1:0] channel enable only */
#define DES_PWM_CTRL         0x400u    /* RW [1:0] channel enable */
#define DES_PWM_PERIOD       0x404u    /* RW period in us (default 20000) */
#define DES_PWM_CH0          0x408u    /* RW pulse width in us, applied at the next period start */
#define DES_PWM_CH1          0x40Cu
#define DES_PWM_CNT          0x410u    /* R  periods started */
#define DES_PWM_ID           0x414u    /* R  0x50574D31 */
#define DES_PWM_ID_VALUE     0x50574D31u

/* des_io, PL_BASE + 0x500 (v11): e-stop latch, IR1 / LIMIT1 capture, belt encoder (pl/soc_base/src/des/des_io.v) */
#define DES_IO_STATUS        0x500u    /* R  b0 estop latched b1 estop pressed b2 IR1 blocked b3 LIMIT1 b4..6 enc A/B/Z */
#define DES_IO_CTRL          0x504u    /* W  b0 estop clear (ignored while pressed) b1 encoder zero b2 IR1 count clear */
#define DES_IO_ESTOP_TS      0x508u    /* R  FREE_CNT at the last latch */
#define DES_IO_ESTOP_CNT     0x50Cu    /* R  latch events */
#define DES_IO_IR1_CNT       0x510u    /* R  IR1 blocked edges */
#define DES_IO_IR1_TS        0x514u    /* R  FREE_CNT at the last blocked edge */
#define DES_IO_IR1_POS       0x518u    /* R  stepper POS at that edge */
#define DES_IO_IR1_ENC       0x51Cu    /* R  encoder count at that edge */
#define DES_IO_ENC_CNT       0x520u    /* R  signed x4 count */
#define DES_IO_ENC_ERR       0x524u    /* R  illegal transitions */
#define DES_IO_ENC_ZPOS      0x528u    /* R  count at the last index edge */
#define DES_IO_ENC_SPEED     0x52Cu    /* R  signed counts per 10 ms */
#define DES_IO_ENC_ZCNT      0x530u    /* R  index edges */
#define DES_IO_ID            0x534u    /* R  0x494F3031 */
#define DES_IO_CFG           0x538u    /* RW b0 IR1 polarity (0: low = blocked) b1 LIMIT1 polarity (0: low = active) */
#define DES_IO_ID_VALUE      0x494F3031u
#define DES_IO_ST_ESTOP_LATCHED 0x01u
#define DES_IO_ST_ESTOP_NOW     0x02u
#define DES_IO_ST_IR1_BLOCKED   0x04u
#define DES_IO_ST_LIMIT1        0x08u

#define DES_ID_VALUE     0x44455301u
#define DES_FREE_CNT_HZ  75000000u

/* speed in steps/s -> Q0.32 phase increment per 75 MHz clock */
static inline uint32_t des_step_inc(double steps_per_s)
{
    return (uint32_t)(steps_per_s * 4294967296.0 / DES_FREE_CNT_HZ + 0.5);
}

#endif
