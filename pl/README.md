# pl/ — 可编程逻辑（PL）

模块划分与寄存器映射见 [PL设计](../docs/PL设计.md)，IO 分配见 [硬件与接线](../docs/硬件与接线.md)，构建与打包见 [开发环境与构建](../docs/开发环境与构建.md)。

## 布局

```
pl/
├── soc_base/            # PL 工程本体：TD 6.2.1 打开 soc_prj.al 直接编译
│   │                    #   = 厂家 NPU demo 底板（MIPI / ISP / VDMA / HDMI / NPU 软核）
│   │                    #   + 我们的全部改动与自研模块；改动记录见其 README
│   ├── src/des/         #   自研模块：
│   │   ├── des_regs.v           # ✅ 寄存器堆 @0x8030_1000（ID/版本/计数/LED/帧计数/拍摄锁存），板端 v9 在用
│   │   ├── servo_bus.v          # ✅ 德晟总线主机 @+0x100，仿真 + 上板（帧时长 = 理论值；6 舵机全部应答）
│   │   ├── stepper_ctrl.v       # ✅ 步进 @+0x200，仿真 + 上板（计步精确；TB6600 实物走带）
│   │   ├── des_vision.v         # ISP 后图像路径（灰度/高斯/Sobel/二值 + ROI 统计）@+0x300，仿真 790 项过，v8 上板验证
│   │   ├── des_pwm.v            # 带尾两级翻板 PWM @+0x400（仿真 11 项过），v8 上板验证（读回 / 50Hz）；v10 去光源输出（光源手动）
│   │   ├── awb_seq_div.v        # 仿真过：ISP 白平衡逐位除法器（awb.v 改版用，省约 4k LUT）
│   │   └── io_sync.v            # ⬜ 急停锁存/对射/限位 去抖 + 统一时间戳（未接入 des 寄存器总线）
│   ├── src/constraint/  #   管脚 io.adc（CEP = BANK11 3.3V）、时序 timing.sdc
│   └── soc_prj.al       #   TD 工程入口（自研模块已登记）
├── sim/                 # testbench（des_axi / servo_bus / stepper / vision / vision2 / pwm / awb，入口 run_sim.sh；时序关键模块须能导出时间戳/计数）
└── test/                # 独立验证工程（不跑 Linux 时 JTAG 下载）
    └── step_test/       # 步进驱动器点动：100MHz 分频方波驱动 TB6600
```

## 工程组织

- `soc_base/` 是**完整可编译工程**：TD 6.2.1 打开 `soc_prj.al` → 综合 / 布局布线 → 比特流；改动直接提交，对厂家原版做了什么见 `soc_base/README.md` 改动记录表。
- **板端运行 v9；仓库源码为 v10**（v10 去 `des_pwm` 光源输出，未重编、未上板）。版本对应的模块状态见 [PL设计](../docs/PL设计.md) 模块清单。
- **统一用 TD 6.2.1 Release 175876**：与厂家比特流同版本，原样重编逐位一致已验证，不触发格式升级。2026.1 与 6.2.1 的 MSI 同 UpgradeCode、不能并存，装 6.2.1 前先卸 2026.1；许可证用 2026.1 附带那份。命令行编译见 [开发环境与构建](../docs/开发环境与构建.md)；`.bit` 用 [bootbin](../scripts/bootbin/) 打包。

## 资源预算（依据官方 P&R 报告，本机厂家资料，不入库）

- 官方 demo 基线：**LUT6 79%（41,524 / 52,480）、slice 85.8%（45,034 / 52,480）、ERAM 92.5%、DSP 59.6%（143 / 240）**，WNS −4.12ns，违规集中在跨时钟域。（slice 与 LUT6 是两个口径，引用时说清。）
- 自研模块全部加入后（v8 起）：slice 约 82%、ERAM 261/280（剩 19 块）。
- 自研 P0 模块预算 ≤4000 LUT / ≤10 ERAM：总线 UART 用小 FIFO、PL 内不做乘除法（加减速表 PS 算）、加自研跨域路径后须补 `set_clock_groups`。
- 超支砍单顺序：形态学 / CCL → overlay、tensor_pack；亚像素与视觉定位已在 PS 实现，不再占 PL。

## 约定

- 寄存器偏移与 `docs/PL设计.md`、`ps/src/hw/des_regs.h` 同一次提交更新。
- 自研图像算子（P1/P2）直接在 `soc_base/src/des/` 里写（厂家 `src/isp/` 那套 line_buffer / `matrix_3x3` 框架没接进链路，已删除），改动登记进 `soc_base/README.md` 改动记录表。
- 每个模块配 testbench；时序关键模块要能导出测试报告用的时间戳 / 计数。
