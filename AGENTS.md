# AGENTS.md — DragonEyeSorter 协作与工程规范

面向在本仓库工作的 AI 助手与队员。团队 2 人：A 负责硬件 / FPGA，B 负责软件 / AI；目标是两条线互不阻塞。

## 项目一句话

安路飞龙 F3P SoC 上的塑胶齿轮 / 齿条在线质检分拣：NPU 缺陷分类 + 传送带跟踪抓取（6 轴总线舵机机械臂夹 OK 件）+ 带尾翻板分流 NG + PL 硬实时运动控制与总线 + CAN。赛事：2026 全国大学生嵌入式芯片与系统设计竞赛 · 安路赛道选题四。初赛约 2026-11-10 提交（PPT + 技术文档 + 演示视频）。

## 先读哪里

- **两个 AI 同时在改这个仓库**：提交身份、文档同步、Git 与板子的额外规矩见本文件「工作流」与「Git 与板子」两节，必须遵守。
- [docs/README.md](docs/README.md)：文档地图（读者 / 唯一源 / 状态）与写作约定，**文档分工以它为准**。
- [docs/系统概述.md](docs/系统概述.md)：定位、总体架构、**已锁定的决策**、优先级分级。
- [docs/环境与上手.md](docs/环境与上手.md)：需要哪些环境（要什么 / 从哪拿 / 怎么验证）、`.env`、板子一次性配置、从零跑起来。
- [docs/开发环境与构建.md](docs/开发环境与构建.md)：工具链、启动链、PL / PS 构建、打包、部署方式、操作禁忌。
- [docs/板端布局.md](docs/板端布局.md)：板上有什么文件、放哪、上板前对文件本身的要求（机器读的清单在 [scripts/deploy/manifest.tsv](scripts/deploy/manifest.tsv)）。
- 数字与证据：[docs/测试与验证.md](docs/测试与验证.md) 是**实测数字的唯一汇总**；逐次实测与决策沿革看 [docs/log/](docs/log/README.md)。
- 事实裁决顺序：**板卡手册 / 原理图 / 实测数据 > docs/log/ > 设计文档 > README**。
- **参数一律查文档**：型号、电压、bank 号、寄存器偏移、拨码凭记忆复述最容易错。
- 标"草案，待确认"的条目视为未拍板，提方案时注明"待确认"。

## 目录约定

| 目录 | 内容 | 语言 / 工具 |
|---|---|---|
| `docs/` | 设计文档 + `log/` 实测记录与沿革 + `figures/` 插图与生成脚本 | 中文 Markdown |
| `pl/` | PL 工程 `soc_base/`（厂家底板 + 我们的改动与自研模块 `src/des/`）、`sim/` testbench、`test/` 独立验证工程 | Verilog-2001 / SV，安路 TD 6.2.1 |
| `ps/` | Linux 应用（调度、推理、界面、存储、CAN、寄存器头文件） | C/C++（界面为 OpenCV 自绘 + FreeType，不用 Qt） |
| `train/` | 分类模型：合成数据、训练、导出 + UINT8 量化、评估 | Python + ultralytics + ORT（WSL / GPU） |
| `models/` | `label_list.json`（类别表唯一源） | 数据文件 |
| `pc/` | PC 端：plc_sim（CAN「产线 PLC」模拟器）、report（测试报告出数） | Python |
| `scripts/` | 构建与部署：`bootbin/` 打 BOOT.bin、`sd_image/` 写 SD 卡、`deploy/` 板端部署清单与上板 | PowerShell / Bash |

## 硬性约定（违反会导致联调事故）

