# soc_prj_Runs — TD 运行目录（只入库"让干净克隆能跑起来"的最小集合）

`*_Runs/` 整体被根 `.gitignore` 忽略：里面是综合 / 布局 / 布线的数据库、日志、比特流，几十 MB 到几 GB，且每次编译都在变。但**命令行流程需要下面这几份小文件**，所以它们是 `git add -f` 强制入库的（改动照常提交）：

| 文件 | 作用 | 注意 |
|---|---|---|
| `syn_1/soc_prj.prj` | 工程文件（源文件表 / 器件 / 顶层） | 引用 `../../src/**`，含自研模块；综合不会改写它 |
| `syn_1/settings.cfg` | `DefaultFlow.tcl` 的流程参数（起止步骤、器件、封装、速度） | `DefaultFlow.tcl` 第一步就是 `source ./settings.cfg`，缺了直接失败 |
| `phy_1/settings.cfg` | 同上，`run_type phy` / `start_step opt_place` / `end_step bitgen` / `parent ../syn_1` | |
| `phy_1/soc_prj.prj` | 与 `syn_1` 那份同一个工程，另带 `<compress>on</compress>` 与 `<PlaceProperty><opt_timing>high</opt_timing>`（厂家 GUI 的设置） | `phy_1` 也要有自己的这份，缺了报 `PRJ-8642 ERROR: Project file soc_prj.prj does not exist.`；属性不同则 bitgen 产物与布局质量跟 v8 不可比 |

跑法（两个目录各跑一次，完整说明与耗时见 [docs/09](../../../docs/09-开发环境与构建.md)）：

```
td_commands_prompt.exe <TD_ROOT>/scripts/DefaultFlow.tcl < NUL
```

⚠ `td_commands_prompt.exe` **出错也返回 exit 0**（报错后掉进 `td >`、stdin 是 NUL 就退出）。判断成败只能看本目录里的 `.<step>.end.f` 标记与产物文件（`soc_prj_gate.db` / `soc_prj.bit` 等）。

其余文件（`*.db` / `*.log` / `*.logw` / `*.bit` / `*.timing` / `*.area` / `*.qor` / `.*.f` / `flow.status` …）都是编译生成的，不入库。
