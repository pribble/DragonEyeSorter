#!/usr/bin/env python3
"""make_figures.py — diagrams for the technical document and the PPT (SVG + PNG, Chinese labels).

    python docs/report/figures/make_figures.py          # writes fig_*.svg / fig_*.png next to this file

Edit the numbers / labels here and re-run; the figures are not hand-drawn so they stay consistent with the docs.
Needs matplotlib and a CJK font (Microsoft YaHei on Windows, Noto Sans CJK / WenQuanYi on Linux).
"""
import os

import matplotlib

matplotlib.use('Agg')
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.patches import FancyArrowPatch, FancyBboxPatch, Rectangle, Polygon, Circle  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
plt.rcParams['font.sans-serif'] = ['Microsoft YaHei', 'Noto Sans CJK SC', 'WenQuanYi Zen Hei', 'SimHei', 'DejaVu Sans']
plt.rcParams['axes.unicode_minus'] = False
plt.rcParams['svg.fonttype'] = 'none'   # keep text as text in the SVG (editable in PPT)

# palette: PL blue, PS green, NPU orange, external gray, accent red
C = {
    'pl': ('#dbe9f7', '#2f6db3'), 'ps': ('#e0f2e3', '#2e8b57'), 'npu': ('#fde9d4', '#d9822b'),
    'ext': ('#eeeeee', '#666666'), 'mem': ('#f3e8f8', '#8a4fa8'), 'acc': ('#fbe3e3', '#c0392b'),
}


def canvas(w, h, title=None):
    fig = plt.figure(figsize=(w / 10, h / 10), dpi=100)
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, w)
    ax.set_ylim(0, h)
    ax.axis('off')
    if title:
        ax.text(w / 2, h - 2.5, title, ha='center', va='top', fontsize=15, weight='bold', color='#222')
    return fig, ax


def box(ax, x, y, w, h, text, kind='pl', fs=9.5, weight='normal', round_=1.2, lw=1.3, align='center'):
    face, edge = C[kind]
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle=f'round,pad=0,rounding_size={round_}', fc=face, ec=edge, lw=lw))
    tx = x + w / 2 if align == 'center' else x + 1.2
    ax.text(tx, y + h / 2, text, ha=align, va='center', fontsize=fs, weight=weight, color='#1a1a1a', linespacing=1.35)
    return (x, y, w, h)


def group(ax, x, y, w, h, label, kind):
    face, edge = C[kind]
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle='round,pad=0,rounding_size=2', fc='none', ec=edge, lw=2,
                                ls=(0, (5, 3))))
    ax.text(x + 1.5, y + h - 1.2, label, ha='left', va='top', fontsize=11, weight='bold', color=edge)


def arrow(ax, p, q, text=None, color='#444', lw=1.4, style='-|>', fs=8, off=(0, 1.0), both=False, ls='-'):
    a = FancyArrowPatch(p, q, arrowstyle=('<|-|>' if both else style), mutation_scale=11, lw=lw, color=color,
                        linestyle=ls, shrinkA=0, shrinkB=0)
    ax.add_patch(a)
    if text:
        ax.text((p[0] + q[0]) / 2 + off[0], (p[1] + q[1]) / 2 + off[1], text, ha='center', va='bottom', fontsize=fs,
                color=color)


def poly(ax, pts, color='#444', lw=1.4, ls='-'):
    """Poly-line with an arrow head on the last segment (routes around boxes)."""
    for a, b in zip(pts[:-2], pts[1:-1]):
        ax.plot([a[0], b[0]], [a[1], b[1]], color=color, lw=lw, ls=ls, solid_capstyle='butt')
    arrow(ax, pts[-2], pts[-1], color=color, lw=lw, ls=ls)


def save(fig, name):
    for ext in ('svg', 'png'):
        fig.savefig(os.path.join(HERE, f'{name}.{ext}'), dpi=200 if ext == 'png' else None, facecolor='white')
    plt.close(fig)
    print('wrote', name)


