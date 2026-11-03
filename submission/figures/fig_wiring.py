#!/usr/bin/env python3
"""fig_wiring — 系统电气连接：开发板引脚 → 外设与执行机构 ← 供电与急停（报告 2.2.3 节）。

画布 = 报告落版宽度 447.9pt（15.8cm），字面字号即纸面字号（见 figkit 顶部说明）。
三栏逐行对齐：左栏开发板引脚（J12 CEP 排针 3.3 V、J11 端子、板载接口），中栏外设，右栏供电。
每个外设左接信号、右接供电，没有孤立模块；信号线细、供电线粗，均水平走线。
供电树：220 V → 24 V 主电源 LRS-350-24；+V1 经急停常闭 → 受控 24 V（步进驱动器）→ 300 W 降压
6.8 V（舵机母排）；+V2 → 常电 24 V（对射、继电器 / 三色灯）→ 5 V 降压（编码器、BSS138 高压侧）。
事实来源：docs/硬件与接线.md（J12 引脚表、J11 定义、供电树、急停逻辑）。
"""
import figkit as fk

NAME = 'fig_wiring'
H = 384.0

BX, BW = 4.0, 128.0            # 开发板引脚
EX, EW = 140.0, 156.0          # 外设
PX, PW = 304.0, 140.0          # 供电
TOP = 330.0
PITCH = 35.0
BOX_H = 25.0
PBX, PBW = PX + 6, 104.0       # 供电框
BUS_IN, BUS_OUT = PBX + PBW + 9, PBX + PBW + 20   # 右侧两条馈线
LW_PWR = 1.6

ROWS = [   # (引脚, 外设, 方向)  out = 板 → 外设，in = 外设 → 板，both = 双向
    ('J12 PIN3 / 4 / 5\nPUL+ · DIR+ · ENA+',      'TB6600 驱动器 → 57 步进\n3.3 V 直驱 · 共阴',              'out'),
    ('J12 PIN19\nSERVO_BUS 半双工',               '六轴总线舵机 #1~#6\n经 BSS138 电平转换',                'both'),
    ('J12 PIN18\nFLAP1_PWM 50 Hz',                'MG996R 挡板舵机\nPWM 直驱',                             'out'),
    ('J12 PIN6 / 7 / 8\nENC A · B · Z',            '计米轮编码器\n经 BSS138 电平转换',                       'in'),
    ('J12 PIN9\nIR1 · 内部上拉',                   '对射光电接收端\nNPN 集电极开路',                         'in'),
    ('J11 第 5 / 6 位\n485 A · B（des_485）',      'RS485 继电器模块 → 三色灯\nUSB-RS485 → PC（旁听）',      'both'),
    ('J11 第 3 / 2 位\nCAN H · L（PS CAN0）',       'USB-CAN → PC（PLC 模拟）\n500 kbit/s · 120 Ω 终端',      'both'),
    ('HDMI · 网口 · USB · FEP\nPS 侧板载接口',      '显示器 · PC · 键鼠 · 相机\nModbus-TCP / 运维站 · IMX415', 'both'),
    ('J12 PIN15\nESTOP_IN · 低有效',               '急停按钮 · 常开 23-24\n按下即通知 PL 锁存',               'in'),
]


def y_of(i):
    return TOP - i * PITCH


def mid(i):
    return y_of(i) + BOX_H / 2.0


def pwr_line(ax, pts, arrow_to=None):
    """供电线：粗实线折线，末段带箭头。"""
    xs, ys = zip(*pts)
    ax.plot(xs, ys, color=fk.ARROW, lw=LW_PWR, solid_capstyle='butt', zorder=2)
    if arrow_to:
        fk.arrow(ax, pts[-1], arrow_to, lw=LW_PWR, ms=6.0)


