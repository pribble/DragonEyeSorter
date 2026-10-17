#!/usr/bin/env python3
"""make_system_wiring.py — the whole system's wiring on one page: mains, 24 V supply, emergency stop, conveyor driver,
servo supply (XY6020L 6.8 V), 5 V supply, level shifter, arm servo chain, flap servos and the board header J12.
Solid = wired, dashed = still to wire / to buy. Long-distance nets (PL ground, 5 V) are drawn as labelled tags.
Source of truth for the values: docs/硬件与接线.md, docs/到货联调清单.md.

    python docs/figures/make_system_wiring.py
"""
import os

import matplotlib

matplotlib.use('Agg')
import matplotlib.patheffects as pe  # noqa: E402
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.patches import Circle, FancyBboxPatch  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
plt.rcParams['font.sans-serif'] = ['Microsoft YaHei', 'Noto Sans CJK SC', 'WenQuanYi Zen Hei', 'SimHei', 'DejaVu Sans']
plt.rcParams['axes.unicode_minus'] = False

# net colours
P24, G24 = '#d32f2f', '#111111'      # +24 V (after the e-stop), 24 V ground
P68, G68 = '#c2185b', '#6d4c41'      # 6.8 V servo supply, its ground (XY6020L OUT-)
P5, P33 = '#f9a825', '#ff7f0e'       # 5 V, 3.3 V
G5 = '#5d4037'                        # 5 V ground = 5 V module OUT- (same potential as 24 V ground)
PUL, DIR, ENA = '#1565c0', '#2e7d32', '#00838f'
BUS, FLAP, EST = '#7b1fa2', '#8d6e63', '#006064'

fig = plt.figure(figsize=(22, 14.6))
ax = fig.add_axes([0, 0, 1, 1])
ax.set_xlim(0, 220)
ax.set_ylim(0, 146)
ax.axis('off')
HALO = [pe.Stroke(linewidth=6, foreground='white'), pe.Normal()]


def box(x, y, w, h, title, fc='#f5f5f5', ec='#616161', dashed=False, fs=12, ty=None):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle='round,pad=0,rounding_size=1.2', fc=fc, ec=ec, lw=2,
                                ls='--' if dashed else '-'))
    ax.text(x + w / 2, y + h - 1.6 if ty is None else ty, title, ha='center', va='top', fontsize=fs, weight='bold')


def term(x, y, label, side='r', color='#212121', fs=10):
    ax.add_patch(Circle((x, y), 0.7, fc='#ffd54f', ec='#6d4c41', lw=1, zorder=5))
    if side == 'r':
        ax.text(x - 1.3, y, label, ha='right', va='center', fontsize=fs, color=color)
    elif side == 'l':
        ax.text(x + 1.3, y, label, ha='left', va='center', fontsize=fs, color=color)
    elif side == 'b':
        ax.text(x, y + 1.4, label, ha='center', va='bottom', fontsize=fs, color=color, rotation=90)


def wire(pts, color, dashed=False, lw=2.6):
    xs, ys = zip(*pts)
    ax.plot(xs, ys, color=color, lw=lw, ls=(0, (5, 3)) if dashed else '-', solid_capstyle='round',
            path_effects=HALO, zorder=3)


def tag(x, y, text, color, side='l', dashed=False):
    """net label: 'l' = the tag sits to the left of (x, y), 'r' = to the right"""
    ha = 'right' if side == 'l' else 'left'
    ax.text(x + (-1 if side == 'l' else 1), y, text, ha=ha, va='center', fontsize=10, color='white', weight='bold',
            bbox=dict(boxstyle='round,pad=0.25', fc=color, ec=color), zorder=6)


def dot(x, y, color):
    ax.add_patch(Circle((x, y), 0.8, fc=color, ec=color, zorder=6))


def note(x, y, text, fs=10, color='#424242', ha='left', va='top', weight='normal'):
    ax.text(x, y, text, ha=ha, va=va, fontsize=fs, color=color, weight=weight)


ax.text(2, 143.5, 'DragonEyeSorter 全系统接线图', fontsize=20, weight='bold', va='top')
note(64, 143, '实线 = 已接   虚线 = 待接 / 待买   线交叉处没有圆点 = 不相连   彩色小标签 = 同名的点接在一起'
     '（比如所有「PL 地」都进同一个 WAGO）', fs=12, color='#212121')

