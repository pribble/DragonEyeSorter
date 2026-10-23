# 平台域独立终验报告 — 厂家 RTL 重构（verifier-platform）

- **日期/时刻**：最终复跑 2026-10-24 01:49:26 +0800，`run_final_verify.sh` **rc=0 / TOTAL_FAIL=0**（RTL 冻结于 01:35:20，即 bilinear/uiFDMA 两处 TD 语法级修正之后；证据 `evidence/env.txt`）
- **仓库/分支**：DragonEyeSorter 工作区 · `pl/refactor-3`（仓库根由脚本从 `__file__` 推导，脚本内不含主机路径）
- **HEAD**：`9a905cc343fd25003ee5a7188d31c52ce5955ac3`（golden 同为该 rev）
- **工具链**：WSL2 Linux 6.18 · Icarus Verilog 12.0 · Verilator 5.020 · Python 3.12
- **证据包**：`pl/sim/equiv/audit/evidence/`（`env.txt`、哈希、git status、每项检查完整日志；证据快照允许含当时主机路径）
- **复现**：仓库根目录 `bash pl/sim/equiv/audit/run_final_verify.sh`（退出码 0 = 全 PASS）

## 1. 结论摘要

| 项 | 结论 |
|---|---|
| 行为等价（golden vs 工作区，逐拍 trace 逐字节） | **36/36 PASS**（含平台域 dev 12 项 + verifier 对抗 13 项 + ISP 域 11 项） |
| `system_top.v` 结构等价审计 | **EQUIVALENT**：hard_diff=0，仅 6 条「删除死网/空连接」SOFT + 3 个死 wire 声明；端口/参数/全部 assign/always 一致 |
| 审计脚本「能失败」变异自检 | 4 个已知变异全部 DIFF、原样/纯注释改动 EQUIVALENT → 既不漏报也不误报 |
| 端口·参数契约（MIPI/HDMI/write_fdma/pwm/system_top，17 文件） | **PASS**（公共模块头全一致；两处参数特化经 Lead 批准，见 §7） |
| lint（verilator -Wall -Wno-fatal，golden vs 新） | **PASS**：无新增 error；新增告警全部为已解释/已批准类别（死网 PINMISSING、参数位宽收紧、DEVICE 特化 UNUSEDPARAM 已加 waiver） |
| 编码（全仓 187 个 `.v/.sv/.vh`） | **PASS**：全部 strict UTF-8、无 BOM（golden 侧 11 个 GBK 为重构前固有，仅信息） |
| 三份工程文件 | **PASS**：各 51 条、全存在、无重复、三份集合一致；相对 golden 正好删除 8 个未用 gamma LUT |
| des 域 8 项回归 vs `sim_baseline` | **8/8 逐字节 identical**（旁证，主验证属 verifier-isp 分工） |
| 结论 | **平台域重构通过独立验证，可提交/综合** |

## 2. 行为等价验证矩阵（`pl/sim/equiv/tests/*.f`，golden 9a905cc vs 工作区）

平台域相关（dev 的 test + verifier-platform 独立对抗 test），全部 `PASS … trace lines identical`：

