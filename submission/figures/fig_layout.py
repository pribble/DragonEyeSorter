#!/usr/bin/env python3
"""fig_layout — 图 4 工位布局（俯视、按比例）。

画布 = 报告落版宽度 447.9pt（15.8cm）× 280pt，字面字号即纸面字号（见 figkit 顶部说明）。
比例尺 0.5pt/mm：皮带 800×300mm → 400×150pt，相机视野 373×214mm → 186.5×107pt，尺寸与
`submission/技术文档.md` §II-A / §VII-A 一致（相机定版实测：373×214mm、0.1945 mm/像素）。零件从
右往左走，机械臂在行程的右手侧（图上方），龙门与灯在另一侧；抓取线 = 视野下游边 + 65mm，
带尾侧一块导流挡板接报废与返修（两种放行曲线、两只料盒），带尾为复检箱。
"""
import figkit as fk

NAME = 'fig_layout'
H = 280.0
K = 0.5                                # pt / mm

BX0, BW = 44.0, 400.0                  # 皮带 800mm
BY0, BH = 62.0, 150.0                  # 皮带 300mm
CY = BY0 + BH / 2.0
FOV_W, FOV_H = 373 * K, 214 * K        # 相机视野 373 × 214mm（相机定版实测）
FOV_R = BX0 + BW - 60 * K              # 视野上游边距上料端 60mm
FOV_L = FOV_R - FOV_W
FOV_B, FOV_T = CY - FOV_H / 2.0, CY + FOV_H / 2.0
PICK = FOV_L - 65 * K                  # 抓取线 = 视野下游边 + 65mm（正文 §VII-A）
ARM_X0, ARM_W = 146.0, 106.0           # 机械臂（跨在抓取线上方）
BIN_X0, BIN_W = 40.0, 104.0            # 良品料盒
# 导流挡板（11-01 实装，des_app.json flaps.gates[0]）：带尾侧一块，报废与返修共用。台面坐标 (mm)：转轴在机械臂
# 对侧带边外 (557, −178)，摆杆 321mm、合上与带边成 42.3°，末端约 (322, +36)；+y 指向机械臂一侧（图上方）
PIV_X, PIV_Y = FOV_R - 557 * K, CY - 178 * K     # 转轴
TIP_X, TIP_Y = FOV_R - 322 * K, CY + 36 * K      # 合上时的摆杆末端
BOX_RW = (198.0, 39.0, 50.0, 15.0)               # 带边外两只料盒：报废落点更靠带尾（左），返修更靠上料端（右）
BOX_RK = (250.0, 39.0, 50.0, 15.0)               # 示意位置（料盒没有实测坐标），压在灯条与龙门框之间


