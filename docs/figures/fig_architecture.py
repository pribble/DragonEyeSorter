#!/usr/bin/env python3
"""fig_architecture — 图 2-1 系统架构（PL 硬实时 + PS 调度 + NPU 推理）。

画布 = 报告落版宽度 447.9pt（15.8cm）× 300pt，字面字号即纸面字号（见 figkit 顶部说明）。
标签一律「名称 + 极短限定」；细节写正文（§2.1 / §2.4 / §3），图里不堆句子。
列：左外设 | PL | PS | 右外设；列内按行对齐，箭头只走同行的短横线。
"""
import figkit as fk

H = 300.0

LX, LW = 2.0, 56.0                   # 左外设
PLX, PLW = 62.0, 188.0               # PL 分组
PSX, PSW = 256.0, 140.0              # PS 分组
RX, RW = 400.0, 46.0                 # 右外设
I0, I1 = 68.0, 242.0                 # PL 内框
J0, J1 = 262.0, 390.0                # PS 内框
MIDX = 160.0                         # PL 内左右分栏
PMX = 326.0                          # PS 内两栏之间的走线通道

ROWS = dict(r1=(242, 42), r2=(206, 30), r3=(162, 34), r4=(118, 34), r5=(74, 34),
            r6=(30, 34), r7=(4, 22))


