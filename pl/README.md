# pl/ — 可编程逻辑（PL）

模块划分与寄存器映射见 [PL设计](../docs/PL设计.md)，IO 分配见 [硬件与接线](../docs/硬件与接线.md)，构建与打包见 [开发环境与构建](../docs/开发环境与构建.md)。

## 布局

```
pl/
├── soc_base/            # PL 工程本体：TD 6.2.1 打开 soc_prj.al 直接编译（厂家 NPU demo 底板 + 我们的全部改动）
│   ├── src/des/         #   自研模块（偏移与状态见 PL设计）：
│   │   ├── des_regs.v           # 寄存器堆 @0x8030_1000（ID / 版本 / 计数 / LED / 帧计数 / 拍摄锁存）
│   │   ├── servo_bus.v          # 德晟总线主机 @+0x100（仿真 + 上板，6 舵机全部应答）
│   │   ├── stepper_ctrl.v       # 步进 @+0x200（仿真 + 上板，TB6600 实物走带）
│   │   ├── des_vision.v         # ISP 后图像路径（灰度 / 高斯 / Sobel / 二值 + ROI）@+0x300（已上板）
│   │   ├── des_pwm.v            # 导流挡板 PWM @+0x400（已上板；光源改手动后去掉输出）
│   │   ├── des_io.v             # 急停 / 对射 / 限位 / 编码器 @+0x500（已上板）
│   │   ├── des_485.v            # RS485 Modbus-RTU 主机 @+0x600（v16，10-25 已上板）
│   │   ├── awb_seq_div.v        # ISP 白平衡逐位除法器（替代厂家除法器 IP，省约 4k LUT）
│   │   └── des_common.vh        # 同步 / 去抖公共模块（被 des_regs / des_io 纯 include 引用，不占寄存器地址）
│   ├── src/{ui_isp,MIPI_V2,hdmi_tx,write_fdma}/  # 厂家明文 RTL（2026-10 多轮零行为重构，最近一轮见 docs/log/2026-10-28-PL第五轮精简.md；gamma 只留 2.2 一张表）
│   ├── src/constraint/  #   管脚 io.adc（CEP = BANK11 3.3V）、时序 timing.sdc
│   └── soc_prj.al       #   TD 工程入口
└── sim/                 # testbench 与仿真入口（run_sim.sh 单目标 / run_all.sh 全量 9 项，默认并行；
                         #   equiv/ 厂家 RTL golden-vs-工作区逐拍等价回归 36 项，默认并行）
```

## 工程组织

- `soc_base/` 是**完整可编译工程**：TD 6.2.1 打开 `soc_prj.al` → 综合 / 布局布线 → 比特流；改动直接提交，对厂家原版做了什么见 [soc_base/README](soc_base/README.md)。
- **板端运行版本与各模块上板状态以 [PL设计](../docs/PL设计.md) 为准**（本文件只管目录与工程组织）。
- **统一用 TD 6.2.1 Release 175876**：与厂家比特流同版本，原样重编逐位一致已验证，不触发格式升级。2026.1 与 6.2.1 的 MSI 同 UpgradeCode、不能并存，装 6.2.1 前先卸 2026.1；许可证用 2026.1 附带那份。命令行编译见 [开发环境与构建](../docs/开发环境与构建.md)；`.bit` 用 [boot](../scripts/boot/) 打包。

## 资源预算（依据官方 P&R 报告，本机厂家资料，不入库）

