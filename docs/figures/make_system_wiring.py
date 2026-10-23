#!/usr/bin/env python3
"""make_system_wiring.py — the whole system's wiring on one page: mains, 24 V supply, emergency stop, conveyor driver,
servo supply (XY6020L 6.8 V), 5 V supply, encoder, level shifter, arm servo chain, diverter gate servos, the board
header J12 and the CAN terminal J11.
Solid = wired, dashed = to wire now / not installed yet. Long-distance nets (PL ground, 5 V) are drawn as labelled tags.
Source of truth for the values: docs/硬件与接线.md, docs/到货联调清单.md. State as of 2026-10-23.

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
P24, G24 = '#d32f2f', '#111111'      # +24 V, 24 V ground (= PL ground through the level shifter)
P68, G68 = '#c2185b', '#6d4c41'      # 6.8 V servo supply, its ground (XY6020L OUT-)
P5, P33 = '#f9a825', '#ff7f0e'       # 5 V, 3.3 V
G5 = '#5d4037'                        # 5 V ground = 5 V module OUT- (same potential as 24 V ground)
PUL, DIR, ENA = '#1565c0', '#2e7d32', '#00838f'
BUS, GATE, EST, ENC = '#7b1fa2', '#8d6e63', '#006064', '#455a64'

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


def tag(x, y, text, color, side='l'):
    """net label: 'l' = the tag sits to the left of (x, y), 'r' = to the right"""
    ha = 'right' if side == 'l' else 'left'
    ax.text(x + (-1 if side == 'l' else 1), y, text, ha=ha, va='center', fontsize=10, color='white', weight='bold',
            bbox=dict(boxstyle='round,pad=0.25', fc=color, ec=color), zorder=6)


def dot(x, y, color):
    ax.add_patch(Circle((x, y), 0.8, fc=color, ec=color, zorder=6))


def note(x, y, text, fs=10, color='#424242', ha='left', va='top', weight='normal'):
    ax.text(x, y, text, ha=ha, va=va, fontsize=fs, color=color, weight=weight)


ax.text(2, 143.5, 'DragonEyeSorter 全系统接线图（2026-10-23）', fontsize=20, weight='bold', va='top')
note(84, 143, '实线 = 已接   虚线 = 今天要接 / 还没装   线交叉处没有圆点 = 不相连   彩色小标签 = 同名的点接在一起'
     '（比如所有「PL 地」都进同一个 WAGO）', fs=11.5, color='#212121')

# ---------------- mains and 24 V supply ----------------
box(2, 112, 20, 16, '插排 220V\n（带开关 = 总闸）', fc='#eceff1')
box(30, 95, 24, 35, 'LRS-350-24\n开关电源', fc='#ffebee', ec=P24)
term(30, 122, '', 'l')
wire([(22, 122), (30, 122)], G24)
note(23, 125.5, 'L / N / FG\nFG 必须接地', fs=9)
for lab, y in (('+V ①', 125), ('+V ②', 120), ('+V ③', 115), ('−V ①', 109), ('−V ②', 105), ('−V ③', 99)):
    term(54, y, lab, 'r', P24 if lab.startswith('+') else G24)
note(30, 93.6, '3 个 +V 内部相通、3 个 −V 内部相通：\n用哪个都行，一孔只压一根线', fs=8.5)

# lights: not on the 24 V supply, not on the PL
box(2, 87, 23, 19, '光源（手动）', fc='#fffde7', ec='#9e9d24', fs=11)
note(3.5, 102, '插排 → 12V 适配器\n→ 光源控制器 → 两根条形灯\n不接 PL、不经急停\n档位定好后不再动', fs=8.8)

# emergency stop
box(64, 112, 24, 20, '急停 22mm 自锁\n一常开一常闭', fc='#fff8e1', ec='#f57f17', fs=10.5)
note(60, 137.5, '常闭 11-12：按下 → 步进 + 机械臂 + 挡板一起断电\n常开 23-24：按下 → PIN15 拉低 → PL 锁存、立刻停步进',
     fs=9.5, color='#e65100')
term(64, 116, '11', 'l')
term(88, 116, '12', 'r')
note(76, 117.6, '常闭', fs=9, ha='center')
term(88, 123, '23', 'r')
term(88, 120, '24', 'r')
note(80, 123.6, '常开', fs=9, ha='center')
wire([(54, 125), (59, 125), (59, 116), (64, 116)], P24)
wire([(88, 123), (90, 123)], EST)
tag(90, 123, '→ PIN15', EST, side='r')
wire([(88, 120), (90, 120)], G24)
tag(90, 120, '→ PL 地', G24, side='r')

# controlled +24 V: e-stop 12 -> new WAGO -> TB6600 VCC and XY6020L IN+
box(91, 108.5, 6, 5, '', fc='#ffebee', ec=P24, dashed=True)
note(94, 111.9, 'WAGO', fs=7.5, ha='center', weight='bold', color=P24)
wire([(88, 116), (89.5, 116), (89.5, 111), (91, 111)], P24)
wire([(97, 111), (100, 111)], P24)
wire([(95, 108.5), (95, 54), (100, 54)], P24, dashed=True)
ax.text(96.2, 82, '受控 24V（急停之后）', fontsize=9.5, color=P24, weight='bold', rotation=90, ha='left',
        va='center')
note(99, 119.6, '今天：12 和 TB6600 之间加这个 WAGO（原来直连），\n再从它分一根到 XY6020L IN+', fs=8.5, color='#c62828')

# ---------------- conveyor: TB6600 + 57 stepper ----------------
box(100, 84, 32, 30, 'TB6600 步进驱动器', fc='#e8f5e9', ec='#2e7d32')
term(100, 111, 'VCC', 'l')
term(100, 105, 'GND', 'l')
wire([(54, 105), (100, 105)], G24)
note(102, 100.5, '拨码 1=OFF 2=OFF 3=ON\n4=OFF 5=ON 6=ON\n（16 细分，2.5A）\n共阴：+ 端 PL 3.3V 直驱', fs=9)
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
box(100, 70, 32, 8, '57 步进电机 → 传送带主动辊', fc='#e8f5e9', ec='#2e7d32', fs=10.5, ty=76.6)
note(100, 69, '0.020921mm/步（10-21 标定）；方向 dir = −1', fs=9)

# ---------------- 5 V supply (always on, not through the e-stop) ----------------
box(28, 70, 24, 16, '5V 降压模块\n（常电，不经急停）', fc='#fffde7', ec=P5, fs=10.5)
term(52, 80, 'IN+', 'r')
term(52, 76, 'IN−', 'r')
term(28, 80, 'OUT+', 'l')
term(28, 76, 'OUT−', 'l')
wire([(54, 115), (57.5, 115), (57.5, 80), (52, 80)], P24)
wire([(54, 99), (56, 99), (56, 76), (52, 76)], G24)
note(59, 92, '+V ③ / −V ③ 走「常电」\n（不经过急停）', fs=9)
wire([(28, 80), (25, 80)], P5)
tag(24.5, 80, '5V', P5, side='l')
wire([(28, 76), (25, 76)], G5)
tag(24.5, 76, '5V 地', G5, side='l')
note(27, 69.4, '现状：机械臂临时也挂在这里（4.9V）\n→ 今天拆下，改接 6.8V 母排', fs=8.5, color='#c62828')

# encoder (metering wheel) on the 5 V supply, outputs through the level shifter
box(28, 48.5, 24, 16, '计米轮编码器\nE6B2-CWZ6C 1000P', fc='#eceff1', ec=ENC, fs=10)
note(29.5, 58.6, '棕 → 5V      蓝 → PL 地\n黑 A → BSS138 HV2\n白 B → HV3   橙 Z → HV4\n屏蔽 → PL 地（只接板子端，未接）',
     fs=8.4)

# ---------------- servo supply XY6020L ----------------
box(100, 34, 32, 24, '数控降压 XY6020L（今天接）', fc='#fce4ec', ec=P68, fs=11)
term(100, 54, 'IN+', 'l')
term(100, 44, 'IN−', 'l')
term(132, 54, 'OUT+', 'r')
term(132, 44, 'OUT−', 'r')
wire([(54, 109), (60, 109), (60, 107), (90, 107), (90, 44), (100, 44)], G24, dashed=True)
note(103.5, 52, '6.8V · 限流 5A · 上电不输出\n过压保护 7.2V · 按键锁\n先空载量出 6.8V 再接舵机\n不另加电容（模块自带）',
     fs=8.8)
note(103.5, 41.6, 'OUT− 只接 6.8V 地母排，不碰\n24V 地 / PL 地（否则限流失效）', fs=8.8, color='#c62828')

# 6.8 V bus and its ground (two new WAGOs)
box(133.5, 51, 10, 6, '', fc='#fce4ec', ec=P68, dashed=True)
note(138.5, 55.9, '6.8V 母排\nWAGO', fs=7.5, ha='center', weight='bold', color=P68)
box(133.5, 41, 10, 6, '', fc='#efebe9', ec=G68, dashed=True)
note(138.5, 45.9, '6.8V 地母排\nWAGO', fs=7.5, ha='center', weight='bold', color=G68)
wire([(132, 54), (133.5, 54)], P68, dashed=True)
wire([(132, 44), (133.5, 44)], G68, dashed=True)
wire([(143.5, 54), (145, 54), (145, 27), (104.5, 27), (104.5, 16)], P68, dashed=True)
wire([(138.5, 41), (138.5, 30), (102, 30), (102, 16)], G68, dashed=True)
note(110, 32.4, '6.8V 地 → #1 地（挡板棕线也接这个 WAGO）', fs=8.5, color=G68, weight='bold')
note(110, 26.2, '6.8V → #1 V+（挡板红线也接这个 WAGO）', fs=8.5, color=P68, weight='bold')

# arm servo chain
names = ['#1 底座', '#2 肩', '#3 肘', '#4 腕俯仰', '#5 腕旋', '#6 夹爪']
for i, n in enumerate(names):
    x = 100 + i * 12
    box(x, 7, 10, 9, n, fc='#fce4ec', ec=P68, fs=9, ty=14.2)
    if i:
        wire([(x - 2, 10), (x, 10)], '#9e9e9e', lw=2)
note(100, 5.6, '机械臂：6 个总线舵机菊花链（PH2.0：信号 / V+ / 地，三根都并联），ID 1~6\n'
     '入口 = #1 那根剪开的线；限流以后加到 8A 时，在 #3/#4 再补一路电', fs=8.6)
for x, lab in ((102, '地'), (104.5, 'V+'), (107, '信号')):
    term(x, 16, '', 'b')
    ax.text(x, 18.3, lab, ha='center', va='bottom', fontsize=8, zorder=7,
            bbox=dict(boxstyle='round,pad=0.1', fc='white', ec='none'))

# diverter gates (MG996R), not mounted yet
for (y, lab, py, xs) in ((36, '挡板 1（报废）MG996R', 47, 176), (27, '挡板 2（返修）MG996R', 41, 179)):
    box(152, y, 18, 7, lab, fc='#efebe9', ec=GATE, dashed=True, fs=9, ty=y + 6.2)
    note(153, y + 2.9, '红 → 6.8V 母排 · 棕 → 地母排', fs=7.6)
    wire([(170, y + 3.5), (xs, y + 3.5), (xs, py), (182, py)], GATE, dashed=True)

# ---------------- level shifter BSS138 ----------------
box(148, 50, 22, 40, 'BSS138 电平转换', fc='#e3f2fd', ec='#1565c0', fs=11)
left = [('HV', 84), ('GND', 79), ('HV2', 73), ('HV3', 68), ('HV4', 63), ('HV1', 55)]
right = [('LV', 84), ('GND', 79), ('LV2', 73), ('LV3', 68), ('LV4', 63), ('LV1', 55)]
for lab, y in left:
    term(148, y, lab, 'l', '#1565c0', fs=9.5)
for lab, y in right:
    term(170, y, lab, 'r', '#1565c0', fs=9.5)
note(159, 49.4, '两侧 GND 内部相通\n= PL 地与 24V 地\n唯一的连接点', fs=8, ha='center')
wire([(148, 84), (146, 84)], P5)
tag(145.5, 84, '5V', P5, side='l')
wire([(148, 79), (146, 79)], G5)
tag(145.5, 79, '5V 地', G5, side='l')
for lab, y in (('编码器 黑 A', 73), ('白 B', 68), ('橙 Z', 63)):
    wire([(148, y), (146, y)], ENC)
    tag(145.5, y, lab, ENC, side='l')
wire([(170, 79), (172, 79)], G24)
tag(172.5, 79, 'PL 地', G24, side='r')
wire([(170, 84), (182, 84)], P33)
for y in (73, 68, 63):
    wire([(170, y), (182, y)], ENC)
wire([(170, 55), (182, 55)], BUS)
wire([(148, 55), (147, 55), (147, 21), (107, 21), (107, 16)], BUS)
note(148, 24.6, '舵机信号', fs=9.5, color=BUS, weight='bold')

# ---------------- the board ----------------
box(182, 36, 36, 98, 'FPGA 板 MLK-F3P（DR1）', fc='#ede7f6', ec='#4527a0', fs=13)
note(200, 129.6, '原配 12V 适配器单独供电', fs=9.5, ha='center')
note(184, 127.4, 'J12 排针（CEP，3.3V）', fs=10.5, weight='bold', color='#4527a0')
note(184, 125.1, 'PIN1/2 在最右一列（丝印 PWR GND），往左每列 +2：\n第 k 列 = PIN 2k−1 / 2k', fs=7.8, color='#4527a0')
pins = [(120, 'PIN15', 'ESTOP_IN（低 = 急停）', EST, False), (116, 'PIN2', 'GND（= PL 地，接 WAGO）', G24, False),
        (111, 'PIN3', 'STEP_PUL', PUL, False), (105, 'PIN4', 'STEP_DIR', DIR, False),
        (99, 'PIN5', 'STEP_ENA', ENA, False), (84, 'PIN1', '3.3V（只给 BSS138 LV）', P33, False),
        (73, 'PIN6', 'ENC_A', ENC, False), (68, 'PIN7', 'ENC_B', ENC, False), (63, 'PIN8', 'ENC_Z', ENC, False),
        (55, 'PIN19', 'SERVO_BUS（机械臂）', BUS, False), (47, 'PIN18', '挡板 1 PWM（FORK_PWM）', GATE, True),
        (41, 'PIN20', '挡板 2 PWM（FLAP2_PWM）', GATE, True)]
for y, p, f, c, todo in pins:
    term(182, y, '', 'l')
    note(183.5, y, p, fs=10, color=c, weight='bold', va='center')
    note(190.5, y, f, fs=9.3, color=c, va='center')
wire([(182, 120), (178, 120)], EST)
tag(177.5, 120, '急停 23', EST, side='l')
wire([(182, 116), (178, 116)], G24)
tag(177.5, 116, 'PL 地（WAGO）', G24, side='l')
note(184, 38.6, 'PIN9 IR1 / PIN12 LIMIT1 可选（未接）；其余空闲', fs=8.3, color='#757575')

# board ports and the CAN terminal
box(174, 16.5, 44, 13, '板上其它接口', fc='#f3e5f5', ec='#6a1b9a', fs=10.5)
note(176, 26.4, 'HDMI → 显示器（1080p 60fps）\nPS 网口 → 笔记本（192.168.137.1）· 串口 USB → COM4\n'
     'USB → 鼠标 / 键盘 · FEP → VID005 子卡 → IMX415 相机', fs=8.4)
box(174, 1.5, 44, 14, 'J11 插头 → USB-CAN（今天接）', fc='#e0f2f1', ec='#00695c', dashed=True, fs=10.5)
note(176, 11.6, 'J11（3.81mm 6 位）：1 地 · 2 CANL · 3 CANH · 4 地 · 5/6 RS485（不接）\n'
     'USB-CAN（ZQWL）：H ← 3、L ← 2、GND ← 1（或 4）\n'
     '终端：板上已有 120Ω；适配器端短接 RS 加 120Ω\n→ 断电量 H–L ≈ 60Ω；USB 插笔记本', fs=8.2)

# ---------------- rules ----------------
box(2, 4, 54, 41, '必须遵守', fc='#fff3e0', ec='#e65100', fs=12)
note(4, 39.6, '① 一个螺钉端子只压一根线；多股线压管型端子\n'
     '② 6.8V 地母排（XY6020L OUT−）只接机械臂和挡板舵机的地，\n'
     '   不接 24V 地、不接 PL 地（否则限流失效）\n'
     '③ PL 地与 24V 地只在 BSS138 那一处相连（经 5V 地）\n'
     '④ 5V 地从 5V 模块 OUT− 取；它的 IN−、OUT− 不在外面并联\n'
     '⑤ J12 PIN1 只给 BSS138 的 LV，别的都不接\n'
     '⑥ 接线、插拔一律断电；电源模块先空载量电压再接负载\n'
     '⑦ 急停：步进 + 机械臂 + 挡板一起断电，机械臂会靠自重落下；\n'
     '   复位 = 旋开按钮 → 界面「复位报警」→ 按 XY6020L 输出键\n'
     '⑧ 上电顺序：板子（等 1 分钟）→ 220V 总闸\n'
     '   → 托住机械臂 → 按 XY6020L 输出键\n'
     '⑨ 线规：电源主干 16AWG；母排 → 驱动器 / 舵机 20AWG；\n'
     '   信号 22AWG / 杜邦线', fs=9.3)

out = os.path.join(HERE, 'fig_system_wiring.png')
fig.savefig(out, dpi=110, facecolor='white')
print('wrote', out)
