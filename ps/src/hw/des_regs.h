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
                                          10 des_pwm w/o lamp, 11 des_io, 12 PS CAN0 pins (no register change; v13~v15
                                          kept 12), 16 CAP_ENC0/1 + des_485 RS485, 17 des_bpi belt speed PI + des_ptrig
                                          position trigger + des_pwm SLEW (source only so far: not built, not on board) */
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
#define DES_REG_CAP_ENC0     0x038u    /* R  : belt encoder count (des_io ENC_CNT) at writer start (v16) */
#define DES_REG_CAP_ENC1     0x03Cu    /* R  : belt encoder count at done (v16) */

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
#define DES_PWM_SLEW         0x418u    /* RW (v17) us per period the output width moves toward CHx; 0 = at once */
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

/* des_485, PL_BASE + 0x600 (v16): RS485 Modbus-RTU on J11 (pl/soc_base/src/des/des_485.v) */
#define DES_485_CTRL         0x600u    /* W  b0 go b1 reply wanted b2 clear counters / result b3 listen ack b4 abort
                                          [14:8] PDU bytes 1..62 (go / len ignored while busy); R echo [14:8] b1 b0 */
#define DES_485_STATUS       0x604u    /* R  b0 busy b1 done b2 timeout b3 CRC error b4 reply OK b5 parity / stop error
                                          b6 overflow (>64 bytes) b7 listen frame ready [15:8] last frame bytes incl. CRC
                                          b16 DE b17 RX line b18 receiving b19 listen CRC ok b20 listen parity / stop error */
#define DES_485_TIMEOUT_US   0x608u    /* RW reply timeout from the last stop bit to the first start bit (100000) */
#define DES_485_DIV          0x60Cu    /* RW clocks per bit (75 MHz / baud; 7813 = 9600) */
#define DES_485_CFG          0x610u    /* RW [1:0] parity 0 none 1 even 2 odd */
#define DES_485_T35_US       0x614u    /* RW bus silence that ends a frame / must precede a request (Modbus t3.5) */
#define DES_485_TX_T0        0x618u    /* R  FREE_CNT at the first start bit */
#define DES_485_TX_T1        0x61Cu    /* R  FREE_CNT at the end of the last stop bit (DE released) */
#define DES_485_RX_T0        0x620u    /* R  FREE_CNT at the first start bit of the last frame received */
#define DES_485_RX_T1        0x624u    /* R  FREE_CNT at its last byte */
#define DES_485_CNT_TX       0x628u
#define DES_485_CNT_OK       0x62Cu
#define DES_485_CNT_TIMEOUT  0x630u
#define DES_485_CNT_CRC      0x634u    /* frames with a bad CRC (replies and listen frames) */
#define DES_485_CNT_FERR     0x638u    /* frames with a parity / stop-bit error */
#define DES_485_CNT_LISTEN   0x63Cu    /* frames received while idle */
#define DES_485_ID           0x640u    /* R  0x34383501 */
#define DES_485_QUIET_US     0x644u    /* R  microseconds since the bus was last driven (saturates at 65535) */
#define DES_485_TX_BUF       0x680u    /* W  64 bytes, little-endian words: PDU without CRC */
#define DES_485_RX_BUF       0x6C0u    /* R  64 bytes of the last frame received (CRC included) */
#define DES_485_ID_VALUE     0x34383501u

/* des_bpi, PL_BASE + 0x700 (v17): belt speed PI on the metering-wheel encoder (pl/soc_base/src/des/des_bpi.v).
 * Speeds are Q0.32 encoder counts per 75 MHz clock, 24 bits kept (below 2^-8 counts per clock = 292968 counts/s);
 * relative quantities are Q0.16 (65536 = 100 %). W = write-only (reads 0xDEADBEEF). */
