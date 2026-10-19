# PL 设计

> 构建方法见 [开发环境与构建](开发环境与构建.md)；实测数据见 [docs/log](log/README.md)。

## 基线

在官方 NPU demo 工程 `soc_prj`（MIPI RX + ISP + VDMA + HDMI + NPU 软核，已过 P&R）上叠加自研模块；顶层 `src/system_top.v`，PS 与 AXI 互联在块设计 `design_0`。

**板端运行 v12**（10-19 上板，BOOT `a026ead4…`，`BOOT.bin.prev` = v11 `89fb03c8…`，v12 = v11 + PS CAN0 引到 F20 / F19；75MHz +0.102ns、像素时钟 +0.102ns、HDMI 742.9MHz +0.041ns，pl_check 0 失败）。v11 记录：（10-18 上板，BOOT md5 `89fb03c8…`，`BOOT.bin.prev` = v9 `e3a9d500…`；v10 = des_pwm 去光源输出，未单独编译；v11 = v10 + `des_io`）。布局布线 25 分钟；自研时钟域时序：75MHz +0.137ns、像素时钟 +0.103ns，HDMI 串行器 742.9MHz −0.066ns（v8 −0.338ns 目视无问题、v9 +0.120ns，厂家路径随布局浮动），其余违例在厂家 NPU / 150M 域、与厂家原版相同。`pl_check.sh` 21 项全过（des_io ID、急停未锁存、编码器静止不跳 + 原有各项，60.02fps 0 跳帧，步进首脉冲 10.6µs）。

**✅ CAN 已在 v12 引到管脚（10-19 查出、当晚上板）**：底板 CAN 收发器接 PL 管脚 F20（TX）/ F19（RX），RS485 接 L17 / N16 / N15（硬件手册 p21）。官方 NPU demo 块设计没开 PS CAN（`design_0/Design.xml` CAN0_Available=0；`ARM_Processor_System_*.v` 里 `can0_rx_in` 接 `1'b0`、`can0_tx_out` 悬空），io.adc 也没约束这几脚 → Linux `can0` 只能内部回环，接 USB-CAN 实物不会通；v12（`a026ead4…`）：PS 硬核 `can0_tx_out` / `can0_rx_in`（TD `arch/dr1_macro.v`）经 `ARM_Processor_System_9c4afc9a72e9.v` → `design_0_ARM_Processor_System_0.v` → `design_0.v` → `system_top`（`CAN0_TXD` / `CAN0_RXD`）引到 F20 / F19（LVCMOS33，RX 上拉）。单节点验证：发帧后发送错误计数停在 128、状态停在 ERROR-PASSIVE、不进 BUS-OFF（没人应答的典型现象；v11 时计数一直是 0，因为接收脚接 0，控制器等不到总线空闲）→ 发送 / 接收经收发器的回路已通；双节点实测等 J11 插头（KF2EDGK-3.81-6P）。注意：① ipms_canfd 驱动的错误帧把 ACK 位置码 0x19 写进了 data[2]，candump 会显示成 bit-error，别被误导；② 「开机拒绝经典模式」与接收脚无关：`ip link … fd off` 直接返回 EINVAL，驱动只支持 FD 模式，FD 控制器收发经典帧不受影响（`can_up.sh` 的退回 FD 模式照用）；③ 底板连接器管脚表（`08_框图及pinlist` B2B 表，20250526）把 CAN 写成 M14 / M15、把 F19 / F20 写成 HDMI，与硬件手册和官方 io.adc（HDMI 在 M14 等）矛盾，以手册 + 实测为准，那份表是旧的。RS485 三线（L17 / N16 / N15）尚未引出（要先定 PL UART 还是 PS UART 经 EMIO）。

### 官方源码可复用性（`soc_prj/src/` 清点）

