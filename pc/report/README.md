# pc/report — 测试报告出数

赛题硬性项⑥要「同步精度 / 采集延迟 / AI 耗时 / 总线稳定性」四类实测数据：`des_app live / run` 每判定一件就往 `/root/des/data/log/<日期>/parts_<会话>.csv` 写一行（44 列，定义见 `ps/src/app/part_log.cpp` 的 `csv_header()`），本目录脚本把 CSV 变成报告里的表。

```bash
scp -r $BOARD_HOST:/root/des/data/log ./records
```

## part_stats.py（只用标准库）

| 用法 | 出什么 |
|---|---|
| `python part_stats.py records/*/parts_*.csv` | 件数、节拍（件/min）、处置 / 去向、非良品原因、NPU 推理耗时与「帧完成 → 结果可用」延迟分布、尺寸统计 + Cpk、PL 粗测 vs PS 测量 |
| `python part_stats.py ok.csv=gear_ok flash.csv=gear_flash` | 每个文件一种已知类别（采集时一次只放一类）→ 多类准确率、OK/NG 准确率、混淆矩阵 |
| `python part_stats.py --truth gear_ok a.csv b.csv` | 同上，所有文件同一类 |
| `python part_stats.py --fed 200 run.csv` | 实际投放 200 件 → 漏检率 |
| `python part_stats.py --same-part --true-size 31.98 loop.csv` | 同一件反复过线 → 重复性 σ、相对卡尺的平均 / 最大误差（赛题中级 < 0.3mm） |
| `--md out.md --json out.json` | 表格 / 数字另存 |
| `--plots figs` | 报告图（PNG，需 matplotlib）：`ai_time`（推理与帧完成→结果延迟分布，标 p50 / p95）、`size_gear`（逐件尺寸趋势 + 规格线 + Cpk）、`confusion`（有真值时）、`throughput`（单文件累计件数）；有配方号列时概况表多一行「配方」 |

自检（无板，退出码 = 失败项数）：`python test_part_stats.py` —— 44 列真实格式夹具实跑出 Markdown / JSON + 坏 label_list 优雅降级。同款自检还有 `pc/feed/test_des_feed.py`、`pc/modbus/test_mb_client.py`（假板 / 假从站注入帧）。

尺寸规格按每条记录的 `recipe` 列取 `scripts/deploy/out/recipes.json` 里该配方的值（换型前后的记录分组统计，图也分开出；现 5 条配方全为齿轮，齿条已取消）；记录没有配方号时才用脚本内置缺省 32±0.15（`part_stats.py` 的 `DEFAULT_SPEC`）；`--gear-od/--gear-tol` 等对所有记录强制覆盖；类别名、OK/NG 判定来自 `scripts/deploy/out/label_list.json`。

## 长时间运行（板上）

`scripts/deploy/out/soak.sh [秒=7200] [输出目录]`（测试脚本，手动 `scp scripts/deploy/out/soak.sh $BOARD_HOST:/root/des/`）：起 CAN（默认回环；`SOAK_CAN=normal` 要有真 PLC / USB-CAN 应答）、`des_app run` + `canplc` 当 PLC；每分钟记内存 / 线程、负载、磁盘、CAN 收发与错误计数、PL 帧号到 `mon.csv`，结束出 `summary.txt`（帧率、RSS、STATUS 帧丢失、计数）。同一脚本即「连续运行 2h 无故障」测试；不加 `--go` 时机械臂空跑。
