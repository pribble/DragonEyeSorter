#!/usr/bin/env python3
"""fig_pl_regs — 图 5 PL 自研模块、寄存器段与管脚。

画布 = 报告落版宽度 447.9pt（15.8cm）× 310pt，字面字号即纸面字号（见 figkit 顶部说明）。
左栏：PS 驱动 → 官方 fvdma_ctrl 的 64KB 窗口（地址线 4 → 16 位）→ 自研寄存器总线
→ 按 ``addr[11:8]`` 分段译码；右栏做成「寄存器段表」三线表：每行一个段，左侧偏移标签、
中间模块与两行限定、右侧管脚，顶线 / 底线 + 行间极浅分隔，不用行框与斑马底。表比堆框更
清楚，也不再画总线干线。七段 = des_regs / servo_bus / stepper_ctrl / des_vision / des_pwm /
des_io / des_485（v16，与报告表 4、`ps/src/hw/des_regs.h`、[PL设计](../../docs/PL设计.md)
分段一致）；新模块从 0x7xx 起登记。
"""
import figkit as fk

NAME = 'fig_pl_regs'
H = 310.0

LX, LW = 4.0, 182.0                    # 左栏
RX, RW = 190.0, 254.0                  # 右栏
ROW_H, ROW_GAP = 36.0, 2.0             # 右栏行高 / 行距（7 行）
ROW_TOP = 278.0
SEP = 150.0                            # 行内「模块 | 管脚」竖线位置（相对行左边）

# 每段：偏移 / 模块名 / 两行限定 / 两行管脚
MODS = [
    ('0x000', 'des_regs',
     ['ID / 版本 / 自由计数', '拍摄锁存：帧号 + 位置'],
     ['开始写 / 写完', '像素域翻转 + 两级同步']),
    ('0x100', 'servo_bus',
     ['协议主机 · SYNC WRITE', '补帧头 · 校验 · 超时'],
     ['SERVO_BUS Y9 开漏', 'BSS138 → 6 舵机']),
    ('0x200', 'stepper_ctrl',
     ['梯形加减速 · 运动中改目标', '刹车距离 · 首脉冲'],
     ['STEP PUL/DIR/ENA', 'Y12/Y13/T9 · TB6600']),
    ('0x300', 'des_vision',
     ['灰度 / 高斯 / 二值 / Sobel', 'ROI 统计 · 像素域 111MHz'],
     ['HDMI 直接显示处理图', '60fps 切模式可见']),
    ('0x400', 'des_pwm',
     ['导流挡板 2 路 50Hz PWM', '新脉宽下周期生效'],
     ['FLAP1 Y6 / FLAP2 Y8', 'MG996R ×1']),
    ('0x500', 'des_io · v11',
     ['急停锁存 → 立即停脉冲', '对射 / 限位 · 编码器计数'],
     ['ESTOP_IN V11 · IR1 V6', 'ENC A/B/Z U10/T5/U5']),
    ('0x600', 'des_485 · v16',
     ['RS485 Modbus-RTU 帧收发', '配置 · 状态 · 计数'],
     ['485 TX L17 · RX N16', 'DE N15 · SP3485EN']),
]


def _row_paper(ax, y, idx, off, name, lines, pins):
    """paper 主题：三线表行——偏移 / 模块 / 管脚，列对齐，行内不加边框。"""
    x = RX + 6
    fk.text(ax, x + 8, y + 27, off, fs=fk.FS['sub'], weight='bold', color=fk.INK)
    fk.text(ax, x + 52, y + 27, name, fs=fk.FS['box'], weight='bold', color=fk.INK)
    for i, s in enumerate(lines):
        fk.text(ax, x + 52, y + 15 - i * 9.6, s, fs=fk.FS['sub'], color=fk.MUTED)
    for i, s in enumerate(pins):
        fk.text(ax, x + SEP + 8, y + 15 - i * 9.6, s, fs=fk.FS['sub'], color=fk.MUTED)


def _table_paper(ax):
    """paper 主题右栏：三线表（顶线 / 底线 + 行间极浅分隔），列不竖线、不用斑马底。"""
    x = RX + 6
    w = RW - 12
    row_h, gap = ROW_H - 1.0, ROW_GAP          # 行略压低，给底线与分组虚线框留出余量
    y_last = ROW_TOP - row_h - (len(MODS) - 1) * (row_h + gap)
    fk.hline(ax, x, x + w, ROW_TOP + 2.5, color=fk.INK, lw=1.0)
    for i, (off, name, lines, pins) in enumerate(MODS):
        y = ROW_TOP - row_h - i * (row_h + gap)
        _row_paper(ax, y, i, off, name, lines, pins)
        if i:
            fk.hline(ax, x, x + w, y + row_h + gap - 0.5, color=fk.RULE, lw=0.4, zorder=1.2)
    fk.hline(ax, x, x + w, y_last - 1.5, color=fk.INK, lw=1.0)


def draw():
    fig, ax = fk.new_fig(H)

    fk.group(ax, LX, 6, LW, 300, '寄存器通道', 'ps')
    fk.group(ax, RX, 6, RW, 300, 'PL 自研模块与管脚', 'pl', note='0x000 ~ 0x600')

    # ---------------- 左栏：PS → 窗口 → 总线 → 译码 ----------------
    bx, bw = LX + 6, LW - 12
    fk.box(ax, bx, 234, bw, 30, 'PS 侧驱动\n/dev/mem → AXI GP0', 'ps')
    fk.box(ax, bx, 186, bw, 42, '官方 fvdma_ctrl（不动官方块）\n'
           '64KB 窗口 0x8030_0000 · 地址线 4→16 位\n厂家段 0x0000~0x0FFF（mipicam 驱动）', 'pl')
    fk.box(ax, bx, 142, bw, 36, '自研寄存器总线 0x1000~0x1FFF\n75MHz · 单周期写', 'pl')
    fk.box(ax, bx, 100, bw, 34, 'PL_BASE 0x8030_1000\n单次读 188ns（20 万次统计）', 'pl')
    fk.box(ax, bx, 54, bw, 40, '按 addr[11:8] 分段译码\n0x000 ~ 0x600 → 右侧各段', 'pl')

    fk.text(ax, bx + 2, 42, '新模块从 0x7xx 起（用前先登记）', fs=fk.FS['sub'], color=fk.FAINT)
    fk.text(ax, bx + 2, 30, 'CEP 排针 J12 = BANK11 · 3.3V LVCMOS', fs=fk.FS['sub'],
            color=fk.FAINT)

    cx = LX + LW / 2.0
    fk.arrow(ax, (cx, 234), (cx, 228), ms=6.0)
    fk.arrow(ax, (cx, 186), (cx, 178), ms=6.0)
    fk.arrow(ax, (cx, 142), (cx, 134), ms=6.0)
    fk.arrow(ax, (cx, 100), (cx, 94), ms=6.0)

    # ---------------- 右栏：寄存器段表 ----------------
    _table_paper(ax)

    return fig, ax


def main():
    fig, ax = draw()
    return fk.save(fig, NAME)


if __name__ == '__main__':
    main()
