#!/usr/bin/env python3
"""fig_timebase — 图 2-2 时间基准：拍摄瞬间锁存带位置，零件坐标与处理延迟无关。

画布 = 报告落版宽度 447.9pt（15.8cm）× 195pt，字面字号即纸面字号（见 figkit 顶部说明）。
上：时间轴 + 两次锁存（POS0 / POS1）+ 卷帘读出区 + 行插值公式；
下：左「零件的带坐标怎么算」、右「带来的好处」两块面板。
时间轴只示意先后、不按比例（各段从 16ms 到数百 ms），轴端点已注明「示意」。
数值取自技术文档 §2.3 与旧图脚本：卷帘读出 ≈16ms、NPU 7.8ms、定位 / 测量 ~6ms、停带误差 ≤0.04mm。
"""
import figkit as fk

NAME = 'fig_timebase'
H = 195.0

# 时间轴：事件位置（pt）与各自颜色（PL 蓝 / PS 绿 / NPU 橙 / 告警红）
TICK_Y = 156.0
EVENTS = [
    (40.0, '帧写入开始\nPL 锁存 POS0', fk.C['pl'][1], True),
    (134.0, '帧写完\nPL 锁存 POS1', fk.C['pl'][1], True),
    (214.0, 'PS 取到帧\n定位 · 测量 ~6ms', fk.C['ps'][1], False),
    (292.0, 'NPU 结果\n7.8ms · 异步', fk.C['npu'][1], False),
    (378.0, '判定 → 停带 / 挡板\n数十~数百 ms 后', fk.C['acc'][1], False),
]
BAND = (40.0, 116.0, 94.0, 28.0)       # 卷帘读出区：x0, y0, w, h（正好是两次锁存的间隔）
PANEL_Y, PANEL_H = 3.0, 90.0


def draw():
    fig, ax = fk.new_fig(H)
    S = fk.FS['sub']

    # ---------------- 时间轴 ----------------
    fk.arrow(ax, (8, TICK_Y), (430, TICK_Y), color=fk.MUTED, lw=1.0, ms=6.0)
    fk.text(ax, 430, 152, '时间（示意）', ha='right', va='top', fs=S, color=fk.MUTED)
    for x, label, col, lock in EVENTS:
        ax.plot([x, x], [TICK_Y - 4, TICK_Y + 4], color=col, lw=2.0, zorder=2)
        if lock:                                   # 两次锁存：实心点 + 加粗标签
            fk.circle(ax, x, TICK_Y, 1.7, color=col)
        fk.text(ax, x, 162, label, ha='center', va='bottom', fs=S, color=col,
                weight='bold' if lock else 'normal')

    # 卷帘读出：只在「帧写入开始 → 帧写完」之间
    x0, y0, bw, bh = BAND
    fk.rect(ax, x0, y0, bw, bh, fc=fk.C['pl'][0], ec=fk.C['pl'][1], lw=0.9)
    for x in (x0, x0 + bw):                        # 两次锁存到读出区的引导线
        ax.plot([x, x], [y0 + bh, TICK_Y - 4], color=fk.C['pl'][1], lw=0.6,
                ls=(0, (2, 2)), zorder=1)
    fk.text(ax, x0 + bw / 2, y0 + bh / 2, '卷帘读出 ≈ 16ms\n第 r 行按 r/H 插值',
            ha='center', va='center', fs=S, rect=BAND)

    # 行插值公式（左对齐在卷帘读出区下方，供 PPT 放大面板裁切）
    fk.text(ax, 40, 104, '该行带位置 B = POS0 + (POS1 − POS0) · r / H', ha='left', va='center',
            fs=fk.FS['box'])

    # ---------------- 左：零件的带坐标 ----------------
    fk.group(ax, 6, PANEL_Y, 214, PANEL_H, '零件带坐标（只算一次）', 'pl')
    fk.text(ax, 12, 68, 'x = 单应(像素)：拍摄时台面坐标', fs=S)
    fk.text(ax, 12, 53, 'B = 该行曝光时的带位置（插值）', fs=S)
    fk.text(ax, 12, 38, 's = x − B：零件带坐标，之后不变', fs=fk.FS['box'], weight='bold')
    fk.text(ax, 12, 22, '任意时刻位置 = s + B_now（读步进计数）', fs=S)

    # ---------------- 右：带来的好处 ----------------
    fk.group(ax, 228, PANEL_Y, 213, PANEL_H, '带来的好处', 'ps')
    for y, s in [(66, '• 推理 / 测量晚到也不影响位置'),
                 (52, '• 停带、变速、反转都无需处理'),
                 (38, '• 停带：步进目标改为「s 到抓取线」'),
                 (24, '• 硬件在环停带误差 ≤ 0.04mm（1~2 步）')]:
        fk.text(ax, 234, y, s, fs=S)

    return fig, ax


# PPT 放大区（pt，左下原点；放大倍数按面板宽 ÷ 裁切区宽算，裁切区越窄字放大越多）
REGIONS = {
    'latency': (4, 94, 246, 190),      # 时间轴左段：两次锁存刻度 + 卷帘读出 16ms + 行插值公式
    'formula': (4, 1, 444, 95),        # 下方两块面板（带坐标怎么算 / 带来的好处）
}


def main():
    fig, ax = draw()
    return fk.save(fig, NAME, REGIONS)


if __name__ == '__main__':
    main()
