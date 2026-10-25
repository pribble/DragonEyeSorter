#!/usr/bin/env python3
"""fig_architecture — 图 2-1 系统架构（PL 硬实时 + PS 调度 + NPU 推理）。

画布 = 报告落版宽度 447.9pt（15.8cm）× 300pt，字面字号即纸面字号（见 figkit 顶部说明）。
版式：四列（外设 / PL / PS / 外设）+ 行对齐；卡片只写「名称 + 极短限定」，细节写正文
（§2.1 / §2.4 / §3、§4.1）。域识别靠卡片左侧色条与分组标题，不靠高饱和填充。
"""
import figkit as fk

NAME = 'fig_architecture'
H = 300.0

EXT_L, EXT_W = 4.0, 54.0            # 左外设
PL_X, PL_W = 64.0, 188.0            # PL
PS_X, PS_W = 258.0, 134.0           # PS
EXT_R, EXT_RW = 398.0, 46.0         # 右外设

PL_L, PL_LW = 68.0, 84.0            # PL 左栏
PL_R, PL_RW = 156.0, 92.0           # PL 右栏
PL_FULL = (68.0, 180.0)

PS_L, PS_LW = 262.0, 60.0           # PS 左栏
PS_R, PS_RW = 326.0, 62.0           # PS 右栏
PS_FULL = (262.0, 126.0)

ROW = dict(cam=(226.0, 36.0), r2=(184.0, 34.0), r3=(140.0, 36.0), r4=(96.0, 36.0),
           r5=(52.0, 36.0), estop=(10.0, 36.0))

C_Y = dict(cam=244, r2=201, r3=158, r4=114, r5=70, estop=28)