# ---------------- mains and 24 V supply ----------------
box(2, 112, 20, 16, '插排 220V\n（带开关）', fc='#eceff1')
note(3, 106, '急停到货前（现状）：\n+V ① 直接接 TB6600 VCC，\n插排开关 = 急停', fs=10, color='#c62828')
box(30, 95, 24, 35, 'LRS-350-24\n开关电源', fc='#ffebee', ec=P24)
term(30, 122, '', 'l')
wire([(22, 122), (30, 122)], G24)
note(23, 125.5, 'L / N / FG\nFG 必须接地', fs=9)
for lab, y in (('+V ①', 125), ('+V ②', 120), ('+V ③', 115), ('−V ①', 109), ('−V ②', 105), ('−V ③', 99)):
    term(54, y, lab, 'r', P24 if lab.startswith('+') else G24)

# emergency stop
box(64, 112, 24, 20, '急停（待买）\n22mm 自锁 · 一常开一常闭', fc='#fff8e1', ec='#f57f17', dashed=True, fs=10.5)
note(60, 137.5, '常闭 11-12：按下 → 步进 + 全部舵机断电\n常开 23-24：按下 → PIN15 拉低 → PL 立刻停步进',
     fs=9.5, color='#e65100')
term(64, 116, '11', 'l')
term(88, 116, '12', 'r')
note(76, 117.6, '常闭', fs=9, ha='center')
term(88, 123, '23', 'r')
term(88, 120, '24', 'r')
note(80, 123.6, '常开', fs=9, ha='center')
wire([(54, 125), (59, 125), (59, 116), (64, 116)], P24, dashed=True)
wire([(88, 123), (90, 123)], EST, dashed=True)
tag(90, 123, '→ PIN15', EST, side='r')
wire([(88, 120), (90, 120)], G24, dashed=True)
tag(90, 120, '→ PL 地', G24, side='r')

# controlled +24 V bus: e-stop 12 -> TB6600 VCC and XY6020L IN+
wire([(88, 116), (95, 116), (95, 111), (100, 111)], P24)
wire([(95, 111), (95, 54), (100, 54)], P24, dashed=True)
dot(95, 111, P24)
ax.text(96.2, 92, '受控 24V', fontsize=9.5, color=P24, weight='bold', rotation=90, ha='left', va='center')

# ---------------- conveyor: TB6600 + 57 stepper ----------------
box(100, 84, 32, 30, 'TB6600 步进驱动器', fc='#e8f5e9', ec='#2e7d32')
term(100, 111, 'VCC', 'l')
term(100, 105, 'GND', 'l')
wire([(54, 105), (100, 105)], G24)
note(102, 100.5, '拨码 1=OFF 2=OFF 3=ON\n4=OFF 5=ON 6=ON\n（16 细分，2.5A）', fs=9)
sig = [('PUL+', 111, PUL), ('PUL−', 108, G24), ('DIR+', 105, DIR), ('DIR−', 102, G24), ('ENA+', 99, ENA),
       ('ENA−', 96, G24)]
for lab, y, c in sig:
    term(132, y, lab, 'r', c, fs=9.5)
for lab, y, c in sig[0::2]:
    wire([(132, y), (182, y)], c)
wire([(132, 108), (136, 108), (136, 92)], G24)
for y in (108, 102, 96):
    wire([(132, y), (136, y)], G24)
    dot(136, y, G24)
tag(136, 91, '→ PL 地', G24, side='r')
note(138, 95.5, '三个「−」并在一起', fs=9)
for i, (lab, c) in enumerate((('A+ 蓝', '#1e88e5'), ('A− 红', '#e53935'), ('B+ 黑', '#212121'), ('B− 绿', '#43a047'))):
    x = 104 + i * 7
    term(x, 84, '', 'b')
    wire([(x, 84), (x, 78)], c, lw=2.2)
    note(x + 0.8, 81, lab, fs=8.5, va='center')
box(100, 70, 32, 8, '57 步进电机 → 传送带主动轴', fc='#e8f5e9', ec='#2e7d32', fs=10.5, ty=76.6)
note(100, 69, '实测 0.0207mm/步（4 圈 265mm）；方向 dir = −1', fs=9)