- **明文可改（在用的）**：`ui_isp/` 整套 ISP（含 `image_cut`、`bilinear_interpolation.v`、白平衡 `awb.v`）、`write_fdma/uiFDMA.v` + `uidbuf_write.v`（帧写 DDR）、`MIPI_V2/src/csi_unpacket.v` + `design_top_wrapper.v`、`MIPI_V2/mipi_rx/`（`rx_clk_gen.v`、`hs_rx_wrapper.sv`、D-PHY 包装）、`hdmi_tx/`（`uihdmi_tx.v`、`hdmi_phy_warpper.v`、`lane_lvds_10_1.v`）、`pwm.v`（厂家 AXI PWM，块设计里 3 个实例，输出只驱动 `vtg_lock` / `axis_to_video_lock` 状态位）、`design_0` 里的 AXI4-Lite 从机寄存器模板（`fvdma_ctrl`，即我们改过的寄存器桥）。
- **加密（改不了）**：共 15 个 `pragma protect` 文件 —— NPU 软核、VDMA（含 2 个 `.vh`）、视频时序发生器、AXI Matrix ×3 / 协议转换 ×2 / Stream-to-Video Out、HDMI 控制器、MIPI D-PHY 两条数据通道、`write_fdma/wfifo.v`（清单见 [pl/soc_base/README](../pl/soc_base/README.md)）。
- **已删（未被例化的厂家源码）**：`src/isp/` 的厂家 Sobel / 3×3 / 行缓存 / 灰度均值等 7 个文件、`src/MIPI_V2/src/` 的 RAW8 老解包路径与第二套 AWB（7 个文件）、白平衡用的厂家流水线除法器 `divider`（1.3 MB）；理由与验证见 [log/2026-10-17-清理未用的厂家PL源码](log/2026-10-17-清理未用的厂家PL源码.md)。
- **官方没有、我们的贡献点**：运动控制全家桶（步进脉冲 + 加减速、舵机总线主机、QEI、帧锁存、IO 同步）、二值 / 形态学 / CCL / 投影测量 / ROI 缩放 / 张量打包、AXI 寄存器堆。

## 模块清单

| 模块 | 职责 | 来源 | 优先级 | 状态与上板证据 |
|---|---|---|---|---|
| `mipi_rx` + `isp` + `vdma` + `hdmitx` | 采集与显示 | 官方 | P0 | ✅ 官方基线 |
| `des_regs`（含 `enc_frame_latch`） | 寄存器窗口 + 自由计数器 + 帧计数 + 拍摄锁存带位置 | 官方模板 + 自研 | P0 | ✅ v5 加入；板端 v9 在用（拍摄锁存实测，[log/2026-10-10-相机60fps与PS骨架](log/2026-10-10-相机60fps与PS骨架.md)） |
| `stepper_ctrl` | 步进脉冲 + 梯形 / S 曲线加减速 + 速度 / 位置模式 + 已发脉冲计数 | 自研 | P0 | ✅ v9 在板；TB6600 + 57 步进走带、3200 步定位准确（[log/2026-10-16-传送带首次上电](log/2026-10-16-传送带首次上电.md)） |
| `servo_bus`（= `servo_bus_master`） | 半双工 UART 115200 + 德晟 v4.01 帧 + SYNC WRITE / REG WRITE + ACTION + 位置 / 电流 / 温度轮询 | 自研 | P0 | ✅ v2 加入，板端 v9；6 个舵机全部应答（[log/2026-10-17-舵机总线首次联通](log/2026-10-17-舵机总线首次联通.md)） |
| `des_io`（取代未接入的 `io_sync`） | 急停去抖 + 锁存（锁存期间步进停住）、对射 / 限位去抖 + 边沿捕获（时间戳 / 带位置 / 编码器计数）、编码器 4 倍频计数 + 滤波 + Z 相 + 测速 | 自研 | P0 / P1 | ✅ v11 上板（仿真 28 项；板上读 ID / 状态正常）；⬜ 急停实按、编码器 / 对射实物 |
| `des_pwm` | 两个侧边导流挡板的舵机 PWM，按带位置触发 | 自研 | P1 | ✅ v8 上板验证（读回 / 50Hz，未接舵机）；v10 源码去光源输出、未上板 |
| `des_vision` | 边缘 / 二值显示 + ROI 投影测尺寸 | 自研 | P1 | ✅ v8 上板，6 种模式实画面验证、统计读回正确（[log/2026-10-11-软件闭环_NPU_界面_PL视觉](log/2026-10-11-软件闭环_NPU_界面_PL视觉.md)） |
| `rs485_modbus` | Modbus-RTU 主站（寄存器窗口改 `0x6xx`） | 自研 | P1 | ⬜ 未做（网口 Modbus-TCP 从站已在 PS 侧做完，见软件与UI） |
| `awb_seq_div` | ISP 自动白平衡逐位除法器（替换厂家流水线除法器） | 自研 | P0 | ✅ v7 加入，省约 4k LUT |
| `img_morph` / `blob_ccl` / `roi_scaler` / `tensor_pack` / `overlay` | 形态学、连通域、ROI 缩放、张量打包、叠加 | 自研 | P2 | ⬜ 未做（大块图像算法优先放 PS） |
| `traj_interp` | 多轴时间同步插补（115200 带宽承载不了高频插补点） | 自研 | P2 | ⬜ 未做 |
| `servo_pwm_x4` | 机械臂 PWM 驱动对比实验（需改星型布线） | 自研 | P2 | ⬜ 未做 |