| test | cycles | 域 / 说明 |
|---|---:|---|
| pwm | 306514 | pwm.v 默认参数 + 缩周期 + 中途复位（dev） |
| x_pwm | 12000 | verifier 对抗：占空比 0/100%、除零参数、LFSR 随机复位 |
| csi_unpacket | 377 | csi_unpacket（dev） |
| x_csi_unpacket | 26000 | verifier 对抗：长度 0/1/2/5/63/100、包头背靠背、valid 间隙、x 态、中途复位 |
| hs_rx | 333 | hs_rx_wrapper（dev，加密子模块桩） |
| x_hs_rx | 16000 | verifier 对抗：lane 极性随机、字节顺序、双时钟、中途复位 |
| mipi_dphy | 599 | dphy wrapper 整链（dev） |
| x_mipi_dphy | 4000 | verifier 对抗：LP mux/lane_error/参考时钟抖动/复位中途 |
| design_top_wrapper | 2999 | design_top_wrapper（dev，ISP 链桩隔离） |
| x_design_top | 30000 | verifier 对抗：PLL lock→复位、CSI 流整链 30k 拍 |
| uihdmi_tx | 3055 | uihdmi_tx（dev，TMDS 控制器桩） |
| hdmi_phy | 1940 | hdmi_phy_warpper（dev） |
| lane_lvds | 1540 | lane_lvds_10_1（dev） |
| x_uihdmi | 5000 | verifier 对抗：随机/边界码、消隐、中途复位 |
| x_hdmi_phy | 3999 | verifier 对抗：bit 反转连线、全 0/全 1/单 bit 游走 |
| x_lane_lvds | 3999 | verifier 对抗：双时钟相位、空闲/突发、复位 |
| fs_cap | 4407 | fs_cap（dev） |
| x_fs_cap | 2999 | verifier 对抗：VIDEO_ENABLE=1/0、VS 毛刺、x 态 |
| uiFDMA | 15007 | uiFDMA（dev，随机 AXI 从机） |
| x_uiFDMA | 12000 | verifier 对抗：长度 1/2/15/16/17/32/64/255/256/257、随机 ready、中途复位、FDMA 背压 |
| uidbuf_write | 24009 | uidbuf_write（dev，真实 fs_cap+wfifo） |
| x_uidbuf_write | 40000 | verifier 对抗：system_top 实参大帧实例 + 8×4 小帧完整帧状态机 + ENABLE_WRITE=0 分支、异步复位 |
| wfifo | 14999 | wfifo 双时钟（dev） |
| **x_system_top** | **200000** | **整顶层**：桩化 design_0/design_top_wrapper（shared），真实 des_* + fs_cap/uiFDMA/uidbuf_write/wfifo；覆盖 stepper 脉冲/servo 总线/PWM 翻转/led/CAN/I2C 与帧写 AXI 数据流 |
| x_rx_clk_gen | 1993 | verifier：按工程实配 DEVICE=DR1 三实例（非 DR1 分支为已批准特化） |
| （ISP 域其余 11 项：awb/bilinear/demosaic/gamma/isp_front/x_awb/x_bilinear/x_demosaic/x_gamma/x_isp_front/x_isp_top） | — | 同轮全 PASS，主验证属 verifier-isp |

全量：`equiv: 36 passed, 0 failed`（`evidence/equiv_all_36.log`）。

## 3. `system_top.v` 结构等价审计（`audit/audit_system_top.py`）

golden `9a905cc:pl/soc_base/src/system_top.v` vs 工作区（sha256 见 §6）：

- 顶层端口：25 个，名/方向/位宽/顺序 **完全一致**；参数 5 个一致。
- 例化：10 个例化的 模块名/实例名/参数覆盖 **完全一致**；端口连接表达式差异仅 6 条，均为**删除死连接**：
  1. `design_0.pwm_out(pwm_out_unused)` — 悬空死网（SOFT-DEADNET）
  2. `design_top_wrapper.S_pll_lock(S_pll_lock)` — 死网（SOFT-DEADNET）
  3. `design_top_wrapper.S_lane_error(S_lane_error)` — 死网（SOFT-DEADNET）
  4. `uiFDMA.M_AXI_AWQOS()` — 原就是空连接（SOFT-EMPTY）
  5. `uidbuf_write.O_fdma_wirq(fdma_wirq)` — 死网（SOFT-DEADNET）
  6. `uidbuf_write.O_fmda_wbuf(fdma_wbuf)` — 死网（SOFT-DEADNET）
  另有 3 个只被上述死连接使用的 wire 声明被删（`S_lane_error`/`S_pll_lock`/`pwm_out_unused`）。
- `assign` 11 条、`always` 3 个（含敏感表与全部语句）**逐文本一致**；无新增/改写逻辑。
- 审计脚本变异自检（`audit/selftest_audit.sh`）：原样=EQUIVALENT；改 `des_hb_cnt[27]→[26]`、改一处端口连线、交换顶层端口顺序=DIFF；纯注释改动=EQUIVALENT。

## 4. 端口·参数契约（`audit/check_contracts_platform.py`）

17 个文件（MIPI_V2 / hdmi_tx / write_fdma / pwm.v / system_top.v）全部 module 的
端口方向/名/位宽/顺序与 parameter 名/默认值 **PASS**。
两处**已批准的参数语义特化**（不改变端口/参数名/默认值）：
- `rx_clk_gen.v`、`lane_lvds_10_1.v` 删除非 `DEVICE="DR1"` 的 generate 分支；非 DR1 时输出 z（工程仅用 DR1）。**Lead 已批准**（用户总原则：只有一种配置被使用的参数分支可以砍掉），`pl/sim/equiv/README` 等文档登记。
- gamma `GAMMA_10x` 特化 + 8 个未用 LUT 删除（Lead 已批准，工程文件同步）。

