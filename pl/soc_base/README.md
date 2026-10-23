# pl/soc_base/ — PL 工程（soc_prj，TD 6.2.1）

厂家 NPU demo `soc_prj` 底板 + 我们的全部改动；**仓库里这一份即可直接编译**：TD 6.2.1 打开 `soc_prj.al` → 综合 / 布局布线 → `soc_prj.bit`。**板端当前 v15（最新）**：v15 = 第三轮重构源码（`9e01d8f`，自研 + 厂家明文 RTL + `pl/sim` 框架）全量重编 + 上板（`BOOT.bin` `b0aef232…`，比特流 `e14fbb65…`；`pl_check.sh` 21 项回归通过、2min soak 0 error；寄存器 `VERSION` 仍 12）。自研各域 0 违例；v14 的 HDMI 串行 −0.164ns 与 PS HP0→NPU −0.133ns 厂家路径端点已消失，NPU 软核域内 −0.093ns / 42 端点仍属厂家加密 IP；TMDS 视觉待复核 ⚠；版本与上板证据见下「板端版本台账」。明细见 [log/2026-10-23-PL二次重构](../../docs/log/2026-10-23-PL二次重构.md) · [后全量重编与上板](../../docs/log/2026-10-23-PL二次重构后全量重编.md)；各模块上板状态与验证记录见 [docs/PL设计.md](../../docs/PL设计.md)。2026-10-24 又对**厂家明文 RTL（非 des）**做了一轮零行为重构（源码净减 55.7%、TD 综合 0 error），随后**第三轮 PL 重构**把自研 `des/` 与顶层、厂家明文 RTL、`pl/sim` 仿真框架一起再精简（零外部行为变化）；这两轮重构当时均未重编比特流，其源码由 v15 全量重编 + 上板转正，明细见 [log/2026-10-24-PL厂家RTL重构](../../docs/log/2026-10-24-PL厂家RTL重构.md) · [第三轮重构](../../docs/log/2026-10-24-PL第三轮重构.md) · [第三轮重编与上板](../../docs/log/2026-10-24-PL第三轮重编上板v15.md)。

来源：米联客 / 安路 NPU 演示套件 `demo.zip → demo/soc_prj/`（2025-12 版）。TD 统一 6.2.1（与厂家比特流同版本，原样重编逐位一致已验证）；其他版本打开会提示格式升级。

## 板端版本台账

| 版本 | 上板日期 | BOOT.bin md5 | 自研域时序 | pl_check |
|---|---|---|---|---|
| v8 | 10-11 | `f1c6de46…` | HDMI 串行 742.9MHz −0.338ns | 19 项全过 |
| v9 | 10-15 | `e3a9d500…` | 75MHz +1.738ns / 像素 +0.104ns / HDMI +0.120ns | 20 项全过 |
| v10 | — | —（des_pwm 去光源输出，**未单独编译 / 未上板**） | — | — |
| v11 | 10-18 | `89fb03c8…`（比特流 `d643f655…`）；`BOOT.bin.prev` = v9 `e3a9d500…` | 75MHz +0.137ns / 像素 +0.103ns / HDMI −0.066ns | 21 项全过 |
| v12 | 10-19 | `a026ead4…`；`BOOT.bin.prev` = v11 `89fb03c8…` | 75MHz +0.102ns / 像素 +0.102ns / HDMI +0.041ns | 0 失败 |
| v13 | 10-23 | `48c6bc7b…`（比特流 `6afcb26f…`）；`BOOT.bin.prev` = v12 `a026ead4…` | 75MHz +0.292ns / 像素 +0.116ns / HDMI +0.055ns；自研 0 违例（NPU 软核域内 −0.340 新增，见 log） | 0 失败 |
| v14 | 10-23 | `a72e00f7…`（比特流 `a8a29533…`）；`BOOT.bin.prev` = v13 `48c6bc7b…` | 75MHz +0.341ns / 像素 +0.129ns / 150M +0.140ns；HDMI 串行 −0.164ns（1 端点，厂家路径 ⚠，TMDS 视觉待复核） | 0 失败（21 项） |
| v15 | 10-24 | `b0aef232…`（比特流 `e14fbb65…`）；`BOOT.bin.prev` = v14 `a72e00f7…` | 75MHz +0.112ns / 像素 +0.106ns / 150M +0.187ns；自研 0 违例（v14 的 HDMI 串行与 PS HP0→NPU 端点消失；NPU 软核域内 −0.093ns / 42 端点仍属厂家 IP，见 log） | 0 失败（21 项） |