def draw():
    fig, ax = fk.new_fig(H)
    B, S = fk.FS['box'], fk.FS['sub']
    y1, h1 = ROWS['r1']
    y2, h2 = ROWS['r2']
    y3, h3 = ROWS['r3']
    y4, h4 = ROWS['r4']
    y5, h5 = ROWS['r5']
    y6, h6 = ROWS['r6']
    y7, h7 = ROWS['r7']

    fk.group(ax, PLX, 4, PLW, 290, 'PL（可编程逻辑）· 硬实时', 'pl')
    fk.group(ax, PSX, 4, PSW, 290, 'PS（Linux）· des_app', 'ps')

    # ---------------- PL ----------------
    fk.box(ax, I0, y1, I1 - I0, h1, '相机通路：MIPI 接收 + ISP + 图像预处理\n'
           '4 lane · 去马赛克 / 白平衡 · 灰度\n'
           'Sobel / 二值 · ROI', 'pl', fs=S)
    fk.box(ax, I0, y2, 66, h2, '显示输出\nHDMI TX', 'pl', fs=B)
    fk.box(ax, 140, y2, I1 - 140, h2, '寄存器窗口 0x8030_1000\nAXI-Lite · 单次读 188ns', 'pl', fs=S)
    fk.box(ax, I0, y3, MIDX - I0, h3, '舵机总线 servo_bus\n6 轴 SYNC WRITE\n时间戳 · 超时', 'pl', fs=S)
    fk.box(ax, MIDX + 6, y3, I1 - MIDX - 6, h3, '帧写入 + 拍摄锁存\n帧号 · FREE_CNT\n带位置 POS', 'pl', fs=S)
    fk.box(ax, I0, y4, MIDX - I0, h4, '步进控制 stepper_ctrl\n梯形加减速 · 刹车距离\n首脉冲 10.6µs', 'pl', fs=S)
    fk.box(ax, MIDX + 6, y5, I1 - MIDX - 6, y4 + h4 - y5, 'NPU\n硬核 512 MAC\n+ 软核算子（官方 IP）\nArmNN / Alnpu\nUINT8 QDQ',
           'npu', fs=S)
    fk.box(ax, I0, y5, MIDX - I0, h5, '挡板 PWM des_pwm\n2 路 50Hz\n报废 / 返修分流', 'pl', fs=S)
    fk.box(ax, I0, y6, I1 - I0, h6, '急停锁存：ESTOP_IN 硬件立即停脉冲\n锁存 HALTED（不经 PS）', 'acc', fs=S)
    fk.box(ax, I0, y7, I1 - I0, h7, 'PL 贡献：脉冲 / 总线帧 / IO 采样都在 PL\n（评分要求：功能不能全放 PS）', 'pl', fs=S)

    # ---------------- PS ----------------
    fk.box(ax, J0, y1, J1 - J0, h1, '核 0 · 界面与显示（实时优先级）\n'
           '取帧 → 叠加跟踪框 / 状态 → 贴界面\n总览 / 曲线 / 参数 / 记录 60fps', 'ps', fs=S)
    fk.box(ax, J0, y2, J1 - J0, h2, '核 1 · 定位与跟踪\ns = x − B → 停带 / 挡板调度', 'ps', fs=S)
    fk.box(ax, J0, 158, 58, 38, 'NPU 线程\nROI 224²\n7.8ms / 件', 'npu', fs=S)
    fk.box(ax, 328, 158, 62, 38, 'CAN 状态机\n10Hz 状态\n每件结果', 'ps', fs=S)
    fk.box(ax, J0, 110, 58, 38, '机械臂线程\n逆运动学 → 航点\nSYNC WRITE', 'ps', fs=S)
    fk.box(ax, 328, 110, 62, 38, '存储线程\n每件 CSV\n不良件图', 'ps', fs=S)
    fk.box(ax, J0, 22, J1 - J0, 56, '标定与工具\ncalib-cam / belt / arm\n'
           'measure · collect · soak · pl_check\nPC：PLC 模拟器 · part_stats', 'ps', fs=S)

    # ---------------- 外设 ----------------
    fk.box(ax, LX, y1, LW, h1, '相机 IMX415\n4 lane MIPI\n1080p60', 'ext', fs=S)
    fk.box(ax, LX, y2, LW, h2, 'HDMI 显示器\n1080p60', 'ext', fs=S)
    fk.box(ax, LX, y3, LW, h3, '6 轴机械臂\n总线舵机', 'ext', fs=S)
    fk.box(ax, LX, y4, LW, h4, '传送带\n57 步进电机', 'ext', fs=S)
    fk.box(ax, LX, y5, LW, h5, '侧边导流挡板\nMG996R ×2', 'ext', fs=S)
    fk.box(ax, LX, y6, LW, h6, '急停\n常闭切动力\n常开报 PL', 'acc', fs=S)
    fk.box(ax, RX, y1, RW, h1, 'USB\n鼠标 / 键盘', 'ext', fs=S)
    fk.box(ax, RX, 158, RW, 38, 'PC 产线 PLC\nUSB-CAN\n500k 状态', 'ext', fs=S)
    fk.box(ax, RX, 110, RW, 38, 'SD 卡\n每件 CSV\n不良件图', 'ext', fs=S)

    # ---------------- 线（列间只留「框里看不出来」的 2 个标签：AXI 寄存器 / ROI 裁剪） ----
    fk.arrow(ax, (58, 263), (I0, 263), ms=5.0)
    fk.arrow(ax, (I0, 221), (58, 221), ms=5.0)
    fk.arrow(ax, (58, 179), (I0, 179), both=True, ms=4.5)
    fk.arrow(ax, (58, 135), (I0, 135), ms=5.0)
    fk.arrow(ax, (I0, 91), (58, 91), ms=5.0)
    fk.arrow(ax, (58, 47), (I0, 47), color=fk.C['acc'][1], ms=5.0)
    fk.arrow(ax, (I1, 263), (J0, 263), ms=5.0)
    fk.arrow(ax, (I1, 221), (J0, 221), 'AXI', both=True, fs=S, off=(0, 2.2), ms=5.0)
    fk.arrow(ax, (I1, 150), (J0, 177), 'ROI', color=fk.C['npu'][1], both=True, fs=S, off=(1.5, 2.2), ms=5.0)
    fk.arrow(ax, (355, 206), (355, 196), color=fk.C['ps'][1])
    fk.poly(ax, [(300, 206), (PMX, 206), (PMX, 152), (300, 152), (300, 148)], color=fk.C['ps'][1])
    fk.arrow(ax, (J1, 129), (RX, 129), color=fk.C['ps'][1])
    fk.arrow(ax, (RX, 263), (J1, 263))

    return fig, ax


REGIONS = {
    # PPT 放大区（pt，左下原点）：PL 自研模块 + NPU（下边界取 70，让 NPU 框整块进面板，
    # 顺带带上挡板 PWM 框；上边界 202 只到帧写入框，不含 核0）
    'pl_modules': (I0 - 4, 70, I1 + 6, 202),
    # PS 侧软件分工
    'ps_threads': (J0 - 4, 104, J1 + 4, 240),
}


def main():
    fig, ax = draw()
    return fk.save(fig, 'fig_architecture', REGIONS)


if __name__ == '__main__':
    main()
