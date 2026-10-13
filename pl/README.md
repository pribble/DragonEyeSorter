# pl/ — 可编程逻辑（PL）

模块划分与寄存器映射见 [docs/03-PL设计](../docs/03-PL设计.md)，IO 分配见 [docs/05-硬件与接线](../docs/05-硬件与接线.md)，构建与打包见 [docs/09-开发环境与构建](../docs/09-开发环境与构建.md)。

## 布局

```
pl/
├── soc_base/            # PL 工程本体：TD 6.2.1 打开 soc_prj.al 直接编译
│   │                    #   = 厂家 NPU demo 底板（MIPI / ISP / VDMA / HDMI / NPU 软核）
│   │                    #   + 我们的全部改动与自研模块；改动记录见其 README
│   ├── src/des/         #   自研模块：
│   │   ├── des_regs.v           # ✅ 寄存器堆 @0x8030_1000（ID/版本/计数/LED/帧计数/拍摄锁存），已上板 v5
│   │   ├── servo_bus.v          # ✅ 德晟总线主机 @+0x100，仿真 + 上板（帧时长 = 理论值）
│   │   ├── stepper_ctrl.v       # ✅ 步进 @+0x200，仿真 + 上板（计步精确）
│   │   ├── des_vision.v         # ISP 后图像路径（灰度/高斯/Sobel/二值 + ROI 统计）@+0x300，仿真 790 项过，v8 上板验证
│   │   ├── des_pwm.v            # 带尾两级翻板 PWM + 光源 @+0x400（仿真 11 项过），v8 上板验证（读回 / 50Hz）
│   │   ├── awb_seq_div.v        # 仿真过：ISP 白平衡逐位除法器（awb.v 改版用，省约 4k LUT）
│   │   └── io_sync.v            # ⬜ 急停锁存/对射/限位 去抖 + 统一时间戳（待接入 des 寄存器总线）
│   ├── src/constraint/  #   管脚 io.adc（CEP = BANK11 3.3V）、时序 timing.sdc
│   └── soc_prj.al       #   TD 工程入口（自研模块已登记）
├── sim/                 # testbench（des_axi / servo_bus / stepper / vision / pwm / awb，入口 run_sim.sh；时序关键模块须能导出时间戳/计数）
└── test/                # 独立验证工程（不跑 Linux 时 JTAG 下载）
    └── step_test/       # 步进驱动器点动：100MHz 分频方波驱动 TB6600
```

## 工程组织

- 仓库里的 `soc_base/` 就是**完整可编译工程**：TD 6.2.1 打开 `soc_prj.al` → 综合 / 布局布线 → 生成比特流。改动直接提交，历史用 git 看；对厂家原版逐条做了什么，见 `soc_base/README.md` 的改动记录表。
- **统一用 TD 6.2.1 Release 175876**（2026-10-10 两人确认）：与厂家比特流同版本，原样重编**逐位一致**已验证，不产生格式升级。2026.1 与 6.2.1 MSI 同 UpgradeCode 不能并存，需先卸 2026.1；许可证用 2026.1 附带那份（到 2027-03-31），6.2 包附带那份已过期。命令行编译见 docs/09。编译出的 `.bit` 用 [scripts/bootbin](../scripts/bootbin/) 打包。

## 资源预算（依据官方 P&R 报告，见 reference_src/npu_demo_pr_reports/）

官方 demo 基线已用 slice 85.8% / ERAM 92.5% / DSP 59.6%（WNS −4.12ns，违规集中在跨时钟域）。
自研 P0 模块预算 ≤4000 LUT / ≤10 ERAM：总线 UART 用小 FIFO、PL 内不做乘除法（加减速表 PS 算）、
加自研跨域路径后须补 `set_clock_groups`。超支砍单顺序：亚像素 → 形态学 → overlay。

## 约定

- 寄存器偏移与 `docs/03-PL设计.md`、`ps/common/des_regs.h` 同一次提交更新。
- 自研图像算子（P1/P2）复用官方 line_buffer / `matrix_3x3` 框架时，直接在 `soc_base/src/` 里改，改动登记进 `soc_base/README.md` 的改动记录表。
- 每个模块配 testbench；时序关键模块要能导出测试报告用的时间戳 / 计数。
