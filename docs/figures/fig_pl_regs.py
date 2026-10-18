#!/usr/bin/env python3
"""fig_pl_regs — 图 3-1 PL 自研模块、寄存器窗口与管脚。

画布 = 报告落版宽度 447.9pt（15.8cm）× 280pt，字面字号即纸面字号（见 figkit 顶部说明）。
左栏：PS → 官方 fvdma_ctrl 的 64KB 地址窗口（地址线 4 → 16 位）→ 0x1000~0x1FFF 自研寄存器总线
→ `addr[11:8]` 分段译码；右栏：每个寄存器段一个模块框，右边接管脚框（引脚取自
`pl/soc_base/src/constraint/io.adc`，各段内容与 `docs/技术文档.md` §2.4 / §3 一致）。
图里只留「名称 + 极短限定」，寄存器表、时序、仿真项写正文。
"""
import figkit as fk

NAME = 'fig_pl_regs'
H = 280.0

# 左栏（寄存器通道）/ 右栏（模块 + 管脚）
GX0, GX1 = 2.0, 198.0                 # 左分组
MX0, MX1 = 204.0, 446.0               # 右分组
BX, BW = 6.0, 188.0                   # 左栏整宽
MOD_X, MOD_W = 208.0, 132.0           # 模块框
PIN_X, PIN_W = 346.0, 96.0            # 管脚框
SPINE = 201.0                         # 分段译码 → 各模块的竖直干线
ROW_TOP, ROW_H, ROW_STEP = 254.0, 37.5, 41.7

# 每个寄存器段：模块名 / 两行说明 / 右侧伙伴（管脚或信号）
MODS = [
    ('0x000  des_regs',
     ['ID / 版本 / 75MHz 自由计数', '拍摄锁存：帧号 + 带位置 + 时间戳'],
     '开始写 / 写完\n像素域翻转 + 两级同步'),
    ('0x100  servo_bus',
     ['德晟协议主机 · SYNC WRITE', '硬件补帧头 + 校验 · 超时 3ms'],
     'SERVO_BUS Y9 开漏\n→ BSS138 → 6 舵机'),
    ('0x200  stepper_ctrl',
     ['梯形加减速 · 运动中改目标', '刹车距离 BRAKE · 首脉冲 10.6µs'],
     'STEP_PUL/DIR/ENA\nY12/Y13/T9\n→ TB6600 驱动器'),
    ('0x300  des_vision',
     ['灰度 / 高斯 / 二值 / Sobel', 'ROI 统计 · 像素域 111MHz'],
     'HDMI 直接显示处理图\n60fps 下切模式可见'),
    ('0x400  des_pwm',
     ['导流挡板 2 路 50Hz PWM', '新脉宽下周期生效 · 无毛刺'],
     'FLAP1 Y6\nFLAP2 Y8\n→ MG996R ×2'),
    ('0x500  des_io（v11）',
     ['急停锁存 → 立即停脉冲', '对射 / 限位捕获 · 编码器计数'],
     'ESTOP_IN V11\nIR1 V6 · LIMIT1 V7\nENC_A/B/Z U10/T5/U5'),
]


def _rows():
    """各模块框的左下角 y（自上而下 = 段号从小到大）。"""
    return [ROW_TOP - ROW_H - i * ROW_STEP for i in range(len(MODS))]


def _mod(ax, x, y, w, h, title, lines):
    """框 + 第一行名称（8.5pt 粗体）+ 次级说明（7.5pt），逐行核对是否落在框内。"""
    fk.box(ax, x, y, w, h, '')
    r = (x, y, w, h)
    fk.text(ax, x + w / 2, y + h - 8.5, title, fs=fk.FS['box'], ha='center', va='center',
            weight='bold', rect=r)
    for i, s in enumerate(lines):
        fk.text(ax, x + w / 2, y + h - 19.2 - i * 10.7, s, fs=fk.FS['sub'], ha='center',
                va='center', rect=r)


