# pl/ — 可编程逻辑（PL）

安路 TD 工程与 RTL 源码。模块划分见 [docs/03-PL设计.md](../docs/03-PL设计.md)，IO 引脚分配见 [docs/05-硬件与接线.md](../docs/05-硬件与接线.md)。

规划布局：

```
pl/
├── rtl/            # 自研模块源码（img_*、blob_ccl、stepper_ctrl、servo_* 等）
├── ip/             # 官方 IP 与封装（mipi_rx、isp、vdma、uifdma、can_ctrl 等）
├── constrs/        # 引脚 / 时序约束
├── sim/            # 仿真 testbench
└── project/        # TD 工程（只入源文件，编译产物不入库）
```

约定：

- 遵循 `AGENTS.md` 硬性约定，尤其是寄存器映射与 PS 侧头文件同源同步。
- 自研图像算子尽量复用官方 line_buffer 流水线框架，保持 `*_valid` / `*_data` 握手风格一致。
