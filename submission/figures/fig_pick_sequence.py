#!/usr/bin/env python3
"""fig_pick_sequence — 图 8 良品停带抓取与不良品导流挡板时序（三段式泳道，示意）。

画布 = 报告落版宽度 447.92pt（15.8cm）× 259pt，字面字号即纸面字号（见 figkit 顶部说明）。
自上而下三条虚线泳道，左对齐行标签，判定处分成 ② / ③ 两条支路：

① 共同处理（良品 / 不良品相同）：拍摄瞬间锁存带位置（PL）→ 定位 5.9ms/帧 →
   亚像素测量 4~5ms → NPU 8.0ms × 3~6 次软投票 → 判定；段内注明
   「位置 = 拍摄瞬间锁存的带位置，与取帧 / 推理延迟无关」，并只在此段标一次「时间 →」。
② 良品支路：写停带目标（PL 梯形减速）→ 停在抓取线 → 下探 400ms / 合爪 300ms →
   抬起 → 料盒 / 张爪 250ms → 恢复走带（只有良品停带；来不及停准 / 冲突 → 复检，不倒车）。
③ 不良品支路：碰件前合上挡板 → 零件碰件 → 按去向摆开放行（返修 0.6s、报废 2.75s）→
   不停带继续走（不良品不停带；报废件碰件后再走 50mm 才开始摆）。
底部两条皮带状态条：② 良品 走带 50mm/s → 停住（抓取线）→ 恢复走带；③ 不良品 全程走带。
判定结果经左侧分支母线分到 ② / ③ 两条泳道；支路末端汇到「带尾复检 / 完成」不画。
全图只示意先后、不按时间比例；时长取默认配置（定位 5.9ms、亚像素测量 4~5ms、NPU 8.0ms、
下探 400ms、合爪 300ms、张爪 250ms、带速 50mm/s），与 2.3.2 节（3）一致。
版式沿用 figkit 期刊线稿风：白底细边方框、灰色虚线分组框、黑色细箭头、无装饰色。
"""
import figkit as fk
from matplotlib.patches import FancyBboxPatch

NAME = 'fig_pick_sequence'

# ---- 画布与纵向布局（pt，原点左下）--------------------------------------------------------
H = 259.0
CARD_H = 32.0                      # 卡片统一高度：名称 8.5pt + 限定 7.5pt 两行

L1_Y, L1_H = 186.0, 52.0           # ① 共同处理（含位置口径一行，只在此段标时间方向）
L2_Y, L2_H = 120.0, 40.0           # ② 良品支路（只有良品停带）
L3_Y, L3_H = 60.0, 40.0            # ③ 不良品支路（不良品不停带）
C1_Y = L1_Y + 3.0 + 11.0 + 2.0     # ① 卡片行下沿（上方留位置口径一行）
C2_Y = L2_Y + 4.0
C3_Y = L3_Y + 4.0
NOTE1_Y = L1_Y + 8.5               # ① 位置口径一行（7.5pt）
FORK_Y = 181.0                     # 判定分支：横向母线高度

# ---- 横向布局 ------------------------------------------------------------------------------
X_LANE0, X_LANE1 = 20.0, 440.0     # 泳道虚线框左右边
X_IN0, X_IN1 = 30.0, 430.0         # 卡片行首 / 末（泳道内留白 10pt）
X_RAIL = 10.0                      # 判定分支母线（左留白里的一根竖线）

# ---- 底部皮带状态条 ------------------------------------------------------------------------
BELT_X0, BELT_X1 = 96.0, X_LANE1   # 状态条左右端（左侧留行名）
BELT_H = 14.0
BELT_BAD_Y, BELT_GOOD_Y = 22.0, 38.0
BELT_SPLIT = (246.0, 342.0)        # 良品条：走带 / 停住 / 恢复走带 的分界

# ---- 三条泳道的内容 ------------------------------------------------------------------------
# 每张卡片 = (第一行名称 8.5pt 粗体, 第二行限定 7.5pt 灰色)；每行 ≤ 12 汉字当量。
LANE1 = [('锁存带位置', '拍摄瞬间 · PL'),
         ('定位 5.9 ms', '1/4 降采样'),
         ('测量 4~5 ms', '亚像素'),
         ('NPU 8.0 ms', '3~6 次软投票'),
         ('判定', '良品 / 不良品')]
LANE2 = [('写停带目标', 'PL 梯形减速'),
         ('停在抓取线', '皮带停住'),
         ('下探 400 ms', '合爪 300 ms'),
         ('抬起 → 料盒', '张爪 250 ms'),
         ('恢复走带', '放料照走')]
LANE3 = [('碰件前合上挡板', '按带位置算时刻'),
         ('零件碰件', '顶到摆杆'),
         ('按去向摆开放行', '返修 0.6 s / 报废 2.75 s'),
         ('不停带继续走', '落对应料盒')]
LANE3_W = (88.0, 82.0, 102.0, 88.0)   # ③ 卡片宽按字数给（合计 360，剩余均分为 3 个箭头间隙）


def _lane(ax, x, y, w, h, label, note=None):
    """灰色虚线泳道框 + 框上方左对齐行标签；note 给同行右端的一行灰色小字。"""
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle='round,pad=0,rounding_size=3.5',
                                fc='none', ec='#7F7F7F', lw=0.7, ls=(0, (4.0, 2.4)),
                                zorder=1))
    ty = y + h + 8.0
    fk.text(ax, x, ty, label, fs=fk.FS['group'], weight='bold')
    if note:
        fk.text(ax, x + w, ty, note, fs=fk.FS['sub'], color=fk.FAINT, ha='right')