1. **类别表五处同源**：训练 / NPU JSON / 分拣映射 / UI / 数据库只在 `models/label_list.json` 维护一份，其余生成或加载。
2. **寄存器映射同源**：PL 的 AXI-Lite 偏移改动时，必须同步更新 `docs/PL设计.md` 与 `ps/src/hw/` 头文件，同一次提交完成。分段固定：`0x0xx` des_regs / `0x1xx` servo_bus / `0x2xx` stepper_ctrl / `0x3xx` des_vision / `0x4xx` des_pwm；**新模块从 `0x5xx` 起**，用前先在 `docs/PL设计.md` 登记。
3. **IO 电平**：CEP 是 BANK11，固定 3.3V；舵机总线空闲 3.7V，**经 BSS138 双向电平转换接入**。改接线先查 `docs/硬件与接线.md`。
4. **PL 贡献要真实**：评分规则"全部功能仅 PS 实现扣分"——时序关键的脉冲、总线帧、IO 采样放在 PL。
5. **实测与口径诚实**：指令分辨率 vs 实测精度、"采集 / 显示实时性" vs "推理按物料触发"分开报告；未实测的标 ⚠。
6. **定量数据进 log**：实测数字写 `docs/log/YYYY-MM-DD-主题.md`，数字汇总进 `docs/测试与验证.md`，设计文档只保留结论与引用。
7. **不入库**：数据集、模型权重、压缩包、SD 镜像、内核 / rootfs / ArmNN 二进制，以及**能由源码编出来的二进制**（`ps/build/`、`scripts/deploy/out/des_app`）。反过来，**文本类的部署件要入库**：`scripts/deploy/out/` 里平铺的就是发到板子上的那一份（`des.env`、`.sh` 脚本、3 个刷进来的 `.json`、`MD5SUMS.txt`），改一次提交一次；**板端脚本与 `des.env` 的唯一源就是 `scripts/deploy/out/`**（就地改、就地提交；配置的源仍是 `ps/src/` 与 `models/`）。**清单条目与各目录文件数以 `scripts/deploy/manifest.tsv` 为准**（当前 11 条：主程序、三个配置与类别表、两个 NPU 模型文件、自启链五个件），不在文档里写死。采购清单、价格、链接也不放进本仓库。
   **白名单例外**：① `pl/soc_base/`（厂家 soc_prj 底板工程，含加密 IP）；② `scripts/bootbin/parts/` 的 `fsbl.elf`、`u-boot.elf`（BOOT.bin 中不随 PL / 应用重编而变的固定零件，有 MD5 记录）与 `MD5SUMS.txt`（零件台账）。`*.bit` / `*.hpf` / `*.elf` 总体忽略，BOOT.bin 不入库、打包产出。**BOOT.bin 的比特流分区一律接我们自己的比特流**（`make_bootbin.ps1 -Bit` 必填）；厂家基线比特流、厂家完整 BOOT.bin 与「原样重打包验证」路径均已下线，`parts/` 只留两个 `.elf`。
8. **测试埋点随代码一起写**：时间戳、计数器、统计接口不能等最后补。
9. **文档不写主机路径**：设计文档、README、脚本注释里只允许出现 ① 仓库内相对路径 ② 板端路径（`/root/des`、`/mnt/mmcblk0p1`、`/opt/armnn_lib`） ③ 环境变量名与 `<占位符>`。要引用外部路径就写 `.env` 的变量（`$FD_ROOT` / `$TD_ROOT` / `$ARMNN_LIB` / `$OPENCV_LIB` / `$FREETYPE_INC` / `$BOARD_LIBS` / `$BOARD_HOST` / `$BOARD_IP` / `$SSH_KEY`）。**板端命令一律写 `ssh $BOARD_HOST` / `scp <本地文件> $BOARD_HOST:<板端路径>`**，不写 IP、不写 `-i <私钥>`。带盘符的主机绝对路径只允许出现在 `docs/log/` 的历史记录与本机 `.env` 里（原因见 [docs/log/README](docs/log/README.md)）。
10. **新 PL 模块**：跨时钟域用「翻转 + 两级同步」（像素域 ↔ 75MHz 已在 `set_clock_groups -exclusive` 里），不要直接跨域采样；单模块控制在几百 LUT 以内（整片 slice 已约 82%、ERAM 只剩 19 块），大块图像算法放 PS。
11. **环境前提不算既有事实**：机器相关的依赖（工具链、SSH 密钥、ssh 别名、库目录）必须进 [docs/环境与上手](docs/环境与上手.md) §1「需要哪些环境」表，写明"要什么 / 从哪拿 / 怎么验证"；正文只引用，不写成默认存在（如"私钥是 `~/.ssh/…`"暗示它已经有了）。同理，文档里的**预期输出与实测数字必须来自实测**（板上输出、`readelf`、log），不许按命名习惯猜。
12. **PS 产物分四层，唯一源 [ps/README.md](ps/README.md)**：① `des_app`——板端产品运行时，只有它进 `scripts/deploy/manifest.tsv`；② `des_tool`——运维 / 现场工具；③ `test_*`——自动测试与硬件在环；④ `desreg`、`canplc`——底层调试与陪测。四层的划分、构建目标与部署去向只在 `ps/README.md` 维护，其余文档引用它。**`des_app` / `des_tool` 运行期间 `desreg` 只用 `r` 只读**（舵机总线 / 步进只能有一个主人），写操作统一由 `des_app` / `des_tool` 执行。

## 工作流