> 板端 v9 含 v8 及以前全部模块；表中的 v5 / v8 是模块加入版本，不是当前板端版本（源码 v10 未上板）。
>
> CAN 主线走 PS SocketCAN（内核已有 `can0` / `can1`），PL 零工作量；TD 自带 CAN IP 备选。协议与实测见 [软件与UI](软件与UI.md)。

### 实现约束

- 寄存器读多路选择须为组合逻辑：`fvdma_ctrl` 桥按 1 拍取数，打 1 拍则 `0x3xx` 读回全是 THR。
- `clk_mipi` 在 `timing.sdc` 里约束到 `U_io_clk_div_1.clkdivout`（8.0ns），实测像素时钟 111.1MHz；厂家原 SDC 写的是硬 MIPI PHY 引脚名，对本设计（LVDS IO）不生效（TD 报 `No pins matched`）。

### 资源

- 官方 demo 基线：LUT6 79%（41,524 / 52,480）/ slice 85.8%（45,034 / 52,480）/ ERAM 92.5% / DSP 59.6%（143 / 240），WNS −4.12ns；NPU 软核独占 2.24 万 LUT + 156 ERAM。LUT6 与 slice 是两个口径，引用时说清（见 [pl/README](../pl/README.md) 资源预算）。
- v6 加入 `des_vision` 后 slice 87.8% → 90.2%，两次布线失败；ISP 白平衡改逐位除法器（v7）后 slice 81.7%，现约 82%（10-13 全量重编 82.41%），ERAM 261/280（剩 19 块）。
- 不再加大模块；仍不够时 `des_vision` 用 `LANES=2`（滤波逻辑减半，见 `des_vision` 一节）。
- v6 布局后的资源大头（`phy_1/soc_prj_phy.area` 层次表）：

| 模块 | LUT | 加法链 | FF | ERAM |
|---|---|---|---|---|
| NPU 软核 `Video_NPU` | 22557 | 6220 | 23741 | 156 |
| ↳ YOLO 后处理 `u_soft_yolo` / 缩放 `u_resize_top` | 3051 / 3413 | 930 / 1397 | 4934 / 3090 | 31 / 66 |
| **ISP 自动白平衡 `u_awb`**（两个 50 级流水线除法器 IP 占 2074 / 2168 / 5516） | 2341 | 2402 | 6168 | 10 |
| 我们的 `des_vision` / `servo_bus` / `stepper_ctrl` | 932 / ~500 / 299 | 229 / — / 434 | 1611 / — / 334 | 2 / 0 / 0 |

- ISP 白平衡改逐位除法器（`pl/soc_base/src/des/awb_seq_div.v`，改 `pl/soc_base/src/ui_isp/awb_1_1/awb.v`）：厂家两个「每时钟出一个商」的流水线除法器改为背靠背逐位除（每 49 时钟一个新增益），增益与原版差 ≤1 步；不再每帧开头把增益复位成 1.0。对照仿真 `run_sim.sh awb`（原版 + 厂家原语行为模型 vs 改版，12 帧随机彩色）：数据有效 / 帧起始 / 行尾逐拍一致，像素差 ≤2，白平衡正常。省约 4k LUT + 2k 加法链 + 5k FF。
- NPU 软核里的 YOLO 后处理 / 缩放单元理论上可省（我们是 ROI 分类 + PS 预处理），但 ArmNN Alnpu 后端是否依赖不确定、风险大，不动。

## 寄存器通道（已实现）

不改块设计：借用 GP0 上 `fvdma_ctrl`（`mipicam`）的 64KB 窗口 `0x8030_0000~0x8030_FFFF`，把它的地址线从 4 位加宽到 16 位，**偏移 `0x1000~0x1FFF` 引出到 PL 顶层的寄存器总线**（`des_clk` 75MHz / `des_wr` 单周期写脉冲 / `des_waddr` / `des_wdata` / `des_raddr` / `des_rdata`）。`0x0000~0x0FFF` 仍是厂家寄存器，`mipicam` 驱动照常使用；厂家寄存器的写入已排除我们的地址段。改动文件与原理见 [pl/soc_base](../pl/soc_base/)，仿真 [pl/sim/tb_des_axi.v](../pl/sim/tb_des_axi.v)。

