# pl/sim/equiv/ — 厂家 RTL 行为等价回归（golden vs 工作区）

改**非 des 的厂家明文 RTL**（`soc_base/src/{ui_isp,MIPI_V2,hdmi_tx,write_fdma}/`、顶层
`system_top.v`、`pwm.v`）时，用来证明「外部行为逐拍不变」。原理：同一个 testbench 分别接
golden RTL（固定 git rev）与工作区 RTL，各跑一遍，逐拍 trace 必须**逐字节相同**
（含 x/z 态与 cycle 数）。改 RTL 前先跑通「未改动 = golden vs 工作区 PASS」证明 test 本身有效。

## 用法

```bash
bash pl/sim/equiv/run_equiv.sh <test> [golden_rev]   # 单目标；rev 默认取 baseline.rev
bash pl/sim/equiv/run_all_equiv.sh [golden_rev]      # 全部目标，任一 FAIL 返回非 0
```

golden rev 在 [baseline.rev](baseline.rev)（本次重构的基线 = 重构前 HEAD；换一轮重构就更新它）。
`EQUIV_KEEP=1` 保留临时目录（golden 源文件 / trace / 编译日志）便于排查。
依赖：WSL + Icarus Verilog 12 + `python3`（与 `pl/sim/run_sim.sh` 同环境；python3 只用于 `~` 依赖的注释清洗）。

## testbench 约定（各域 owner 自己写）

`tb/<test>.v`：顶层模块名必须叫 `tb_<test>`。
- `$value$plusargs("TRACE=%s", f)` 取 trace 文件名，每拍 `$fdisplay` 写一行：
  自增 cycle 计数 + 被测模块**全部输出**（x/z 原样打印）；
- 激励确定（固定 `$random` 种子或固定向量）；结束时打印 `EQUIV_CYCLES <n>` 和 `EQUIV_DONE` 再 `$finish`；
- 只比接口行为，不比内部实现；**不要为"迁就"某一侧改共用 TB**。

`tests/<test>.f`：依赖清单，每行一个，`#` 起注释：
- `pl/soc_base/src/...` — RTL 依赖：golden 从 git rev 取、新版取工作区（两侧都编）；
- `@ pl/sim/equiv/stubs/xxx.v` — 加密模块 / 硬宏的行为桩，两种构建都用工作区这一份；
- `% pl/soc_base/src/...` — **仅 golden 侧**依赖：从 git rev 取、只编进 golden（给本轮删除/特化的文件用，
  例如 gamma 特化后删掉的 8 个 LUT）；
- `~ pl/soc_base/src/...` — 两侧都编（同普通路径），但 golden 副本先做**注释清洗**
  （`strip_comments.py`，字节级、块注释到第一个 `*/`）。厂家文件有 iverilog 不认的注释病
  （`top_demosaic.v` 的 `/*//input`），注释不参与仿真语义；同一文件可同时写普通行和 `~` 行；
- `+ -I<dir>` — 原样传给 iverilog 的额外参数。

改模块端口 / 参数必须 Lead 批准。本次重构批准的参数特化只有两处：
`gamma.GAMMA_10x` 只支持 2.2（isp_top 写死）、MIPI `rx_clk_gen` / HDMI `lane_lvds_10_1` 的
`DEVICE` 只支持 DR1（工程唯一可达配置）；其它模块端口/参数不变。

## 现有目标（36 项）

| 域 | test | 覆盖 |
|---|---|---|
| 顶层/IO | `pwm`、`x_pwm` | `pwm.v` 默认参数 + 缩周期实例，含中途复位；`x_` 为对抗版 |
| 顶层/IO | `x_system_top` | 整顶层：桩化 design_0 / design_top_wrapper，真实 des_* 与帧写链，200k 拍 |
| ISP 前端 | `isp_front`、`x_isp_front` | BLC / image_correction / image_cut / Brightness / contrast / Saturation / data128_96 / data96_128 / raw10_unpacket_4lane（含非实际参数实例） |
| demosaic | `demosaic`、`bilinear`、`x_demosaic`、`x_bilinear` | top_demosaic 整链（1920 宽实际参数 + 小参数回绕）、bilinear 四模式 + 非法模式 X |
| gamma | `gamma`、`x_gamma` | 特化到 2.2 后 4 像素 × RGB 全码值 |
| awb | `awb`、`x_awb` | awb + signal_delay，多帧 / 增益饱和 / x 传播 |
| MIPI | `csi_unpacket`、`mipi_dphy`、`hs_rx`、`design_top_wrapper`、`x_csi_unpacket`、`x_mipi_dphy`、`x_hs_rx`、`x_rx_clk_gen`、`x_design_top` | 明文链路；加密 D-PHY / 通道对齐用桩 |
| HDMI | `uihdmi_tx`、`hdmi_phy`、`lane_lvds`、`x_uihdmi`、`x_hdmi_phy`、`x_lane_lvds` | 明文链路；加密 TMDS 控制器用桩 |
| 帧写 | `fs_cap`、`uiFDMA`、`uidbuf_write`、`wfifo`、`x_fs_cap`、`x_uiFDMA`、`x_uidbuf_write` | 帧写 DDR；`wfifo` 是明文（非加密），uidbuf_write 直接用真实 wfifo |

`x_` 前缀是独立验证者写的对抗性 test（不同种子 / 更长激励 / 独立桩），与各域 owner 的 test 互不替代：
两者都要过。本次 36 项全部 PASS（明细与 cycle 数见
[log/2026-10-24-PL厂家RTL重构](../../../docs/log/2026-10-24-PL厂家RTL重构.md)）。

## audit/ — 可复现审计脚本与证据

`audit/` 放端口/参数契约、system_top 结构、编码、lint、工程文件一致性的检查脚本（`audit/evidence/`
是运行产物、不入库），`audit/VERIFY_platform_20261024.md` 是平台域终验报告。常用：

```bash
python3 pl/sim/equiv/check_contracts.py                 # ISP 域端口/参数契约（允许 gamma 参数特化）
python3 pl/sim/equiv/audit/check_contracts_platform.py  # 平台域契约
python3 pl/sim/equiv/audit/audit_system_top.py          # system_top 结构等价审计
python3 pl/sim/equiv/audit/check_encoding.py            # 全仓 UTF-8 无 BOM
python3 pl/sim/equiv/audit/check_prj_files.py           # 三份 TD 工程文件与磁盘一致性
bash    pl/sim/equiv/audit/check_run_all_vs_baseline.sh # 8 项功能仿真 vs 基线逐字节
bash    pl/sim/equiv/audit/run_final_verify.sh          # 终验一键：36 项 + lint + 审计
```

## 与 `run_sim.sh` 的分工

- `pl/sim/run_sim.sh` / `run_all.sh`：des 自研模块的功能 testbench，日志是行为基线，**冻结不改**；
- 本目录：厂家 RTL 的 golden-vs-新逐拍等价。两者都要过。