v13 为本次 PL 重构后全量重编版；PL 寄存器 `VERSION` 仍读 12（重构未改寄存器接口）。

v14 为二次重构源码全量重编 + 上板版（`pl_check.sh` 21 项 0 失败、2min soak 0 error；HDMI 串行域 1 个 −0.164ns 厂家路径端点 ⚠，TMDS 视觉待复核）；重编资源 / 时序与审计见 [log/2026-10-23-PL二次重构后全量重编](../../docs/log/2026-10-23-PL二次重构后全量重编.md)。

v15 为第三轮重构源码（自研 + 厂家明文 RTL + `pl/sim` 框架，`9e01d8f`）全量重编 + 上板版（56m25s、0 error；P&R slice 83.19%、线长 −3.8%；自研各域 0 违例，v14 的 HDMI 串行 / PS HP0→NPU 厂家路径端点消失；`pl_check.sh` 21 项 0 失败、2min soak 0 error）；见 [log/2026-10-24-PL第三轮重编上板v15](../../docs/log/2026-10-24-PL第三轮重编上板v15.md)。

v12 上板记录原在 PL设计「基线」段，10-20 文档收敛移入本表；逐次明细见 [docs/log](../../docs/log/README.md)。

## 目录内容

- `soc_prj.al` / `soc_prj.hpf` — TD 工程入口
- `design_0/` — 图形化 SoC 设计：PS + MIPI RX + ISP + VDMA + HDMI 时序 + **NPU 软核 IP**；`design_0.address.xml` 是 GP 口地址分配（vdmafb 0x80100000 / VDMA 0x80200000 / mipicam 0x80300000 / soft_npu GP1 0x1F0000000）——**我们的 AXI-Lite 从机（PL_BASE）从这里分配，避开上述段**
- `design_0/ip/` — 例化 IP 的包装与 RTL（多数为安路闭源加密文件）
- `al_ip/` — 安路 IP 参数（PLL、NPU_SOFT 等）
- `src/` — 明文 RTL：`system_top.v`、`hdmi_tx/`、`MIPI_V2/`（`mipi_rx/` + `src/` 只剩 `csi_unpacket.v` 与 `design_top_wrapper.v`）、`write_fdma/`、`ui_isp/`、`pwm.v`（2026-10 两轮零行为重构，见 [log/2026-10-24-PL厂家RTL重构](../../docs/log/2026-10-24-PL厂家RTL重构.md) · [log/2026-10-24-PL第三轮重构](../../docs/log/2026-10-24-PL第三轮重构.md)）
- `src/ui_isp/` — ISP 链路**平铺一层**：`isp_top.v`、`raw10_unpacket_4lane.v` 与 8 个单文件模块（`BLC.v`、`image_cut.v`、`contrast_adj.v`、`Brightness_adjustment.v`、`Saturation_adj.v`、`image_correction.v`、`data96_128.v`、`data128_96.v`）直接放根目录；`gamma/`（2 个 `.v`：`gamma.v` + 唯一在用的 `lut_2_2.v`）、`demosaic_4x_2_0/`（6 个 `.v` + 2 个厂家 `blk_mem_gen` IP）、`awb_1_1/`（2 个 `.v` + 1 个 IP）因多文件 / IP 保留子目录
- `src/des/` — **自研模块**：des_regs、servo_bus、stepper_ctrl、des_vision、des_pwm、des_io、awb_seq_div（已登记进 `soc_prj.al`）；`des_common.vh`（同步 / 去抖公共模块，被 des_regs / des_io 引用，纯 `include` 无需单独登记）
- `src/constraint/` — 管脚 `io.adc`（CEP = BANK11 3.3V）、时序 `timing.sdc`
- `soc_prj_Runs/syn_1/soc_prj.prj` 与 `soc_prj_Runs/phy_1/soc_prj.prj` — 综合 / 布局布线工程文件（`*_Runs/` 被 ignore，改动要 `git add -f`）

