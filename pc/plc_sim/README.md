# pc/plc_sim — 产线 PLC 模拟器（PC + USB-CAN，赛题硬性项⑤的外设一侧）

PC 扮演产线 PLC，经 CAN（500 kbit/s，协议见 `docs/06` / `ps/bus/can_proto.h`）控制 DragonEyeSorter：

- 下发：启动 / 停止 / 暂停（联锁）/ 恢复 / 复位报警、带速、配方、心跳（2 Hz）、对时（连上时 + 每 60s；板子没有 RTC）
- 显示：板子状态（10 Hz）、计数与良率、逐件结果（类别 / 去向 / 置信度 / 尺寸）、报警、日志
- 统计（测试报告「总线稳定性」）：收发帧数、命令 ACK 往返（平均 / 最大）、STATUS 间隔 >150ms 次数、未应答命令

```bash
python plc_gui.py --backend virtual        # 无硬件：进程内模拟板子（规则与 ps/app/runner.cpp 相同）
python plc_gui.py --backend zqwl           # ZQWL-UCAN-101E —— ⚠ 等卖家给的二次开发 SDK（DLL + 头文件）
python plc_gui.py --backend python-can --iface socketcan --channel can0   # Linux / 其他 python-can 设备
python can_proto.py                        # 协议自检（与 can_proto.h 字节布局一致）
```

只依赖 Python 3 标准库（tkinter）；`python-can` 后端需要 `pip install python-can`。

**USB-CAN 接线**：适配器 H / L 接板子 CAN 端子 H / L（⚠ 先确认 CAN 引到了板载端子，风险 R7），两端各一个 120Ω 终端电阻（适配器的 RS 端子 / 跳线），GND 共地。
