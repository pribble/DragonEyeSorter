# pc/report — 测试报告出数

赛题硬性项⑥要「同步精度 / 采集延迟 / AI 耗时 / 总线稳定性」四类实测数据。板上 `des_app live / run` 每判定一件就往
`/root/des/data/log/<日期>/parts_<会话>.csv` 写一行（39 列，定义见 `ps/app/part_log.cpp` 的 `csv_header()`），
这里的脚本把它们变成报告里的表。

```bash
scp -r -i ~/.ssh/id_ed25519_anlu root@192.168.137.100:/root/des/data/log ./records
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

规格默认齿轮外径 32±0.15、齿条长 50±0.5（`--gear-od/--gear-tol/--rack-len/--rack-tol` 改）。类别名、OK/NG 判定来自
`models/label_list.json`。

## 长时间运行（板上）

`ps/tools/soak.sh [秒=7200] [输出目录]`（部署到 `/root/des/soak.sh`）：起 CAN（默认回环；`SOAK_CAN=normal` 时要有真 PLC /
USB-CAN 应答）、`des_app run` + `canplc` 当 PLC（START、5Hz 心跳、监听），每分钟记 des_app 内存 / 线程、负载、可用内存、
磁盘、CAN 收发与错误计数、PL 帧号到 `mon.csv`，结束出 `summary.txt`（显示帧率、RSS 首末 / 最大、STATUS 帧丢失、最后的计数）。
接上实物后同一脚本就是「连续运行 2h 无故障」的测试；不加 `--go` 时机械臂空跑。
