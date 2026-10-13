# pl/soc_base/ — PL 工程（soc_prj，TD 6.2.1）

厂家 NPU demo `soc_prj` 底板 + 我们的全部改动，**仓库里这一份就是可直接编译的完整工程**：
TD 6.2.1 打开 `soc_prj.al` → 综合 / 布局布线 → `soc_prj.bit`。

来源：米联客/安路 NPU 演示套件 `demo.zip → demo/soc_prj/`（2025-12 版）。TD 统一用 6.2.1
（与厂家比特流同版本，原样重编逐位一致已验证）；其他版本打开会提示格式升级，产生的 diff
用 `git checkout` 还原。

## 目录内容

- `soc_prj.al` / `soc_prj.hpf` — TD 工程入口
- `design_0/` — 图形化 SoC 设计：PS + MIPI RX + ISP + VDMA + HDMI 时序 + **NPU 软核 IP**；
  `design_0.address.xml` 是 GP 口地址分配（vdmafb 0x80100000 / VDMA 0x80200000 /
  mipicam 0x80300000 / soft_npu GP1 0x1F0000000）——**我们自己的 AXI-Lite 从机
  （PL_BASE）从这里分配，避开上述段**
- `design_0/ip/` — 例化 IP 的实例化包装与 RTL（多数为安路闭源加密文件，见下）
- `al_ip/` — 安路 IP 参数（PLL、NPU_SOFT 等）
- `src/` — 明文 RTL：system_top.v、isp/、hdmi_tx/、MIPI_V2/、write_fdma/、ui_isp/、pwm.v
- `src/des/` — **自研模块**：des_regs、servo_bus、stepper_ctrl、des_vision、des_pwm、
  awb_seq_div、io_sync（已登记进 `soc_prj.al`）
- `src/constraint/` — 管脚 `io.adc`（CEP = BANK11 3.3V）、时序 `timing.sdc`
- `soc_prj_Runs/syn_1/soc_prj.prj` — 综合工程文件（`*_Runs/` 被 ignore 规则挡住，改动要 `git add -f`）

## 加密 IP

`design_0/ip/` 下 13 个 `*.enc.v` / `pragma protect` 文件是安路闭源 IP（NPU 软核、MIPI
DPHY、HDMI 控制器、AXI Matrix 等），随本工程一起编译使用。

## 寄存器总线

- 时钟：`des_clk` = `fvdma_ctrl` 的 AXI 时钟（`pll_75m`，75MHz）
- PS 地址：`0x8030_1000 + 偏移`（GP0 上 `fvdma_ctrl` 的窗口是 `0x8030_0000~0x8030_FFFF`；
  `0x0000~0x0FFF` 仍是厂家寄存器，供 `mipicam` 驱动使用，厂家写入已排除我们的地址段）
- 寄存器定义见 [docs/03-PL设计.md](../../docs/03-PL设计.md)
- 仿真：[pl/sim/tb_des_axi.v](../sim/tb_des_axi.v)（`run_sim.sh des_axi`）

## 改动记录（对厂家 soc_prj 的改动，v2~v8）

