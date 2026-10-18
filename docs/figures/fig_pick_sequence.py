#!/usr/bin/env python3
"""fig_pick_sequence — 图 4-1 良品停带抓取与不良品挡板分流的时序（示意）。

画布 = 报告落版宽度 447.9pt（15.8cm）× 212pt，字面字号即纸面字号（见 figkit 顶部说明）。
一条时间轴、按线程分行（相机 / 定位·跟踪 / 测量 / NPU / 判定 / 挡板 / 皮带 / 机械臂）：
左边良品：定位 → 判定 → 写停带目标（只为良品停）→ 机械臂抓取；右边不良件：不停带，
挡板按带位置提前 110mm 合上、过后 190mm 打开。时间只示意先后、不按比例；
时长取 des_app.json 默认值（定位 5.9ms、亚像素测量 4~5ms、NPU 7.8ms、下探 400ms /
合爪 300ms / 张爪 250ms），挡板 110 / 190mm 与带速 50mm/s 与技术文档 §3.5 / §4.4 一致。
"""
import figkit as fk

NAME = 'fig_pick_sequence'
H = 212.0

# 时间轴横向范围与各线程的纵坐标（pt，左下原点）
X0, X1 = 58.0, 444.0
Y = dict(cam=200.0, track=177.0, measure=154.0, npu=131.0, judge=108.0, flap=77.0,
         belt=52.0, arm=30.0)
RUN_Y, STOP_Y = 55.0, 51.0            # 皮带：走带 / 停住
X_STOP, X_GO = 254.0, 320.0           # 停带点 / 恢复走带


def draw():
    fig, ax = fk.new_fig(H)
    B, S = fk.FS['box'], fk.FS['sub']
    INK, MUTED = fk.INK, fk.MUTED
    pl, ps, npu, acc = fk.C['pl'][1], fk.C['ps'][1], fk.C['npu'][1], fk.C['acc'][1]

    # ---------------- 行参考线与行名 ----------------
    for key, label in [('track', '定位 / 跟踪'), ('measure', '测量'), ('npu', 'NPU 分类'),
                       ('judge', '判定'), ('flap', '挡板'), ('arm', '机械臂')]:
        ax.plot([X0, X1], [Y[key], Y[key]], color='#ececec', lw=0.8, zorder=0)
        fk.text(ax, 52, Y[key], label, ha='right', va='center', fs=B, color=INK)
    fk.text(ax, 52, Y['cam'], '相机 60fps', ha='right', va='center', fs=B, color=INK)
    fk.text(ax, 52, Y['belt'], '皮带', ha='right', va='center', fs=B, color=INK)
    fk.text(ax, X1, Y['cam'], '时间 →', ha='right', va='center', fs=S, color=MUTED)

    # 相机：60fps 连续出帧（刻度间距只是示意；右端给「时间 →」让位）
    for x in range(int(X0), 404, 8):
        ax.plot([x, x], [Y['cam'] - 5, Y['cam'] + 5], color=pl, lw=0.8, zorder=1)

    # ---------------- 良品支路：定位 → 判定 → 停带 → 抓取 ----------------
    fk.box(ax, 62, Y['track'] - 9.5, 124, 19, '每帧定位 5.9ms', 'ps', fs=B)
    fk.box(ax, 88, Y['measure'] - 9.5, 88, 19, '亚像素测量 4~5ms', 'ps', fs=B)
    for x in (62, 106, 150):
        fk.box(ax, x, Y['npu'] - 9.5, 38, 19, '7.8ms', 'npu', fs=B)
    fk.text(ax, 194, Y['npu'], '3~6 次软投票', fs=S, color=npu, bg=True)
    fk.box(ax, 186, Y['judge'] - 9.5, 74, 19, '良品 → 停带抓取', 'ps', fs=B)

    # 判定 → 写停带目标（PL 梯形减速，停在抓取线）
    fk.poly(ax, [(223, Y['judge'] - 9.5), (223, RUN_Y + 3), (X_STOP - 4, RUN_Y + 0.8)], color=ps, lw=1.2)
    fk.text(ax, 217, Y['flap'], '写停带目标\n梯形减速停准', ha='right', va='center', fs=S, color=ps, bg=True)

    # ---------------- 皮带：走带 → 停住（只为良品）→ 恢复走带 ----------------
    ax.plot([X0, X_STOP, X_STOP, X_GO, X_GO, X1],
            [RUN_Y, RUN_Y, STOP_Y, STOP_Y, RUN_Y, RUN_Y], color=ps, lw=1.8, zorder=1)
    fk.text(ax, 64, RUN_Y + 2.5, '50 mm/s', fs=S, color=ps, bg=True)
    fk.text(ax, X_STOP + 6, RUN_Y + 2.5, '停在抓取线', fs=S, color=ps, bg=True)
    fk.text(ax, X_GO - 12, RUN_Y + 2.5, '恢复走带 · 放料照走', fs=S, color=ps, bg=True)

    # ---------------- 机械臂：下探合爪 → 抬起放料（皮带照走） ----------------
    fk.box(ax, 252, Y['arm'] - 14, 56, 28, '下探 400ms\n合爪 300ms', 'ext', fs=B)
    fk.box(ax, 316, Y['arm'] - 14, 57, 28, '抬起 → 料盒\n张爪 250ms', 'ext', fs=B)

    # ---------------- 不良件支路：判定不停带，挡板按带位置合上 / 打开 ----------------
    fk.box(ax, 378, Y['judge'] - 9.5, 66, 19, '不良件 → 挡板', 'acc', fs=B)
    fk.box(ax, 378, Y['flap'] - 12, 66, 24, '提前 110mm 合上\n过后 190mm 打开', 'acc', fs=S)
    fk.arrow(ax, (411, Y['judge'] - 9.5), (411, Y['flap'] + 12), color=acc, lw=1.1, ms=5.0)

    # ---------------- 图注（正文细节不进图） ----------------
    fk.text(ax, 8, 7, '位置按拍摄瞬间锁存的带位置算（图 2-2）；不良件不停带；'
                     '来不及停准 / 冲突 → 复检，不倒车。', fs=S, color=MUTED)

    return fig, ax


# PPT 放大区（pt，左下原点；放大倍数按面板宽 ÷ 裁切区宽算）
REGIONS = {
    'good': (56, 14, 377, 207),        # 定位 → 判定 → 停带 → 写停带目标 → 下探 / 合爪 / 抬起放料
    'ng': (372, 60, 446, 126),         # 不良件判定 → 挡板按带位置合上 / 打开
}


def main():
    fig, ax = draw()
    return fk.save(fig, NAME, REGIONS)


if __name__ == '__main__':
    main()