# ------------------------------------------------------------------------------------------------ 1 architecture
def fig_architecture():
    fig, ax = canvas(160, 100, '系统架构：PL 硬实时 + PS 调度 + NPU 推理（安路飞龙 DR1M90）')
    # ---- external devices, each level with the block it talks to
    box(ax, 2, 72, 16, 8, 'IMX415 相机\n4 lane MIPI 1080p60', 'ext', fs=8)
    box(ax, 2, 60, 16, 8, 'HDMI 显示器\n1080p60 + 操作界面', 'ext', fs=8)
    box(ax, 2, 36, 16, 8, '6 轴机械臂\n德晟总线舵机 115200', 'ext', fs=8)
    box(ax, 2, 24, 16, 8, '传送带\n57 步进 + TB6600', 'ext', fs=8)
    box(ax, 2, 13, 16, 7, '带尾两级翻板\nMG996R ×2', 'ext', fs=8)
    box(ax, 2, 3, 16, 7, '急停（常闭切动力，\n常开报 PL）', 'acc', fs=8)
    box(ax, 142, 82, 16, 6, 'USB 鼠标 / 键盘', 'ext', fs=8)
    box(ax, 142, 41, 16, 9, 'PC「产线 PLC」\nUSB-CAN 500k', 'ext', fs=8)
    box(ax, 142, 27, 16, 9, 'SD 卡：每件 CSV\n+ 不良件图像', 'ext', fs=8)

    # ---- PL
    group(ax, 22, 1, 56, 88, 'PL（可编程逻辑）', 'pl')
    box(ax, 25, 72, 15, 8, 'MIPI RX\n(LVDS) + 解包', 'pl', fs=8.5)
    box(ax, 43, 72, 15, 8, 'ISP\n去马赛克 / 白平衡*', 'pl', fs=8.5)
    box(ax, 61, 72, 15, 8, 'des_vision\n灰度 / 高斯 / Sobel / 二值', 'pl', fs=7.8, weight='bold')
    box(ax, 25, 60, 15, 8, 'HDMI TX\n(VDMA 读显示缓冲)', 'pl', fs=8)
    box(ax, 61, 60, 15, 8, '帧写入 DMA\n+ 拍摄锁存', 'pl', fs=8.5)
    box(ax, 25, 47, 51, 9, 'des_regs：AXI-Lite 寄存器窗口 0x8030_1000（PS 单次读 188ns）\n'
        '75MHz 自由计数器 · 帧号 / 帧周期 · 拍摄瞬间的帧号 + 时间戳 + 带位置', 'pl', fs=8)
    box(ax, 25, 35, 24, 10, 'servo_bus  德晟协议主机\n补帧头/校验 · SYNC WRITE\n应答解析 · 超时 · 时间戳', 'pl', fs=7.8)
    box(ax, 25, 23, 24, 10, 'stepper_ctrl\n梯形加减速 · 位置/速度模式\n刹车距离 · 首脉冲 10.6µs', 'pl', fs=7.8)
    box(ax, 25, 12.5, 24, 8, 'des_pwm\n两级翻板 50Hz + 光源', 'pl', fs=7.8)
    box(ax, 25, 3, 24, 7, '急停锁存：步进立即停', 'acc', fs=7.8)
    box(ax, 52, 3, 24, 42, 'NPU\n硬核 512 MAC\n+ 软核算子\n（官方 IP）\n\nArmNN + Alnpu\nUINT8 QDQ', 'npu', fs=8.5)

    # ---- PS
    group(ax, 82, 1, 56, 88, 'PS（双核 Cortex-A35 · Linux）des_app', 'ps')
    box(ax, 85, 72, 50, 10, '核 0（实时优先级）：取帧 → BGRA→BGR → 叠加跟踪框 / 状态\n'
        '→ 贴操作界面（总览 / 曲线 / 参数 / 记录）→ 翻页 60fps', 'ps', fs=8)
    box(ax, 85, 55, 50, 12, '核 1：定位 (x, y, θ) → 亚像素几何测量（视差 / 卷帘补偿）\n'
        '→ 跟踪表（带坐标 s = x − B）→ 停带抓取 / 翻板调度', 'ps', fs=8)
    box(ax, 85, 41, 24, 10, 'NPU 线程：ROI 224²\nYOLOv8n-cls UINT8\n7.8ms / 件 · 软投票', 'npu', fs=7.8)
    box(ax, 111, 41, 24, 10, 'CAN 状态机\n空闲/运行/暂停/急停/故障\n10Hz 状态 · 每件结果', 'ps', fs=7.8)
    box(ax, 87, 27, 22, 10, '机械臂线程\n逆运动学 → 6 轴航点\n→ SYNC WRITE', 'ps', fs=7.8)
    box(ax, 111, 27, 24, 10, '存储（后台线程）\n每件 39 列 CSV\n不良件裁切图', 'ps', fs=7.8)
    box(ax, 85, 4, 50, 18, '标定与工具\ncalib-cam（单应）· calib-belt（mm/步）· calib-arm（手眼）\n'
        'measure · collect（训练集）· soak（长测）· pl_check（PL 回归）\nPC：PLC 模拟器 · part_stats 出报告', 'ps', fs=8)

    # ---- data paths
    arrow(ax, (18, 76), (25, 76))                                   # camera -> MIPI
    arrow(ax, (40, 76), (43, 76))
    arrow(ax, (58, 76), (61, 76))
    arrow(ax, (68.5, 72), (68.5, 68))                               # vision -> writer
    arrow(ax, (25, 64), (18, 64))                                   # HDMI TX -> monitor
    arrow(ax, (76, 65), (85, 75), '相机帧（DDR）', fs=7.5, off=(-1.5, 1.6))
    arrow(ax, (85, 72.6), (40, 64.6), color='#2e8b57', ls='--')
    ax.text(50, 67.6, '显示缓冲（DDR）', fontsize=7.5, color='#2e8b57')
    arrow(ax, (76, 51.5), (85, 59), 'AXI-Lite', both=True, fs=7.5, off=(-2.5, 0.6))
    arrow(ax, (18, 40), (25, 40), both=True)                        # arm <-> servo bus
    arrow(ax, (25, 28), (18, 28))                                   # stepper -> belt driver
    arrow(ax, (25, 16.5), (18, 16.5))                               # pwm -> flaps
    arrow(ax, (18, 6.5), (25, 6.5), color='#c0392b')                # estop -> latch
    arrow(ax, (97, 55), (97, 51), color='#d9822b', both=True)       # worker <-> npu thread (crops / votes)
    poly(ax, [(85, 58), (83.6, 58), (83.6, 32), (87, 32)], color='#2e8b57')   # worker -> arm thread
    arrow(ax, (123, 55), (123, 51), color='#2e8b57')                # worker -> CAN state machine
    arrow(ax, (135, 46), (142, 46), both=True)                      # CAN <-> PC
    arrow(ax, (135, 32), (142, 32))                                 # storage -> SD
    arrow(ax, (142, 85), (135, 80))                                 # mouse -> core 0
    ax.text(80, -0.2, '* ISP 白平衡的两个流水线除法器换成逐位除法器（省约 4k LUT + 5k FF），PL slice 占用 82%', ha='center',
            va='top', fontsize=7.5, color='#555')
    save(fig, 'fig_architecture')


