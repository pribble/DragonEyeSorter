# 给队友 AI 的说明（两人各用一个 AI 同时改这个仓库）

> 由 KristanYoong 那边的 Claude 写，2026-10-11 晚。通用规范见 [AGENTS.md](AGENTS.md)；这里只写**两个 AI 并行工作需要对齐的事**和**最新状态**。有不同意见直接改本文件并在提交信息里说明。

## 1. 谁在提交

- 本仓库只有两个人提交：**队友 = `pribble <1169128753@qq.com>`**，**KristanYoong = `KristanYoong <KristanYoong35894@hotmail.com>`**（由他的 Claude 会话代提交）。
- ⚠ 2026-10-11 21:10 之前，KristanYoong 的 AI **误用了 pribble 身份**：那之前约 70 个 pribble 提交里大部分是它做的，队友本人的有 `a7cccbe`、`a22a482`、`736033c`、`09852e2` 等。已推送的不改写历史；从 `8e083c1` 起它只用自己的身份。**你提交一律用 pribble 身份。**
- 分辨改动来源：看提交说明里的作者身份（21:10 之后）和内容；拿不准就问你的用户。

## 2. 最新状态（2026-10-11 21:10）

| 项 | 状态 |
|---|---|
| 板上 PL | **v8**（VERSION 8，BOOT md5 `f1c6de46…`；`BOOT.bin.prev` = v5，`BOOT.bin.vendor` = 厂家原版） |
| PL 回归 | 板上 `/root/des/pl_check.sh`（源码 `ps/tools/pl_check.sh`）**19 项全过**：PL 帧率 60.02fps、des_vision 读回 + 6 种模式、像素时钟 111.1MHz、PWM 读回 + 50Hz、命令延迟 |
| 寄存器窗口 | `0x0xx` des_regs、`0x1xx` servo_bus、`0x2xx` stepper_ctrl、`0x3xx` des_vision、`0x4xx` des_pwm；**`0x5xx` 起空闲**——`io_sync` 建议用 `0x5xx`，用之前先在 docs/03 登记 |
| PL 资源 | slice 约 82%、ERAM 261 / 280（剩 19 块）、DSP 65%；v6 在 90% 时两次布线失败，靠把 ISP 白平衡的流水线除法器换成逐位除法器才降下来 |
| 显示 | HDMI 60fps；显示 VDMA 翻页必须「写 START_1 → 再写 V_SIZE」（只写地址不生效，docs/06） |
| 最新记录 | `docs/log/2026-10-11-软件闭环_NPU_界面_PL视觉.md` §6~§9、`docs/07` 已测数据表 |

## 3. 新 PL 模块的规矩（v6~v8 花了约 8 小时换来的）

1. **读数据必须是组合逻辑**（`always @*` 的 case）。`fvdma_ctrl` 桥在同一时钟给出地址和 RVALID、下一沿就取 RDATA；打一拍的读多路选择读回的是**上一个地址**的值（v7 上 0x3xx 全读成阈值寄存器）。testbench 也要按这个时序读：设地址后**下一拍**就取数（照 `pl/sim/tb_des_vision.v` 的 `rreg`）。
2. **像素时钟域现在有时序约束**：`pl/soc_base/src/constraint/timing.sdc` 把 `clk_mipi` 挂在 `u_rx_clk_gen/U_io_clk_div_1.clkdivout`、8.0ns。厂家原来那行挂在不存在的硬核 MIPI 引脚上，整个像素域（ISP、des_vision、帧写入）从未做过时序分析，v7 因此漏帧。该约束保持现状（v8 的核心修复）；跨像素域 ↔ 75MHz 的信号用翻转 + 两级同步（已在 exclusive clock group 里）。
3. **控制资源**：新模块控制在几百 LUT 以内；大块图像算法放 PS。
4. **每次换比特流**：上一个能用的版本留作 `BOOT.bin.prev`；跑 `pl_check.sh`；改动提交进 `pl/soc_base/`（`soc_prj_Runs/syn_1/soc_prj.prj` 被 `*_Runs/` 忽略规则挡住，要 `git add -f`）。

## 4. 文档同步的规矩

- **数字带日期和来源；同一指标有新旧两次实测，用新的、旧的放括号**。例：PS 定位耗时 10-11 2h 浸泡平均 5.9ms（7091 个采样），10-10 首测 6.8ms——上次你把 docs/07 改回了 6.8ms，已更正。
- 改状态行（✅ / 🔄 / ⚠）前先 `git pull`，再看最新的 `docs/log/` 和提交记录：对方刚上板的东西很容易被写成「待编」。
- 尽量**追加**而不是整行重写长状态行，`docs/03`、`docs/07` 最容易两边同时改、冲突。
- `KristanYoong` 本机的交接记录不在仓库里；他那边的进展会落到 `docs/log/`、`docs/07`、README 进度表。

## 5. Git

- 推送前 `git pull --rebase`。
- `pl/soc_base/` 里 3 个文件（两个 `DR1_PHY_PLL_WRAPERR_*.v`、`proc_sys_reset_*.v`）因 CRLF 永远显示「已修改」，不入库：先 `git update-index --assume-unchanged <这 3 个文件>`（否则 `rebase --autostash` 会失败），rebase 完再 `--no-assume-unchanged`。
- Windows Git Bash 里 heredoc / `printf` 会吞反斜杠（`\b` `\t` 被转义）：含 Windows 路径或 `\n` 的脚本用编辑器写成文件再执行。

## 6. 板子

- `ssh -i ~/.ssh/id_ed25519_anlu root@192.168.137.100`（PS 网口，PC 192.168.137.1）；测试程序在 `/root/bin`、`/root/des`，不动系统文件和 eMMC。
- `des_app` 运行期间 `desreg` 只用 `r` 只读（舵机总线 / 步进只能有一个主人）。
- 两边可能同时连板子：跑长测试（`soak.sh`）或换 BOOT.bin 前，在你的用户那里打个招呼。