**`PL_BASE` = `0x8030_1000`**，已实现的寄存器（`des_regs`，在 `system_top.v` 里）：

| 偏移 | R/W | 名称 | 说明 |
|---|---|---|---|
| 0x000 | R | `DES_ID` | 固定 `0x44455301`（"DES" + 版本 01） |
| 0x004 | R | `DES_VERSION` | 见下表 |
| 0x008 | R/W | `SCRATCH` | 读写测试 |
| 0x00C | R | `FREE_CNT` | 75MHz 自由计数器（硬件时间戳 / 延迟测量） |
| 0x010 | R/W | `LED_CTRL` | b0 = 接管 LED1，b1 = LED1 亮灭 |
| 0x014 | R | `FRAME_CNT` | ISP 输出帧起始计数（只看 tuser 上升沿） |
| 0x018 | R | `FRAME_TS` | 最近一次帧起始时的 `FREE_CNT` |
| 0x01C | R | `FRAME_PERIOD` | 最近两次帧起始的间隔（75MHz 计数；60fps ≈ 1,250,000） |
| 0x020 | R | `CAP_SEQ` | 已完成的 DMA 取帧次数 |
| 0x024 | R | `CAP_FCNT` | 写模块开始写这一帧时的 `FRAME_CNT` |
| 0x028 / 0x02C | R | `CAP_T0` / `CAP_T1` | 开始写 / 完成中断时的 `FREE_CNT` |
| 0x030 / 0x034 | R | `CAP_POS0` / `CAP_POS1` | 开始写 / 完成中断时的带位置（步进 `POS`，以后换编码器） |
| 其余 | R | — | 读回 `0xDEADBEEF` |

`DES_VERSION` 编码（PS 按它判断模块是否存在）：

| 值 | 含义 |
|---|---|
| 1 | 仅寄存器通道 |
| 2 | 接入舵机总线 + 步进 |
| 3 | 帧起始修正、步进共阴 |
| 4 | 相机 INCK 改 27MHz（60fps） |
| 5 | 拍摄锁存 + 步进 BRAKE |
| 6 | `des_vision` |
| 7 | `des_pwm` + AWB 除法器 |
| 8 | 读多路选择 + `clk_mipi` 修正 |
| 9 | 步进 `TARGET == POS` 就地停 |
| 10 | `des_pwm` 去光源输出（源码，未上板） |

**拍摄锁存**：厂家帧写模块 `uidbuf_write` 真正开始写一帧时翻转 `O_W_start_tgl`，完成中断 `f2p_irq` 上升沿再翻转一次，两路翻转经两级同步进 75MHz 域。开始时先锁到暂存，完成时才整组更新 `CAP_*` → PS 在 `WaitFinish` 返回后有整整一帧时间去读，不会和下一帧抢。PS 用 `CAP_POS0/1` 按件所在图像行插值，得到该行读出时刻的带位置（卷帘快门）。

### 舵机总线主机 `servo_bus`（`PL_BASE + 0x100`）

[pl/soc_base/src/des/servo_bus.v](../pl/soc_base/src/des/servo_bus.v)：115200 8N1 单线半双工，**开漏发送**（只拉低，高电平靠 BSS138 模块上拉），发送期间关闭接收（不收回声），每次传输后强制留 1 字节时间（约 87µs）的总线换向间隔。PS 只填帧主体，`FF FF` 帧头和校验和由硬件补。

| 偏移 | R/W | 说明 |
|---|---|---|
| 0x100 | W | `CTRL`：b0 发送（自清零）/ b1 等应答 / b2 清状态和计数 / b[14:8] 帧主体字节数（≤64） |
| 0x104 | R | `STATUS`：b0 忙 / b1 完成 / b2 超时 / b3 接收错（校验、长度、帧错）/ b4 应答正确 / b[14:8] 收到字节数 / b[23:16] 舵机状态字节 |
| 0x108 | R/W | 应答超时，单位 µs（默认 3000，从发送结束起算） |
| 0x10C / 0x110 / 0x114 | R | 时间戳（75MHz 计数）：首个起始位 / 最后停止位结束 / 应答收完 —— 测试报告的帧时长与同步误差 |
| 0x118 / 0x11C / 0x120 | R | 计数：超时次数 / 接收错误次数 / 已发帧数 —— 总线稳定性统计 |
| 0x140~0x17F | W/R | 发送缓冲 64 字节：帧主体 `[ID, LEN, INSTR, 参数…]`，字节 i 在第 i/4 个字的 `[8*(i%4)+:8]` |
| 0x180~0x19F | R | 接收缓冲 32 字节：应答去掉 `FF F5` 后的 `[ID, LEN, STATUS, 参数…, CHK]` |

