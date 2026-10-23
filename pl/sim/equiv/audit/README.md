# pl/sim/equiv/audit/ — 平台域独立验证脚本与证据（verifier-platform）

本目录是**独立验证者**（verifier-platform）为「本轮厂家 RTL 重构」平台域
（`MIPI_V2/`、`hdmi_tx/`、`write_fdma/`、`system_top.v`、`pwm.v`）写的**只读**验证工具与证据包。
不修改任何被测 RTL / dev 的 tb、tests、stubs。

> 文件名约定：本 README 列出的脚本 = verifier-platform 所有；目录里其它文件名（如 `check_demosaic_*.py`、
> `dev-mipi-hdmi_*.py`）是其他队友放的，不属于本证据包，未做改动。

## 一键复跑（仓库根目录，WSL）

```bash
bash pl/sim/equiv/audit/run_final_verify.sh
```
产物写到 `pl/sim/equiv/audit/evidence/`：`env.txt`（date/HEAD/golden/tool 版本）、
`sha256_platform_rtl.txt`、`sha256_project_and_fw.txt`、`git_status.txt` 以及下面每项的完整日志。
`evidence/` 是每次运行的本机生成物，已由根 `.gitignore` 忽略、**不入库**（可一键复现）；
入库的是本目录脚本与 `VERIFY_platform_20261024.md` 终验报告。
全部 PASS 时退出码 0。

## 脚本清单（每个脚本的作用）

| 脚本 | 作用 |
|---|---|
| `audit_paths.py` | 公共路径工具：`find_repo_root()` 从 `__file__` 向上找 `.git`/`baseline.rev` 推导仓库根，**所有脚本不含主机路径**，可用 `--repo <dir>` 覆盖 |
| `run_final_verify.sh` | 终验总入口：按顺序跑下面全部检查并把日志/哈希收进 `evidence/` |
| `audit_system_top.py` | **system_top.v 结构等价审计**：golden vs 工作区逐项比对（a）顶层端口名/方向/位宽/顺序（b）参数（c）全部例化的模块名/实例名/参数覆盖/每个端口连接表达式（d）assign/always 逻辑文本（e）wire/reg 逐信号声明；端口连接删除按「空连接/常量/死网」分级，死网删除记 SOFT，其余记 HARD。退出码 0=无 HARD |
| `selftest_audit.sh` | `audit_system_top.py` 的「能失败」变异自检：原样= EQUIVALENT、改 assign/改连线/换端口顺序= DIFF、纯注释改动= EQUIVALENT |
| `check_contracts_platform.py` | 平台域**端口·参数契约**比对：逐文件提取所有 module 的端口方向/名/位宽/顺序与 parameter 名/默认值，比对 golden vs 工作区；公共模块变化= FAIL，文件内私有 helper= INFO |
| `check_encoding.py` | 全仓 `*.v/*.sv/*.vh` 编码检查：strict UTF-8 可解码 + 无 BOM；`--rev` 同时检查 golden 版 |
| `check_prj_files.py` | 三份 TD 工程文件（`soc_prj.al`、`syn_1/soc_prj.prj`、`phy_1/soc_prj.prj`）一致性：条目数、去重、逐条实际文件存在、三份集合/顺序一致、`--rev` 报告相对 golden 的增删 |
| `lint_compare.py` | golden vs 工作区 `verilator --lint-only -Wall -Wno-fatal` 告警多重集对比（归一化去路径/行号），输出新增/消失告警与 error；目标：pwm/system_top/mipi_chain/hdmi_chain/uidbuf_write/uiFDMA/fs_cap |
| `gen_lint_stubs.py` + `lint_sys_stubs.v` | 从 golden 的 `design_0.v` / `design_top_wrapper.v` 自动生成 lint 空壳桩（端口方向位宽与真实工程一致），供 system_top 单独 lint；`lint_compare.py` 依赖此文件 |
| `run_x_tests.sh` | 批量跑 `pl/sim/equiv/tests/x_*.f`（verifier-platform 的对抗性等价 test），逐项打印 PASS/FAIL 与耗时 |
| `check_run_all_vs_baseline.sh` | `pl/sim/run_all.sh` 的 des 域 8 项回归与 `pl/soc_base/soc_prj_Runs/_vendor_refactor/sim_baseline/` 逐字节 diff（旁证：平台域改动不扰动 des 回归） |
| `x_stepper_ctrl_g2012.v` | **仅 x_system_top 用**：des/stepper_ctrl.v 的逐字节副本，把 `dist` 这个 SystemVerilog 关键字改名为 `distv`（其余完全一致，sha256 见报告），golden/新两侧共用；用于让整顶层 TB 能在 `-g2012` 下与新版 `wfifo.v` 一起编译 |
| `README.md` / `VERIFY_platform_20261024.md` | 本说明 / 终验报告（验证矩阵、结构审计、lint、编码、工程文件、风险清单、复现命令） |