def _cards(ax, y, items, widths, gap, kinds=None):
    """一排卡片 + 相邻卡片之间的细箭头，返回每张卡片的左边 x。"""
    xs, x = [], X_IN0
    for w in widths:
        xs.append(x)
        x += w + gap
    kinds = ['ps'] * len(items) if kinds is None else kinds
    for (t, s), x, w, kind in zip(items, xs, widths, kinds):
        fk.box(ax, x, y, w, CARD_H, t + '\n' + s, kind)
    for i in range(len(items) - 1):
        fk.arrow(ax, (xs[i] + widths[i], y + CARD_H / 2.0),
                 (xs[i + 1], y + CARD_H / 2.0), ms=6.0)
    return xs


def _belt(ax):
    """底部皮带状态条：良品三段（走带 / 停住 / 恢复走带）、不良品全程走带，全用文字标出。"""
    b = fk.BELT_FILL, fk.BELT_EDGE
    x = [BELT_X0, BELT_SPLIT[0], BELT_SPLIT[1], BELT_X1]
    for i, txt in enumerate(('走带 50 mm/s', '停住 · 抓取线', '恢复走带 50 mm/s')):
        seg = (x[i], BELT_GOOD_Y, x[i + 1] - x[i], BELT_H)
        fk.rect(ax, *seg, fc=b[0] if i == 1 else fk.WHITE, ec=b[1], lw=0.7)
        fk.text(ax, x[i] + seg[2] / 2.0, BELT_GOOD_Y + BELT_H / 2.0, txt,
                fs=fk.FS['sub'], ha='center', va='center', rect=seg)
    seg = (BELT_X0, BELT_BAD_Y, BELT_X1 - BELT_X0, BELT_H)
    fk.rect(ax, *seg, fc=fk.WHITE, ec=b[1], lw=0.7)
    fk.text(ax, BELT_X0 + seg[2] / 2.0, BELT_BAD_Y + BELT_H / 2.0,
            '全程走带 50 mm/s（不停带）', fs=fk.FS['sub'], ha='center', va='center', rect=seg)
    for lab, yy in (('② 良品皮带', BELT_GOOD_Y), ('③ 不良品皮带', BELT_BAD_Y)):
        fk.text(ax, BELT_X0 - 8.0, yy + BELT_H / 2.0, lab, fs=fk.FS['sub'],
                ha='right', va='center', color=fk.MUTED)


def draw():
    fig, ax = fk.new_fig(H)

    # ---------------- ① 共同处理：锁存位置 → 定位 → 测量 → NPU 投票 → 判定 ----------------
    _lane(ax, X_LANE0, L1_Y, X_LANE1 - X_LANE0, L1_H, '① 共同处理（两路相同）')
    x1 = _cards(ax, C1_Y, LANE1, [72.0] * 5, 10.0,
                kinds=['ps', 'ps', 'ps', 'ps', 'npu'])
    fk.rect(ax, x1[4], C1_Y, 72.0, CARD_H, fc='none', ec=fk.INK, lw=1.2)   # 判定：加重边框
    fk.text(ax, X_IN0, NOTE1_Y,
            '位置 = 拍摄瞬间锁存的带位置，与取帧 / 推理延迟无关',
            fs=fk.FS['sub'], color=fk.MUTED, va='center')
    fk.text(ax, 374.0, NOTE1_Y, '时间 →', fs=fk.FS['sub'], color=fk.MUTED,
            ha='right', va='center')            # 全图只在这一段标时间方向

    # ---------------- ② 良品支路：写停带目标 → 停准 → 抓取放料 → 恢复走带 ----------------
    _lane(ax, X_LANE0, L2_Y, X_LANE1 - X_LANE0, L2_H,
          '② 良品支路（只有良品停带）', note='来不及停准 / 冲突 → 复检，不倒车')
    _cards(ax, C2_Y, LANE2, [72.0] * 5, 10.0)

    # ---------------- ③ 不良品支路：合挡板 → 碰件 → 摆开放行 → 继续走 ----------------
    gap3 = (X_IN1 - X_IN0 - sum(LANE3_W)) / (len(LANE3) - 1)
    _lane(ax, X_LANE0, L3_Y, X_LANE1 - X_LANE0, L3_H,
          '③ 不良品支路（不停带，挡板导流）', note='报废件碰件后再走 50 mm 才开始摆')
    _cards(ax, C3_Y, LANE3, LANE3_W, gap3)

    # ---------------- 判定 →（左侧母线）→ ② / ③ 两条支路 ----------------
    jx = x1[4] + 36.0                                  # 判定卡片中心
    y2c, y3c = C2_Y + CARD_H / 2.0, C3_Y + CARD_H / 2.0
    ax.plot([jx, jx, X_RAIL, X_RAIL], [C1_Y, FORK_Y, FORK_Y, y3c],
            color=fk.ARROW, lw=0.9, solid_joinstyle='miter', solid_capstyle='butt', zorder=2)
    fk.arrow(ax, (X_RAIL, y2c), (X_IN0, y2c), color=fk.ARROW, lw=0.9, ms=6.0)
    fk.arrow(ax, (X_RAIL, y3c), (X_IN0, y3c), color=fk.ARROW, lw=0.9, ms=6.0)

    # ---------------- 底部：两条皮带状态条 ----------------
    _belt(ax)

    # ---------------- 全图注记（细节不进图）----------------
    fk.text(ax, X_LANE0, 11.5,
            '示意：时长取默认配置，不按时间比例；良品一次抓取 4.26 s。',
            fs=fk.FS['sub'], color=fk.MUTED, va='center')

    return fig, ax


def main():
    fig, ax = draw()
    return fk.save(fig, NAME)


if __name__ == '__main__':
    main()
