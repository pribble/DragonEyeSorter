#!/usr/bin/env python3
"""fig_sw_flow — 图 7 PS 软件单件处理流程与关键输入输出变量（技术文档 2.3.1 节）。

画布 = 报告落版宽度 447.9pt（15.8cm）× 146pt。两行各四格的蛇形数据流：
上行 取帧与显示（核 0）→ 零件定位（核 1）→ 亚像素测量（核 1）→ NPU 分类（异步队列），
折返后下行 双判定 → 跟踪与调度（核 1）→ 执行（PL / 机械臂）→ 本地存储（独立线程）。
每格第二、三行是关键输入 / 输出变量（与技术文档 2.3.2 各小节一致）；
NPU 与存储经异步队列解耦，工作线程（核 1）不等待推理与写卡。
版式沿用 figkit 期刊线稿风：白底方框细边、黑色细箭头、无装饰色。
"""
import figkit as fk

NAME = 'fig_sw_flow'
H = 146.0

BW, BH, GAP = 100.0, 44.0, 12.0               # 盒宽 / 盒高 / 水平间距
X0 = (447.92 - (4 * BW + 3 * GAP)) / 2.0      # 水平居中
Y1, Y2 = 90.0, 22.0                           # 上行 / 下行盒底
MID = 78.0                                    # 折返走线高度

STEPS = [
    ('取帧与显示（核 0）', '入 DMA 帧 · CAP 锁存', '出 带位置 B · 显示缓冲'),
    ('零件定位（核 1）', '入 灰度图 · 平场基准', '出 x, y, θ, 长, 宽'),
    ('亚像素测量（核 1）', '入 ROI 裁切图', '出 外径/孔径/齿数/颜色'),
    ('NPU 分类（异步队列）', '入 224×224 UINT8', '出 7 类概率 · 8.0 ms'),
    ('双判定', '入 分类结果 ⊕ 测量结果', '出 处置 良品/NG/复检'),
    ('跟踪与调度（核 1）', '入 带坐标 s · 刹车距离', '出 停带目标 · 挡板时刻'),
    ('执行（PL / 机械臂）', '入 目标位置 · 运动时间', '出 步进脉冲 · SYNC WRITE'),
    ('本地存储（独立线程）', '入 判定与测量全量', '出 44 列 CSV · NG 裁切图'),
]


def draw():
    fig, ax = fk.new_fig(H)
    xs = [X0 + i * (BW + GAP) for i in range(4)]

    for i, (t, si, so) in enumerate(STEPS[:4]):
        fk.box(ax, xs[i], Y1, BW, BH, '%s\n%s\n%s' % (t, si, so), 'ps')
    for i, (t, si, so) in enumerate(STEPS[4:]):
        fk.box(ax, xs[i], Y2, BW, BH, '%s\n%s\n%s' % (t, si, so), 'ps')

    # 上行顺排箭头 + 折返走线（④ 底部 → 中线 → ⑤ 顶部）+ 下行顺排箭头
    for i in range(3):
        fk.arrow(ax, (xs[i] + BW, Y1 + BH / 2.0), (xs[i + 1], Y1 + BH / 2.0))
    x4, x5 = xs[3] + BW / 2.0, xs[0] + BW / 2.0
    ax.plot([x4, x4], [Y1, MID], color=fk.ARROW, lw=0.9, solid_capstyle='butt', zorder=2)
    ax.plot([x4, x5], [MID, MID], color=fk.ARROW, lw=0.9, solid_capstyle='butt', zorder=2)
    fk.arrow(ax, (x5, MID), (x5, Y2 + BH))
    for i in range(3):
        fk.arrow(ax, (xs[i] + BW, Y2 + BH / 2.0), (xs[i + 1], Y2 + BH / 2.0))

    fk.text(ax, X0, 8, '定位 / 测量 / 调度集中在核 1；NPU 与存储经异步队列解耦，'
                       '每件零件过视野时分类 3~6 次、按票累加。', fs=fk.FS['sub'], color=fk.MUTED)

    return fig, ax


def main():
    fig, ax = draw()
    return fk.save(fig, NAME)


if __name__ == '__main__':
    main()