| 文件 | 改动 | 目的 |
|---|---|---|
| `src/system_top.v` | 厂家 `pwm_out` 不再接 `led[0]`；`led[0]`（板载 LED1，T19）默认由 50MHz 计数器第 27 位驱动（亮约 2.7s / 灭约 2.7s），寄存器可接管；新增 `des_regs` 寄存器堆并接到 `design_0` 引出的寄存器总线 | 验证"改 PL 源码 → 上板生效"；PS↔PL 寄存器通道 |
| `design_0/hdl/design_0.v` | `fvdma_ctrl_0` 的 AXI 读写地址线 `[3:0]` → `[15:0]`；新增端口 `des_clk / des_wr / des_waddr[11:0] / des_wdata[31:0] / des_raddr[11:0] / des_rdata[31:0]` | 用 `fvdma_ctrl` 的 64KB 窗口挂自研寄存器，免改块设计 / FSBL / 设备树 |
| `design_0/ip/design_0_fvdma_ctrl_0/design_0_fvdma_ctrl_0.v` | 地址宽 4 → 16，新端口透传 | 同上 |
| `design_0/ip/design_0_fvdma_ctrl_0/RTL/fvdma_ctrl_300e9b49f7b5.v` | 偏移 `0x1000~0x1FFF` 的读写走寄存器总线（写按 AXI 握手出单周期脉冲）；厂家寄存器写入排除该段（否则会因地址别名改写相机帧地址）；读数据二选一 | 同上 |
| `src/system_top.v`、`src/constraint/io.adc`、`soc_prj.al`、`soc_prj_Runs/syn_1/soc_prj.prj`（v2） | 接入 `servo_bus`（0x1xx）、`stepper_ctrl`（0x2xx）与帧计数；读数据按 `des_raddr[11:8]` 分路；CEP 管脚 `STEP_PUL/DIR/ENA` = Y12/Y13/T9、`ESTOP_IN` = V11（上拉）、`SERVO_BUS` = Y9（开漏）；三个 .v 登记进工程（`src/des/`） | 运动控制上板 |
| `src/system_top.v`（v3） | 帧起始改为只看 ISP `tuser` 上升沿（`isp_top` 的 `O_tuser` 是输入直通，不与 `tvalid` 对齐）；`STEP_COMMON_CATHODE=1`：PUL 高有效、ENA 高 = 脱机 | 帧计数修正；步进共阴直驱（免 74HCT245） |
| `al_ip/PLL/PLL.v`（v4） | CLKC3（`O_imx334_clk_24m` = IMX415 INCK）`DIV 44→39`、`CPHASE 43→38`、`DUTY_INT 22→20`：23.86MHz → 26.92MHz；`des_regs` VERSION=4 | 传感器寄存器是 27MHz INCK 那套，原 INCK 不对导致只有 53fps（见 docs/log/2026-10-10-相机60fps与PS骨架） |
| `src/write_fdma/uidbuf_write.v`、`src/system_top.v`（v5） | 帧写模块新增输出 `O_W_start_tgl`（真正开始写一帧时翻转）；顶层把它与完成中断 `f2p_irq` 上升沿的翻转送进 `des_regs` 的拍摄锁存（`CAP_*`），`stepper_ctrl` 新增 `pos_o` 供锁存；VERSION=5 | 跟踪表要「这一帧拍摄时刻的带位置」（docs/03 拍摄锁存） |
| `src/system_top.v`、`soc_prj.al`、`soc_prj_Runs/syn_1/soc_prj.prj`（v6） | ISP 输出与帧写模块之间插入 `des_vision`（`ud_wvs / ud_wde / ud_wdata` 改走 `vis_*`），寄存器段 0x3xx；VERSION=6。两次布线失败（slice 90%），未上板 | PL 硬件灰度 / 滤波 / 边缘 / 粗测（领域 3） |
| `src/ui_isp/awb_1_1/awb.v`（v7，新文件 `src/des/awb_seq_div.v`） | ISP 自动白平衡的两个 50 级流水线除法器 IP 换成背靠背逐位除法器（每 49 时钟一个增益），增益不再每帧复位；对照仿真 `run_sim.sh awb` | 省约 4k LUT + 5k FF：slice 90.2% → 81.7%，布线 5h 不通 → 23 分钟通过 |
| `src/system_top.v`、`src/constraint/io.adc`（v7） | 接入 `des_pwm`（0x4xx）：`FLAP1_PWM` = PIN18 Y6、`FLAP2_PWM` = PIN20 Y8、`LAMP_EN` = PIN17 Y7；`stepper_ctrl` 启动预置相位（首脉冲 1.94ms → 10.6µs）；VERSION=7 | 带尾两级翻板 + 光源；指令响应 |
| `src/constraint/timing.sdc`（v8） | **`clk_mipi` 改挂到实际的字节时钟** `u_mipi_dphy_rx_over_lvds_wrapper/u_rx_clk_gen/U_io_clk_div_1.clkdivout`，8.0ns。厂家原约束挂在不存在的硬核 MIPI 引脚上（TD：`No pins matched … defined a virtual clock`），整个像素时钟域（ISP、des_vision、帧写入）从未做过时序分析；v7 上 tuser 边沿检测因此漏帧 | 像素域时序收敛 |
| `src/system_top.v`（v8） | `des_vision` / `des_pwm` 读数据改组合逻辑（桥接逻辑同一时钟取数，打拍的读回上一个地址）；VERSION=8 | 寄存器读回正确 |

## 已知问题（编译前先看）

- 厂家最后一次 P&R：WNS **-4.120ns**，违例集中在 vtg 148.5M ↔ NPU 150M 跨时钟域
  （`timing.sdc` 只做了 clock-group 约束的一部分）。我们重编时要补约束。
- 资源基线：85.81% slice / 92.50% ERAM——加我们自己的模块前先评估余量。
- 首次编译先确认能原样复现，再动增量。

## 与 boot 打包的关系

本工程编译产物 `soc_prj.bit` 喂给 `scripts/bootbin/make_bootbin.ps1`；
FSBL/U-Boot 用仓库内零件（`scripts/bootbin/parts/`），除非改了 PS 配置才需重编 FSBL。
