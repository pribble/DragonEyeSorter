#!/usr/bin/env python3
"""fig_wiring — 图 6 系统电气连接：供电与急停、开发板引脚、外设（报告 2.2.3 节）。

画布 = 报告落版宽度 447.9pt（15.8cm），字面字号即纸面字号（见 figkit 顶部说明）。
三栏：左栏供电树（急停常闭只断受控 24V 一支：步进驱动器、6.8V 降压与舵机母排；常电 24V
不断）；中栏开发板引脚（J12 CEP 排针 3.3V、J11 端子、板载接口）；右栏外设。中栏与右栏
逐行对齐，信号线一律水平、不交叉，外设的供电写在框内第二行，不再画电源线。
事实来源：docs/硬件与接线.md（J12 引脚表、J11 定义、供电树、急停逻辑）与
docs/figures/fig_system_wiring.png（2026-11-02 接线实况）。
"""
import figkit as fk

NAME = 'fig_wiring'
H = 376.0

# 三栏（pt）
PX, PW = 4.0, 132.0            # 供电
BX, BW = 142.0, 138.0          # 开发板
EX, EW = 290.0, 154.0          # 外设
TOP = 322.0                    # 第一行框顶
PITCH = 35.0                   # 行距
BOX_H = 25.0

# 中栏引脚 / 右栏外设，逐行对齐；方向：out = 板 → 外设，in = 外设 → 板，both = 双向
ROWS = [
    ('J12 PIN3 / 4 / 5\nPUL+ · DIR+ · ENA+',      'TB6600 驱动器 → 57 步进\n3.3 V 直驱 · 共阴 · 高有效',        'out'),
    ('J12 PIN19\nSERVO_BUS 半双工',               '六轴总线舵机 #1~#6\n经 BSS138 · 6.8 V 母排',             'both'),
    ('J12 PIN6 / 7 / 8\nENC A · B · Z',            '计米轮编码器\n经 BSS138 · 5 V 供电',                   'in'),
    ('J12 PIN18\nFLAP1_PWM 50 Hz',                'MG996R 挡板舵机\n直驱 · 6.8 V 母排',                   'out'),
    ('J12 PIN9\nIR1 · 内部上拉',                   '对射光电接收端\nNPN 集电极开路 · 常电 24 V',          'in'),
    ('J12 PIN15\nESTOP_IN · 低有效',               '急停常开触点 23-24\n与左侧常闭触点同一按钮',            'in'),
    ('J11 第 5 / 6 位\n485 A · B（des_485）',      '4 路继电器模块 → 三色灯\n地址 1 · 9600 bit/s · 常电 24 V', 'both'),
    ('J11 第 3 / 2 位\nCAN H · L（PS CAN0）',       'USB-CAN → PC（PLC 模拟）\n500 kbit/s · 板上 120 Ω 终端', 'both'),
    ('HDMI · 网口 · USB · FEP\nPS 侧板载接口',      '显示器 · PC · 键鼠 · 相机\nModbus-TCP · VID005 + IMX415', 'both'),
]


def _row_y(i):
    return TOP - i * PITCH


def _power(ax):
    """左栏供电树：急停常闭串在 +V1 一支上，下游全部随急停断电；+V2 常电支路绕开急停。"""
    x, w, h = PX + 12, PW - 18, BOX_H
    ys = {}
    spec = [
        ('mains', 322, '220 V 插排（总闸）\n12 V 适配器 → 开发板、光源'),
        ('psu',   284, '24 V 主电源\nLRS-350-24 · +V1 / +V2 / −V'),
        ('estop', 246, '急停常闭触点 11-12\n按下即断开 +V1'),
        ('ctl',   208, '受控 24 V 母排\n→ 步进驱动器 VCC'),
        ('buck',  170, '300 W 恒压恒流降压\n24 V → 6.8 V · 限流'),
        ('servo', 132, '6.8 V 舵机母排\n保险丝 · 舵机链两处进电'),
        ('const',  86, '常电 24 V 母排\n急停不断 · 对射 / 继电器'),
        ('five',   44, '5 V 降压模块\n编码器 · BSS138 高压侧'),
    ]
    for key, y, s in spec:
        fk.box(ax, x, y, w, h, s, 'ext')
        ys[key] = y
    cx = x + w / 2.0
    chain = ['mains', 'psu', 'estop', 'ctl', 'buck', 'servo']
    for a, b in zip(chain, chain[1:]):
        fk.arrow(ax, (cx, ys[a]), (cx, ys[b] + h), ms=5.5)
    fk.arrow(ax, (cx, ys['const']), (cx, ys['five'] + h), ms=5.5)
    # +V2：从主电源左侧沿栏边下到常电母排（绕开急停与受控支路）
    lx = PX + 6.5
    ax.plot([x, lx, lx], [ys['psu'] + h / 2.0, ys['psu'] + h / 2.0, ys['const'] + h / 2.0],
            color=fk.ARROW, lw=0.9, solid_capstyle='butt', zorder=2)
    fk.arrow(ax, (lx, ys['const'] + h / 2.0), (x, ys['const'] + h / 2.0), ms=5.5)
    # 受控支路的范围：急停以下三框外侧画一条虚线括号，标「随急停断电」
    bx = x + w + 3.0
    ax.plot([bx - 2, bx, bx, bx - 2], [ys['ctl'] + h, ys['ctl'] + h, ys['servo'], ys['servo']],
            color=fk.FAINT, lw=0.7, ls=(0, (2.0, 1.6)), zorder=2)


def _board_and_devices(ax):
    for i, (pin, dev, way) in enumerate(ROWS):
        y = _row_y(i)
        fk.box(ax, BX + 6, y, BW - 12, BOX_H, pin, 'pl')
        fk.box(ax, EX + 6, y, EW - 12, BOX_H, dev, 'ext')
        a, b = (BX + BW - 6, y + BOX_H / 2.0), (EX + 6, y + BOX_H / 2.0)
        if way == 'in':
            a, b = b, a
        fk.arrow(ax, a, b, both=(way == 'both'), ms=5.5)


def draw():
    fig, ax = fk.new_fig(H)
    fk.group(ax, PX, 30, PW, 340, '供电与急停', 'ext')
    fk.group(ax, BX, 30, BW, 340, '开发板引脚', 'pl', note='J12 = 3.3 V')
    fk.group(ax, EX, 30, EW, 340, '外设与执行机构', 'ext')
    _power(ax)
    _board_and_devices(ax)
    fk.text(ax, PX + 2, 19, '地：J12 PIN2 单点接 24 V 地母排；6.8 V 输出负端只接舵机地母排，'
            '不与输入负端外接。', fs=fk.FS['note'], color=fk.MUTED)
    fk.text(ax, PX + 2, 8, 'BSS138 低压侧取 J12 PIN1 的 3.3 V；虚线括号内三项随急停一起断电。',
            fs=fk.FS['note'], color=fk.MUTED)
    return fig, ax


def main():
    fig, ax = draw()
    return fk.save(fig, NAME)


if __name__ == '__main__':
    main()
