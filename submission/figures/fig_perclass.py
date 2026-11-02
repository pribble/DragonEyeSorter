#!/usr/bin/env python3
"""fig_perclass — 图 15 七类逐类准确率（板端 NPU 实测，期刊线稿风条形图）。

数据全部取自台账 docs/测试与验证.md（2026-10-29 板上 NPU 逐图评测，v16 比特流；
737 张测试图均未参与训练，合计 682/737 = 92.54%，与表 9 / 正文同源）：
  良品 249/250 · 缺料轻 80/80 · 缺料重 80/80 · 脏污轻 62/80 · 脏污重 92/92 · 飞边轻 45/80 · 飞边重 74/75
误判去向（脏污轻→重脏污 17 张、飞边轻→良品 23 张）写在正文里，不入图——
其余零星误判未逐图记录，画进图会变成臆测。完整 7×7 混淆矩阵同理不画。

    python submission/figures/fig_perclass.py        # 单张调试
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import figkit as fk

NAME = 'fig_perclass'

# (类别, 判对, 总数)——顺序与 label_list.json / 表 5 一致，不按数值排序
DATA = [('良品', 249, 250), ('缺料轻', 80, 80), ('缺料重', 80, 80), ('脏污轻', 62, 80),
        ('脏污重', 92, 92), ('飞边轻', 45, 80), ('飞边重', 74, 75)]

# 几何（pt）：画布 447.92 × 96，绘图区四周留白；字号取 figkit.FS 档位。
# 判对数 / 总数放柱内顶部（最矮的柱也有 34pt，容得下一行 7.5pt），画布才能压到 64pt（柱最矮 18pt，仍容得下 7.5pt 标注）。
H = 52.0
M_L, M_R, M_T, M_B = 34.0, 8.0, 7.0, 17.0
PW, PH = fk.FIG_W - M_L - M_R, H - M_T - M_B
BAR_W = 34.0


def draw():
    fig, ax = fk.new_fig(H)
    x0, y0 = M_L, M_B
    # 网格与 y 刻度（0 / 50 / 100%）
    for v in (50, 100):
        y = y0 + PH * v / 100.0
        fk.hline(ax, x0, x0 + PW, y, color=fk.RULE, lw=0.5, ls=(0, (3.0, 2.4)), zorder=0.5)
        fk.text(ax, x0 - 4, y, ('%d%%' % v) if v == 100 else str(v), fs=fk.FS['sub'],
                ha='right', va='center', color=fk.MUTED)
    fk.text(ax, x0 - 4, y0, '0', fs=fk.FS['sub'], ha='right', va='center', color=fk.MUTED)
    # 坐标轴
    fk.tick(ax, x0, y0, y0 + PH, lw=0.9)
    fk.hline(ax, x0, x0 + PW, y0, color=fk.EDGE, lw=0.9)
    # 柱与标注：判对数 / 总数在柱内顶部，类别名在轴下
    slot = PW / len(DATA)
    for i, (name, ok, n) in enumerate(DATA):
        cx = x0 + slot * (i + 0.5)
        top = y0 + PH * ok / float(n)
        fk.rect(ax, cx - BAR_W / 2.0, y0, BAR_W, top - y0, fc='#CFCFCF', ec=fk.EDGE, lw=0.7)
        fk.text(ax, cx, top - 3.2, '%d/%d' % (ok, n), fs=fk.FS['sub'], ha='center',
                va='top', color=fk.INK)
        fk.text(ax, cx, y0 - 8.5, name, fs=fk.FS['sub'], ha='center', va='top', color=fk.BODY)
    return fig, ax


def main():
    fig, ax = draw()
    return fk.save(fig, NAME)


if __name__ == '__main__':
    main()
