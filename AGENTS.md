# AGENTS.md — DragonEyeSorter 协作与工程规范

面向在本仓库工作的 AI 助手与队员。团队 2 人：A 负责硬件 / FPGA，B 负责软件 / AI；目标是两条线互不阻塞。

## 项目一句话

安路飞龙 F3P SoC 上的塑胶齿轮在线质检分拣：NPU 缺陷分类（良品 / 缺料 / 飞边 / 黑点）+ 传送带跟踪抓取（6 轴总线舵机机械臂夹良品入盒）+ 侧边导流挡板分流 NG + PL 硬实时运动控制与总线 + CAN / Modbus-TCP。赛事：2026 全国大学生嵌入式芯片与系统设计竞赛 · FPGA 创新设计赛道 · 安路选题四。**作品提交 2026-11-04 18:00**：初赛作品报告 + 演示视频（线上评审），决赛现场答辩（介绍演示 + 提问 + FPGA 编程考核）。材料清单与倒排见 [submission/提交材料清单](submission/提交材料清单.md)。

## 先读哪里

- **两个 AI 同时在改这个仓库**：「工作流」「Git 与板子」两节必须遵守。
- [docs/README.md](docs/README.md)：文档地图（读者 / 唯一源 / 状态）与写作约定，**文档分工以它为准**。
- 认知顺序：[系统概述](docs/系统概述.md) → [环境与上手](docs/环境与上手.md) → [开发环境与构建](docs/开发环境与构建.md) → [板端布局](docs/板端布局.md)。
- 数字与证据：[测试与验证](docs/测试与验证.md) 是**实测数字的唯一汇总**；逐次实测与决策沿革看 [docs/log/](docs/log/README.md)。事实裁决顺序：**板卡手册 / 原理图 / 实测数据 > docs/log/ > 设计文档 > README**。
- **参数一律查文档**（型号、电压、bank、寄存器偏移、拨码）：凭记忆复述最容易错。标「草案，待确认」的条目视为未拍板，提方案时注明。

## 目录约定

| 目录 | 内容 | 语言 / 工具 |
|---|---|---|
| `docs/` | 设计文档 + `log/` 实测与沿革 + `figures/` 插图生成脚本 | 中文 Markdown |
| `pl/` | PL 工程 `soc_base/`（厂家底板 + 自研 `src/des/`）、`sim/` testbench、`test/` 独立验证 | Verilog-2001 / SV，安路 TD 6.2.1 |
| `ps/` | Linux 应用（调度、推理、界面、存储、CAN、寄存器头文件） | C/C++（界面 OpenCV 自绘 + FreeType，不用 Qt） |
| `train/` · `models/` | 训练 / 导出量化；`models/label_list.json` 是类别表唯一源 | Python + ultralytics + ORT（WSL / GPU） |
| `pc/` | plc_sim（CAN「产线 PLC」模拟器）、report（测试报告出数） | Python |
| `scripts/` | `bootbin/` 打 BOOT.bin、`sd_image/` 烧卡、`deploy/` 板端部署 | PowerShell / Bash |
| `submission/` | 提交材料清单、报告 / PPT 生成链；生成物 `out/` 不入库 | 中文 Markdown + PowerShell / Python |

## 硬性约定（违反会导致联调事故）

