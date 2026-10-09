# soc_patch — 对厂家 `soc_prj` 的改动

只放改过的文件，目录结构与 `soc_prj` 相同。使用方法：

1. 把厂家 `05-5_NPU演示/demo/soc_prj` 拷到无中文路径（例：`D:\ANLU\work\soc_prj`）
2. 用本目录的文件覆盖同名文件
3. 在 `soc_prj_Runs/syn_1`、`phy_1` 下依次运行 `td_commands_prompt.exe D:/ANLU/TD/scripts/DefaultFlow.tcl < NUL`（见 docs/09）
4. `scripts/bootbin/make_bootbin.ps1 -Bit soc_prj_Runs/phy_1/soc_prj.bit`

⚠ `design_0/` 下的文件是块设计生成物，批处理编译直接读它们；**若在 TD 图形界面里重新生成块设计，这些改动会被覆盖**，需重新打补丁。

## 改动记录

| 文件 | 改动 | 目的 |
|---|---|---|
| `src/system_top.v` | 厂家 `pwm_out` 不再接 `led[0]`；`led[0]`（板载 LED1，T19）默认由 50MHz 计数器第 27 位驱动（亮约 2.7s / 灭约 2.7s），寄存器可接管；新增 `des_regs` 寄存器堆并接到 `design_0` 引出的寄存器总线 | 验证"改 PL 源码 → 上板生效"；PS↔PL 寄存器通道 |
| `design_0/hdl/design_0.v` | `fvdma_ctrl_0` 的 AXI 读写地址线 `[3:0]` → `[15:0]`；新增端口 `des_clk / des_wr / des_waddr[11:0] / des_wdata[31:0] / des_raddr[11:0] / des_rdata[31:0]` | 用 `fvdma_ctrl` 的 64KB 窗口挂自研寄存器，免改块设计 / FSBL / 设备树 |
| `design_0/ip/design_0_fvdma_ctrl_0/design_0_fvdma_ctrl_0.v` | 地址宽 4 → 16，新端口透传 | 同上 |
| `design_0/ip/design_0_fvdma_ctrl_0/RTL/fvdma_ctrl_300e9b49f7b5.v` | 偏移 `0x1000~0x1FFF` 的读写走寄存器总线（写按 AXI 握手出单周期脉冲）；厂家寄存器写入排除该段（否则会因地址别名改写相机帧地址）；读数据二选一 | 同上 |

## 寄存器总线

- 时钟：`des_clk` = `fvdma_ctrl` 的 AXI 时钟（`pll_75m`，75MHz）
- PS 地址：`0x8030_1000 + 偏移`（GP0 上 `fvdma_ctrl` 的窗口是 `0x8030_0000~0x8030_FFFF`；`0x0000~0x0FFF` 仍是厂家寄存器，供 `mipicam` 驱动使用）
- 寄存器定义见 [docs/03-PL设计.md](../../docs/03-PL设计.md)
- 仿真：[pl/sim/tb_des_axi.v](../sim/tb_des_axi.v)（`run_sim.sh des_axi`）
