// pl_regs.h — PL AXI-Lite 寄存器偏移（与 docs/03-PL设计.md 寄存器映射草案 v2 同源）
// 改任何偏移必须同一次提交同步 docs/03-PL设计.md 与本文件（AGENTS.md 约定 2）。
// PL_BASE 在 design_0 分配后回填（避开 0x80100000~0x8030FFFF）。
#ifndef PL_REGS_H
#define PL_REGS_H

#include <stdint.h>

#define PL_BASE 0x00000000u /* TODO: design_0 分配后填 */

/* 0x000 系统控制/状态 */
#define REG_CTRL        0x000u /* W  使能/软复位/带启动/臂使能/翻板使能/急停复位 */
#define REG_STATUS      0x004u /* R  就绪/急停锁存/臂忙/翻板忙/限位/失步/过载/总线超时 */
#define REG_IRQ_STAT    0x008u /* RW 中断状态（写1清） */
#define REG_IRQ_EN      0x00Cu /* RW 中断使能 */
#define REG_TIMESTAMP   0x010u /* R  PL 自由运行计数器（指令延迟测量） */

/* 0x020 传送带（步进） */
#define REG_BELT_TARGET 0x020u /* W  目标位置（脉冲数） */
#define REG_BELT_SPEED  0x024u /* W  速度 */
#define REG_BELT_ACC    0x028u /* W  加速度 */
#define REG_BELT_POS    0x02Cu /* R  已发脉冲计数 */

/* 0x040 编码器 / 帧锁存（P1） */
#define REG_ENC_POS     0x040u /* R  编码器位置 */
#define REG_ENC_SPEED   0x044u /* R  带速 */
#define REG_SLIP_ERR    0x048u /* R  失步偏差 */
#define REG_FRAME_LATCH 0x04Cu /* R  {帧号, 帧起始带位置} */

/* 0x060 机械臂（舵机总线） */
#define REG_ARM_POS(n)   (0x060u + 4u * (n)) /* W  6 轴目标 0x060~0x074 */
#define REG_ARM_TIME     0x078u /* W  运动时长 */
#define REG_ARM_GO       0x07Cu /* W  写 1 发 SYNC WRITE */
#define REG_ARM_STATUS   0x080u /* R  忙/完成/超时 */
#define REG_ARM_FB_POS(n) (0x090u + 4u * (n)) /* R 回读位置 0x090~0x0A4 */
#define REG_ARM_FB_CUR(n) (0x0A8u + 4u * (n)) /* R 舵机电流 0x0A8~0x0BC */
#define REG_ARM_FB_TEMP(n) (0x0C0u + 4u * (n)) /* R 舵机温度 0x0C0~0x0D4 */
#define REG_BUS_STAT     0x0D8u /* R 总线应答/校验错/超时计数 */

/* 0x0E0 翻板（P1） */
#define REG_FLAP_ANGLE1  0x0E0u /* W */
#define REG_FLAP_ANGLE2  0x0E4u /* W */
#define REG_FLAP_TRIGGER 0x0E8u /* W 触发带位置 */

/* 0x0F0 IO */
#define REG_IO_OUT       0x0F0u /* W 光源使能/三色灯 */
#define REG_IO_IN        0x0F4u /* R 对射/限位/急停 */
#define REG_IO_TIMESTAMP 0x0F8u /* R 多通道同步采样时间戳 */

/* CTRL / STATUS 位定义（草案，实现时细化） */
#define CTRL_SW_RST      (1u << 0)
#define CTRL_BELT_EN     (1u << 1)
#define CTRL_ARM_EN      (1u << 2)
#define CTRL_FLAP_EN     (1u << 3)
#define CTRL_ESTOP_CLR   (1u << 4)
#define STATUS_READY     (1u << 0)
#define STATUS_ESTOP     (1u << 1)
#define STATUS_ARM_BUSY  (1u << 2)
#define STATUS_FLAP_BUSY (1u << 3)
#define STATUS_SLIP      (1u << 4)
#define STATUS_OVERLOAD  (1u << 5)
#define STATUS_BUS_TMO   (1u << 6)

#endif /* PL_REGS_H */