1. **类别表五处同源**：训练 / NPU JSON / 分拣映射 / UI / 数据库只在 `models/label_list.json` 维护，其余生成或加载。
2. **寄存器映射同源**：AXI-Lite 偏移改动时同步更新 [PL设计](docs/PL设计.md) 与 `ps/src/hw/` 头文件，同一次提交完成。分段：`0x0xx` des_regs / `0x1xx` servo_bus / `0x2xx` stepper_ctrl / `0x3xx` des_vision / `0x4xx` des_pwm / `0x5xx` des_io；**新模块从 `0x6xx` 起**，用前先登记。
3. **IO 电平**：CEP 是 BANK11，固定 3.3V；舵机总线空闲 3.7V，**经 BSS138 双向电平转换接入**；改接线先查 [硬件与接线](docs/硬件与接线.md)。
4. **PL 贡献要真实**：评分规则「全部功能仅 PS 实现扣分」——时序关键的脉冲、总线帧、IO 采样放在 PL。
5. **实测与口径诚实**：指令分辨率 vs 实测精度、「采集 / 显示实时性」vs「推理按物料触发」分开报告；未实测的标 ⚠。
6. **定量数据进 log**：实测数字写 `docs/log/YYYY-MM-DD-主题.md`，汇总进 [测试与验证](docs/测试与验证.md)，设计文档只留结论与引用。
7. **不入库**：数据集、模型权重、压缩包、SD 镜像、内核 / rootfs / ArmNN 二进制，以及能由源码编出来的二进制（`ps/build/`、`scripts/deploy/out/des_app`）。**文本类部署件要入库**：`scripts/deploy/out/` 平铺的就是发到板上的那一份，改一次提交一次，**板端脚本与 `des.env` 的唯一源就是它**。**清单条目与目录文件数以 [manifest.tsv](scripts/deploy/manifest.tsv) 为准**，不在文档里写死；采购清单、价格、链接不进仓库。
   白名单例外：① `pl/soc_base/`（厂家工程，含加密 IP）；② `scripts/bootbin/parts/` 的 `fsbl.elf`、`u-boot.elf` 与 `MD5SUMS.txt`。`*.bit` / `*.hpf` / `*.elf` 总体忽略，BOOT.bin 不入库、打包产出；**比特流分区一律接我们自己的比特流**（`make_bootbin.ps1 -Bit` 必填），厂家比特流 / 完整 BOOT.bin / 原样重打包路径均已下线。
8. **测试埋点随代码一起写**：时间戳、计数器、统计接口不能等最后补。
9. **文档不写主机路径**：只允许 ① 仓库内相对路径 ② 板端路径（`/root/des`、`/mnt/mmcblk0p1`、`/opt/armnn_lib`） ③ 环境变量名与 `<占位符>`。外部路径写 `.env` 变量（`$FD_ROOT` / `$TD_ROOT` / `$ARMNN_LIB` / `$OPENCV_LIB` / `$FREETYPE_INC` / `$BOARD_LIBS` / `$BOARD_HOST` / `$BOARD_IP` / `$SSH_KEY`）；板端命令一律写 `ssh $BOARD_HOST` / `scp <本地文件> $BOARD_HOST:<板端路径>`，不写 IP 与 `-i <私钥>`。带盘符主机路径只允许在 `docs/log/` 历史与本机 `.env`。
10. **新 PL 模块**：跨时钟域用「翻转 + 两级同步」（像素域 ↔ 75MHz 已在 `set_clock_groups -exclusive`），不直接跨域采样；单模块几百 LUT 以内（整片 slice 已约 82%、ERAM 剩 19 块），大块图像算法放 PS。
11. **环境前提不算既有事实**：机器相关依赖（工具链、SSH 密钥、别名、库目录）必须进 [环境与上手](docs/环境与上手.md) §1「需要哪些环境」表（要什么 / 从哪拿 / 怎么验证），正文只引用。预期输出与实测数字必须来自实测，不按命名习惯猜。
12. **PS 产物分四层，唯一源 [ps/README.md](ps/README.md)**：① `des_app` 产品运行时（只有它进 manifest.tsv）；② `des_tool` 运维 / 现场工具；③ `test_*` 测试与硬件在环；④ `desreg`、`canplc` 底层调试与陪测。四层的划分、构建目标与部署去向只在 `ps/README.md` 维护。**`des_app` / `des_tool` 运行期间 `desreg` 只用 `r` 只读**（舵机总线 / 步进只能有一个主人）。

## 工作流