- 仿真 [pl/sim/tb_servo_bus.v](../pl/sim/tb_servo_bus.v)（`run_sim.sh servo_bus`）用协议文档原例当标准答案：PING、READ 位置、SYNC WRITE、无应答超时、错误校验，19 项全过；SYNC WRITE 18 字节实测线上时长 1562µs，与理论值一致。
- ⚠ 协议文档 2.3.6 的 SYNC WRITE 例子校验值印的是 `0x8B`，按文档自己的公式算应为 `0xBA`（其余 5 个例子都与公式相符）。硬件按公式算；真舵机同步写无反应时先查这里，实测 `0xBA` 正确（[log/2026-10-17-舵机总线首次联通](log/2026-10-17-舵机总线首次联通.md)）。

### 步进控制 `stepper_ctrl`（`PL_BASE + 0x200`）

[pl/soc_base/src/des/stepper_ctrl.v](../pl/soc_base/src/des/stepper_ctrl.v)：

- **模式**：速度模式（传送带连续走、改速度自动平滑过渡）+ 位置模式（梯形加减速，停在精确步数）。
- **速度表示**：每时钟前进的步数，Q0.32 定点；`inc = v[步/s] × 2^32 / 75e6`，单位换算由 PS 做；升降速节拍 10kHz。
- **输出极性**：按 TB6600 共阳接法（PUL 低有效、脉冲 5µs；换向后等 10µs 再发脉冲；ENA 使能时输出高）；顶层 `STEP_COMMON_CATHODE=1` 反相为共阴直驱（PUL 高有效，ENA 高 = 脱机），见 [硬件与接线](硬件与接线.md)。
- **运动中改写 `TARGET`**：传送带「停在某步」的做法（减速点自动重算）；目标落在运动方向身后（写得太晚）时减速停在目标之后（`AT_TARGET` = 0），不卡在 moving；改写成恰好等于当前位置则就地停（`AT_TARGET` = 1，v9 起）。
- **停止**：`estop` 输入或写 HALT 立即停脉冲（不减速），置 HALTED。
- **速度上限**：按每秒 5 万步用（5µs 脉冲需 10µs 周期）。

| 偏移 | R/W | 说明 |
|---|---|---|
| 0x200 | W/R | `CTRL`：b0 使能 / b1 启动（脉冲）/ b2 减速停（脉冲）/ b3 立即停（脉冲）/ b4 模式（0 速度，1 位置）/ b5 位置清零（脉冲）/ b6 速度模式方向 |
| 0x204 | R | `STATUS`：b0 运动中 / b1 方向 / b2 到位 / b3 已急停 / b4 已使能 |
| 0x208 | R/W | `TARGET` 目标位置（有符号，步） |
| 0x20C / 0x210 / 0x214 | R/W | `VMAX_INC` 巡航速度 / `ACC_INC` 每节拍速度变化 / `VMIN_INC` 起停速度 |
| 0x218 / 0x21C / 0x220 | R | `POS` 当前位置（有符号）/ `CUR_INC` 当前速度 / `STEPS` 累计步数 |
| 0x224 | R | `BRAKE`：从当前速度减速到停需要的步数（PS 判断「还来不来得及停」） |

仿真 [pl/sim/tb_stepper_ctrl.v](../pl/sim/tb_stepper_ctrl.v)（`run_sim.sh stepper`）用独立脉冲监视器核对：+1000 步与 −200 步精确到位、换向建立时间 ≥10µs、脉宽 5.0µs、最高速不超、速度模式减速停、HALT / 急停后零脉冲、ENA 极性；巡航中 BRAKE > 0、运动中改写目标精确停住、目标改到身后减速停（越过步数 = BRAKE，减速时长 ≥1ms，不许急停）不卡死；运动中目标改成当前位置 → 停住、到位、可再次启动。

