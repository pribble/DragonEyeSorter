# pl/ — 可编程逻辑（PL）

模块划分与寄存器映射见 [docs/03-PL设计](../docs/03-PL设计.md)，IO 分配见 [docs/05-硬件与接线](../docs/05-硬件与接线.md)，构建与打包见 [docs/09-开发环境与构建](../docs/09-开发环境与构建.md)，第三方代码边界见 [docs/10-第三方代码引用规范](../docs/10-第三方代码引用规范.md)。

## 布局

```
pl/
├── rtl/            # 自研模块（2026-10-10 统一：以已上板验证的一套为准）
│   ├── des_regs.v           # ✅ 寄存器堆 @0x8030_1000（ID/版本/计数/LED/帧计数），已上板
│   ├── servo_bus.v          # ✅ 德晟总线主机 @+0x100，仿真 + 上板（帧时长 = 理论值）
│   ├── stepper_ctrl.v       # ✅ 步进 @+0x200，仿真 + 上板（计步精确）
│   ├── io_sync.v            # ⬜ 急停锁存/对射/限位 去抖 + 统一时间戳（待接入 des 寄存器总线）
│   ├── axi_lite_regs.v      # ⛔ 已被 des_regs.v 取代（骨架，保留待删）
│   └── servo_bus_master.v   # ⛔ 已被 servo_bus.v 取代（骨架，保留待删）
├── soc_base/       # 厂家 NPU demo 底板工程 soc_prj（2026-10-09 入库，含闭源加密 IP，
│                   #   许可与使用边界见其 README；TD 2026.1 SP2 打开，接受格式升级）
├── constrs/        # TODO 自研模块管脚/时序约束片段（CEP = BANK11，3.3V）
├── sim/            # TODO testbench（时序关键模块须能导出时间戳/计数）
├── soc_patch/      # 对 soc_base 底板的改动（只放改过的源文件，覆盖回底板复现）
└── test/           # 独立验证工程（不跑 Linux 时 JTAG 下载）
    └── step_test/  # 步进驱动器点动：100MHz 分频方波驱动 TB6600
```

## 与底板工程（soc_base）的关系

- 底板 = 厂家 NPU demo `soc_prj`（MIPI / ISP / VDMA / HDMI / NPU 软核），**已入库 `soc_base/`**（2026-10-09 决定，原"工程不入库"作废；加密 IP 仅随底板使用，见其 README）。
- 本目录自研模块（rtl/、constrs/、sim/、test/）与**对底板的改动**（soc_patch/，按原相对路径镜像）分开放。复现方法：把 `soc_patch/` 覆盖回 `soc_base/` 即可；TD 6.2.2 首次打开产生的格式升级 diff 不提交。
- **统一用 TD 6.2.1 Release 175876**（2026-10-10 两人确认；取代 10-09「用 2026.1 接受格式升级」）：与厂家比特流同版本，原样重编**逐位一致**已验证，不产生格式升级。2026.1 与 6.2.1 MSI 同 UpgradeCode 不能并存，需先卸 2026.1；许可证用 2026.1 附带那份（到 2027-03-31），6.2 包附带那份已过期。命令行编译见 docs/09。编译出的 `.bit` 用 [scripts/bootbin](../scripts/bootbin/) 打包。

## 资源预算（依据官方 P&R 报告，见 reference_src/npu_demo_pr_reports/）

官方 demo 基线已用 slice 85.8% / ERAM 92.5% / DSP 59.6%（WNS −4.12ns，违规集中在跨时钟域）。
自研 P0 模块预算 ≤4000 LUT / ≤10 ERAM：总线 UART 用小 FIFO、PL 内不做乘除法（加减速表 PS 算）、
加自研跨域路径后须补 `set_clock_groups`。超支砍单顺序：亚像素 → 形态学 → overlay。

## 约定

- 寄存器偏移与 `docs/03-PL设计.md`、`ps/common/des_regs.h` 同一次提交更新（`pl_regs.h` 已被取代）。
- 自研图像算子（P1/P2）复用官方 line_buffer / `matrix_3x3` 框架时，走 soc_patch 标注 derived。
- 每个模块配 testbench；时序关键模块要能导出测试报告用的时间戳 / 计数。
- **禁止参考米联客 demo 代码拷贝**（"only for learning" 无开源许可）——学思路自己写，见 docs/10。
