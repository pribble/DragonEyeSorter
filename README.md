# DragonEyeSorter · 龙瞳分拣

基于 **安路飞龙（Dragon）F3P SoC** 的塑胶传动件在线质检分拣系统。

POM 齿轮与齿条随机摆放流过相机：视觉出每件位置与角度 + 亚像素几何测量（外径/孔径/齿数/长度），NPU（UINT8 量化 YOLOv8n-cls，逐件 ROI）判 8 类缺陷，按带位置逐件跟踪。**良品**到抓取线停带 → 6 轴总线舵机机械臂腕旋对准夹起 → 按品种入齿轮盒/齿条盒；**不良品与存疑件**不停带到带尾 → 翻板分流 NG 箱/复检箱。PC 经 CAN 当「产线 PLC」下发启停与调速，HDMI 界面实时显示画面、计数、曲线与参数。

> 2026 全国大学生嵌入式芯片与系统设计竞赛 · 安路赛道 · 选题四（基于安路飞龙 SoC 的智能工业应用系统）

![SoC](https://img.shields.io/badge/SoC-Anlogic%20Dragon%20F3P%20DR1M90G-009688)
![赛道](https://img.shields.io/badge/2026嵌入式竞赛-安路赛道·选题四-2196F3)

## 总体架构

```
                   ┌──────────────────── PL（可编程逻辑）────────────────────┐
IMX415 ──MIPI──►   │ MIPI RX → ISP → des_vision（灰度 / 高斯 / Sobel / 二值 +   │
                   │   ROI 面积·外接框·质心）→ 帧写 DDR ──► HDMI TX ──► 1080p 显示器 │
                   │ 运动控制：步进脉冲 + 加减速 · 舵机总线主机（SYNC WRITE + 遥测）│
                   │           带尾翻板 PWM · 帧起始锁存带位置 · 急停锁存          │
                   │ AXI-Lite 寄存器堆 + 中断 ◄──────────────────────────────┤
                   └──────────────────────────┬───────────────────────────────┘
                                              │ AXI（/dev/mem）
                   ┌──────────────────────────┴───────────────────────────────┐
                   │ PS（双核 A35 + Linux）                                    │
                   │  视觉定位 (x, y, θ) · 亚像素测量 · NPU 分类（ArmNN + Alnpu）│
                   │  跟踪表 + 抓取调度 + IK · CAN · HDMI 界面 · 每件记录 + NG 图 │
                   └───────────────────────────────────────────────────────────┘
```

## 硬件平台

| 部件 | 型号 / 说明 |
|---|---|
| 核心板 | MLK-F3P-DR1M90G（DR1M90GEG400：双核 Cortex-A35 + FPGA + 0.4T NPU） |
| 视觉 | MLK-FEP-VID005 子卡 + IMX415（MIPI CSI） |
| 传送带 | 57 步进电机 + TB6600 类驱动器（共阴直驱）；编码器（P1） |
| 机械臂 | 德晟 DS-SY15A 总线舵机 ×6（#1~#4 + 腕旋 + 夹爪），PL 经 BSS138 直驱 |
| 带尾翻板 | MG996R PWM 舵机 ×2（两级：放行 / NG，返修 / 报废）|
| 传感 / 安全 | 对射光电、急停（常闭切动力 + 常开报 PL） |
| 总线 | CAN（P0）、RS485 Modbus（P1） |

## 仓库结构

```
DragonEyeSorter/
├── docs/          # 设计文档 + log/ 实测记录 + figures/ 插图（入口：docs/README.md）
├── pl/            # PL 工程（soc_base/：厂家底板 + 自研模块）、sim/ 仿真、test/ 独立验证工程
├── ps/            # 板上程序：des_app（取帧 / 定位 / 测量 / 跟踪 / 调度 / NPU / 界面 / 存储 / CAN）、desreg、测试
├── train/         # 分类模型：合成数据、训练、导出 + UINT8 量化、评估（WSL / GPU）
├── models/        # label_list.json（类别表唯一源）
├── pc/            # PC 端：plc_sim（CAN「产线 PLC」模拟器）、report（测试报告出数）
├── scripts/       # BOOT.bin 打包、写 SD 卡
├── AGENTS.md      # 协作规范（AI 助手与新队员先读）
├── README.md
└── .env.example   # 本机路径模板：复制为 .env 后填写，见 docs/环境与上手
```

## 快速上手

| 步骤 | 做什么 |
|---|---|
| 0. 环境配置（首次必做） | Windows PowerShell：`cp .env.example .env` 填本机工具路径 → `scripts\test_env.ps1` 自检；要什么环境、板子一次性设置、从零跑起来都见 [环境与上手](docs/环境与上手.md) |
| 1. 开发环境 | 工具版本（TD 6.2.1 / FD 2026.1 / FD 自带交叉编译器）、启动链、SD 卡布局，见 [开发环境与构建](docs/开发环境与构建.md) |
| 2. 启动板子 | 持久化 SD 卡启动（[sd_image](scripts/sd_image/)），根文件系统改动会保留 |
| 3. 改 PL | TD 编比特流 → [bootbin](scripts/bootbin/) 打包 `BOOT.bin` → 替换 `/mnt/mmcblk0p1/BOOT.bin` → 重启 |
| 4. 写 PS 应用 | FD 自带 `aarch64-linux-gnu-gcc` 交叉编译，`/dev/mem` mmap 读写 PL 寄存器，`scp` 到板上运行 |

## 当前进度

| 阶段 | 内容 | 状态 |
|---|---|---|
| W1 平台验证 | NPU 推理实测（选 YOLOv8n@416）、量化格式逆向、采集显示、电平 / 供电 / 驱动器实测 | ✅ 大部分完成 |
| 阶段 0 编译链 | 持久化 SD ✅ · `/dev/mem` ✅ · BootGen 重打包上板 ✅ · TD 6.2.1 ✅ · SSH ✅ · 原样重编 `soc_prj`（与厂家逐位一致）✅ | ✅ |
| 阶段 1 三条线 | PL：寄存器窗口、舵机总线主机、步进梯形加减速、帧起始锁存（v5 上板，帧时序 = 理论值）；相机 60.02fps；CAN 协议 v1.1 回环 | ✅ |
| 阶段 2 软件闭环 | des_app run：60fps 显示 + 定位 / 跟踪 / 停带调度 + 机械臂线程 + CAN 状态机；亚像素测量；NPU 分类链路（7.8ms / 件）；HDMI 操作界面；每件存储；PC 端 PLC 模拟器 | ✅ 软件；🟡 等实物联调 |
| PL 视觉 / 翻板 | des_vision（硬件灰度 / 滤波 / 边缘 / 粗测量）、des_pwm（两级翻板 + 灯） | ✅ v8 上板，`pl_check.sh` 19 项全过（6 种显示模式实画面、PWM 50Hz、步进首脉冲 10.6µs、像素时钟 111.1MHz） |
| 阶段 3~4 | 实物标定 / 数据集 / 训练、测试报告、PPT / 文档 / 视频（骨架见 docs/参赛材料骨架.md），约 11-10 提交 | ⬜ |

## 文档

文档地图与演进规则见 [docs/README.md](docs/README.md)；实测数据按日期归档在 [docs/log/](docs/log/)。
