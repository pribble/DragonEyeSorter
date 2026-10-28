# soc_prj_Runs — TD 运行目录（只入库“让干净克隆能跑起来”的最小集合）

`*_Runs/` 整体被根 `.gitignore` 忽略：里面是综合 / 布局 / 布线的数据库、日志、比特流，几十 MB 到几 GB，每次编译都在变；但**命令行流程需要下面这几份小文件**，故 `git add -f` 强制入库（改动照常提交）：

| 文件 | 作用 | 注意 |
|---|---|---|
| `syn_1/soc_prj.prj` | 工程文件（源文件表 / 器件 / 顶层） | 引用 `../../src/**`，含自研模块；综合不会改写它 |
| `syn_1/settings.cfg` | `DefaultFlow.tcl` 的流程参数（起止步骤、器件、封装、速度）：`run_type syn` / `read_design`→`opt_gate` | `DefaultFlow.tcl` 第一步就是 `source ./settings.cfg`，缺了直接失败；只用于「只综合、看资源 / gate 估计」的快速检查 |
| `phy_1/settings.cfg` | `run_type phy` / `start_step opt_place` / `end_step bitgen` / `parent ../syn_1`，另带 `set arr_filter false` / `set drHoldFix on` | **单进程全流程的底版**：把 `start_step` 改为 `read_design` 即正式构建；按原样两段式跑会丢约束（见下），不再用于正式构建 |
| `phy_1/soc_prj.prj` | 与 `syn_1` 那份同一个工程，另带 `<compress>on</compress>` 与 `<PlaceProperty><opt_timing>high</opt_timing>`（厂家 GUI 的设置） | 缺了报 `PRJ-8642 ERROR: Project file soc_prj.prj does not exist.`；属性不同则 bitgen 产物与布局质量不可比 |

## 正式构建：单进程

run 目录放 **`phy_1/soc_prj.prj`**（含 `<opt_timing>high</opt_timing>` 与 `<compress>on</compress>`）与一份 `settings.cfg`：**以 `phy_1/settings.cfg` 为底**（已带 `run_type phy` / `parent ../syn_1` / `set arr_filter false` / `set drHoldFix on`），只把 `start_step` 由 `opt_place` 改为 `read_design`、`end_step` 保持 `bitgen`。在该目录下跑一次：

```
td_commands_prompt.exe <TD_ROOT>/scripts/DefaultFlow.tcl < NUL
```

约束在 `read_design` 之后解析、全程有效；产物（`soc_prj.bit` / QoR 报告 / 日志）都在本 run 目录。完整 recipe 与耗时判据见 [开发环境与构建](../../../docs/开发环境与构建.md)「PL：构建与打包」与「编译时间与拥堵排查」。

本轮（第六轮）的最终构建（单进程、默认 `opt_timing=high`）run 目录为 `pl/soc_base/soc_prj_Runs/full_r6final`（**scratch run 目录、不入库**，可保留作证据、随时清理）；候选 A（HEAD 源码 + SDC）的实验 run 目录在**仓库外临时快照副本**中（同属 scratch、不入库）。两者都不进提交。

## 两段式为什么退役

`phy_1` 那段用 `run_type phy` / `start_step opt_place` / `parent ../syn_1` 从 `syn_1/soc_prj_gate.db` 重新导入网表并**重放 SDC 文本**；重放时 `derive_clocks` 尚未为 PLL 引脚建立时钟对象，`timing.sdc` 里所有 `get_clocks {…u_DR1_PHY_PLL.clkc[N]}` 匹配为空 → `set_clock_groups` 全部 no-op（两段式 phy **初始段 12 条 `USR-6001 No clocks matched`**；单进程**初始 `read_sdc` 段 0 条**，place / route 各重放一次出现 8 条但不影响已生效的 clock group——判据看最终 STA）。10-26 / 10-28 两次 route 不收敛都是在这个状态下跑的，根因分析与修复见 [log/2026-10-28-PL第六轮构建收敛](../../../docs/log/2026-10-28-PL第六轮构建收敛.md)。

因此 `syn_1` / `phy_1` 只保留两个用途：① `syn_1` 做「只综合、看资源与 gate 估计」的快速检查；② 在现成布局库上做只读探针实验。**它们的 place / route 结果不能作为时序签核与收敛证据。** 布局检查点改为「单进程跑到 `opt_place` 结束、立即读 `place.qor`，不达标杀进程」，不要用本目录两段式续跑。

## 跑法注意

- ⚠ `td_commands_prompt.exe` **出错也返回 exit 0**（报错后掉进 `td >`，stdin 是 NUL 就退出）。判断成败只能看 run 目录里的 `.<step>.end.f` 标记与产物文件（`soc_prj_gate.db` / `soc_prj.bit` 等）。
- `*.db` / `*.log` / `*.bit` / `*.timing` / `*.area` / `*.qor` / `.*.f` / `flow.status` 等编译生成物不入库；不要为了提交把 run 目录整个 `git add`。