def draw():
    fig, ax = fk.new_fig(H)
    fk.group(ax, BX, 30, BW, 348, '开发板引脚', 'pl', note='J12 = 3.3 V')
    fk.group(ax, EX, 30, EW, 348, '外设与执行机构', 'ext')
    fk.group(ax, PX, 30, PW, 348, '供电与急停', 'ext')

    # 左、中两栏：信号（细线）
    for i, (pin, dev, way) in enumerate(ROWS):
        y = y_of(i)
        fk.box(ax, BX + 6, y, BW - 12, BOX_H, pin, 'pl')
        fk.box(ax, EX + 6, y, EW - 12, BOX_H, dev, 'ext')
        a, b = (BX + BW - 6, mid(i)), (EX + 6, mid(i))
        if way == 'in':
            a, b = b, a
        fk.arrow(ax, a, b, both=(way == 'both'), ms=5.5)

    # 右栏：供电框（与所供外设同行；跨两行的母排框同时供两行）
    def pbox(r0, r1, s, kind='acc'):
        y0 = y_of(r1); h = (y_of(r0) + BOX_H) - y0
        fk.box(ax, PBX, y0, PBW, h, s, kind)
        return y0, h
    ctl = pbox(0, 0, '受控 24 V 母排\n随急停断电')
    srv = pbox(1, 2, '6.8 V 舵机母排\n300 W 降压 · 限流\n保险丝')
    five = pbox(3, 3, '5 V 降压模块\n编码器 · BSS138')
    cst = pbox(4, 5, '常电 24 V 母排\n急停不断\n对射 · 继电器 / 三色灯')
    mains = pbox(6, 6, '220 V 插排（总闸）\n12 V 适配器 → 开发板 · 光源', 'ext')
    psu = pbox(7, 7, '24 V 主电源 LRS-350-24\n+V1 → 急停 · +V2 → 常电')
    nc = pbox(8, 8, '急停常闭 11-12\n按下断开 +V1')

    # 供电 → 外设（粗线，向左）
    for r in (0, 1, 2, 3, 4, 5):
        pwr_line(ax, [(PBX, mid(r)), (PBX - 2, mid(r))], arrow_to=(EX + EW - 6, mid(r)))
    # 急停：常闭（右栏）与常开（中栏）是同一按钮 → 虚线
    ax.plot([PBX, EX + EW - 6], [mid(8), mid(8)], color=fk.FAINT, lw=0.8, ls=(0, (2.4, 1.8)), zorder=2)

    cx = PBX + PBW / 2.0
    # 树内相邻：220 V → 主电源；主电源 +V1 → 急停常闭；受控 24 V → 6.8 V；常电 24 V → 5 V
    fk.arrow(ax, (cx, mains[0]), (cx, psu[0] + psu[1]), lw=LW_PWR, ms=6.0)
    fk.arrow(ax, (cx, psu[0]), (cx, nc[0] + nc[1]), lw=LW_PWR, ms=6.0)
    fk.arrow(ax, (cx, ctl[0]), (cx, srv[0] + srv[1]), lw=LW_PWR, ms=6.0)
    fk.arrow(ax, (cx, cst[0] + cst[1]), (cx, five[0]), lw=LW_PWR, ms=6.0)
    # 右侧馈线：主电源 +V2 → 常电 24 V（内侧）；急停常闭 → 受控 24 V（外侧），互不交叉
    pwr_line(ax, [(PBX + PBW, mid(7)), (BUS_IN, mid(7)), (BUS_IN, mid(4) - PITCH / 2.0)],
             arrow_to=(PBX + PBW, mid(4) - PITCH / 2.0))
    pwr_line(ax, [(PBX + PBW, mid(8)), (BUS_OUT, mid(8)), (BUS_OUT, mid(0))],
             arrow_to=(PBX + PBW, mid(0)))

    fk.text(ax, BX + 2, 19, '细线 = 信号，粗线 = 供电；J12 PIN2 单点接 24 V 地母排，BSS138 低压侧取 J12 PIN1 的 3.3 V。',
            fs=fk.FS['note'], color=fk.MUTED)
    fk.text(ax, BX + 2, 8, '虚线 = 同一急停按钮的常开 / 常闭触点；6.8 V 输出负端只接舵机地母排；急停按下时受控 24 V、6.8 V 一起断电，常电 24 V 保持。',
            fs=fk.FS['note'], color=fk.MUTED)
    return fig, ax


def main():
    fig, ax = draw()
    return fk.save(fig, NAME)


if __name__ == '__main__':
    main()
