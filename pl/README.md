# pl/ — 可编程逻辑（PL）

模块划分与寄存器映射见 [docs/03-PL设计.md](../docs/03-PL设计.md)，IO 分配见 [docs/05-硬件与接线.md](../docs/05-硬件与接线.md)，构建与打包见 [docs/09-开发环境与构建.md](../docs/09-开发环境与构建.md)。

## 布局

```
pl/
├── rtl/            # 自研模块源码（stepper_ctrl、servo_bus_master、io_sync、axi_lite_regs …）
├── constrs/        # 自研模块的管脚 / 时序约束片段（CEP = BANK11，3.3V）
├── sim/            # 仿真 testbench
├── soc_patch/      # 对官方 soc_prj 的改动（system_top.v、design_0 等），只放改过的源文件
└── test/           # 独立验证工程（不跑 Linux 时 JTAG 下载）
    └── step_test/  # 步进驱动器点动：100MHz 分频出方波驱动 TB6600
```

## 与官方工程的关系

- 主工程是厂家 NPU demo 的 `soc_prj`（MIPI / ISP / VDMA / HDMI / NPU 软核），**整个工程不入库**（体积大、含厂家源码）。本目录只放自研模块和对官方工程的改动，按 `soc_patch/` 里的文件覆盖回本地 `soc_prj` 即可复现。
- 官方 `soc_prj` 用 **TD 6.2.1** 打开，放在无中文、无空格的路径。
- 编译出的 `.bit` 用 [scripts/bootbin](../scripts/bootbin/) 打包成 `BOOT.bin`。

## 约定

- 寄存器偏移与 `docs/03-PL设计.md`、`ps/common/` 头文件同一次提交更新。
- 自研图像算子尽量复用官方 line_buffer / `matrix_3x3` 框架，保持 `*_valid` / `*_data` 握手风格一致。
- 每个模块配 testbench；时序关键模块（步进、总线、IO 采样）要能导出测试报告用的时间戳 / 计数。
