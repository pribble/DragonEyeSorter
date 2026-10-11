# AGENTS.md — DragonEyeSorter 协作与工程规范

面向在本仓库工作的 AI 助手与队员。团队 2 人：A 负责硬件 / FPGA，B 负责软件 / AI；目标是两条线互不阻塞。

## 项目一句话

安路飞龙 F3P SoC 上的塑胶齿轮 / 齿条在线质检分拣：NPU 缺陷分类 + 传送带跟踪抓取（6 轴总线舵机机械臂夹 OK 件）+ 带尾翻板分流 NG + PL 硬实时运动控制与总线 + CAN。赛事：2026 全国大学生嵌入式芯片与系统设计竞赛 · 安路赛道选题四。初赛约 2026-11-10 提交（PPT + 技术文档 + 演示视频）。

## 先读哪里

- **两个 AI 同时在改这个仓库**：先读 [给队友AI的说明.md](给队友AI的说明.md)（提交身份、最新 PL 状态、新 PL 模块与文档同步的规矩）。
- 设计与决策：[docs/README.md](docs/README.md)（01~12）。**已锁定的决策**汇总在 [docs/01-系统概述](docs/01-系统概述.md)。
- 工具链、构建、部署、操作禁忌：[docs/09-开发环境与构建](docs/09-开发环境与构建.md)。
- 事实裁决顺序：**板卡手册 / 原理图 / 实测数据 > docs/log/ > 设计文档 > README**。
- **不要凭记忆复述参数**（型号、电压、bank 号、寄存器偏移、拨码最容易错）——查文档。
- 标"草案，待确认"的条目不是定论，提方案时不要当成已定。

## 目录约定

| 目录 | 内容 | 语言 / 工具 |
|---|---|---|
| `docs/` | 设计文档 + `log/` 实测记录 | 中文 Markdown |
| `pl/` | 自研 RTL、testbench；`soc_base/` 厂家底板工程、`soc_patch/` 我们的改动；`pl/test/` 独立验证工程 | Verilog-2001 / SV，安路 TD 6.2.1 |
| `ps/` | Linux 应用（调度、推理、界面、存储、CAN、寄存器头文件） | C/C++（界面为 OpenCV 自绘 + FreeType，不用 Qt） |
| `train/` | 分类模型：合成数据、训练、导出 + UINT8 量化、评估 | Python + ultralytics + ORT（WSL / GPU） |
| `models/` | `label_list.json`（类别表唯一源） | 数据文件 |
| `pc/` | PC 端：plc_sim（CAN「产线 PLC」模拟器）、report（测试报告出数） | Python |
| `scripts/` | 构建与部署：BOOT.bin 打包、SD 镜像 | PowerShell / Bash |

## 硬性约定（违反会导致联调事故）

1. **类别表五处同源**：训练 / NPU JSON / 分拣映射 / UI / 数据库只在 `models/label_list.json` 维护一份，其余生成或加载，禁止手抄。
2. **寄存器映射同源**：PL 的 AXI-Lite 偏移改动时，必须同步更新 `docs/03-PL设计.md` 与 `ps/common/` 头文件，同一次提交完成。
3. **IO 电平**：CEP 是 BANK11，固定 3.3V；舵机总线空闲 3.7V，**必须经 BSS138**，禁止直连。改接线先查 `docs/05-硬件与接线.md`。
4. **PL 贡献要真实**：评分规则"全部功能仅 PS 实现扣分"——时序关键的脉冲、总线帧、IO 采样放在 PL。
5. **实测与口径诚实**：指令分辨率 vs 实测精度、"采集 / 显示实时性" vs "推理按物料触发"分开报告；未实测的标 ⚠。
6. **定量数据进 log**：实测数字写 `docs/log/YYYY-MM-DD-主题.md`，设计文档只保留结论与引用。
7. **不入库**：数据集、模型权重、压缩包、SD 镜像、内核/rootfs/ArmNN 二进制。采购清单、价格、链接也不放进本仓库。**白名单例外**（2026-10-09 决定）：① `pl/soc_base/`（厂家 soc_prj 底板工程，含加密 IP，见第 9 条）；② `scripts/bootbin/parts/` 的 `fsbl.elf`、`u-boot.elf`（BOOT.bin 中不随 PL/应用重编而变的固定零件，有 MD5 记录）；`*.bit`/`*.hpf`/`*.elf` 仍总体忽略，BOOT.bin 不入库、打包产出。
8. **测试埋点随代码一起写**：时间戳、计数器、统计接口不能等最后补。
9. **第三方源码入库边界**（详见 [docs/10-第三方代码引用规范](docs/10-第三方代码引用规范.md)）：
   - **可入库**：安路官方 MIT 授权源码（face_detection 推理链，已入 `ps/vision/` 作为零件检测改造起点；mipicam 驱动入 `ps/kernel_modules/`）、厂家 soc_prj 底板工程整体（`pl/soc_base/`，含加密 IP——作为底板使用，不单独分发加密文件；我们的改动只入 `pl/soc_patch/`）、安路 GPL-2.0 内核驱动源码（入 `ps/kernel_modules/`，保留 GPL 头并注明）。
   - **禁止入库 / 禁止提取**：米联客培训 demo 源码（"All rights reserved, only for learning"，只在本机 `reference_src/` 参考）；从 soc_prj 提取加密 `.enc.v` 单独分发或反向；内核 / rootfs / ArmNN 二进制。
   - **入库格式**：原样拷贝时保留原版权头不许删；基于厂家文件修改时，文件头注明"derived from Anlogic \<原路径\>，修改：…"。仓库整体 MIT 不覆盖第三方文件的原始许可。

## 工作流

- 分支：`main` 保护；工作分支按 `pl/stepper-ctrl`、`ps/qt-ui`、`models/yolov8n-416` 命名。
- 提交信息：中文祈使句 + 范围前缀，例：`pl: 新增舵机总线 SYNC WRITE 发送`、`ps: 修复跟踪表槽位覆盖`、`docs: 记录 BOOT.bin 上板结果`、`scripts: 打包脚本支持自定义 FSBL`。
- 每次验证产出一份 log，并同步设计文档的定性结论；结论被推翻时改写设计文档，log 保留留痕。
- `.ps1` 存为 UTF-8 带 BOM，`.sh` 用 LF。

## 操作禁忌（血的教训）

- **Linux 运行时禁止用 BitWriter 往 SRAM 下载比特流**（会覆盖 PL 里的 MIPI / VDMA / NPU）。断电重启即恢复。
- **关机用 `poweroff` 或先 `sync`**：根分区是持久化 ext4。
- **eMMC 出厂系统是救生艇**，不要写。
- 换 `BOOT.bin` 前把原文件改名备份。
- 写 SD 卡不要用 `mountvol /p` 卸卷（见 `scripts/README.md`）。
- 新电源接入机械臂前，必须逐个测舵机 V+~GND 电阻并检查旷量。