### PL 视觉 `des_vision`（`PL_BASE + 0x300`，✅ v8 上板验证）

[pl/soc_base/src/des/des_vision.v](../pl/soc_base/src/des/des_vision.v)：插在 ISP 输出（128 位 AXI-Stream，每拍 4 个 BGRA 像素，MIPI 字节时钟）与写帧 DMA 之间，不反压。对应赛题领域 3：简单级「单路 1080P 图像 PL 采集；**硬件灰度滤波**」、中级「**PL 硬件边缘检测、尺寸粗测量**」。

- 灰度 BT.601（77R+150G+29B）>>8；一块 ERAM 存两行（480×64 位）组成 3×3 窗口；高斯 [1 2 1;2 4 2;1 2 1]/16、Sobel |Gx|+|Gy|，加法树拆 3 级流水。
- **显示模式**（下一帧生效，HDMI 60fps 直接看到）：0 原图（1 拍直通）/ 1 灰度 / 2 高斯 / 3 二值（高斯 ≥ 阈值）/ 4 Sobel（饱和到 255）/ 5 边缘（Sobel ≥ 阈值）；处理模式 6 拍延迟、alpha=0。3×3 结果落在中心左一像素、上一行（不加整行延迟，写帧时序不变）。输出源在帧起始那一拍按新模式选，每个帧起始恰好传给写帧模块一次。
- **ROI 统计**（每帧起始发布）：二值图在 ROI（或全帧）内的面积、外接框、Σx / Σy（质心）、边缘像素数、每帧像素时钟数。PS 每帧把 ROI 设到离光轴最近的零件 → 每件「PL 粗测」（面积等效直径、外接框 mm）进记录，与 PS 亚像素测量并排。
- 仿真 [pl/sim/tb_des_vision.v](../pl/sim/tb_des_vision.v)（`run_sim.sh vision`，32×12 帧、tvalid 随机空拍）：6 种模式逐像素对行为模型、统计量、ROI、写到一半的设置等下一帧、帧起始计数，790 项全过。
- **参数 `LANES`**（布线后备）：4 = 每个像素（默认，行为不变）；2 = 每拍只处理第 0、2 个像素，滤波 / 统计逻辑约减半，处理图 960 宽、每个结果显示在 2 个像素上，统计按处理的像素计（PS `des_vis_read` 按 ACTIVE[27:24] 把面积 / 边缘数 ×4/LANES）。`run_sim.sh vision2` 同样 790 项全过。
- 资源（综合）：约 1k LUT + 2 ERAM + 13 DSP。

| 偏移 | R/W | 说明 |
|---|---|---|
| 0x300 | R/W | `CTRL`：[2:0] 模式，[8] 全帧统计（忽略 ROI） |
| 0x304 | R/W | `THR`：[7:0] 二值阈值，[26:16] 边缘阈值 |
| 0x308 / 0x30C | R/W | `ROI_X` / `ROI_Y`：[10:0] 起，[26:16] 止（像素 / 行） |
| 0x310 | W | `APPLY`：以上设置在下一帧起始生效 |
| 0x314 | R | `ID` = 0x56495331 |
| 0x320 | R | `SEQ` 已发布统计的帧数 |
| 0x324 / 0x338 | R | `AREA` 亮像素数 / `EDGES` 边缘像素数 |
| 0x328 / 0x32C | R | `BBOX_X` / `BBOX_Y`：[10:0] 最小，[26:16] 最大 |
| 0x330 / 0x334 | R | `SUMX` / `SUMY`（质心 = SUM / AREA） |
| 0x33C | R | `CYCLES` 每帧像素时钟数 |
| 0x340 | R | `ACTIVE` 正在用的设置：[2:0] 模式、[8] FULL、[23:16] 阈值、[27:24] LANES |

### 导流挡板 `des_pwm`（`PL_BASE + 0x400`，✅ v8 上板验证，未接舵机）

[pl/soc_base/src/des/des_pwm.v](../pl/soc_base/src/des/des_pwm.v)：2 路 50Hz 舵机 PWM（两个侧边导流挡板，MG996R；通道 = 挡板序号）。脉宽单位 µs，新值在下一周期开头生效（不出半截脉冲），钳位 2550µs；周期计数供 PS 确认已生效。对应领域 3 中级「NG 不良品输出 IO 控制；不良品实时分拣触发」。仿真 [pl/sim/tb_des_pwm.v](../pl/sim/tb_des_pwm.v) 11 项过。

