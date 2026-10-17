# DragonEyeSorter · 龙瞳分拣

基于 **安路飞龙（Dragon）F3P SoC** 的塑胶传动件在线质检分拣系统。

POM 齿轮与齿条随机摆放流过相机：视觉出每件位置与角度 + 亚像素几何测量（外径/孔径/齿数/长度），NPU（UINT8 量化 YOLOv8n-cls，逐件 ROI）判 8 类缺陷，按带位置逐件跟踪。**良品**到抓取线停带 → 6 轴总线舵机机械臂腕旋对准夹起 → 按品种入齿轮盒/齿条盒；**不良品与存疑件**不停带到带尾 → 两级翻板分流（复检 / NG、返修 / 报废）。PC 经 CAN 当「产线 PLC」下发启停与调速，HDMI 界面实时显示画面、计数、曲线与参数。

> 2026 全国大学生嵌入式芯片与系统设计竞赛 · 安路赛道 · 选题四（基于安路飞龙 SoC 的智能工业应用系统）

![SoC](https://img.shields.io/badge/SoC-Anlogic%20Dragon%20F3P%20DR1M90G-009688)
![赛道](https://img.shields.io/badge/2026嵌入式竞赛-安路赛道·选题四-2196F3)

系统架构与设计原则见 [系统概述](docs/系统概述.md)（总览图 [`figures/fig_architecture.png`](docs/figures/fig_architecture.png)）；硬件型号与接线见 [硬件与接线](docs/硬件与接线.md)。

## 快速上手

| 步骤 | 做什么 |
|---|---|
| 0. 环境配置（首次必做） | 复制 `.env.example` 为 `.env` 并填本机工具路径 → 跑 `scripts/test_env.ps1` 自检；需要哪些环境、板子一次性配置见 [环境与上手](docs/环境与上手.md) |
| 1. 开发环境 | 工具版本（TD 6.2.1 / FD 2026.1 / FD 自带交叉编译器）、启动链、SD 卡布局见 [开发环境与构建](docs/开发环境与构建.md) |
| 2. 启动板子 | 持久化 SD 卡启动（[sd_image](scripts/sd_image/)），根文件系统改动会保留 |
| 3. 改 PL | TD 编比特流 → [bootbin](scripts/bootbin/) 打包 `BOOT.bin` → 替换 `/mnt/mmcblk0p1/BOOT.bin` → 重启 → 跑 `pl_check.sh` 回归 |
| 4. 改 PS 应用 | `make -C ps all` 交叉编译 → [scripts/deploy](scripts/deploy/) 部署（`-DryRun` 先对比、`-Dest` 先在板上试新版） |

## 当前进度

| 阶段 | 内容 | 状态 |
|---|---|---|
| W1 平台验证 | NPU 推理实测（选 YOLOv8n@416）、量化格式逆向、采集显示、电平 / 供电 / 驱动器实测 | ✅ 大部分完成 |
| 阶段 0 编译链 | 持久化 SD ✅ · `/dev/mem` ✅ · BOOT.bin 打包上板 ✅ · TD 6.2.1 ✅ · SSH ✅ · TD 原样重编 `soc_prj`（与厂家逐位一致）✅ | ✅ |
| 阶段 1 三条线 | PL：寄存器窗口、舵机总线主机、步进梯形加减速、帧起始锁存（均已在板端实测，帧时序 = 理论值）；相机 60.02fps；CAN 协议 v1.1 回环 | ✅ |
| 阶段 2 软件闭环 | `des_app run`：60fps 显示 + 定位 / 跟踪 / 停带调度 + 机械臂线程 + CAN 状态机；亚像素测量；NPU 分类链路（7.8ms / 件）；HDMI 操作界面（五页）；每件存储；PC 端 PLC 模拟器 | ✅ 软件；🟡 等实物联调 |
| PL 视觉 / 翻板 | `des_vision`（硬件灰度 / 滤波 / 边缘 / 粗测量）、`des_pwm`（两级翻板 PWM，v10 源码去掉光源输出） | ✅ 板端 v9 实测，`pl_check.sh` 18 项全过（6 种显示模式实画面、PWM 50Hz、步进首脉冲 10.6µs、像素时钟 111.1MHz）；v10 未上板 |
| 阶段 3~4 | 实物标定 / 数据集 / 训练、测试报告、PPT / 技术文档 / 演示视频（骨架见 [参赛材料骨架](docs/参赛材料骨架.md)），约 11-10 提交 | ⬜ |

## 文档

| 入口 | 内容 |
|---|---|
| [docs/README.md](docs/README.md) | 文档地图（读者 / 唯一源 / 状态）、写作约定、各目录 README 索引 |
| [AGENTS.md](AGENTS.md) | 协作规范：目录约定、11 条硬性约定、工作流、Git 与板子、操作禁忌 |
| [docs/系统概述.md](docs/系统概述.md) | 定位、总体架构、已锁定决策、优先级分级 |
| [docs/测试与验证.md](docs/测试与验证.md) | 全部实测数字的唯一汇总与测试报告框架 |
| [docs/log/](docs/log/) | 按日期的实测记录与决策沿革 |

仓库目录与各目录职责见 [AGENTS.md](AGENTS.md)「目录约定」。
