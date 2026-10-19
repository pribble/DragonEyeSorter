# pc/plc_sim — 产线 PLC 模拟器（PC + USB-CAN，赛题硬性项⑤的外设一侧）

PC 扮演产线 PLC，经 CAN（500 kbit/s，协议见 `docs/软件与UI.md` / `ps/src/hw/can_proto.h`）控制板上 `des_app run --can can0`（只依赖 Python 3 标准库 tkinter；`zqwl` 后端需 `pip install pyserial`，`python-can` 后端需 `pip install python-can`）：

- 下发：启动 / 停止 / 暂停（联锁）/ 恢复 / 复位报警、带速、配方、心跳（2 Hz）、对时（连上时 + 每 60s；板子没有 RTC）
- 显示：板子状态（10 Hz）、计数与良率、逐件结果（类别 / 去向 / 置信度 / 尺寸）、报警、日志
- 统计（测试报告「总线稳定性」）：收发帧数、命令 ACK 往返（平均 / 最大）、STATUS 间隔 >150ms 次数、未应答命令

```bash
python plc_gui.py --backend virtual        # 无硬件：进程内模拟板子（规则与 ps/src/app/runner.cpp 相同）
python plc_gui.py --backend zqwl [--port COM9]   # ZQWL-UCAN-101E：USB 虚拟串口 + 厂家串口协议（zqwl.py），免驱动 / DLL
python plc_gui.py --backend controlcan --dll <ControlCAN.dll 路径> --dev-type 4 --can-index 0
python plc_gui.py --backend python-can --iface socketcan --channel can0   # Linux / 其他 python-can 设备
python can_proto.py                        # 协议自检（与 can_proto.h 字节布局一致）
python zqwl.py                             # 读适配器型号 / 固件 / 序列号（只读）
python zqwl.py listen | send 123#11223344  # 打开 CAN0（500k）收帧 / 发一帧，打印心跳（总线状态、错误计数）
python zqwl.py selftest                    # ZQWL 帧编解码离线自检
```

**ZQWL 后端（10-19 实物验证）**：ZQWL-UCAN-101E 是 USB CDC 设备（VID 0x3562 PID 0x0103），Windows 自带 usbser 驱动，出现为「USB 串行设备 (COMn)」；协议按厂家《ZQWL-USBCANFD 二次开发通讯协议 V1.05》（22 字节配置帧 49 3B … 45 2E、数据帧 5A … A5、心跳 5A FF … A5，细节见 `zqwl.py` 文件头），不用厂家 DLL。启动时设 500k 预置波特率并打开 CAN0（默认不写 flash，`--save` 才存），退出时关通道。实测：读出 UCAN-101E / 固件 V2.7；打开 CAN0 心跳 1Hz「正常」；无其他节点时发一帧 → 被动错误（证明帧被适配器接受、发上总线、没人应答）。用前关掉厂家 ZQWL-USB-CANFD-Tool（串口独占）。GUI「总线统计」末行显示适配器心跳（总线状态、错误计数、收发速率）。

**ControlCAN 后端（其他适配器）**：国产 USB-CAN 多附带周立功兼容 `ControlCAN.dll`：① 装驱动；② 卖家资料里的 `ControlCAN.dll`（与 Python 同为 64 位）放本目录或用 `--dll` 指定；③ 按手册填设备类型号 `--dev-type`（周立功 USBCAN-2A / CANalyst-II = 4，USBCAN-I = 3）；④ 关掉厂家调试软件（独占设备）。500kbit/s 用 BTR0/BTR1 = 0x00/0x1C。卖家 SDK 非 VCI 接口时，照 `backends.py` 的 `ControlCanBus` 另写一个类。

**USB-CAN 接线**：适配器 H → 板子 J11 第 3 位（CANH）、L → 第 2 位（CANL）、GND → 第 1 或第 4 位（J11 要配 KF2EDGK-3.81-6P 插头，位序见 [硬件与接线](../../docs/硬件与接线.md)）；比特流须 ≥ v12。板上已有 120Ω，适配器内部没有（H–L 约 31.6kΩ）→ 适配器端再接 120Ω（RS 端子 / 外并电阻），GND 共地。逐步联调见 [到货联调清单](../../docs/到货联调清单.md) §7。