## 5. lint 对比（`audit/lint_compare.py`，verilator 5.020 `-Wall -Wno-fatal`）

| target | golden 诊断 | 新诊断 | 未解释新增 | 说明 |
|---|---:|---:|---:|---|
| pwm | 1 | 0 | 0 | 只消失 EOFNEWLINE |
| system_top | 321 | 300 | 0 | 新增 6×PINMISSING（上面 6 条死连接删除）+ 11 条继承自 uiFDMA 的位宽收紧告警（已解释） |
| mipi_chain | 68 | 31 | 0 | 消失 37；DEVICE 特化 UNUSEDPARAM 已由源码 `lint_off` waiver 消除 |
| hdmi_chain | 20 | 6 | 0 | 消失 14；同上 |
| uidbuf_write | 53 | 41 | 0 | 消失 12（未用信号/位宽） |
| uiFDMA | 30 | 21 | 0 | 9 条位宽告警：burst 长度/字节数改按参数计算位宽（BURST_LEN_W/BURST_BYTES_W），实参取值域内无截断，x_uiFDMA 12000 拍逐字节等价佐证 |
| fs_cap | 5 | 0 | 0 | 消失 5 |

**结论：无新增 lint error，无未解释新增告警。**

## 6. 编码与工程文件

- 编码（`audit/check_encoding.py`）：工作区 **187 个** `*.v/*.sv/*.vh` 全部 strict UTF-8 可解码、无 BOM；
  golden 侧 11 个 GBK 文件（重构前固有）只做信息展示。原 11 个 GBK 文件均已在本轮转 UTF-8。
- 三份工程文件（`audit/check_prj_files.py`）：`soc_prj.al` / `syn_1/soc_prj.prj` / `phy_1/soc_prj.prj`
  各 **51 条**、去重后 51、逐条实际文件存在、三份集合一致；相对 golden 各 **-8**（正好是 8 个被删 gamma LUT），无其它增删。
- des 域 8 项回归（`audit/check_run_all_vs_baseline.sh`）：`des_axi/servo_bus/stepper/io/pwm/vision/vision2/awb`
  与 `_vendor_refactor/sim_baseline` **逐字节 identical**。

## 7. 被测文件 sha256（工作区，2026-10-24 01:29）

```
e5def3d4fd01ab04c7b26db33a89863732d3c00c4ad3faa926d9d27a9dcbaf6c  pl/soc_base/src/system_top.v
b392c741e3c313c64641aabeef580246762007479844276c88f073b036715139  pl/soc_base/src/pwm.v
af19bf33e4b7f8b30335529d8db8c23ad0daaa539573460f91c9b285dfe54520  pl/soc_base/src/MIPI_V2/src/design_top_wrapper.v
882c49e35fcb98f7dc0d8677049424518deadedb7bebfa289c0f46b309e6cf7e  pl/soc_base/src/MIPI_V2/src/csi_unpacket.v
1e991c25751d2a92db33cbac8683344298b5906ba967ee7ea981f5a7f5c69fc2  pl/soc_base/src/MIPI_V2/mipi_rx/mipi_dphy_rx_over_lvds_wrapper.v
0c3e5389074e4f9d4b7671d223e68b385de00aecbebcf2e43a916121ac8b51e2  pl/soc_base/src/MIPI_V2/mipi_rx/hs_rx_wrapper.sv
c906c6529954be3852a00bc3e94062b0a1e80663b7aef6bc3e405e2e201d34ab  pl/soc_base/src/MIPI_V2/mipi_rx/rx_clk_gen.v
834a7b323e2418f540bbe1cd0c939664c4e50fc2bd5a6a385952d932eeafcbdc  pl/soc_base/src/hdmi_tx/uihdmi_tx.v
887b9d9ec1d357267c2511ee924def35eae4fb249311b5bf28ab1827d4ba4783  pl/soc_base/src/hdmi_tx/hdmi_phy_warpper.v
4f59b8106b39468f0168192f0af6226373a37497f58b77fca311c0e9f88d1970  pl/soc_base/src/hdmi_tx/lane_lvds_10_1.v
9c47e98a38c079a707dc47fac1d22a1281a5be530c53ba562e99c0eaa0ddb9bb  pl/soc_base/src/write_fdma/fs_cap.v
87e5a05e69f5fa9ed361c7414557ecd1d338eca824c2f0271726aeecb0380e77  pl/soc_base/src/write_fdma/uiFDMA.v
0dd7980f97b85752951a48d2e903b39a419a10513e564e5eea601111aa1666d1  pl/soc_base/src/write_fdma/uidbuf_write.v
df2bf999d0de4de8aa6bba13743510187184472949a7dae3d7e4927936fcd658  pl/soc_base/src/write_fdma/wfifo.v
a93f92926bae616d7b575f2f38e3ce10d3d3362c1008cabac75df4f578952acf  pl/soc_base/soc_prj.al
f637f3849b13d8b42b528ee2d589c87ab11cef9d5317aa092aa277cc2e708716  pl/soc_base/soc_prj_Runs/syn_1/soc_prj.prj
7cd7baf94e67ecdfb719b64bb418d88e48e1b98e50002a70396d2b5f5c1e0fa5  pl/soc_base/soc_prj_Runs/phy_1/soc_prj.prj
```
（golden/HEAD `9a905cc343fd25003ee5a7188d31c52ce5955ac3`；`run_equiv.sh` sha256 `de889d32a35352628f9f583c10341c3d2dc6d86c7d1330eb6ff39da378bc4c3e`）