# ---------------- 5 V supply (常电, not through the e-stop) ----------------
box(28, 70, 24, 16, '5V 降压模块\n（可调，先量后接）', fc='#fffde7', ec=P5, dashed=True, fs=10.5)
term(52, 80, 'IN+', 'r')
term(52, 76, 'IN−', 'r')
term(28, 80, 'OUT+', 'l')
term(28, 76, 'OUT−', 'l')
wire([(54, 115), (57.5, 115), (57.5, 80), (52, 80)], P24, dashed=True)
wire([(54, 99), (56, 99), (56, 76), (52, 76)], G24, dashed=True)
note(59, 92, '+V ③ / −V ③ 走「常电」\n（不经过急停）', fs=9)
wire([(28, 80), (25, 80)], P5, dashed=True)
tag(24.5, 80, '5V', P5, side='l')
wire([(28, 76), (25, 76)], G5, dashed=True)
tag(24.5, 76, '5V 地', G5, side='l')
box(28, 56, 24, 10, '风扇 5V（可选）', fc='#fffde7', ec=P5, dashed=True, fs=10, ty=64.3)
note(29.5, 60.6, '红 ← 5V   黑 ← 5V 地\n过渡期可插 USB 口', fs=8.5)

# ---------------- servo supply XY6020L ----------------
box(100, 34, 32, 24, '数控降压 XY6020L', fc='#fce4ec', ec=P68)
term(100, 54, 'IN+', 'l')
term(100, 44, 'IN−', 'l')
term(132, 54, 'OUT+', 'r')
term(132, 44, 'OUT−', 'r')
wire([(54, 109), (92, 109), (92, 44), (100, 44)], G24, dashed=True)
note(102, 51, '设 6.8V · 限流 5A（首次 2A）· 上电不输出\nOUT+/OUT− 并 1000µF（长脚 = +）\n现状：屏幕不亮，待查',
     fs=9)
note(102, 39.5, 'OUT− 只接舵机地，不和任何\n别的地相连（否则限流失效）', fs=9, color='#c62828')

# arm servo chain
names = ['#1 底座', '#2', '#3', '#4', '#5 腕旋', '#6 夹爪']
for i, n in enumerate(names):
    x = 100 + i * 12
    box(x, 7, 10, 9, n, fc='#fce4ec', ec=P68, dashed=True, fs=9.5, ty=14.2)
    if i:
        wire([(x - 2, 10), (x, 10)], '#9e9e9e', dashed=True, lw=2)
note(100, 5.6, '机械臂：6 个总线舵机菊花链（PH2.0 三芯：信号 / V+ / GND，三根都并联），ID 1~6\n'
     '入口 = #1（剪开的那根线，三根线等万用表认）；以后在 #3/#4 再补一路电', fs=9.5)
for x, lab in ((102, '地'), (104.5, 'V+'), (107, '信号')):
    term(x, 16, '', 'b')
    ax.text(x, 18.3, lab, ha='center', va='bottom', fontsize=8, zorder=7,
            bbox=dict(boxstyle='round,pad=0.1', fc='white', ec='none'))
wire([(132, 54), (137, 54), (137, 27), (104.5, 27), (104.5, 16)], P68, dashed=True)
wire([(132, 44), (134, 44), (134, 30), (102, 30), (102, 16)], G68, dashed=True)
note(138, 42, '6.8V', fs=10, color=P68, weight='bold')
note(124, 31.6, '6.8V 地', fs=9, color=G68, weight='bold')

# flap servos
for (y, lab, py, xs) in ((44, '翻板 1 MG996R', 60, 176), (32, '翻板 2 MG996R', 54, 179)):
    box(152, y, 18, 8, lab, fc='#efebe9', ec=FLAP, dashed=True, fs=9.5, ty=y + 6.6)
    note(153, y + 2.8, '红 6.8V · 棕 6.8V 地', fs=8)
    wire([(182, py), (xs, py), (xs, y + 4), (170, y + 4)], FLAP, dashed=True)
note(152, 30.5, '翻板电源也接 XY6020L 的 6.8V', fs=8.5, color=P68)

# ---------------- level shifter BSS138 ----------------
box(146, 58, 24, 28, 'BSS138 电平转换', fc='#e3f2fd', ec='#1565c0', fs=11)
for lab, y in (('HV', 80), ('GND', 74), ('HV1', 68)):
    term(146, y, lab, 'l', '#1565c0')
for lab, y in (('LV', 80), ('GND', 74), ('LV1', 68)):
    term(170, y, lab, 'r', '#1565c0')
