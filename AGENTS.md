# AGENTS.md — DragonEyeSorter

给 AI 的常驻说明：项目是什么、按任务去哪读、无论做什么都要守的红线。人看 [README](README.md) 与 [文档索引](docs/README.md)。

> 只记一条：结论以文档与实测为准；动手前先在「按任务读」找到唯一源，不凭记忆、不猜。

## 项目

安路飞龙 F3P SoC 上的塑胶齿轮在线质检分拣：NPU 七分类（良品 / 缺料轻重 / 脏污轻重 / 飞边轻重）+ 传送带跟踪抓取（6 轴总线舵机机械臂）+ 侧边挡板分流 NG + PL 硬实时控制与总线 + CAN / Modbus-TCP。赛事：2026 全国大学生嵌入式芯片与系统设计竞赛 · FPGA 创新设计赛道 · 安路选题四，**作品提交 2026-11-04 18:00**（报告 + 演示视频，决赛答辩），要求见 [材料清单](submission/提交材料清单.md)。

现状：软件闭环与板端 PL 已实测；单机联调（10-16~26）、相机 / 传送带 / 机械臂标定、7 类实物训练上板已完成（10-29）；整线联调、完整测试报告、演示视频、答辩未完成。实测数字先查 [测试与验证](docs/测试与验证.md)。

## 事实纪律

- 裁决顺序：**板卡手册 / 原理图 / 实测数据 > [log](docs/log/README.md) > 设计文档 > README**。
- 参数（型号、电压、bank、引脚、寄存器偏移、拨码）**一律查文档**，不凭记忆复述；标「草案，待确认」的当未拍板。
- 数字只来自实测（`pip list`、板上输出、log），不猜、不按命名推断，未实测标 ⚠；唯一汇总 [测试与验证](docs/测试与验证.md)，沿革进 [log](docs/log/README.md)。
- 查不到就问人，不用猜测填空。

## 按任务读（动手前先在这找唯一源）

| 要做的事 | 先读 |
|---|---|
| 项目全貌、设计原则 | [系统概述](docs/系统概述.md) |
| 改 PL / 加模块 / 动寄存器 | [PL设计](docs/PL设计.md)（分段、CDC、资源） |
| 改 PS 应用 / 界面 / CAN / Modbus | [ps/README](ps/README.md) · [软件与UI](docs/软件与UI.md) |
| 改视觉 / 测量 / 训练 / 模型 | [视觉与检测](docs/视觉与检测.md) · [train/README](train/README.md) |
| 改机械臂 / 跟踪 / 分拣 | [运动控制与分拣](docs/运动控制与分拣.md) |
| 硬件、接线、电平 | [硬件与接线](docs/硬件与接线.md) |
| 编译 / 部署 / 上板 / 换 BOOT.bin / 环境 | [开发环境与构建](docs/开发环境与构建.md) · [scripts/README](scripts/README.md) · [环境与上手](docs/环境与上手.md) |
| 写文档、出报告、搬数字 | [文档索引](docs/README.md) §写作约定与「文档类型与放置」 |
| PC 侧（PLC 模拟器 / 出数） | [pc/README](pc/README.md) · [plc_sim](pc/plc_sim/README.md) · [report](pc/report/README.md) |
| 提交材料、答辩 | [材料清单](submission/提交材料清单.md) |

## 红线（细节都在 docs，这里只留必守项）

1. **唯一源**：类别表 [label_list.json](scripts/deploy/out/label_list.json)；寄存器 = [PL设计](docs/PL设计.md) + `ps/src/hw/`；部署件 = `scripts/deploy/out/` + [manifest.tsv](scripts/deploy/manifest.tsv)；文档地图 = [文档索引](docs/README.md)。
2. **同一次提交**：改寄存器偏移 → RTL / 头文件 / PL 设计一起提交；新模块地址从 `0x6xx` 起先登记。
3. **入库边界**：数据集、权重、压缩包、SD 镜像、内核 / rootfs / ArmNN 二进制、源码可编出的二进制不入库；`scripts/deploy/out/` 文本部署件**必须入库**（改一次提交一次）；条数以 [manifest.tsv](scripts/deploy/manifest.tsv) 为准，采购信息不进仓库。
4. **不写主机路径 / IP / 私钥**：外部路径用 `.env` 变量（见 [.env.example](.env.example)），细则与例外见 [文档索引](docs/README.md) §写作约定。
5. **PL 贡献要真实**：评分规则「仅 PS 实现扣分」——时序关键的脉冲、总线帧、IO 采样放 PL，大块图像算法放 PS。
6. **上板安全**：人工跑 `des_app` / `des_tool` 或 `desreg` 写操作前先 `des_service.sh stop`，用完 `start`；`des_app` / `des_tool` 运行期间 `desreg` 只读 `r`。长测试 / 换 `BOOT.bin` 前先问人；`BOOT.bin` 必接我们自己的比特流（`-Bit` 必填）。
7. **关机与系统**：不 `halt` / `poweroff`（看门狗约 32s 自行复位）；关机走 `/root/des/shutdown.sh` 并提醒人 2 分钟内断电；不动 eMMC 出厂系统与系统文件。
8. **文档纪律**：正文只写当前结论，日期 / 沿革 / 踩坑留 [log](docs/log/README.md)；每次验证出一份 log 并同步设计文档结论。

## 提交与验证

- 分支 `pl/…`、`ps/…`、`models/…`、`docs/…`；提交信息 = 中文祈使句 + 范围前缀（`pl: 新增舵机总线 SYNC WRITE 发送`）；身份用 `.git/config` 预配的，推送前 `git pull --rebase`。
- 可能有人并发改同一文件：长状态行整行重写、冲突逐处对齐，不整段覆盖。
- 验证：改 PL 跑 `/root/des/pl_check.sh`；改 PS 跑 `make -C ps all`；改 RTL 先写能失败的测试（断言要具体，见 [测试与验证](docs/测试与验证.md)）。
- 文档：改文档后、提交前跑 `pwsh -File scripts/check/check_docs.ps1`；改寄存器 / 类别表 / 部署件后跑 `pwsh -File scripts/check/check_sources.ps1`；改检查器规则后跑 `pwsh -File scripts/check/selftest.ps1`（规则与例外见 `scripts/check/doc_check.json` / `scripts/check/source_check.json`）。
- 编码：`.ps1` UTF-8 BOM，`.sh` LF。

## 维护本文件

新增规则先过三问：**每轮都用？不写就会主动做错？代价高？** 不过关写进对应 docs，这里只加指针。
上限：红线 10 条、全文 60 行，超了先删一条。