def draw():
    fig, ax = fk.new_fig(H)
    S, B = fk.FS['sub'], fk.FS['box']

    fk.group(ax, GX0, 6, GX1 - GX0, 268, '寄存器通道（不改官方块设计）', 'pl')
    fk.group(ax, MX0, 6, MX1 - MX0, 268, 'PL 自研模块与管脚', 'pl')

    # ---------------- 左栏：PS → 官方窗口 → 自研总线 → 分段译码 ----------------
    fk.box(ax, BX, 224, BW, 30, 'PS（Linux）：des_app / desreg\n/dev/mem 映射 · AXI GP0', 'ps', fs=S)
    fk.box(ax, BX, 174, BW, 38, '官方 fvdma_ctrl（相机 DMA 控制器）\n64KB 地址窗口 0x8030_0000\n'
           '把地址线由 4 位加宽到 16 位', 'pl', fs=S)
    fk.box(ax, BX, 128, 88, 36, '0x0000~0x0FFF\n厂家寄存器段\n（mipicam 驱动）', 'ext', fs=S)
    fk.box(ax, BX + 92, 128, 96, 36, '0x1000~0x1FFF\n自研寄存器总线\n75MHz · 单周期写', 'pl', fs=S)
    fk.box(ax, BX, 90, BW, 32, 'PL_BASE 0x8030_1000\n单次读 188ns（20 万次统计）', 'pl', fs=S,
           weight='bold')
    fk.box(ax, BX, 48, BW, 30, '按 addr[11:8] 分段译码\n0x000 ~ 0x500 → 右侧各模块', 'pl', fs=S)

    fk.arrow(ax, (100, 224), (100, 212), ms=5.0)
    fk.arrow(ax, (100, 174), (100, 168), ms=5.0)
    ax.plot([50, 146], [168, 168], color='#444444', lw=1.0, solid_capstyle='butt', zorder=2)
    ax.plot([50, 50], [168, 164], color='#444444', lw=1.0, solid_capstyle='butt', zorder=2)
    ax.plot([146, 146], [168, 164], color='#444444', lw=1.0, solid_capstyle='butt', zorder=2)
    fk.arrow(ax, (146, 128), (146, 122), ms=5.0)
    fk.arrow(ax, (100, 90), (100, 78), ms=5.0)

    # 左栏脚注（新模块起址 / 排针电平）—— 面向评委，不写仓库路径与内部脚本名
    fk.text(ax, 8, 34, '新模块从 0x5xx 起（用前先登记）', fs=S, color=fk.MUTED)
    fk.text(ax, 8, 23, 'CEP 排针 J12 = BANK11 · 3.3V LVCMOS', fs=S, color=fk.MUTED)

    # ---------------- 右栏：各段模块 + 管脚 ----------------
    ys = _rows()
    fk.poly(ax, [(194, 63), (SPINE, 63), (SPINE, ys[-1] + ROW_H / 2)], color=fk.C['pl'][1], lw=1.1)
    for (title, lines, pin), y in zip(MODS, ys):
        cy = y + ROW_H / 2
        _mod(ax, MOD_X, y, MOD_W, ROW_H, title, lines)
        fk.arrow(ax, (SPINE, cy), (MOD_X, cy), color=fk.C['pl'][1], lw=1.1, ms=5.0)
        n = pin.count('\n') + 1
        ph = 28.0 if n == 2 else 37.0
        fk.box(ax, PIN_X, cy - ph / 2, PIN_W, ph, pin, 'ext', fs=S)
        fk.arrow(ax, (MOD_X + MOD_W, cy), (PIN_X, cy), color=fk.C['pl'][1], lw=1.1, ms=5.0)

    return fig, ax


REGIONS = {
    # PPT 放大区（pt，左下原点）：宽×高都要 ≤ 364×235pt，面板 7.41×4.78in 上等效字号才 ≥11pt
    'regs': (2, 44, 200, 258),                     # PS → 窗口 → 分段译码（不含脚注）
    'modules': (200, 128, 446, 217),               # 0x100 servo_bus + 0x200 stepper_ctrl 两行及管脚
}


def main():
    fig, ax = draw()
    return fk.save(fig, NAME, REGIONS)


if __name__ == '__main__':
    main()
