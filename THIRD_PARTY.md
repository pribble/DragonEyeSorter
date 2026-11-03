# 第三方组件与来源

本仓库中**自研**与**非自研**部分的边界。自研部分为本队编写；非自研部分按原许可证或厂家授权使用，未改动处保持原样。

## 自研

| 范围 | 位置 |
|---|---|
| PL 自研模块（寄存器窗口、舵机总线主机、步进、帧起始锁存、视觉处理、PWM、IO / 编码器、RS485 Modbus-RTU 主机） | `pl/soc_base/src/des/` 及其接入顶层的改动；仿真 `pl/sim/` |
| PS 应用（采集、定位、亚像素测量、NPU 调用、跟踪调度、机械臂、挡板、CAN / Modbus-TCP / RS485、HDMI 界面、记录、运维） | `ps/src/`、`ps/tests/`、`ps/tools/` |
| PC 工具（PLC 模拟器、Modbus 客户端、现场数据接收、远程运维站、统计报表） | `pc/` |
| 训练与模型导出流程 | `train/`（只含脚本，不含数据集与权重） |
| 构建、部署、检查脚本与文档 | `scripts/`、`docs/` |

## 非自研（随仓库提供）

| 组件 | 位置 | 来源 / 许可 |
|---|---|---|
| PL 底板工程（MIPI 接收、ISP、HDMI 输出、VDMA / AXI 互联、NPU 软核等 IP 与块设计） | `pl/soc_base/`（`src/des/` 除外） | 米联客 F3P NPU 演示工程 + 安路 TD 生成的 IP；在其上接入自研模块 |
| 启动零件 `fsbl.elf`、`u-boot.elf` | `scripts/boot/parts/` | 厂家 NPU demo：`fsbl.elf` 为原件，`u-boot.elf` 由厂家 BOOT.bin 解出（台账见同目录 `MD5SUMS.txt`） |
| JSON for Modern C++ 3.11.3 | `ps/vendor/json.hpp` | nlohmann/json，MIT 许可（文件头保留原版权声明） |

## 非自研（不随仓库提供，构建 / 运行时依赖）

| 组件 | 用途 | 来源 / 许可 |
|---|---|---|
| ArmNN 32.1 + Alnpu 后端 | NPU 推理运行时 | 厂家 NPU demo（板端 `/opt`） |
| OpenCV 4.7、FFmpeg | 图像处理、解码 | 厂家 NPU demo 预编译库；各自开源许可 |
| FreeType | HDMI 界面中文字体渲染 | 板端系统库；FreeType License |
| Linux 内核、根文件系统、交叉编译器 | 板端系统与构建 | 米联客 / 安路 FD 工具链 |
| Ultralytics YOLOv8（`ultralytics` 包）、PyTorch | 分类模型训练 | PyPI；AGPL-3.0 / BSD（只用于 PC 训练，不进板端程序） |
| pyserial 等 Python 包 | PC 工具 | PyPI；见 `train/requirements*.txt` 与各工具 README |
| 舵机协议、舵机规格书、赛题指南、报告模板 | 开发参考 | 厂家 / 主办方资料，代码包不附带原件（见 `docs/ref/README.md`） |
