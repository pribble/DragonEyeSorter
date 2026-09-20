# AGENTS.md — DragonEyeSorter 协作与工程规范

面向在本仓库工作的 AI 助手与新加入的队员。目标是让三条并行关键路径（硬件 / PL / PS+模型）的人互不阻塞。

## 项目一句话

安路飞龙 F3P SoC 上的智能产线视觉检测分拣系统：PL 硬实时图像预处理 + NPU 缺陷分类 + 4 轴伺服分拣 + CAN/RS485 双总线。赛事：2026 全国大学生嵌入式芯片与系统设计竞赛 · 安路赛道选题四。

## 文档体系

- `docs/` 下的设计文档记录团队**当前的工作共识与设计意图**，随实测证据持续修订；演进规则详见 [docs/README.md](docs/README.md)。
- 事实裁决顺序：**板卡手册 / 原理图 / 实测数据 > docs/log/ 记录 > 设计文档 > README**。
- 设计文档保持**定性表述**（做什么、为什么、怎么验证）；定量数据写入 `docs/log/YYYY-MM-DD-*.md`，设计文档正文保留结论与对 log 的引用。
- 修改设计先改设计文档，再改代码；实测结果先记 log，再回填定性结论。

## 目录约定

| 目录 | 内容 | 语言/工具 |
|---|---|---|
| `docs/` | 设计文档（编号主题文件）+ `log/` 实测记录 | 中文 Markdown |
| `pl/` | RTL 源码 + TD 工程 + 约束 | Verilog-2001 / SV，安路 TD |
| `ps/` | Linux 应用（调度状态机、Qt UI、Web UI、存储） | C/C++、Qt、Node |
| `models/` | 训练 / 量化脚本、label_list 唯一源 | Python + ultralytics + ORT |
| `hardware/` | IO 分配、布局图、接线说明 | Markdown + 图 |
| `scripts/` | 测试、部署、性能采集脚本 | Python / Shell |

## 硬性约定（违反会导致联调事故）

1. **类别表五处同源**：`label_list`（训练 / NPU JSON / 分拣映射 / UI / 数据库）只允许在 `models/label_list.json` 维护一份，其余地方生成或加载，禁止手抄。
2. **寄存器映射同源**：PL 的 AXI-Lite 偏移定义改动时，必须同步更新 `docs/03-PL设计.md` 与 `ps/` 侧头文件，同一次提交内完成。
3. **不绑定单一物料的视觉算法**：PL 前景定位只允许背景差分 + 连通域这类物料无关方法，禁止"圆形裁剪"之类的硬编码几何。
4. **IO 电平**：CEP bank 固定 3.3V 不可调；舵机总线空闲电平实测超过 3.3V IO 耐受，**必须经 BSS138 电平转换**，禁止直连。改接线先查 `docs/05-硬件与接线.md`。
5. **实测与口径诚实**：报告指标必须区分指令分辨率 vs 实测精度、"采集/显示实时性" vs "推理按物料触发"。所有数字以实测为准，未实测的条目标 ⚠。
6. **定量数据只进 log**：实测数字写入 `docs/log/`，设计文档只保留定性结论与对 log 的引用。
7. **大文件不入库**：数据集、模型权重、压缩包走 Release 或网盘，不提交进仓库。bitstream 如需入库用 `git add -f` 并在 commit message 说明用途。

## 工作流

- 分支：`main` 保护，工作分支按 `w1-验证` / `pl/stepper-ctrl` / `ps/qt-ui` / `models/yolov8n-416` 命名。
- 提交信息用中文祈使句 + 影响范围前缀，例：`pl: 新增 img_sobel 行缓冲框架`、`ps: 修复跟踪队列槽位覆盖`、`docs: 记录 NPU 首次实测`。
- 每次验证产出一个 `docs/log/YYYY-MM-DD-主题.md`，并在相关设计文档同步定性结论；设计被推翻时改写设计文档，log 保留旧记录留痕，不删。

## 已知操作禁忌（血的教训，勿重蹈）

- Linux 运行期间禁止用 BitWriter 往 SRAM 下载 bit（会覆盖 PL 里的 MIPI/VDMA/NPU IP）。SRAM 下载易失，断电重启即恢复。
- `mipicam.ko` 不跨重启，重启后必须重新 insmod。
- 厂商脚本 `run_mipicam_fb_fire_recognition.sh` 缺 `-s` 参数，手动补 `-s 192.168.1.1:12345`。
- 接通新的大功率电源前，必须先完成逐个舵机 V+~GND 电阻与旷量检查。

## 环境速查

- 板卡：MLK-F3P-DR1M90G（DR1M90GEG400），PS = 双核 Cortex-A35 + Linux，NPU 0.4T。
- 工具链：安路 TD（PL）、ARM 交叉编译（PS）、ultralytics + ONNX Runtime quantize_static（模型，QDQ / UInt8 / per-tensor）。
- 实测基线与当日结论见 `docs/log/` 最新记录。