- 管脚：#16 `FORK_PWM` = PIN18 Y6、#18 `FLAP2_PWM` = PIN20 Y8。
- PS 侧 `src/sched/flap`：零件到拦截点前 110mm 合上、滑出后 190mm 打开（`des_app.json` 的 `flaps.prepare_mm` / `clear_mm`，见 [技术文档](技术文档.md) §3.5 与 §4.4）。
- 光源为**人工手动**开关 / 调光，不接 PL：v10 源码删掉 `CTRL` b8、`lamp` 端口与 `LAMP_EN` 管脚（PIN17 Y7 释放为空闲，见 [log/2026-10-17-des_pwm去掉光源](log/2026-10-17-des_pwm去掉光源.md)）；板端 v9 仍带 b8。

| 偏移 | R/W | 说明 |
|---|---|---|
| 0x400 | R/W | `CTRL`：[1:0] 通道使能（v10 起不再有 b8 光源位） |
| 0x404 | R/W | `PERIOD` µs（默认 20000，最小 2600） |
| 0x408 / 0x40C | R/W | `CH0_US` / `CH1_US` 脉宽 µs |
| 0x410 | R | `CNT` 已开始的周期数 |
| 0x414 | R | `ID` = 0x50574D31 |

### 现场输入 `des_io`（`PL_BASE + 0x500`，v11）

[pl/soc_base/src/des/des_io.v](../pl/soc_base/src/des/des_io.v)，取代从未接入的 `io_sync.v`（只有两拍同步、没有去抖、PS 读不到）：
- **急停**（PIN15）：按下保持 1ms 才算（去抖），**锁存**；锁存信号接步进 `estop`（不发脉冲、拒绝启动）。清除只能由 PS 写 `CTRL.b0`，按钮仍按着时清除无效。PS 在任何状态都能读到（`Runner` 进急停态；复位报警时清锁存）。常闭触点硬件断电不变。
- **IR1 对射**（PIN9）/ **LIMIT1**（PIN12）：去抖 20µs；IR1 每次「挡光」边沿锁存自由计数器、步进位置、编码器计数 → 跟踪误差 = 预测前沿 vs 实测前沿。极性可配（`CFG`）。
- **编码器**（PIN6~8，E6B2-CWZ6C 类 NPN 集电极开路）：两级同步 + 8 拍（约 107ns）滤波，4 倍频（A 超前 B 计正，方向由 PS 配置换算），A/B 同时跳变计为错误，Z 相下降沿锁存计数，10ms 窗口测速。管脚开内部上拉（不接电阻也能用，长线 / 干扰大时外加 4.7k）。
- 仿真 [pl/sim/tb_des_io.v](../pl/sim/tb_des_io.v)（`run_sim.sh io`，1MHz 时钟缩放）28 项：急停 0.5ms 抖动不锁、1.1ms 锁存、按住时清除无效、松开后清除；编码器正反计数、2 拍毛刺不计、双相同跳记错误、Z 锁存、正反测速；IR1 5µs 闪烁不计、挡光边沿锁存位置 / 编码器、极性、限位。
- 板上（10-18）：`desreg io` → `estop ok  IR1 clear  LIMIT1 off  enc 0 (err 0)  A1 B1 Z1`（未接编码器，三相被内部上拉到 1）。⬜ 急停按下 → 锁存 + 界面「急停」、旋开后「复位报警」才清；编码器到货后正反转计数 / 测速对皮带步数。

## 0x5xx 起扩展登记

分段固定：`0x0xx` des_regs / `0x1xx` servo_bus / `0x2xx` stepper_ctrl / `0x3xx` des_vision / `0x4xx` des_pwm / `0x5xx` des_io；窗口 `0x8030_1000~0x8030_1FFF`（4KB），新模块从 `0x6xx` 起。**实现前先在表里登记偏移**，并与 `ps/src/hw/` 头文件同一次提交更新。

| 偏移 | 模块 | 说明 |
|---|---|---|
| `0x5xx` | `des_io`（v11） | 寄存器表在 [pl/soc_base/src/des/des_io.v](../pl/soc_base/src/des/des_io.v) 文件头，PS 侧 `ps/src/hw/des_regs.h` 的 `DES_IO_*` |

规划中的 `rs485_master` 改占 `0x6xx`（结构同 `servo_bus`，见 [软件与UI](软件与UI.md) 工业总线一节），实现时在此登记具体偏移。
