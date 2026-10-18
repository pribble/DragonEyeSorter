#!/usr/bin/env python3
"""fig_layout — 图 7-1 工位布局（俯视，按比例）。

画布 = 报告落版宽度 447.9pt（15.8cm）× 280pt，字面字号即纸面字号（见 figkit 顶部说明）。
比例尺 0.5pt/mm：皮带 800×300mm → 400×150pt，相机视野 398×224mm → 199×112pt，尺寸与
`docs/技术文档.md` §2.1 / §7 一致。零件从右往左走，机械臂在行程的右手侧（图上方），
龙门与灯在另一侧；抓取线 = 视野下游边 + 80mm，挡板 1 报废 / 挡板 2 返修在抓取线下游。
"""
import figkit as fk

NAME = 'fig_layout'
H = 280.0
K = 0.5                                # pt / mm

BX0, BW = 44.0, 400.0                  # 皮带 800mm
BY0, BH = 62.0, 150.0                  # 皮带 300mm
CY = BY0 + BH / 2.0
FOV_W, FOV_H = 398 * K, 224 * K        # 相机视野 398 × 224mm
FOV_R = BX0 + BW - 60 * K              # 视野上游边距上料端 60mm
FOV_L = FOV_R - FOV_W
FOV_B, FOV_T = CY - FOV_H / 2.0, CY + FOV_H / 2.0
PICK = FOV_L - 80 * K                  # 抓取线 = 视野下游边 + 80mm
ARM_X0, ARM_W = 146.0, 106.0           # 机械臂（跨在抓取线上方）
BIN_X0, BIN_W = 40.0, 104.0            # 良品料盒
FLAP1, FLAP2 = 124.0, 80.0             # 挡板 1（报废）/ 挡板 2（返修）
BOX1, BOX2 = (96.0, 28.0, 56.0, 22.0), (36.0, 28.0, 56.0, 22.0)   # 侧边料盒（皮带外下方）