- 分支：`main` 保护；工作分支按 `pl/stepper-ctrl`、`ps/qt-ui`、`models/yolov8n-416` 命名。
- **提交身份固定**：每条线用各自预先配好的身份（`.git/config` 的 `user.name` / `user.email`），不要切换成对方的身份，也不要用临时身份提交——否则分不清改动来源。
- 提交信息：中文祈使句 + 范围前缀，例：`pl: 新增舵机总线 SYNC WRITE 发送`、`ps: 修复跟踪表槽位覆盖`、`docs: 记录 BOOT.bin 上板结果`、`scripts: 打包脚本支持自定义 FSBL`。
- 推送前 `git pull --rebase`；拿不准某段改动的来历就 `git log -- <文件>` 看提交信息。
- 每次验证产出一份 log，并同步设计文档的定性结论；结论被推翻时改写设计文档，log 保留留痕。
- **文档只写当前结论**：日期、决策沿革（「原来 X，后改为 Y」）、版本修正故事、复述踩坑经过都留在 `docs/log/`，正文需要出处时给 log 链接。数字只写最新实测值 + 来源，不写日期；同一指标有新旧两次实测时，旧的放括号。
- **文档分工**（边界见 [docs/README.md](docs/README.md)）：环境与上手 = 环境与从零跑起来；开发环境与构建 = 构建与部署；测试与验证 = 实测数字唯一源；到货联调清单 = 操作单；演示视频脚本 = 分镜唯一源。同一事实只维护一处，其余改链接。
- **换比特流**：上一个能用的版本留作 `BOOT.bin.prev`；跑一次 `pl_check.sh` 回归；改动提交进 `pl/soc_base/`——`soc_prj_Runs/syn_1/soc_prj.prj` 被 `*_Runs/` 忽略规则挡住，要 `git add -f`。
- 长状态行允许整行重写（不必只追加）；两边同时改同一段就解冲突，逐处对齐。
- **改 RTL 修 bug**：先写能抓到 bug 的测试，在旧 RTL 上确认它失败、在新 RTL 上通过；同时检查修复没吃掉相邻的既有行为（测试断言要具体——时长、步数、标志位，不写 `≥` 这类弱断言）。例：v9 步进修复曾把「目标在身后 → 减速停」误改成急停，原测试只断言 `pos ≥ 目标` 没抓到（[log/2026-10-15-审查队友提交](docs/log/2026-10-15-审查队友提交.md)）。
- **版本号 / 实测数字只认实测来源**（`pip list`、板上输出、log），不把它「修正」成看起来合理的值；拿不准就去环境里查。log 文件名的日期 = 实际做事的日期。
- `.ps1` 存为 UTF-8 带 BOM，`.sh` 用 LF。

## Git 与板子

- `pl/soc_base/` 里厂家生成的 `proc_sys_reset_*.v`（`design_0/ip/design_0_Processor_System_Reset_{0,1,2}/RTL/`、`design_0/ip/proc_sys_reset_75M/`）会因 CRLF 显示「已修改」：真碰上（`rebase --autostash` 会因此失败）就先 `git update-index --assume-unchanged <这些文件>`，rebase 完再 `--no-assume-unchanged`。
- Windows Git Bash 的 heredoc / `printf` 会吞反斜杠（`\b`、`\t` 被转义）：含 Windows 路径或 `\n` 的脚本先写成文件再执行。
- `ssh $BOARD_HOST`（PS 网口；别名 / IP / 私钥见 `.env` 的 `BOARD_HOST` / `BOARD_IP` / `SSH_KEY`，PC 侧网卡配置见 [docs/环境与上手](docs/环境与上手.md) §4）；测试程序在 `/root/bin`、`/root/des`，不动系统文件和 eMMC。
- **板上装了开机自启**：`/etc/init.d/S90des` → `/root/des/des_service.sh`，上电即 `des_app run --can can0`。人工跑 `des_app.sh`、`des_tool` 或 `desreg` 写命令前先 `/root/des/des_service.sh stop`，用完 `start`（`des_app.sh` 发现已有 `des_app` 会拒绝启动；`pl_check.sh` / `soak.sh` 自动暂停与恢复）。行为开关与排障见 [docs/环境与上手](docs/环境与上手.md) §4「启动脚本」。
- 跑长测试（`soak.sh`）或换 `BOOT.bin` 前先在用户侧确认（两边可能同时连板子）。

## 操作禁忌

- **BitWriter 往 SRAM 下载只在不跑 Linux 时进行**（PL 里是 Linux 依赖的 MIPI / VDMA / NPU，运行时下载会覆盖它们）；断电重启即恢复。
- **关机用 `/root/des/shutdown.sh`**（快捷方式「6_关机」调它），之后 2 分钟内断电。PS 看门狗在芯片永久在线域、软件关不掉，`halt` / `poweroff` 后板子仍会自行复位重启；`shutdown.sh` 先停 `des_app`，再把看门狗超时调到最大档再 `halt`。板子没有软件可控的电源开关，真正断电只能拔电 / 关开关。细节见 [docs/开发环境与构建](docs/开发环境与构建.md)「操作禁忌」。
- **eMMC 出厂系统是救生艇**，保持原样。
- 换 `BOOT.bin` 前把原文件改名备份。
- 写 SD 卡用 `scripts/sd_image/flash_to_disk.ps1` 或 balenaEtcher（见 [scripts/README.md](scripts/README.md)）。
- 新电源接入机械臂前，必须逐个测舵机 V+~GND 电阻并检查旷量。