## 加密 IP

实测 **12 个 `pragma protect`（加密）文件**，随本工程编译，改不了：

| 位置 | 个数 | 内容 |
|---|---|---|
| `design_0/ip/**/RTL/` | 9 | NPU 软核、VDMA、视频时序发生器（部分加密）、AXI Matrix ×3、AXI 协议转换 ×2、AXI4-Stream to Video Out |
| `src/hdmi_tx/hdmi_tx_controller_wrapper.enc.v` | 1 | HDMI 控制器（TMDS） |
| `src/MIPI_V2/mipi_rx/` | 2 | D-PHY 数据通道接收、通道对齐 |

`src/write_fdma/wfifo.v` 文件名不带 `.enc`，内容里的 `` `pragma protect begin `` **是注释掉的** →
实为明文的 TD IP Generator 生成件（可仿真、可重构）；2026-10 厂家 RTL 重构已把它一并零行为重构
（见 [log/2026-10-24-PL厂家RTL重构](../../docs/log/2026-10-24-PL厂家RTL重构.md)），因此不在加密清单内。

## 删掉的厂家未用源码

2026-10-17 对 140 个明文 `.v/.sv/.vh` 做引用链扫描（从 `system_top` 出发逐级看例化），删掉 **15 个没有任何例化的厂家文件**（`src/isp/` 图像算子 7 个、`MIPI_V2/src/` 的 RAW8 老解包路径 7 个、厂家除法器 IP 1 个），并删 `soc_prj.al` 与两份 `soc_prj.prj` 里的对应登记（73 → 58 条）。`design_0/ip/**`、0 字节的 `define.v` 与厂家 `pwm.v` 保留（被加密 IP 内部或块设计引用）。理由与验证见 [log/2026-10-17-清理未用的厂家PL源码](../../docs/log/2026-10-17-清理未用的厂家PL源码.md)。

二次重构（[log/2026-10-23-PL二次重构](../../docs/log/2026-10-23-PL二次重构.md)）又删除 **13 个不可达的厂家文件**：`design_0/ip/DR1_ARM_Processor_System_0/` 7 个、`design_0/ip/proc_sys_reset_75M/` 5 个、过期块设计生成脚本 `design_0/design_0.tcl` 1 个。判据：从 `system_top` BFS 不可达；TD v13 综合 build log 的 109 条 analyze 文件列表里没有这些 `.v`；syn / phy 的 area 层次报告里没有对应 module；`soc_prj.al`、两份 `soc_prj.prj` 与 `design_0.xml` 的 fileSets 均 0 登记（工程文件零改）；该 `.tcl` 引用了被删的 IP。加密 IP 数量不变（这 13 个都不含 `pragma protect`）。

2026-10-24 厂家 RTL 重构（[log/2026-10-24](../../docs/log/2026-10-24-PL厂家RTL重构.md)）再删除 **8 个未用的 gamma LUT**（`src/ui_isp/gamma/lut_{0_8,1_2,1_4,1_6,1_8,2_0,2_4,2_6}.v`，共 2,227 行 / 103,062 B）：`isp_top` 写死 `GAMMA_10x=22`，其余 8 套曲线无任何例化，属「只有一种配置被使用」的参数分支特化；`soc_prj.al` 与两份 `soc_prj.prj` 同批删 8 条登记（59 → 51 条）。

## 寄存器总线

- 时钟：`des_clk` = `fvdma_ctrl` 的 AXI 时钟（`pll_75m`，75MHz）
- PS 地址：`0x8030_1000 + 偏移`（GP0 上 `fvdma_ctrl` 的窗口是 `0x8030_0000~0x8030_FFFF`；`0x0000~0x0FFF` 仍是厂家寄存器，供 `mipicam` 驱动使用，厂家写入已排除我们的地址段）
- 寄存器定义见 [docs/PL设计.md](../../docs/PL设计.md)
- 仿真：[pl/sim/tb_des_axi.v](../sim/tb_des_axi.v)（`run_sim.sh des_axi`）；全部 8 项 `bash pl/sim/run_all.sh`

## 当前对厂家的改动摘要

逐版经过、验证与踩坑见 [docs/log/](../../docs/log/README.md)；模块状态见 [docs/PL设计.md](../../docs/PL设计.md)。

| 模块 / 文件 | 改了什么 | 为什么 |
|---|---|---|
| 寄存器通道（`src/system_top.v`、`design_0/hdl/design_0.v`、`design_0/ip/design_0_fvdma_ctrl_0/**`） | `fvdma_ctrl_0` AXI 地址宽 4→16，把 64KB 窗口的 `0x1000~0x1FFF` 划给自研寄存器（写按 AXI 握手出单周期脉冲，厂家寄存器写入排除该段），新增 `des_*` 读写端口 | 不改块设计 / FSBL / 设备树就拿到 PS↔PL 寄存器通道 |
| `des_regs` | 帧计数、拍摄锁存（`CAP_*`）、版本 / ID；`led[0]`（板载 LED1，T19）默认由 50MHz 计数器第 27 位驱动，寄存器可接管 | 验证「改 PL 源码 → 上板生效」；PS 读状态 |
| `servo_bus` / `stepper_ctrl` + 帧计数 | 接入 `0x1xx` / `0x2xx`，读数据按 `des_raddr[11:8]` 分路；CEP 管脚 `STEP_PUL/DIR/ENA` = Y12/Y13/T9、`ESTOP_IN` = V11（上拉）、`SERVO_BUS` = Y9（开漏）；`STEP_COMMON_CATHODE=1`（PUL 高有效、ENA 高 = 脱机） | 运动控制上板；共阴直驱免 74HCT245 |
| 帧起始判定（`src/system_top.v`） | 只看 ISP `tuser` 上升沿（`O_tuser` 是输入直通，不与 `tvalid` 对齐） | 帧计数修正 |
| PLL（`al_ip/PLL/PLL.v`） | CLKC3（IMX415 INCK）23.86 → 26.92MHz（DIV 44→39 / CPHASE 43→38 / DUTY_INT 22→20） | 传感器寄存器是 27MHz INCK 那套，原 INCK 只有 53fps |
| 拍摄锁存（`src/write_fdma/uidbuf_write.v`、`stepper_ctrl.pos_o`） | 帧写开始翻转 + 完成中断上升沿送 `des_regs`；`stepper_ctrl` 暴露当前带位置 | 跟踪表要「这一帧拍摄时刻的带位置」 |
| `des_vision`（0x3xx） | 插在 ISP 输出与帧写模块之间（`ud_*` 改走 `vis_*`） | PL 硬件灰度 / 滤波 / 边缘 / 粗测 |
| `awb_seq_div`（改 `src/ui_isp/awb_1_1/awb.v`） | 两个 50 级流水线除法器 IP 换成背靠背逐位除法器（每 49 时钟一个增益，增益不随帧复位）；对照仿真 `run_sim.sh awb` | 省约 4k LUT + 5k FF，布线 5h 不通 → 23 分钟通过 |
| `des_pwm`（0x4xx） | `FLAP1_PWM` = PIN18 Y6、`FLAP2_PWM` = PIN20 Y8；`stepper_ctrl` 启动预置相位（首脉冲 1.94ms → 10.6µs）；光源输出已去掉（`CTRL` b8、`lamp` 端口、`LAMP_EN` = PIN17 Y7 释放） | 导流挡板两级 PWM；光源改人工开关 / 调光 |
| `des_io`（0x5xx） | 急停去抖 + 锁存（锁存期间步进停住）、对射 / 限位去抖 + 边沿捕获（时间戳 / 带位置 / 编码器计数）、编码器 4 倍频 + 滤波 + Z 相 + 测速；取代未接入的 `io_sync`（`src/des/io_sync.v` 已删除，无例化） | 现场输入采样放 PL |
| `timing.sdc` | `clk_mipi` 挂到实际字节时钟 `…/U_io_clk_div_1.clkdivout`（8.0ns） | 厂家原约束挂在不存在的硬核 MIPI 引脚上（虚拟时钟），像素时钟域从未做过时序分析，`tuser` 边沿检测漏帧 |
| `des_vision` / `des_pwm` 读数据 | 改组合逻辑读（桥接逻辑在同一时钟取数，打拍的会读回上一个地址） | 寄存器读回正确 |
| `stepper_ctrl` | 运动中把 `TARGET` 改成恰好等于 `POS` 时就地停；目标在身后仍减速停 | 皮带「停在此处」写法不再卡死 |
| CAN0 管脚 | PS 硬核 `can0_tx_out` / `can0_rx_in` 经 `ARM_Processor_System_*` → `design_0.v` → `system_top` 引到 F20（TX）/ F19（RX），LVCMOS33、RX 上拉 | 官方块设计没开 PS CAN，原 `can0` 只能回环，接不了 USB-CAN / PLC |
| 自研 RTL 二次精简（`src/des/*.v`、`src/des/des_common.vh`、`src/system_top.v`） | 零行为精简：收紧数据类型、合并重复逻辑、清理死代码与注释；另删 13 个不可达厂家 IP 包装与过期块设计脚本（见上节） | 减少代码量、提升综合资源效率；寄存器零变，**未重编比特流**（[log/2026-10-23-PL二次重构](../../docs/log/2026-10-23-PL二次重构.md)） |
| 厂家明文 RTL 零行为重构（`src/{ui_isp,MIPI_V2,hdmi_tx,write_fdma}/`、`src/system_top.v`、`src/pwm.v`） | 逐拍等价前提下精简：合并重复通道 / 重复 always、收紧位宽、删死码与无效 `//synthesis keep`、GB18030 → UTF-8；gamma 单配置特化（9 套曲线 → 2.2，删 8 个 LUT 文件）；MIPI/HDMI `DEVICE` 只留 DR1；`wfifo.v` 确认为明文生成件并一并重构 | 减少代码量（41 文件净 −5,351 行 / −55.7%，−198,191 B）、提升综合效率（LUT −328 / FF −201）；36 项逐拍等价回归 + 8 项功能仿真逐字节一致 + 综合 0 error（[log/2026-10-24-PL厂家RTL重构](../../docs/log/2026-10-24-PL厂家RTL重构.md)） |
| 第三轮 PL 重构（`src/des/*.v`、`src/des/des_common.vh`、`src/system_top.v`、`src/{ui_isp,MIPI_V2,hdmi_tx,write_fdma}/`） | 自研与厂家明文 RTL 再精简：位宽 / 数据类型收紧、重复 always 与重复通道合并、死码与未用参数清理、LUT 表紧凑排版；**零新增 / 零删除 / 零合并**，三份 TD 工程文件零改；`pl/sim` 仿真框架同轮精简与提速（PC 侧，不落板） | 减少代码量（40 文件净 −1,164 行 / −22.6%，−28,985 B）；综合资源基本持平（LUT +51 / FF −96，ERAM / DSP 不变）；36 项逐拍等价 + 8 项功能仿真逐字节一致（[log/2026-10-24-PL第三轮重构](../../docs/log/2026-10-24-PL第三轮重构.md)） |

## 已知问题（编译前先看）

- 厂家最后一次 P&R：WNS **-4.120ns**，违例集中在 vtg 148.5M ↔ NPU 150M 跨时钟域（`timing.sdc` 只做了部分 clock-group 约束），我们重编时要补约束。
- 资源基线：85.81% slice / 92.50% ERAM，加模块前先评估余量。
- 首次编译先确认能原样复现，再动增量。

## 与 boot 打包的关系

本工程编译产物 `soc_prj.bit` 喂给 `scripts/bootbin/make_bootbin.ps1`；FSBL / U-Boot 用仓库内零件（`scripts/bootbin/parts/`），除非改了 PS 配置才需重编 FSBL。完整上板步骤见 [开发环境与构建](../../docs/开发环境与构建.md)「启动链与 BOOT.bin」。