### 7.1 RTL 冻结后 TD 综合兼容微修（语法级，行为由等价 test 覆盖）

| 文件 | 修正 | sha256 |
|---|---|---|
| `ui_isp/demosaic_4x_2_0/bilinear_interpolation.v` | generate-for 加 `begin : g_line / g_word` 命名块（TD HDL-5007） | `b3d034a7df9f8e3bcc4736947096c3b7dd106938044046bed0e1c86fc3d253b8` |
| `write_fdma/uiFDMA.v` | `axi_rstart_locked/_r1/_r2` 声明移到首次使用之前（TD HDL-5373） | `87e5a05e69f5fa9ed361c7414557ecd1d338eca824c2f0271726aeecb0380e77` |
| `ui_isp/image_correction.v` | 删除 golden 即未声明的悬空死 `assign`（TD undeclared symbol） | `018348da6c05e7418d2487241f5099958a03833e26fdb249c7f658a491e89653` |

覆盖等价 test（本轮终验全 PASS）：`bilinear` 4346 cycles、`demosaic` 19174 cycles、`uiFDMA` 15007 cycles。

## 8. 风险清单 / 说明

1. **非 DR1 配置不再工作**（已批准）：`rx_clk_gen`/`lane_lvds_10_1` 的非 DR1 generate 分支被删；只影响参数 CONFIG 的泛化，不影响本工程。文档已登记。
2. **`x_system_top` 的可见性边界**：design_0 / design_top_wrapper 是行为桩（golden/新两侧共用），ISP 视频流→帧写→AXI 的整链行为只在桩提供的接口行为下等价；ISP 真链由 `isp_front/isp_top` 与 TD 覆盖。桩已把 AXI 数据流折进可观察输出以暴露连线错误。
3. **lint 位宽告警**为「按参数计算位宽」后的预期告警（uiFDMA），已用实参逐拍等价 + 取值域分析解释；若未来把 `M_AXI_DATA_WIDTH`/burst 参数改到更大值，建议重跑 lint/等价。
4. **`x_system_top` 需在仓库根目录运行**（manifest 用相对 `-Ipl/soc_base/src/des`）；`audit/x_stepper_ctrl_g2012.v` 是 des/stepper_ctrl.v 的逐字节副本（仅 `dist`→`distv`，规避 SV 关键字），golden/新两侧共用，不参与综合。
5. **RTL 冻结后 TD 兼容微修**（Lead 记录，均在最终树内、本轮终验通过）：`wfifo.v` `output reg valid`→`output valid`、`image_correction.v` 删悬空死 `assign`、`bilinear_interpolation.v` generate 命名块、`uiFDMA.v` 声明顺序前移；详见 §7.1。本报告 sha256 均为这些修正后的最终树。
6. 本轮未重综合、未上板（按 Lead 计划）；本报告只覆盖 RTL 行为等价与工程文件一致性。

## 9. 复现命令（仓库根目录）

```bash
bash pl/sim/equiv/audit/run_final_verify.sh          # 全量：36 项等价 + 审计/契约/lint/编码/工程文件/des 8 项 + 自检
bash pl/sim/equiv/audit/run_x_tests.sh               # 只跑 verifier 的对抗等价 test
python3 pl/sim/equiv/audit/audit_system_top.py       # 顶层结构审计（--golden-file 可做变异自检）
```