note(158, 64.6, '两侧 GND 内部相通\n= PL 地与 24V 地（经 5V 地）\n唯一的连接点', fs=8.5, ha='center')
wire([(170, 80), (182, 80)], P33)
wire([(170, 68), (182, 68)], BUS)
wire([(170, 74), (172, 74)], G24)
tag(172.5, 74, 'PL 地', G24, side='r')
wire([(146, 80), (143, 80)], P5, dashed=True)
tag(142.5, 80, '5V', P5, side='l')
wire([(146, 74), (143, 74)], G5, dashed=True)
tag(142.5, 74, '5V 地', G5, side='l')
wire([(146, 68), (141, 68), (141, 21), (107, 21), (107, 16)], BUS, dashed=True)
note(142, 25, '舵机信号', fs=9.5, color=BUS, weight='bold')
note(146, 89.5, '低压侧（右）今天接；高压侧（左）\n等万用表 + 5V 模块调好再接', fs=9, color='#1565c0')

# ---------------- the board ----------------
box(182, 36, 36, 98, 'FPGA 板 MLK-F3P（DR1）', fc='#ede7f6', ec='#4527a0', fs=13)
note(200, 129.5, '原配 12V 适配器单独供电', fs=9.5, ha='center')
note(184, 125.8, 'J12 排针（CEP，3.3V）', fs=10.5, weight='bold', color='#4527a0')
pins = [(122, 'PIN15', 'ESTOP_IN（低 = 急停）', EST), (118, 'PIN2', 'GND（= PL 地，接 WAGO）', G24),
        (111, 'PIN3', 'STEP_PUL', PUL), (105, 'PIN4', 'STEP_DIR', DIR), (99, 'PIN5', 'STEP_ENA', ENA),
        (80, 'PIN1', '3.3V（只给逻辑，<100mA）', P33), (68, 'PIN19', 'SERVO_BUS', BUS),
        (60, 'PIN18', 'FLAP1_PWM', FLAP), (54, 'PIN20', 'FLAP2_PWM', FLAP)]
for y, p, f, c in pins:
    term(182, y, '', 'l')
    note(183.5, y, p, fs=10, color=c, weight='bold', va='center')
    note(190.5, y, f, fs=9.5, color=c, va='center')
wire([(182, 122), (178, 122)], EST, dashed=True)
tag(177.5, 122, '急停 23', EST, side='l')
wire([(182, 118), (178, 118)], G24)
tag(177.5, 118, 'PL 地（WAGO）', G24, side='l')
note(184, 43, '其余 J12 脚空闲', fs=9, color='#757575')

# board ports and peripherals
box(174, 3, 44, 30, '板上其它接口', fc='#f3e5f5', ec='#6a1b9a', fs=11)
note(176, 28.5, 'HDMI → 显示器（1080p 60fps）\nPS 网口 → 笔记本（192.168.137.1）\n串口 USB → 笔记本 COM4\n'
     'USB → 鼠标 / 键盘\nFEP → VID005 子卡 → IMX415 相机（延长线在路上）\n'
     'J11 绿色端子 CANH0 / CANL0 → USB-CAN（ZQWL）\n   → 笔记本；两端各 120Ω（板上已有）', fs=8.8)

# ---------------- rules ----------------
box(2, 4, 54, 46, '必须遵守', fc='#fff3e0', ec='#e65100', fs=12)
note(4, 43.5, '① 一个螺钉端子只压一根线；多股线压管型端子\n'
     '   （到货前：拧紧，压在压板下，拽一下不松）\n'
     '② 数控电源 OUT− 只接舵机地（及翻板地），\n'
     '   不接 24V 地、不接 PL 地（采样电阻旁路 → 限流失效）\n'
     '③ PL 地与 24V 地只在 BSS138 那一处相连（经 5V 地）\n'
     '④ 5V 地从 5V 模块 OUT− 取；它的 IN−、OUT− 不在外面并联\n'
     '⑤ J12 PIN1 只给 BSS138 的 LV，别的都不接\n'
     '⑥ 接线、插拔一律断电；先量电压再接负载\n'
     '   （5V 模块、数控电源都要先空载量过）\n'
     '⑦ 舵机先在 5V / 6.8V 下只 ping、读位置，\n'
     '   机械臂收拢、周围无障碍再让它动', fs=10)

out = os.path.join(HERE, 'fig_system_wiring.png')
fig.savefig(out, dpi=110, facecolor='white')
print('wrote', out)