- 官方 demo 基线：**LUT6 79%（41,524 / 52,480）、slice 85.8%（45,034 / 52,480）、ERAM 92.5%、DSP 59.6%（143 / 240）**，WNS −4.12ns，违规集中在跨时钟域。（slice 与 LUT6 是两个口径，引用时说清。）
- 自研模块全部加入后（v15 全量重编）：slice 83.19%（43,658 / 52,480）、ERAM 261/280（剩 19 块）、DSP 65%；自研 75M / 像素 / 150M / PS AXI 域 0 违例（NPU 软核域内 −0.093ns / 42 端点属厂家加密 IP；TMDS 视觉待肉眼复核 ⚠）。
- 二次重构（`c1b94b5`，零行为精简）：综合 LUT 58,100→58,122、FF 39,305→39,244、slice 44,097→44,126、ERAM 261/280、DSP 156/240（持平），自研模块 LUT 1,851→1,774、FF 3,685→3,624；源码已全量重编并上板（v14，`pl_check.sh` 21 项 0 失败）：P&R slice 83.26%、ERAM 261/280、DSP 156/240，自研 75M / 像素 / 150M 域 0 违例。行数 / 字节与明细见 [log/2026-10-23-PL二次重构](../docs/log/2026-10-23-PL二次重构.md)、[后全量重编与上板](../docs/log/2026-10-23-PL二次重构后全量重编.md)。
- 厂家明文 RTL 重构（2026-10-24，零行为，**未重编比特流**）：非 des 明文 41 文件（33 改 + 8 删）净 **−5,351 行 / −198,191 B（−55.7% / −53.6%）**；TD 6.2.1 综合（Optimize-Gate，非 P&R）LUT **58,122→57,794（−0.56%）**、FF **39,244→39,043（−0.51%）**、ERAM 261/280、DSP 156/240 不变；36 项逐拍等价回归 + 8 项功能仿真逐字节一致。明细见 [log/2026-10-24-PL厂家RTL重构](../docs/log/2026-10-24-PL厂家RTL重构.md)。
- 第三轮 PL 重构（2026-10-24，零外部行为变化，**未重编比特流**）：自研 `des/` + 顶层 + 厂家明文 RTL 共 40 文件改动（零增删 / 零合并）净 **−1,164 行 / −28,985 B（−22.6% / −13.3%）**，`pl/sim` 框架同步精简与提速；TD 6.2.1 综合（Optimize-Gate，非 P&R）LUT **57,794→57,845（+0.09%）**、FF **39,043→38,947（−0.25%）**、slice 43,756→43,842，ERAM 261/280 / DSP 156/240 不变；36 项逐拍等价 + 8 项功能仿真逐字节一致。明细见 [log/2026-10-24-PL第三轮重构](../docs/log/2026-10-24-PL第三轮重构.md)；该源码随后全量重编并上板（**v15**，0 error、56m25s，P&R slice 83.19% / 线长 −3.8%，`pl_check.sh` 21 项 0 失败），见 [log/2026-10-24-PL第三轮重编上板v15](../docs/log/2026-10-24-PL第三轮重编上板v15.md)。
- 第四轮 PL 精简（2026-10-26，零外部行为变化，**未重编比特流**）：自研域两处落地（servo_bus 删 `idx<32` 死守卫、des_vision 手写 3FF 同步收敛到 `des_togsync`）+ tb_servo_bus 补非法 LEN 用例；其余候选（计数器 32→16、CRC / TX-RX 引擎合一、厂家 RTL 三删、des_pwm 钳位收窄）逐一评估后否决——**自研域经三轮后已到局部最优**。9 项功能仿真 + 36 项逐拍等价全过；TD 综合（Optimize-Gate）`#luts` 46,747→46,745、寄存器 / slice 估 / ERAM / DSP 持平（资源零代价）。明细见 [log/2026-10-26-PL第四轮精简](../docs/log/2026-10-26-PL第四轮精简.md)。
- 第五轮 PL 精简（2026-10-28，零外部行为变化，**未重编比特流**）：自研 `des/` + 顶层 + 厂家明文 RTL 共 39 个 RTL 文件修改（**文件集合零增删**），可编辑明文 52 文件净 **4,945→4,140 行（−805，−16.3%）/ 221,872→190,634 B（−31,238，−14.1%）**；逻辑改动只剩 `des_485` 删一处不可达项（CRC 常量特化经 syn A/B 发现 +272 LUT / +512 FF 资源回归后回退，TD 对「函数实参直接读存储器数组」不友好）；TD 综合（Optimize-Gate，非 P&R）`#luts` 46,745→46,643、`#registers` 40,172→40,163、slice 估 / ERAM / DSP 持平——**不要用行数推断 P&R 改善**；9 项功能仿真日志逐字节相同 + 36 项逐拍等价全过；另 scratch medium 档全流程重编 40m04s 收敛（place 133 个 >100% tile / route 25m28s，候选产物未上板），默认 high 构造未改。明细见 [log/2026-10-28-PL第五轮精简](../docs/log/2026-10-28-PL第五轮精简.md)。
- 自研 P0 模块预算 ≤4000 LUT / ≤10 ERAM：总线 UART 用小 FIFO、PL 内不做乘除法（加减速表 PS 算）、加自研跨域路径后须补 `set_clock_groups`。
- 超支砍单顺序：形态学 / CCL → overlay、tensor_pack；亚像素与视觉定位已在 PS 实现，不再占 PL。

## 约定

- 寄存器偏移与 [PL设计](../docs/PL设计.md)、`ps/src/hw/des_regs.h` 同一次提交更新。
- 自研图像算子直接在 `soc_base/src/des/` 里写（厂家 `src/isp/` 的 line_buffer / `matrix_3x3` 框架没接进链路，已删除），改动登记进 [soc_base/README](soc_base/README.md) 的改动摘要表。
- 改**厂家明文 RTL**（`src/{ui_isp,MIPI_V2,hdmi_tx,write_fdma}/`、`system_top.v`、`pwm.v`）必须跑 `pl/sim/equiv/`
  逐拍等价回归（golden = `pl/sim/equiv/baseline.rev`，36 项）；加密文件与 TD 生成 IP 不动。改 `pl/sim`
  框架 / testbench 后须复跑 36 项等价 + 9 项功能仿真，且 9 份功能仿真日志与基线逐字节一致。
- 每个模块配 testbench；时序关键模块要能导出测试报告用的时间戳 / 计数。
- 仿真：单目标 `bash pl/sim/run_sim.sh <目标>`；全量 `bash pl/sim/run_all.sh`（9 项逐项 PASS / FAIL，任一失败非 0，可从任意目录调用；默认 `SIM_JOBS=nproc` 并行，`SIM_JOBS=1` 串行）。等价回归 `run_all_equiv.sh` 默认 `EQUIV_JOBS=nproc`、可用 `EQUIV_ONLY='x_*'` 过滤；WSL 需 `iverilog`，环境见 [环境与上手](../docs/环境与上手.md) §1。