## 对抗性 test（`pl/sim/equiv/tests/x_*.f` + `tb/x_*.v`，本目录脚本运行）

| test | 覆盖域 | 设计要点 |
|---|---|---|
| `x_pwm` | pwm.v | 默认参数 + 缩周期/占空比 0/100%/除零参数 + 随机复位，固定 LFSR |
| `x_csi_unpacket` | csi_unpacket.v | CSI-2 包序列、长度 0/1/2/5/63/100、包头背靠背、valid 随机间隙、x 态注入、中途复位 |
| `x_hs_rx` | hs_rx_wrapper.sv | lane hs/lp 极性随机、4 lane 字节顺序、双时钟相位、中途复位（加密子模块用共享桩） |
| `x_mipi_dphy` | dphy wrapper 整链 | LDVS LP mux、lane_error、参考时钟抖动、复位中途、真实 rx_clk_gen/hs_rx |
| `x_rx_clk_gen` | rx_clk_gen.v | 按工程实际配置 DEVICE=DR1 三实例（不同频率/相位/复位）；非 DR1 分支已被批准的「单配置特化」删除 |
| `x_design_top` | design_top_wrapper 整链 | PLL lock→复位、CSI 流→raw10/image/ISP 桩、ISP 输出通道、24M 时钟；隔离 ISP 域重构 |
| `x_lane_lvds` / `x_hdmi_phy` / `x_uihdmi` | HDMI 明文链 | 10bit 移位/ODDR 相位、bit 反转连线、随机码字/消隐、中途复位；TMDS 控制器用共享桩 |
| `x_fs_cap` | fs_cap.v | VIDEO_ENABLE=1/0、VS 毛刺、x 态、复位与边沿同拍 |
| `x_uiFDMA` | uiFDMA.v | 实例参数同 system_top，随机 AXI 从机 ready/响应，长度 1/2/15/16/17/32/64/255/256/257，中途复位、FDMA 侧背压 |
| `x_uidbuf_write` | uidbuf_write.v + 真实 fs_cap/wfifo | system_top 实际参数的大帧实例 + 8×4 小帧实例（短仿真内跑完整帧状态机）+ ENABLE_WRITE=0 分支；FS/camera/irq/FIFO 水位/指针回绕/中途复位 |
| `x_system_top` | system_top 整顶层 | 桩化 design_0（产生 150M/50M/des 时钟、des 寄存器写剧本、AXI 写响应、I2C 开漏、CAN/HDMI；并把 AXI 数据流折进可观察输出）+ 桩化 design_top_wrapper（HS 时钟 + ISP 视频流）；真实 des_* 与 fs_cap/uiFDMA/uidbuf_write/wfifo；200k cycles 覆盖 stepper 脉冲/servo 总线/PWM/led/CAN/I2C 与帧写路径 |

## 共享桩（`pl/sim/equiv/stubs/x_*`，golden/新两侧共用，确定性）

`x_mipi_enc_stubs.v`（data_lane/channel_aligner，含构造好的 CSI-2 包流）、`x_mipi_chain_stubs.v`
（raw10/image_correction/isp_top，隔离 ISP 域）、`x_vendor_prim_stubs.v`（DR1/PH1/EG… 厂家 BUFIO/LCLK/ODDR/OSERDES、
PLL、SF1 dphy PLL）、`x_hdmi_stubs.v`（加密 TMDS 控制器）、`x_system_top_stubs.v`（design_0 + design_top_wrapper 行为桩）。

## 复现注意

- `x_system_top` 的 manifest 用相对 `+ -Ipl/soc_base/src/des`，**必须在仓库根目录**运行（`run_equiv.sh`/`run_all_equiv.sh` 的既有约定）。
- 所有 test 激励固定种子/固定向导，无 `$random` 库实现依赖。
