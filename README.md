# DragonEyeSorter · 龙瞳分拣

基于 **安路飞龙（Dragon）F3P SoC** 的塑胶传动件在线质检分拣系统。

POM 齿轮与齿条在传送带上随机摆放流过相机：NPU（量化 YOLOv8n）判 8 类缺陷，视觉给出每件的位置与角度，系统按带位置跟踪每一件；**良品**到抓取线时停带，6 轴总线舵机机械臂腕旋对准后夹起，按品种放入齿轮盒 / 齿条盒；**不良品与存疑件**不停带走到带尾，由翻板分流到 NG 箱 / 复检箱。PC 经 CAN 当"产线 PLC"下发启停与调速，HDMI 界面实时显示画面、计数、曲线与参数。

> 2026 全国大学生嵌入式芯片与系统设计竞赛 · 安路赛道 · 选题四（基于安路飞龙 SoC 的智能工业应用系统）

![SoC](https://img.shields.io/badge/SoC-Anlogic%20Dragon%20F3P%20DR1M90G-009688)
![赛道](https://img.shields.io/badge/2026嵌入式竞赛-安路赛道·选题四-2196F3)
![License](https://img.shields.io/badge/license-MIT-green)

## 总体架构

```
                   ┌──────────────────── PL（可编程逻辑）────────────────────┐
IMX415 ──MIPI──►   │ MIPI RX → ISP ──► VDMA ──► HDMI TX ──► 1080p 显示器       │
                   │        └─► 帧写 DDR（供 PS / NPU 取图）                    │
                   │ 运动控制：步进脉冲 + 加减速 · 舵机总线主机（SYNC WRITE + 遥测）│
                   │           带尾翻板 · 编码器 / 帧起始锁存 · IO 同步采样 + 急停 │
                   │ AXI-Lite 寄存器堆 + 中断 ◄──────────────────────────────┤
                   └──────────────────────────┬───────────────────────────────┘
                                              │ AXI（/dev/mem）
                   ┌──────────────────────────┴───────────────────────────────┐
                   │ PS（双核 A35 + Linux）                                    │
                   │  NPU 推理（ArmNN + Alnpu）· 视觉定位 (x, y, θ) · 跟踪表     │
                   │  抓取调度 + IK · CAN · HDMI 界面（Qt）· SQLite + NG 图片    │
                   └───────────────────────────────────────────────────────────┘
```

## 硬件平台

| 部件 | 型号 / 说明 |
|---|---|
| 核心板 | MLK-F3P-DR1M90G（DR1M90GEG400：双核 Cortex-A35 + FPGA + 0.4T NPU） |
| 视觉 | MLK-FEP-VID005 子卡 + IMX415（MIPI CSI） |
| 传送带 | 57 步进电机 + TB6600 类驱动器；编码器（P1） |
| 机械臂 | 德晟 DS-SY15A 总线舵机 ×6（#1~#4 + 腕旋 + 夹爪），PL 经 BSS138 直驱 |
| 带尾翻板 | MG996R PWM 舵机 |
| 传感 / 安全 | 对射光电、急停（常闭切动力 + 常开报 PL） |
| 总线 | CAN（P0）、RS485 Modbus（P1） |

## 仓库结构

```
DragonEyeSorter/
├── docs/          # 设计文档 01~09 + log/ 实测记录（入口：docs/README.md）
├── pl/            # RTL、约束、TD 工程源文件；test/ 下是独立验证工程
├── ps/            # Linux 应用（调度、推理、UI、存储、寄存器头文件）
├── scripts/       # 构建与部署脚本：BOOT.bin 打包、持久化 SD 镜像
├── AGENTS.md      # 协作规范（AI 助手与新队员先读）
└── README.md
```

## 快速上手

1. 读 [docs/09-开发环境与构建](docs/09-开发环境与构建.md)：工具版本（TD 6.2.1 / FD 2026.1 / FD 自带交叉编译器）、启动链、SD 卡布局。
2. 板子用持久化 SD 卡启动（[scripts/sd_image](scripts/sd_image/)），根文件系统的改动会保留。
3. 改 PL：TD 编出比特流 → [scripts/bootbin](scripts/bootbin/) 打包 `BOOT.bin` → 替换 `/mnt/mmcblk0p1/BOOT.bin` → 重启。
4. 写 PS 应用：FD 自带的 `aarch64-linux-gnu-gcc` 交叉编译，`/dev/mem` mmap 读写 PL 寄存器，`scp` 到板上运行。

## 当前进度

| 阶段 | 内容 | 状态 |
|---|---|---|
| W1 平台验证 | NPU 推理实测（选 YOLOv8n@416）、量化格式逆向、采集显示、电平 / 供电 / 驱动器实测 | ✅ 大部分完成 |
| 阶段 0 编译链 | 持久化 SD ✅ · `/dev/mem` ✅ · BootGen 重打包上板 ✅ · TD 6.2.1 ✅ · SSH ✅ · 原样重编 `soc_prj` 上板 ⬜ | 进行中 |
| 阶段 1 三条线 | AXI 寄存器堆 / 步进 / 舵机总线 / IO；视觉定位 + 模型上板；CAN | ⬜ |
| 阶段 2 主线闭环 | 跟踪抓取 + HDMI 界面，10-24 录保底视频 | ⬜ |
| 阶段 3~4 | P1 加分、测试报告、PPT / 文档 / 视频，约 11-10 提交 | ⬜ |

## 文档

见 [docs/README.md](docs/README.md) 的文档地图与演进规则；实测数据按日期归档在 [docs/log/](docs/log/)。

## License

[MIT](LICENSE)