#define DES_BPI_CTRL         0x700u    /* RW b0 enable (0 = scale 0 = open loop as before) ; W b1 clear integrator */
#define DES_BPI_STATUS       0x704u    /* R  b0 enabled b1 last window stepped b2 stall (no encoder edge for TIMEOUT)
                                          b3 output at the limit b4 reference edge held b5 stepper in cruise
                                          b6 the count went down over the last window */
#define DES_BPI_SET          0x708u    /* RW set-point, Q0.32 counts per clock, [23:0] (0 = nothing to control) */
#define DES_BPI_KP           0x70Cu    /* W  Q4.12 */
#define DES_BPI_KI           0x710u    /* W  Q0.16 per window */
#define DES_BPI_SMAX         0x714u    /* RW output limit, Q0.16, capped at 16384 (25 %) */
#define DES_BPI_DB           0x718u    /* W  dead band on the relative error, Q0.16 */
#define DES_BPI_PERIOD       0x71Cu    /* W  shortest window in clocks, [23:0] (750000 = 10 ms) */
#define DES_BPI_TIMEOUT      0x720u    /* W  clocks without an encoder event = belt standing, [23:0] (7500000 = 100 ms) */
#define DES_BPI_SETTLE       0x724u    /* W  cruise windows skipped after the cruise speed is reached (5) */
#define DES_BPI_MEAS         0x728u    /* R  measured speed, Q0.32 counts per clock */
#define DES_BPI_ERR          0x72Cu    /* R  (SET - MEAS) / SET, signed Q0.16, after the dead band */
#define DES_BPI_SCALE        0x730u    /* R  output: step rate = programmed rate * (1 + SCALE), signed Q0.16 */
#define DES_BPI_INTEG        0x734u    /* R  integral part of SCALE, signed Q0.16 */
#define DES_BPI_MEAS_CNT     0x738u    /* R  windows measured, 16 bits (wraps) */
#define DES_BPI_STEP_CNT     0x73Cu    /* R  PI steps taken, 16 bits (wraps) */
#define DES_BPI_ID           0x748u    /* R  0x42504931 */
#define DES_BPI_ID_VALUE     0x42504931u

/* des_ptrig, PL_BASE + 0x800 (v17): belt position compare trigger, 2 entries (pl/soc_base/src/des/des_ptrig.v) */
#define DES_PTG_CTRL         0x800u    /* W  b0 disarm all entries and clear their flags */
#define DES_PTG_STATUS       0x804u    /* R  [1:0] armed [9:8] fired [17:16] late (threshold already passed at arming) */
#define DES_PTG_FIRE_CNT     0x808u    /* R  fires since reset, 16 bits (wraps) */
#define DES_PTG_ID           0x80Cu    /* R  0x50544731 */
#define DES_PTG_OUT_CLK      0x810u    /* R  TRIG_OUT pulse length in 75 MHz clocks, fixed (7500 = 100 us) */
/* entry 0; entry i (0..1) sits 0x10 * i higher: DES_PTG_THR(i) etc. */
#define DES_PTG_THR0         0x840u    /* RW threshold (stepper POS or encoder count); the low 24 bits are kept and
                                          compared, read back sign-extended */
#define DES_PTG_CFG0         0x844u    /* RW b0 arm (clears on fire) b1 direction (0: source >= THR, 1: source <= THR;
                                          on the wrapping 24-bit difference) b2 source (0 stepper POS, 1 encoder)
                                          [5:4] action (0 none, 1 load flap PWM ch0, 2 ch1)
                                          b7 pulse TRIG_OUT [27:16] PWM width in us for actions 1 / 2 */
#define DES_PTG_TS0          0x848u    /* R  FREE_CNT at the fire */
#define DES_PTG_THR(i)       (DES_PTG_THR0 + 0x10u * (i))
#define DES_PTG_CFG(i)       (DES_PTG_CFG0 + 0x10u * (i))
#define DES_PTG_TS(i)        (DES_PTG_TS0 + 0x10u * (i))
#define DES_PTG_ID_VALUE     0x50544731u
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
