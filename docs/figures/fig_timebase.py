#!/usr/bin/env python3
"""fig_timebase — 图 2-2 时间基准：拍摄瞬间锁存带位置，零件坐标与处理延迟无关。

画布 = 报告落版宽度 447.9pt（15.8cm）× 195pt，字面字号即纸面字号（见 figkit 顶部说明）。
上：时间轴（只示意先后、不按比例）+ 两次锁存（POS0 / POS1）+ 卷帘读出卡 + 行插值公式，
轴端注明「示意」（各段从 16ms 到数百 ms，不按比例）；
下：左「零件的带坐标」、右「带来的好处」两块面板。
数值取自技术文档 §2.3 与旧图脚本：卷帘读出 ≈16ms、定位 / 测量 ~6ms、NPU 7.8ms、
停带误差 ≤0.04mm。版式：事件用小色标刻度（轴为中性灰），文字两级（名称墨色粗体、限定次级灰）。
"""
import figkit as fk

NAME = 'fig_timebase'
H = 195.0

AXIS_Y = 158.0                          # 时间轴
TICK = 4.0                              # 刻度半高
NAME_Y, SUB_Y = 182.0, 169.5            # 事件名 / 限定的基线（在轴上方）
EVENTS = [
    (44.0, '帧写入开始', 'PL 锁存 POS0', 'pl', True),
    (140.0, '帧写完', 'PL 锁存 POS1', 'pl', True),
    (216.0, 'PS 取到帧', '定位 · 测量 ~6ms', 'ps', False),
    (294.0, 'NPU 结果', '7.8ms · 异步', 'npu', False),
    (380.0, '判定 → 停带 / 挡板', '数十~数百 ms 后', 'acc', False),
]
BAND = (44.0, 125.0, 96.0, 26.0)        # 卷帘读出卡：x0, y0, w, h（正落在两次锁存之间）
FORMULA_Y = 113.0
PANEL_Y, PANEL_H = 3.0, 98.0


def draw():
    fig, ax = fk.new_fig(H)
    S, B = fk.FS['sub'], fk.FS['box']

    # ---------------- 时间轴（示意） ----------------
    fk.arrow(ax, (10, AXIS_Y), (436, AXIS_Y), color=fk.ARROW, lw=0.9, ms=6.0)
    fk.text(ax, 436, 148, '时间（示意）', ha='right', va='center', fs=S, color=fk.MUTED)
    for x, name, sub, kind, lock in EVENTS:
        accent = fk.C[kind][1]
        fk.tick(ax, x, AXIS_Y - TICK, AXIS_Y + TICK, color=accent, lw=1.3)
        if lock:                                   # 两次锁存：实心点
            fk.circle(ax, x, AXIS_Y, 1.7, color=accent)
        fk.text(ax, x, NAME_Y, name, ha='center', va='center', fs=B, color=fk.INK,
                weight='bold')
        fk.text(ax, x, SUB_Y, sub, ha='center', va='center', fs=S, color=fk.MUTED)

    # 卷帘读出：两次锁存之间逐行插值（≈16ms）
    fk.box(ax, *BAND, '卷帘读出 ≈ 16ms\n第 r 行按 r / H 插值', 'pl')
    fk.text(ax, 44, FORMULA_Y, '该行带位置 B = POS0 + (POS1 − POS0) · r / H',
            ha='left', va='center', fs=B, color=fk.INK)

    # ---------------- 左下：零件的带坐标 ----------------
    fk.group(ax, 6, PANEL_Y, 214, PANEL_H, '零件的带坐标', 'pl', note='只算一次')
    fk.text(ax, 14, 64, 'x = 单应(像素)：拍摄时台面坐标', fs=B, color=fk.BODY)
    fk.text(ax, 14, 50, 'B = 该行曝光时的带位置（插值）', fs=B, color=fk.BODY)
    fk.text(ax, 14, 36, 's = x − B：零件带坐标，之后不变', fs=B, color=fk.INK, weight='bold')
    fk.text(ax, 14, 21, '任意时刻位置 = s + B_now（读步进计数）', fs=S, color=fk.MUTED)

    # ---------------- 右下：带来的好处 ----------------
    fk.group(ax, 228, PANEL_Y, 214, PANEL_H, '带来的好处', 'ps')
    for y, s in [(64, '推理 / 测量晚到也不影响位置'),
                 (50, '停带、变速、反转都无需处理'),
                 (36, '停带：步进目标改为「s 到抓取线」'),
                 (21, '硬件在环停带误差 ≤ 0.04mm（1~2 步）')]:
        fk.text(ax, 234, y, '·', fs=S, color=fk.C['ps'][1], weight='bold')
        fk.text(ax, 241, y, s, fs=S, color=fk.MUTED)

    return fig, ax


# PPT 重点框（pt，左下原点）：两次锁存 + 卷帘读出 + 行插值公式 / 下方两块说明面板
REGIONS = {
    'latency': (4, 100, 242, 192),
    'formula': (4, 1, 444, 102),
}


def main():
    fig, ax = draw()
    return fk.save(fig, NAME, REGIONS)


if __name__ == '__main__':
    main()