def draw():
    fig, ax = fk.new_fig(H)
    S, B = fk.FS['sub'], fk.FS['box']
    teal, blue = fk.C['pl'][1], fk.C['ps'][1]
    red, amber = fk.C['acc'][1], fk.C['npu'][1]

    # ---------------- 皮带（俯视，按比例） ----------------
    belt = fk.rect(ax, BX0, BY0, BW, BH, fc=fk.BELT_FILL, ec=fk.BELT_EDGE, lw=1.0)
    belt.set_zorder(1.0)
    for x in range(int(BX0) + 22, int(BX0 + BW), 20):
        ln, = ax.plot([x, x - 16], [BY0 + 2, BY0 + BH - 2], color=fk.BELT_HATCH, lw=0.6,
                      zorder=1.2)
        ln.set_clip_path(belt)          # 斜纹不许越出皮带边
    fk.text(ax, BX0 + 4, BY0 + BH + 6, '皮带 800 × 300mm', fs=S, color=fk.MUTED)

    # ---------------- 相机视野 + 定位 / 测量 ----------------
    fk.rect(ax, FOV_L, FOV_B, FOV_W, FOV_H, fc=fk.C['pl'][0], ec=teal, lw=0.9, alpha=1.0)
    cx = (FOV_L + FOV_R) / 2.0
    fk.text(ax, cx, CY + 20, '相机视野 373 × 214mm', fs=B, color=teal, weight='bold', ha='center')
    fk.text(ax, cx, CY + 8, '4.2mm 镜头 · 离带面约 300mm', fs=S, ha='center', color=fk.BODY)
    fk.text(ax, cx, CY - 3, '0.1945 mm/像素 · 定位亚像素测量', fs=S, ha='center', color=fk.BODY)
    fk.text(ax, cx, CY - 14, 'NPU 分类 3~6 次软投票 → 判定', fs=S, ha='center', color=fk.BODY)
    # 台面原点在视野上游边中点，+x = 走料方向（向左），+y = 机械臂一侧（手眼标定：底座在 y = +142mm）
    fk.circle(ax, FOV_R, CY, 1.3, teal)
    fk.arrow(ax, (FOV_R, CY), (FOV_R - 17, CY), color=teal, lw=1.0, ms=6.0)
    fk.arrow(ax, (FOV_R, CY), (FOV_R, CY + 17), color=teal, lw=1.0, ms=6.0)
    fk.text(ax, FOV_R - 10, CY - 8, '+x', fs=S, ha='right', color=teal)
    fk.text(ax, FOV_R + 4, CY + 17, '+y', fs=S, color=teal)
    fk.text(ax, FOV_R + 4, CY - 6, '原点', fs=S, color=teal)
    fk.text(ax, BX0 + BW - 16, BY0 + BH + 6, '上料', fs=B, ha='center', color=fk.MUTED,
            weight='bold')
    fk.arrow(ax, (BX0 + BW, FOV_T + 4), (FOV_R, FOV_T + 4), both=True, color=fk.MUTED, ms=5.0)
    fk.text(ax, (BX0 + BW + FOV_R) / 2.0, FOV_T + 12, '60mm', fs=S, ha='center', color=fk.MUTED)

    # ---------------- 抓取线 + 机械臂 ----------------
    ax.plot([PICK, PICK], [BY0, BY0 + BH], color=blue, lw=1.2, ls=(0, (4, 2.5)), zorder=1.5)
    fk.text(ax, PICK + 4, FOV_T + 13, '抓取线 = 视野下游边 +65mm', fs=S, color=blue)
    fk.box(ax, ARM_X0, 234, ARM_W, 36, '6 轴机械臂（侧装）\n不入镜 · 工作半径 ~300mm\n'
           'SYNC WRITE 一帧下发 6 轴', 'ps')
    fk.arrow(ax, (PICK, 234), (PICK, BY0 + BH), color=blue, lw=1.0, ms=6.5)
    fk.text(ax, PICK - 5, 224, '良品停带抓取', fs=S, color=blue, ha='right')
    fk.box(ax, BIN_X0, 234, BIN_W, 36, '料盒：齿轮\n按规格 · 颜色分选', 'ps')

    # ---------------- 挡板分流（带尾侧一块，报废 / 返修共用）+ 带尾复检 ----------------
    # 合位摆杆：从带边外的转轴斜伸进皮带；碰件后按去向的放行曲线摆开，把零件推出对侧带边
    ax.plot([PIV_X, TIP_X], [PIV_Y, TIP_Y], color=red, lw=1.8, zorder=1.6, solid_capstyle='round')
    fk.circle(ax, PIV_X, PIV_Y, 1.6, red)
    fk.text(ax, PIV_X - 4, PIV_Y - 2, '挡板（报废 + 返修）', fs=S, ha='right', color=red)
    fk.text(ax, 215, BY0 + 12, '碰件后摆开扫出', fs=S, color=red)
    fk.box(ax, *BOX_RW, '料盒：报废', 'acc')
    fk.box(ax, *BOX_RK, '料盒：返修', 'acc')
    for bx in (BOX_RW, BOX_RK):   # 带边 → 料盒
        fk.arrow(ax, (bx[0] + bx[2] / 2.0, BY0 + 1), (bx[0] + bx[2] / 2.0, bx[1] + bx[3]), color=red,
                 lw=0.9, ms=5.0)
    fk.box(ax, 2, 92, 40, 34, '带尾\n复检箱', 'ext')
    fk.arrow(ax, (56, 109), (43, 109), ms=5.0)

    # ---------------- 灯、龙门、走料方向 ----------------
    for y in (FOV_T + 2, BY0 - 6):
        fk.rect(ax, FOV_L, y, FOV_W, 3.5, fc=fk.LIGHT_FILL, ec=fk.LIGHT_EDGE, lw=0.7)
    fk.text(ax, 240, 226, '条形光 ×2（两侧 · 离带面 80~120mm）', fs=S, color=amber)
    fk.box(ax, 186, 4, 160, 32, '龙门支架：板卡 + 相机\n（机械臂的另一侧）', 'ext')
    fk.arrow(ax, (436, 26), (356, 26), color=fk.MUTED, lw=1.2, ms=6.5)
    fk.text(ax, 396, 34, '走料方向', fs=S, ha='center', color=fk.MUTED)

    return fig, ax


def main():
    fig, ax = draw()
    return fk.save(fig, NAME)


if __name__ == '__main__':
    main()