- 分支：`main` 保护；工作分支按 `pl/stepper-ctrl`、`ps/modbus`、`models/gear-4cls` 命名。
- **提交身份固定**：每条线用各自预先配好的身份（`.git/config` 的 `user.name` / `user.email`），不切换成对方身份，也不用临时身份。
- 提交信息：中文祈使句 + 范围前缀，例：`pl: 新增舵机总线 SYNC WRITE 发送`、`ps: 修复跟踪表槽位覆盖`、`docs: 记录 BOOT.bin 上板结果`。
- 推送前 `git pull --rebase`；拿不准改动来历就 `git log -- <文件>`。
- 每次验证产出一份 log 并同步设计文档的定性结论；结论被推翻时改写正文，log 留痕。
- **文档只写当前结论**：日期、决策沿革、版本修正故事、复述踩坑经过都留在 `docs/log/`，正文需要出处时给 log 链接；数字只写最新实测值 + 来源。
- **文档分工**（边界见 [docs/README.md](docs/README.md)）：环境与上手 = 环境；开发环境与构建 = 构建与部署；测试与验证 = 实测数字唯一源；到货联调清单 = 操作单；演示视频脚本 = 分镜 / 拍摄 / 交付唯一源；提交材料清单 = 官方要求与倒排唯一入口。同一事实只维护一处，其余改链接。
- **换比特流**：上一个能用的版本留作 `BOOT.bin.prev`；跑一次 `pl_check.sh` 回归；改动提交进 `pl/soc_base/`——`soc_prj_Runs/syn_1/soc_prj.prj` 被 `*_Runs/` 忽略规则挡住，要 `git add -f`。
- **改 RTL 修 bug**：先写能抓到 bug 的测试（旧 RTL 失败、新 RTL 通过），并检查修复没吃掉相邻的既有行为；断言要具体（时长、步数、标志位），不写 `≥` 这类弱断言。例见 [log/2026-10-15-审查队友提交](docs/log/2026-10-15-审查队友提交.md)。
- **版本号 / 实测数字只认实测来源**（`pip list`、板上输出、log）；log 文件名日期 = 实际做事日期。`.ps1` 存 UTF-8 带 BOM，`.sh` 用 LF。
- 两边同时改文档：长状态行允许整行重写（不必只追加）；两边改到同一段就解冲突、逐处对齐，不整段覆盖。

## Git 与板子

- 厂家生成的 `proc_sys_reset_*.v` 会因 CRLF 显示「已修改」：碰到（会让 `rebase --autostash` 失败）先 `git update-index --assume-unchanged <文件>`，rebase 完再 `--no-assume-unchanged`。
- Windows Git Bash 的 heredoc / `printf` 会吞反斜杠：含 Windows 路径或 `\n` 的脚本先写成文件再执行。
- `ssh $BOARD_HOST`（PS 网口；别名 / IP / 私钥见 `.env`，PC 网卡配置见 [环境与上手](docs/环境与上手.md) §4）；测试程序在 `/root/bin`、`/root/des`，不动系统文件和 eMMC。
- **板上开机自启**：`/etc/init.d/S90des` → `/root/des/des_service.sh`，上电即 `des_app run --can can0`。人工跑 `des_app.sh`、`des_tool` 或 `desreg` 写命令前先 `des_service.sh stop`，用完 `start`（`pl_check.sh` / `soak.sh` 自动暂停与恢复）。开关与排障见 [环境与上手](docs/环境与上手.md) §4。
- 跑长测试（`soak.sh`）或换 `BOOT.bin` 前先在用户侧确认（两边可能同时连板子）。

## 操作禁忌

- **关机用 `/root/des/shutdown.sh`**（快捷方式「6_关机」），之后 2 分钟内断电。PS 看门狗在芯片永久在线域、软件关不掉：`halt` / `poweroff` 后约 32s 板子会自行复位；`shutdown.sh` 先停 `des_app`、把看门狗超时调到最大档（约 127s）再 `halt`。板子没有软件可控电源开关，断电只能拔电 / 关开关。机制见 [开发环境与构建](docs/开发环境与构建.md)「操作禁忌」。
- **eMMC 出厂系统是救生艇**（`/mnt/mmcblk1p*`），保持原样。
- 换 `BOOT.bin` 前把原文件改名备份（`BOOT.bin.prev`），起不来就改回。
- 写 SD 卡用 `scripts/sd_image/flash_to_disk.ps1` 或 balenaEtcher（见 [scripts/README.md](scripts/README.md)）。
- 新电源接入机械臂前，必须逐个测舵机 V+~GND 电阻并检查旷量。