# ------------------------------------------------------------------------------------------------ 2 belt layout
def fig_layout():
    """Top view to scale (0.175 units / mm). Parts travel to the LEFT, so table +x points left and table +y
    (90 deg CCW from x, seen from above) points DOWN; the arm sits on the right-hand side of the travel = table -y =
    the TOP of the drawing (user, 10-11). Origin = middle of the upstream edge of the camera view."""
    k = 0.175
    fig, ax = canvas(160, 100, '工位布局（俯视，按比例；零件从右往左走）')
    bx0, bx1 = 10, 10 + 800 * k            # belt 800 x 300 mm
    by0, by1 = 18, 18 + 300 * k
    cy = (by0 + by1) / 2
    ax.add_patch(Rectangle((bx0, by0), bx1 - bx0, by1 - by0, fc='#f4f4f4', ec='#555', lw=1.5))
    for xx in range(14, int(bx1), 6):
        ax.plot([xx, xx - 2], [by0 + 1, by1 - 1], color='#e2e2e2', lw=0.8)
    ax.text(bx0 + 1, by0 + 1.2, '皮带 800 × 300mm', fontsize=8, color='#555')
    # camera view 398 x 224 mm, upstream edge 60 mm from the loading end
    fov_r = bx1 - 60 * k
    fov_l = fov_r - 398 * k
    fh = 224 * k
    ax.add_patch(Rectangle((fov_l, cy - fh / 2), fov_r - fov_l, fh, fc='#dbe9f7', ec='#2f6db3', lw=1.6, alpha=0.9))
    ax.text((fov_l + fov_r) / 2, cy + fh / 2 - 2.5, '相机视野 ≈ 398 × 224mm（4.2mm 镜头，离带面约 300mm，0.21mm/像素）',
            ha='center', fontsize=7.8, color='#2f6db3')
    ax.text((fov_l + fov_r) / 2, cy + 2, '定位 · 亚像素测量（离光轴最近那帧）\nNPU 分类 3~6 次软投票 → 判定', ha='center',
            va='center', fontsize=8.5)
    # table frame at the origin
    ax.add_patch(Circle((fov_r, cy), 0.9, color='#2f6db3', zorder=5))
    arrow(ax, (fov_r, cy), (fov_r - 9, cy), color='#2f6db3', lw=1.6)
    arrow(ax, (fov_r, cy), (fov_r, cy - 7), color='#2f6db3', lw=1.6)
    ax.text(fov_r - 9.5, cy + 0.8, '+x', fontsize=8, color='#2f6db3', ha='right')
    ax.text(fov_r + 0.8, cy - 7.5, '+y', fontsize=8, color='#2f6db3')
    ax.text(fov_r + 1.2, cy + 1.2, '原点', fontsize=7.5, color='#2f6db3')
    # pick line 80 mm downstream of the view
    pick = fov_l - 80 * k
    ax.plot([pick, pick], [by0, by1], color='#2e8b57', lw=2, ls='--')
    ax.text(pick + 1, by0 + 1.2, '抓取线 = 视野下游边 + 80mm', fontsize=7.8, color='#2e8b57')
    # arm on the right-hand side of the travel = table -y = top of the drawing
    box(ax, pick - 9, by1 + 6, 18, 9, '6 轴机械臂（侧装，\n不入镜，工作半径 ~300mm）', 'ps', fs=7.5)
    arrow(ax, (pick, by1 + 6), (pick, cy + 6), color='#2e8b57', lw=1.6)
    box(ax, pick - 27, by1 + 7, 15, 7, '齿轮盒 / 齿条盒', 'ps', fs=7.5)
    # bar lights on both belt sides along the view, gantry with board + camera on the other side
    for yy in (by1 + 1.5, by0 - 3.7):
        ax.add_patch(Rectangle((fov_l + 4, yy), fov_r - fov_l - 8, 2.2, fc='#fff3b0', ec='#b59b00'))
    ax.text(fov_l + 4, by1 + 5.2, '条形光 ×2（两侧，离带面 80~120mm，内倾 30~45°）', fontsize=7.5, color='#8a7400', va='center')
    box(ax, (fov_l + fov_r) / 2 - 20, 2, 40, 7, '龙门支架：板卡 + 相机（机械臂的另一侧）', 'ext', fs=7.8)
    # belt tail flaps on the left
    box(ax, 1, by0 + 6, 8, by1 - by0 - 12, '带尾\n两级\n翻板\n\n复检/NG\n↓\n返修/报废', 'acc', fs=7.5)
    arrow(ax, (pick - 2, cy - 12), (9.5, cy - 12), '不良件不停带，带尾分流', color='#c0392b', fs=7.5, off=(0, 0.6), ls='--')
    arrow(ax, (pick + 30, by0 - 7), (pick + 2, by0 - 7), '走料方向', color='#c0392b', lw=2, fs=9, off=(0, 0.6))
    ax.text(bx1 - 4, cy, '上料', ha='center', va='center', fontsize=9, weight='bold', color='#555')
    save(fig, 'fig_layout')


