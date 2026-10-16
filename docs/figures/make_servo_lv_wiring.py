#!/usr/bin/env python3
"""make_servo_lv_wiring.py — low-voltage side of the servo-bus level shifter (BSS138 module) on the board header J12:
which three header pins, drawn on the vendor silkscreen (位号图) like make_step_wiring.py. Needs PyMuPDF + matplotlib
and the vendor hardware package (path as argument).

    python docs/figures/make_servo_lv_wiring.py "<vendor>/05_底板尺寸及器件封装/尺寸结构图/MLK_F3P_250526位号图.pdf"
"""
import os
import sys

import matplotlib

matplotlib.use('Agg')
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.patches import Circle, FancyBboxPatch, Rectangle  # noqa: E402

from make_step_wiring import CLIP, DPI, render  # same crop of J12 and pin geometry

HERE = os.path.dirname(os.path.abspath(__file__))
plt.rcParams['font.sans-serif'] = ['Microsoft YaHei', 'Noto Sans CJK SC', 'WenQuanYi Zen Hei', 'SimHei', 'DejaVu Sans']
plt.rcParams['axes.unicode_minus'] = False

ORANGE, BLACK, PURPLE, GREY = '#ff7f0e', '#111111', '#7b1fa2', '#9e9e9e'


def main():
    pdf = sys.argv[1]
    full, _ = render(pdf, dpi=110)
    zoom, _ = render(pdf, CLIP)
    s = DPI / 72.0

    def z(xpt, ypt):  # PDF point -> zoom-image pixel
        return (xpt - CLIP[0]) * s, (ypt - CLIP[1]) * s

    fig = plt.figure(figsize=(13, 9.2))
    ax0 = fig.add_axes([0.02, 0.50, 0.46, 0.46])
    ax0.imshow(full)
    ax0.axis('off')
    k = 110 / 72.0
    ax0.add_patch(Rectangle((176 * k, 262 * k), (294 - 176) * k, (292 - 262) * k, fill=False, ec='#d62728', lw=2.5))
    ax0.set_title('① J12 在哪：板子下边缘，传送带那几根线插的同一排针', fontsize=12, loc='left')

    ax = fig.add_axes([0.02, 0.02, 0.62, 0.44])
    ax.imshow(zoom)
    ax.axis('off')
    ax.set_title('② 放大：最右一列 = PIN1（上）/ PIN2（下），往左每列 +2；PIN19 = 从右数第 10 列上排',
                 fontsize=12, loc='left')
    # pin geometry as in make_step_wiring.py: rightmost column x = 283.2 pt, pitch 7.2 pt; rows 280.4 (odd) / 287.4
    col = lambda i: 283.2 - 7.2 * i
    top, bot = 280.4, 287.4
    for x, y, c, ls in ((col(0), top, ORANGE, '-'), (col(0), bot, BLACK, '-'), (col(9), top, PURPLE, '-'),
                        (col(1), top, GREY, '--'), (col(1), bot, GREY, '--'), (col(2), top, GREY, '--')):
        ax.add_patch(Circle(z(x, y), 13, fill=False, ec=c, lw=3, ls=ls))
    box = dict(fc='white', ec='none', alpha=0.85, pad=1.5)
    tx, ty = z(col(9), top)
    ax.annotate('PIN19  SERVO_BUS（Y9）', xy=(tx, ty - 14), xytext=(10, 18), color=PURPLE, fontsize=11, weight='bold',
                bbox=box, arrowprops=dict(arrowstyle='->', color=PURPLE, lw=1.8), annotation_clip=False)
    ox, oy = z(col(0), top)
    ax.annotate('PIN1  3.3V', xy=(ox + 13, oy), xytext=(ox - 75, oy + 92), color=ORANGE, fontsize=11, weight='bold',
                bbox=box, arrowprops=dict(arrowstyle='->', color=ORANGE, lw=1.8, connectionstyle='arc3,rad=0.6'),
                annotation_clip=False)
    gx, gy = z(col(0), bot)
    ax.annotate('PIN2  GND（已插着去 WAGO 的那根）', xy=(gx - 13, gy + 4), xytext=(gx - 400, gy + 62), color=BLACK,
                fontsize=11, bbox=box, arrowprops=dict(arrowstyle='->', color=BLACK, lw=1.5), annotation_clip=False)

    # ---- right: the BSS138 module, low-voltage side only
    ax2 = fig.add_axes([0.66, 0.02, 0.33, 0.94])
    ax2.set_xlim(0, 100)
    ax2.set_ylim(0, 140)
    ax2.axis('off')
    ax2.add_patch(FancyBboxPatch((50, 40), 44, 80, boxstyle='round,pad=0,rounding_size=2', fc='#e3f2fd', ec='#1565c0',
                                 lw=2))
    ax2.text(72, 124, 'BSS138 四路电平转换模块\n（以模块上的丝印为准）', ha='center', fontsize=11)
    lv = ['LV1', 'LV2', 'LV', 'GND', 'LV3', 'LV4']
    hv = ['HV1', 'HV2', 'HV', 'GND', 'HV3', 'HV4']
    yy = {n: 108 - i * 11 for i, n in enumerate(lv)}
    for i, (a, b) in enumerate(zip(lv, hv)):
        y = 108 - i * 11
        ax2.add_patch(Circle((54, y), 1.8, fc='#ffd54f', ec='#8d6e63'))
        ax2.add_patch(Circle((90, y), 1.8, fc='#eeeeee', ec='#9e9e9e'))
        ax2.text(57.5, y, a, va='center', fontsize=12, weight='bold' if a in ('LV1', 'LV', 'GND') else 'normal')
        ax2.text(86.5, y, b, va='center', ha='right', fontsize=10, color='#9e9e9e')
    ax2.text(72, 44, '低压侧（左）现在接\n高压侧（右）先空着', ha='center', fontsize=10, color='#1565c0')
    for n, c, lab in (('LV', ORANGE, 'J12 PIN1（3.3V）'), ('LV1', PURPLE, 'J12 PIN19'),
                      ('GND', BLACK, 'WAGO 空孔（= PIN2 地）')):
        ax2.plot([3, 52], [yy[n], yy[n]], color=c, lw=3)
        ax2.text(4, yy[n] + 1.8, lab + ' →', color=c, fontsize=11, weight='bold')
    ax2.text(2, 33, '灰色虚线圈 = PIN3/4/5，传送带在用，别动。\n\n全部在断电状态下插拔。\nPIN1 与 PIN2 相邻：插好后\n检查两根杜邦头没有碰在一起。\n\n'
             'LV2~LV4、HV 侧全部空着；\n等万用表到了、5V 模块调好，\n再接 HV（5V）、HV 侧 GND、\nHV1（舵机信号线）。',
             fontsize=10.5, va='top')
    out = os.path.join(HERE, 'fig_servo_lv_wiring.png')
    fig.savefig(out, dpi=150, facecolor='white')
    print('wrote', out)


if __name__ == '__main__':
    main()