def draw():
    fig, ax = fk.new_fig(H)
    S, B = fk.FS['sub'], fk.FS['box']

    # ---------------- 皮带（俯视，按比例） ----------------
    belt = fk.rect(ax, BX0, BY0, BW, BH, fc='#f4f4f4', ec='#555555', lw=1.2)
    for x in range(int(BX0) + 22, int(BX0 + BW), 20):
        ln, = ax.plot([x, x - 16], [BY0 + 2, BY0 + BH - 2], color='#ededed', lw=0.7, zorder=0.5)
        ln.set_clip_path(belt)          # 斜纹不许越出皮带边（首个斜纹曾甩到皮带左外侧）
    fk.text(ax, 48, 196, '皮带 800 × 300mm', fs=S, color=fk.MUTED)

    # ---------------- 相机视野 + 定位 / 测量 ----------------
    fk.rect(ax, FOV_L, FOV_B, FOV_W, FOV_H, fc=fk.C['pl'][0], ec=fk.C['pl'][1], lw=1.4)
    cx = (FOV_L + FOV_R) / 2.0
    fk.text(ax, cx, CY + 19, '相机视野 398 × 224mm', fs=B, color=fk.C['pl'][1], weight='bold',
            ha='center')
    fk.text(ax, cx, CY + 7, '4.2mm 镜头 · 离带面约 300mm', fs=S, ha='center')
    fk.text(ax, cx, CY - 4, '0.21mm/像素 · 定位亚像素测量', fs=S, ha='center')
    fk.text(ax, cx, CY - 15, 'NPU 分类 3~6 次软投票 → 判定', fs=S, ha='center')
    # 台面原点在视野上游边中点，+x = 走料方向（向左），+y = 机械臂的另一侧
    fk.circle(ax, FOV_R, CY, 1.3, fk.C['pl'][1])
    fk.arrow(ax, (FOV_R, CY), (FOV_R - 17, CY), color=fk.C['pl'][1], lw=1.3, ms=6.0)
    fk.arrow(ax, (FOV_R, CY), (FOV_R, CY - 17), color=fk.C['pl'][1], lw=1.3, ms=6.0)
    fk.text(ax, FOV_R - 10, CY - 8, '+x', fs=S, ha='right', color=fk.C['pl'][1])
    fk.text(ax, FOV_R + 4, CY - 19, '+y', fs=S, color=fk.C['pl'][1])
    fk.text(ax, FOV_R + 4, CY + 8, '原点', fs=S, color=fk.C['pl'][1])
    fk.text(ax, BX0 + BW - 18, BY0 + BH + 12, '上料', fs=B, ha='center', color=fk.MUTED,
            weight='bold')
    fk.arrow(ax, (BX0 + BW, FOV_T + 4), (FOV_R, FOV_T + 4), both=True, color=fk.MUTED, ms=4.5)
    fk.text(ax, (BX0 + BW + FOV_R) / 2.0, FOV_T + 11, '60mm', fs=S, ha='center', color=fk.MUTED)

    # ---------------- 抓取线 + 机械臂 ----------------
    ax.plot([PICK, PICK], [BY0, BY0 + BH], color=fk.C['ps'][1], lw=1.6, ls=(0, (4, 2.5)),
            zorder=1.5)
    fk.text(ax, PICK + 4, FOV_T + 13, '抓取线 = 视野下游边 + 80mm', fs=S, color=fk.C['ps'][1])
    fk.box(ax, ARM_X0, 234, ARM_W, 36, '6 轴机械臂（侧装）\n不入镜 · 工作半径 ~300mm\n'
           'SYNC WRITE 一帧下发 6 轴', 'ps', fs=S)
    fk.arrow(ax, (PICK, 234), (PICK, BY0 + BH), color=fk.C['ps'][1], lw=1.4, ms=6.0)
    fk.text(ax, PICK + 5, 224, '良品停带抓取', fs=S, color=fk.C['ps'][1])
    fk.box(ax, BIN_X0, 234, BIN_W, 36, '料盒：齿轮 / 齿条\n按规格 · 颜色分选', 'ps', fs=S)

    # ---------------- 挡板分流（抓取线下游）+ 带尾复检 ----------------
    for x, name in ((FLAP1, '挡板 1'), (FLAP2, '挡板 2')):
        ax.plot([x - 8, x + 8], [BY0 + 20, BY0 + 106], color=fk.C['acc'][1], lw=2.0, zorder=1.5)
        fk.text(ax, x, BY0 + 116, name, fs=S, ha='center', color=fk.C['acc'][1])
        fk.arrow(ax, (x, BY0 + 14), (x, 52), color=fk.C['acc'][1], lw=1.2, ms=5.5)
    fk.box(ax, *BOX1, '料盒：报废', 'acc', fs=S)
    fk.box(ax, *BOX2, '料盒：返修', 'acc', fs=S)
    fk.arrow(ax, (300, BY0 + 11), (56, BY0 + 11), color=fk.C['acc'][1], lw=1.4, ms=6.0,
             ls=(0, (5, 3)))
    fk.text(ax, 250, BY0 + 11, 'NG 不停带分流', fs=S, ha='center', color=fk.C['acc'][1], bg=True)
    fk.box(ax, 2, 92, 40, 36, '带尾\n复检箱\n失效安全', 'ext', fs=S)

    # ---------------- 灯、龙门、走料方向 ----------------
    for y in (FOV_T + 2, BY0 - 6):
        fk.rect(ax, FOV_L, y, FOV_W, 4, fc='#fff3b0', ec='#b59b00', lw=0.8)
    fk.text(ax, 240, 226, '条形光 ×2（两侧 · 离带面 80~120mm）', fs=S, color='#8a7400')
    fk.box(ax, 186, 8, 160, 36, '龙门支架：板卡 + 相机\n（机械臂的另一侧）', 'ext', fs=S)
    fk.arrow(ax, (436, 26), (356, 26), color=fk.C['acc'][1], lw=1.8, ms=7.0)
    fk.text(ax, 396, 34, '走料方向', fs=S, ha='center', color=fk.C['acc'][1])

    return fig, ax


REGIONS = {
    # PPT 放大区（pt，左下原点）
    'view': (215, 82, 438, 198),           # 相机视野框（FOV 215~414 / 81~193）+ 原点标注；
                                           # 收紧到这四边，避开顶部「下游边 + 80mm」半行、
                                           # 右侧 60mm 标注与底部红字（放 PPT 面板时不带碎片）
    'pick': (138, 124, 292, 276),          # 抓取线 + 机械臂
}


def main():
    fig, ax = draw()
    return fk.save(fig, NAME, REGIONS)


if __name__ == '__main__':
    main()
