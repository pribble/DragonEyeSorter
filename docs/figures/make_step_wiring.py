#!/usr/bin/env python3
"""make_step_wiring.py — where the 4 stepper-driver signal wires go on the board (CEP header J12), drawn on the
vendor silkscreen (位号图). Needs PyMuPDF + matplotlib and the vendor hardware package (path as argument).

    python docs/figures/make_step_wiring.py "<vendor>/05_底板尺寸及器件封装/尺寸结构图/MLK_F3P_250526位号图.pdf"
"""
import os
import sys

import matplotlib

matplotlib.use('Agg')
import matplotlib.pyplot as plt  # noqa: E402
import pymupdf  # noqa: E402
from matplotlib.patches import Circle, FancyBboxPatch, Rectangle  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
plt.rcParams['font.sans-serif'] = ['Microsoft YaHei', 'Noto Sans CJK SC', 'WenQuanYi Zen Hei', 'SimHei', 'DejaVu Sans']
plt.rcParams['axes.unicode_minus'] = False

DPI = 300
CLIP = (172, 252, 300, 296)     # PDF points around J12 (pins at y 280 / 287, column labels above)


def render(pdf, clip=None, dpi=DPI):
    page = pymupdf.open(pdf)[0]
    pix = page.get_pixmap(dpi=dpi, clip=pymupdf.Rect(*clip) if clip else None)
    import numpy as np
    img = np.frombuffer(pix.samples, dtype=np.uint8).reshape(pix.height, pix.width, pix.n)
    return img, page.rect


def main():
    pdf = sys.argv[1]
    full, rect = render(pdf, dpi=110)
    zoom, _ = render(pdf, CLIP)
    s = DPI / 72.0

    def z(xpt, ypt):  # PDF point -> zoom-image pixel
        return (xpt - CLIP[0]) * s, (ypt - CLIP[1]) * s

    fig = plt.figure(figsize=(13, 9.2))
    # ---- top: where J12 is on the board
    ax0 = fig.add_axes([0.02, 0.50, 0.46, 0.46])
    ax0.imshow(full)
    ax0.axis('off')
    k = 110 / 72.0
    ax0.add_patch(Rectangle((176 * k, 262 * k), (294 - 176) * k, (292 - 262) * k, fill=False, ec='#d62728', lw=2.5))
    ax0.set_title('① J12 在哪：板子下边缘，CAN/RS485 绿色端子（J11）右边', fontsize=12, loc='left')
    ax0.annotate('J12（CEP 排针，2 排 × 13 针）', xy=(235 * k, 292 * k), xytext=(120 * k, 318 * k),
                 fontsize=11, color='#d62728', arrowprops=dict(arrowstyle='->', color='#d62728', lw=1.5),
                 annotation_clip=False)

    # ---- bottom: the header close up with the four wires
    ax = fig.add_axes([0.02, 0.02, 0.62, 0.44])
    ax.imshow(zoom)
    ax.axis('off')
    ax.set_title('② 放大：每一列上方印着两个引脚名，上排 = 前一个（单号脚），下排 = 后一个（双号脚）', fontsize=12, loc='left')
    # pin centres (PDF points, read off the 300 dpi render): rightmost column x = 283.2, pitch 7.2 pt (2.54 mm),
    # rows y = 280.4 (top: odd pins, "1" printed at its right end) / 287.4 (bottom: even pins, "2")
    col = lambda i: 283.2 - 7.2 * i   # i = 0 is the PWR/GND column (pins 1, 2)
    top, bot = 280.4, 287.4
    pins = {
        'GND': (col(0), bot, '#111111', 'PIN2  GND'),
        'PUL': (col(1), top, '#d62728', 'PIN3  Y12  STEP_PUL'),
        'DIR': (col(1), bot, '#1f77b4', 'PIN4  Y13  STEP_DIR'),
        'ENA': (col(2), top, '#2ca02c', 'PIN5  T9  STEP_ENA'),
    }
    for name, (x, y, c, _) in pins.items():
        px, py = z(x, y)
        ax.add_patch(Circle((px, py), 13, fill=False, ec=c, lw=3))
    p1 = z(col(0), top)
    ax.add_patch(Circle(p1, 13, fill=False, ec='#ff7f0e', lw=2, ls='--'))

    # ---- right: TB6600 signal terminals and the wires
    ax2 = fig.add_axes([0.66, 0.02, 0.33, 0.94])
    ax2.set_xlim(0, 100)
    ax2.set_ylim(0, 140)
    ax2.axis('off')
    ax2.add_patch(FancyBboxPatch((52, 52), 40, 66, boxstyle='round,pad=0,rounding_size=2', fc='#e8f0e8', ec='#2e7d32', lw=2))
    ax2.text(72, 122, '步进驱动器\n上面那组端子（Signal）', ha='center', fontsize=11)
    terms = ['ENA−', 'ENA+', 'DIR−', 'DIR+', 'PUL−', 'PUL+']
    ty = {t: 110 - i * 10.5 for i, t in enumerate(terms)}
    for i, t in enumerate(terms):
        ax2.add_patch(Rectangle((54, ty[t] - 3.5), 10, 7, fc='#9ccc65', ec='#33691e'))
        ax2.text(57.5, ty[t], str(i + 1), ha='center', va='center', fontsize=8)
        ax2.text(67, ty[t], t, va='center', fontsize=12, weight='bold' if t.endswith('+') else 'normal')
    wires = [('PUL+', '#d62728', 'PIN3（Y12）'), ('DIR+', '#1f77b4', 'PIN4（Y13）'), ('ENA+', '#2ca02c', 'PIN5（T9）'),
             ('PUL−', '#111111', 'PIN2（GND）')]
    for t, c, lab in wires:
        ax2.plot([5, 54], [ty[t], ty[t]], color=c, lw=3)
        ax2.text(6, ty[t] + 1.6, lab + ' →', color=c, fontsize=11, weight='bold')
    # minus terminals tied together
    for a, b in (('PUL−', 'DIR−'), ('DIR−', 'ENA−')):
        ax2.plot([53, 50, 50, 53], [ty[a], ty[a], ty[b], ty[b]], color='#111111', lw=2.2)
    ax2.text(2, ty['DIR−'] - 1, '三个「−」用短线\n串起来，一起接地', fontsize=10, color='#111111')
    ax2.text(2, 38, '橙色虚线圈 = PIN1（3.3V）：\n别碰，别和 PIN2 短接。\n\n共阴接法：板子 3.3V 直接驱动，\n不需要 5V。\n\n'
             '确认 PIN1 / PIN2 的办法：\n板子通电，万用表量最右列\n两个脚之间 = 3.3V；\n其中和板子地导通的是 PIN2。',
             fontsize=10.5, va='top')
    out = os.path.join(HERE, 'fig_step_wiring.png')
    fig.savefig(out, dpi=150, facecolor='white')
    print('wrote', out)


if __name__ == '__main__':
    main()
