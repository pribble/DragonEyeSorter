#!/usr/bin/env python3
"""make_board.py — printable chessboard for `des_tool calib-cam` (A4 landscape, exact millimetres, vector PDF).

    python pc/calib_board/make_board.py                       # 10 x 7 squares of 25 mm -> --board 9x6
    python pc/calib_board/make_board.py --cols 12 --rows 8 --square 20

Print at 100 % ("实际大小"), never "fit to page"; measure the printed squares with a caliper and pass that value as
--square. The page carries a 100 mm scale bar to check the printer scale. Needs matplotlib (+ a CJK font for the notes).
"""
import argparse
import os

import matplotlib

matplotlib.use('Agg')
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.patches import Rectangle  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
plt.rcParams['font.sans-serif'] = ['Microsoft YaHei', 'Noto Sans CJK SC', 'WenQuanYi Zen Hei', 'SimHei', 'DejaVu Sans']
plt.rcParams['axes.unicode_minus'] = False
GRAY = '#9a9a9a'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--cols', type=int, default=10, help='squares along the long side')
    ap.add_argument('--rows', type=int, default=7, help='squares along the short side')
    ap.add_argument('--square', type=float, default=25.0, help='square size, mm')
    ap.add_argument('--out', help='PDF path (default: next to this script)')
    a = ap.parse_args()

    W, H = 297.0, 210.0                         # A4 landscape, mm
    bw, bh = a.cols * a.square, a.rows * a.square
    if bw > W - 30 or bh > H - 30:
        raise SystemExit('board %.0f x %.0f mm does not fit A4 with a 15 mm white border' % (bw, bh))
    x0, y0 = (W - bw) / 2, (H - bh) / 2
    fig = plt.figure(figsize=(W / 25.4, H / 25.4))
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, W)
    ax.set_ylim(0, H)
    ax.axis('off')
    for i in range(a.cols):
        for j in range(a.rows):
            if (i + j) % 2 == 0:
                ax.add_patch(Rectangle((x0 + i * a.square, y0 + j * a.square), a.square, a.square, fc='black',
                                       ec='none', antialiased=False))
    # notes and a 100 mm scale bar in the margins, light gray and thin so they never compete with the corners
    inner = '%dx%d' % (a.cols - 1, a.rows - 1)
    ax.text(W / 2, H - 5.5, '相机标定板  ·  %d×%d 格  ·  格边 %.2f mm  ·  内角点 %s（calib-cam --board %s）' % (
        a.cols, a.rows, a.square, inner, inner), ha='center', va='center', fontsize=8, color=GRAY)
    ax.text(W / 2, H - 10.5, '打印选「实际大小 / 100%%」，不要「适合页面」；贴在硬平板上（不起翘、不反光）；'
            '卡尺量一排 %d 格总长 ÷ %d 作为 --square' % (a.cols, a.cols), ha='center', va='center', fontsize=7, color=GRAY)
    sx = (W - 100) / 2
    ax.plot([sx, sx + 100], [8, 8], color=GRAY, lw=0.6)   # >= 5 mm: printers clip the edge
    for k in range(0, 101, 10):
        ax.plot([sx + k, sx + k], [6.4 if k % 50 else 7.0, 8], color=GRAY, lw=0.6)
    ax.text(sx + 102, 7.4, '100 mm（打印后量这段核对比例）', fontsize=6.5, color=GRAY, va='center')
    out = a.out or os.path.join(HERE, '标定板_A4_%s_%gmm.pdf' % (inner, a.square))
    fig.savefig(out, facecolor='white')   # no bbox_inches='tight': the page must stay exactly A4
    print('wrote', out, '(board %.0f x %.0f mm, white border %.1f / %.1f mm)' % (bw, bh, x0, y0))


if __name__ == '__main__':
    main()
