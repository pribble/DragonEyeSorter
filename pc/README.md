# pc/ — 上位机工具（PC 侧）

板子是被控端，PC 侧工具负责产线控制、出数与现场调试。板端部署见 [scripts/deploy](../scripts/deploy/README.md)；CAN / Modbus 协议与板端存储格式的唯一源见 [软件与UI](../docs/软件与UI.md)。

| 目录 | 用途 | 入口 | 依赖 |
|---|---|---|---|
| [plc_sim/](plc_sim/README.md) | 「产线 PLC」模拟器：经 USB-CAN 下发启动 / 停止 / 暂停 / 带速 / 配方 / 心跳 / 对时，显示板端状态、计数与逐件结果；`can_soak.py` 做总线稳定性统计 | `plc_gui.py`（virtual / zqwl / controlcan / python-can 后端）、`can_soak.py` | Python 3 标准库 + tkinter；zqwl 后端需 pyserial，python-can 后端需 python-can |
| [report/](report/README.md) | 测试报告出数与长时间运行汇总：读板上 part CSV → 节拍 / 延迟 / 尺寸 Cpk / 混淆矩阵，出 Markdown / JSON / 图 | `part_stats.py`、`test_part_stats.py` | 标准库；`--plots` 需 matplotlib |
| modbus/ | Modbus-TCP 主站（网口版「产线 PLC」）：读状态、启停、调速、配方、对时、灯光、往返时延 | `mb_client.py status/watch/cmd/speed/recipe/time/lamp/bench` | 标准库 |
| rs485/ | USB-RS485 适配器调试：扫描 / 改地址、读写继电器与三色灯、旁听板子 RS485 帧、冒充从站 | `rs485_tool.py ports/scan/addr/setaddr/coils/set/lamp/sniff` | pyserial |
| feed/ | 现场数据接收：连板上 TCP 端口（`des_app.json` 的 `storage.feed_port`，默认 9500），落盘每件记录与图像、断线重连 | `des_feed.py` | 标准库 |

运行前先按 [环境与上手](../docs/环境与上手.md) 配 `.env`（`BOARD_IP` / `BOARD_HOST` 等）；实物联调按 [到货联调清单](../docs/到货联调清单.md) §7 走。PC 侧脚本不用交叉编译，直接 `python <脚本> --help` 看参数。

自检（无板）：`python report/test_part_stats.py`、`python modbus/test_mb_client.py`、`python feed/test_des_feed.py`。
