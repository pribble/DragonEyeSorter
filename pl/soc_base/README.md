# pl/soc_base/ — 厂家 NPU demo 底板工程（soc_prj，TD 6.2.1）

来源：米联客/安路 NPU 演示套件 `demo.zip → demo/soc_prj/`（2025-12 版），2026-10-09
决定入库作为我们 PL 工程的底板（原约定"工程不入库"作废，见 AGENTS.md 第 7/9 条）。
TD 版本决策（2026-10-10 更新，取代下方 10-09 旧决定）：**统一用 TD 6.2.1 打开**——与厂家比特流同版本，
原样重编逐位一致已验证，**不产生格式升级**。若误用 2026.1/6.2.2 打开会提示格式升级，产生的升级 diff 一律
`git checkout` 还原、不提交；真编译出问题时回退方案是 `git revert` 本目录 + 另行处理。

## 目录内容

- `soc_prj.al` / `soc_prj.hpf` — TD 工程入口
- `design_0/` — 图形化 SoC 设计：PS + MIPI RX + ISP + VDMA + HDMI 时序 + **NPU 软核 IP**；
  `design_0.address.xml` 是 GP 口地址分配（vdmafb 0x80100000 / VDMA 0x80200000 /
  mipicam 0x80300000 / soft_npu GP1 0x1F0000000）——**我们自己的 AXI-Lite 从机
  （PL_BASE）从这里分配，避开上述段**
- `design_0/ip/` — 例化 IP 的实例化包装与 RTL（多数为安路闭源加密文件，见下）
- `al_ip/` — 安路 IP 参数（PLL、NPU_SOFT 等）
- `src/` — 明文 RTL：system_top.v、isp/、hdmi_tx/、MIPI_V2/、write_fdma/、ui_isp/、pwm.v

## 许可边界（重要）

- **加密文件（13 个，`*.enc.v` / `pragma protect`）**：安路闭源 IP（NPU 软核、MIPI
  DPHY、HDMI 控制器、AXI Matrix 等）。只随本工程使用，禁止提取、反向或单独分发。
- **明文文件**：安路 demo 交付物，无单独许可头。按 docs/10 的 B 类"修改底板"模式：
  本目录尽量保持原样，**我们的改动入 `pl/soc_patch/`**（改过的文件 + 说明），合并
  编译时以 patch 覆盖。Soc_prj 工程 = 底板；soc_patch = 我们的增量。

## 已知问题（编译前先看）

- 厂家最后一次 P&R：WNS **-4.120ns**，违例集中在 vtg 148.5M ↔ NPU 150M 跨时钟域
  （`timing.sdc` 只做了 clock-group 约束的一部分）。我们重编时要补约束。
- 资源基线：85.81% slice / 92.50% ERAM——加我们自己的模块前先评估余量。
- TD 统一用 6.2.1 打开（不产生格式升级）；⚠ 2026.1/6.2.2 打开会提示格式升级，不要用。
  首次编译先原样复现，再动增量。

## 与 boot 打包的关系

本工程编译产物 `soc_prj.bit` 喂给 `scripts/bootbin/make_bootbin.ps1`；
FSBL/U-Boot 用仓库内零件（`scripts/bootbin/parts/`），除非改了 PS 配置才需重编 FSBL。