# ------------------------------------------------------------------------------------------------ 3 time base
def fig_timebase():
    fig, ax = canvas(160, 70, '时间基准：拍摄瞬间锁存带位置，零件坐标与延迟无关')
    # timeline
    y = 50
    ax.plot([8, 152], [y, y], color='#444', lw=1.4)
    ax.text(152, y + 1.5, '时间', ha='right', fontsize=8, color='#444')
    events = [(14, '帧写入开始\nPL 锁存 帧号 / FREE_CNT / POS0', '#2f6db3'),
              (44, '帧写完（中断）\nPL 锁存 POS1', '#2f6db3'),
              (66, 'PS 取到帧\n定位 / 测量 (~6ms)', '#2e8b57'),
              (96, 'NPU 结果可用\n(7.8ms，异步)', '#d9822b'),
              (126, '判定 → 停带 / 翻板\n（数十~数百 ms 后）', '#c0392b')]
    for x, t, col in events:
        ax.plot([x, x], [y - 1.5, y + 1.5], color=col, lw=2)
        ax.text(x, y + 3, t, ha='center', va='bottom', fontsize=8, color=col)
    ax.add_patch(Rectangle((14, y - 9), 30, 6, fc='#dbe9f7', ec='#2f6db3'))
    ax.text(29, y - 6, '卷帘读出 ≈ 16ms\n第 r 行带位置 = POS0 + (POS1 − POS0)·r / H', ha='center', va='center',
            fontsize=7.5)
    # formula panel
    box(ax, 8, 4, 70, 31, '', 'pl')
    ax.text(43, 32, '零件的带坐标（只算一次）', ha='center', fontsize=10, weight='bold', color='#2f6db3')
    ax.text(12, 26, 'x = 单应(像素)  —— 拍摄时零件在台面上的位置', fontsize=9)
    ax.text(12, 20.5, 'B = 该行曝光时的带位置（PL 锁存 + 行插值）', fontsize=9)
    ax.text(12, 14.5, 's = x − B  —— 零件在带上的坐标，之后不变', fontsize=9.5, weight='bold')
    ax.text(12, 8.5, '任意时刻零件位置 = s + B_now（B_now 读 PL 步进计数）', fontsize=9)
    box(ax, 82, 4, 70, 31, '', 'ps')
    ax.text(117, 32, '带来的好处', ha='center', fontsize=10, weight='bold', color='#2e8b57')
    ax.text(86, 26, '• 推理 / 测量晚到多少都不产生位置误差', fontsize=9)
    ax.text(86, 20.5, '• 停带、变速、反转不需要任何特殊处理', fontsize=9)
    ax.text(86, 15, '• 停带：把步进目标改写成「s 恰好到抓取线」', fontsize=9)
    ax.text(86, 10, '  硬件在环实测停带误差 ≤ 0.04mm（1~2 步）', fontsize=9)
    save(fig, 'fig_timebase')


if __name__ == '__main__':
    fig_architecture()
    fig_layout()
    fig_timebase()
