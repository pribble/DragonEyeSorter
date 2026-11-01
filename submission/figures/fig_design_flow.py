#!/usr/bin/env python3
"""fig_design_flow — 图 1 设计流程（技术文档 1.6 节）。

画布 = 报告落版宽度 447.9pt（15.8cm）× 140pt。两行各四步的蛇形流程：
上行 需求分析 → 总体架构 → PL 硬实时模块 → PS 应用设计，
折返后下行 标定 → 数据与模型 → 板端部署 → 联调与实测。
每格第二行是该项的关键交付物（与技术文档正文一致，不写正文里没有的数）。
版式沿用 figkit 期刊线稿风：白底方框细边、黑色细箭头、无装饰色。
"""
import figkit as fk

NAME = 'fig_design_flow'
H = 140.0

BW, BH, GAP = 96.0, 38.0, 14.0                # 盒宽 / 盒高 / 水平间距
X0 = (447.92 - (4 * BW + 3 * GAP)) / 2.0      # 水平居中
Y1, Y2 = 88.0, 26.0                           # 上行 / 下行盒底
MID = 76.0                                    # 折返走线高度

STEPS = [
    ('需求分析', '赛题六项硬性要求'),
    ('总体架构', 'PL·PS·NPU 三级划分'),
    ('PL 硬实时模块', '寄存器分段 · 逐模块仿真'),
    ('PS 应用设计', '定位测量 · 跟踪调度 · 界面'),
    ('标定', '相机单应 · 皮带 · 编码器'),
    ('数据与模型', '实物采集 · ReLU · 量化'),
    ('板端部署', 'ArmNN · 推理 8.0 ms'),
    ('联调与实测', '硬件在环 · 总线 · 2 h 长时'),
]


def draw():
    fig, ax = fk.new_fig(H)
    xs = [X0 + i * (BW + GAP) for i in range(4)]

    # 上行 1~4、下行 5~8（蛇形：上 1→4，折返后下 5→8）
    for i, (t, sub) in enumerate(STEPS[:4]):
        fk.box(ax, xs[i], Y1, BW, BH, '%s\n%s' % (t, sub), 'ps')
    for i, (t, sub) in enumerate(STEPS[4:]):
        fk.box(ax, xs[i], Y2, BW, BH, '%s\n%s' % (t, sub), 'ps')

    # 上行顺排箭头 + 折返走线（④ 底部 → 中线 → ⑤ 顶部）+ 下行顺排箭头
    for i in range(3):
        fk.arrow(ax, (xs[i] + BW, Y1 + BH / 2.0), (xs[i + 1], Y1 + BH / 2.0))
    x4, x5 = xs[3] + BW / 2.0, xs[0] + BW / 2.0
    ax.plot([x4, x4], [Y1, MID], color=fk.ARROW, lw=0.9, solid_capstyle='butt', zorder=2)
    ax.plot([x4, x5], [MID, MID], color=fk.ARROW, lw=0.9, solid_capstyle='butt', zorder=2)
    fk.arrow(ax, (x5, MID), (x5, Y2 + BH))
    for i in range(3):
        fk.arrow(ax, (xs[i] + BW, Y2 + BH / 2.0), (xs[i + 1], Y2 + BH / 2.0))

    return fig, ax


def main():
    fig, ax = draw()
    return fk.save(fig, NAME)


if __name__ == '__main__':
    main()