def draw():
    fig, ax = fk.new_fig(H)

    fk.group(ax, EXT_L, 6, EXT_W, 282, '外设', 'ext')
    fk.group(ax, PL_X, 6, PL_W, 282, 'PL · 硬实时', 'pl', note='脉冲 / 总线帧 / IO 采样')
    fk.group(ax, PS_X, 6, PS_W, 282, 'PS · Linux', 'ps', note='des_app')
    fk.group(ax, EXT_R, 6, EXT_RW, 282, '外设', 'ext')

    # ---------------- 左外设 ----------------
    lx, lw = EXT_L + 4, EXT_W - 8
    for key, text in (
            ('cam', '相机\nIMX415\n4 lane'),
            ('r2', '显示器\n1080p60'),
            ('r3', '机械臂\n总线舵机'),
            ('r4', '传送带\n57 步进'),
            ('r5', '侧挡板\nMG996R'),
            ('estop', '急停\n常闭切动力\n常开报 PL')):
        y, h = ROW[key]
        fk.box(ax, lx, y, lw, h, text, 'acc' if key == 'estop' else 'ext')

    # ---------------- PL ----------------
    y, h = ROW['cam']
    fk.box(ax, PL_L, y, PL_FULL[1], h, '相机通路 MIPI RX + ISP + 预处理\n'
           '4 lane · 1080p60 · 去马赛克 / 白平衡\n灰度 / Sobel / 二值 · ROI 裁剪', 'pl')

    y, h = ROW['r2']
    fk.box(ax, PL_L, y, PL_LW, h, '显示输出\nHDMI TX', 'pl')
    fk.box(ax, PL_R, y, PL_RW, h, '寄存器窗口\n0x8030_1000\nAXI · 188ns', 'pl')

    y, h = ROW['r3']
    fk.box(ax, PL_L, y, PL_LW, h, '舵机总线\nservo_bus\nSYNC · 超时', 'pl')
    fk.box(ax, PL_R, y, PL_RW, h, '拍摄锁存\n帧号 · 带位置 POS\nFREE_CNT 计数', 'pl')

    y, h = ROW['r4']
    fk.box(ax, PL_L, y, PL_LW, h, '步进控制\nstepper_ctrl\n首脉冲 10.6µs', 'pl')
    fk.box(ax, PL_R, y, PL_RW, h, 'NPU\n硬核 512 MAC\nArmNN · UINT8', 'npu')

    y, h = ROW['r5']
    fk.box(ax, PL_L, y, PL_LW, h, '挡板 PWM\ndes_pwm · 2 路 50Hz\n报废 / 返修分流', 'pl')
    fk.box(ax, PL_R, y, PL_RW, h, '现场输入\ndes_io · 急停对射\n限位 · 编码器', 'pl')

    y, h = ROW['estop']
    fk.box(ax, PL_L, y, PL_FULL[1], h, '急停锁存（不经 PS）\n'
           'ESTOP_IN 立即停脉冲 · 锁存 HALTED', 'acc')

    # ---------------- PS ----------------
    y, h = ROW['cam']
    fk.box(ax, PS_L, y, PS_FULL[1], h, '核 0 · 界面与显示\n'
           '取帧 → 叠加 → 贴界面 · 60fps\n总览 / 曲线 / 参数 · 记录', 'ps')
    y, h = ROW['r2']
    fk.box(ax, PS_L, y, PS_FULL[1], h, '核 1 · 定位与跟踪\n'
           's = x − B → 停带 / 挡板调度', 'ps')

    y, h = ROW['r3']
    fk.box(ax, PS_L, y, PS_LW, h, 'NPU 线程\nROI 224²\n7.9ms / 件', 'npu')
    fk.box(ax, PS_R, y, PS_RW, h, 'CAN 状态机\n10Hz 状态\n每件结果', 'ps')

    y, h = ROW['r4']
    fk.box(ax, PS_L, y, PS_LW, h, '机械臂线程\n逆解 → 航点\nSYNC WRITE', 'ps')
    fk.box(ax, PS_R, y, PS_RW, h, '存储线程\n每件 CSV\n不良件图', 'ps')

    fk.box(ax, PS_L, 52, PS_FULL[1], 36, '标定与工具（PS 侧）\n'
           'calib-cam · belt · arm\nmeasure · collect · soak', 'ps')
    fk.text(ax, PS_L + 4, 26, 'PC 端另有 PLC 模拟器与出数工具\n'
            '（盘外辅助，不占板端资源）', fs=fk.FS['sub'], color=fk.FAINT)

    # ---------------- 右外设 ----------------
    rx, rw = EXT_R + 3, EXT_RW - 6
    for key, text in (('cam', 'USB\n键鼠'), ('r3', 'PC PLC\nUSB-CAN\n500k'),
                      ('r4', 'SD 卡\nCSV\n不良件图')):
        y, h = ROW[key]
        fk.box(ax, rx, y, rw, h, text, 'ext')

    # ---------------- 连线 ----------------
    c = C_Y
    fk.arrow(ax, (EXT_L + EXT_W, c['cam']), (PL_L - 2, c['cam']), ms=6.0)
    fk.arrow(ax, (PL_L - 2, c['r2']), (EXT_L + EXT_W, c['r2']), ms=6.0)
    fk.arrow(ax, (EXT_L + EXT_W, c['r3']), (PL_L - 2, c['r3']), both=True, ms=5.5)
    fk.arrow(ax, (EXT_L + EXT_W, c['r4']), (PL_L - 2, c['r4']), both=True, ms=5.5)
    fk.arrow(ax, (PL_L - 2, c['r5']), (EXT_L + EXT_W, c['r5']), ms=6.0)
    fk.arrow(ax, (EXT_L + EXT_W, c['estop']), (PL_L - 2, c['estop']), color=fk.C['acc'][1],
             ms=6.0)

    fk.arrow(ax, (PL_X + PL_W, c['r2']), (PS_L - 2, c['r2']), 'AXI', both=True,
             fs=fk.FS['sub'], off=(0, 2.2), ms=6.0)
    fk.arrow(ax, (PL_X + PL_W, c['r3']), (PS_L - 2, c['r3']), 'ROI', color=fk.C['npu'][1],
             off=(0.5, 2.0), fs=fk.FS['sub'], ms=6.0)

    fk.arrow(ax, (PS_X + PS_W, c['cam']), (EXT_R, c['cam']), ms=6.0)
    fk.arrow(ax, (PS_X + PS_W, c['r3']), (EXT_R, c['r3']), ms=6.0)
    fk.arrow(ax, (PS_X + PS_W, c['r4']), (EXT_R, c['r4']), ms=6.0)

    return fig, ax


# PPT 重点框（pt，左下原点）：PL 自研模块区 / PS 软件分工区
REGIONS = {
    'pl_modules': (PL_X + 2, 48, PL_X + PL_W - 2, 266),
    'ps_threads': (PS_X + 2, 92, PS_X + PS_W - 2, 266),
}


def main():
    fig, ax = draw()
    return fk.save(fig, NAME, REGIONS)


if __name__ == '__main__':
    main()
