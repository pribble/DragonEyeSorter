# ps/ — 处理系统软件（PS）

双核 A35 上的 Linux 应用。职责边界见 [软件与UI](../docs/软件与UI.md)，构建与部署命令见 [开发环境与构建](../docs/开发环境与构建.md)。

## 四类产物（本表是唯一源）

判据是**用途与生命周期**；同一批库代码可服务几层，**入口二进制**按层分开：

| 层 | 产物 | 谁在什么场景执行 | 失败长什么样 | 构建目标 | 上板方式 |
|---|---|---|---|---|---|
| ① 产品运行时 | **`des_app`**（只有 `run` / `live`） | 开机自启、无人值守；演示 | 服务重启 / 停线 | `build/des_app` | 进部署清单 [manifest.tsv](../scripts/deploy/manifest.tsv) → `/root/des/` |
| ② 人工运维 / 现场工具 | **`des_tool`**（14 个模式，见下表） | 人看输出、有交互；标定 / 排障 / 采图 | 人读打印 | `build/des_tool` | **不进清单**，手动 `scp` 到 `/root/bin/`（启动器 `des_tool.sh` → `/root/des/`） |
| ③ 自动断言测试 | **`test_*`**（9 个，名单见代码地图） | 回归 / 硬件在环 | 退出码 = 失败项数 | `build/test_*` | 手动 `scp` 到 `/root/bin/` |
| ④ 底层调试与协议模拟 | **`desreg`** / **`canplc`** | 裸读写寄存器 / 总线帧；扮 PLC | 看打印值 | `build/desreg` / `build/canplc` | 手动 `scp` 到 `/root/bin/` |

`des_app` / `des_tool` 共用同一批对象，区别只在**入口与命令集**（产品没有会动机械臂 / 皮带的命令）。**二者运行期间 `desreg` 只用 `r` 只读**（总线只能有一个主人），写操作统一由它们执行。

## 代码地图

| 位置 | 内容 |
|---|---|
| ★ 入口 | `src/app/main.cpp`（`des_app`：只 `run` / `live`）、`src/app/tool_main.cpp`（`des_tool`：14 模式） |
| 共用实现 | `src/{app,hw,sched,measure,ui,vision}/` 编进两个入口（静态链接） |

```
ps/
├── Makefile        # 唯一构建入口
├── src/app/        # 入口与编排：main / tool_main / cli_common / tool_util（仅 des_tool）/
│                   #   runner（CAN + Modbus 状态机）/ live（60fps 主循环）/ camera_rig / locate /
│                   #   config / part_log（含网口上传）/ belt_ctl（行程累加）/ arm_io（机械臂执行 + 手动调试）/
│                   #   rs485_io（塔灯继电器）/ npu（分类 + 异步线程）/ collector（采集，异步 JPEG）/
│                   #   util（时钟 / 文件 / 格式化）/ json_io / bg_queue
├── src/hw/         # 硬件契约：des_hw + des_regs（↔ PL）/ can_link + can_proto（↔ PLC）/
│                   #   modbus_tcp（从站 + 寄存器表）/ modbus_rtu（RTU PDU）/ tcp_listen（监听 + 唤醒）
├── src/sched/      # 纯逻辑库（无硬件 / OpenCV）：part_types / tracker / picker（挡板 / 带速自适应）/ arm（IK）/ geom / beam
├── src/measure/    # 几何测量与标定（齿条已删）：measure_gear / fit / track_measure / roi_crop / flat_field /
│                   #   measure_tool + calib（仅 des_tool）
├── src/ui/         # 界面：hmi / ui_input / ui_text（FreeType 中文）/ display（HDMI + 3/4 缩放）
├── src/vision/     # 厂家 face_detection demo 的 4 个改造件（见下）
├── src/des_app.json# 配置模板（板上与二进制同目录）
├── tools/          # desreg、canplc
├── tests/          # test_scheduler / test_modbus / test_rs485 / test_calib / test_measure / test_belt_hil /
│                   #   test_can / test_npu / test_servo_gap
├── kernel/         # mipicam 驱动源码 + 唯一一份 mipicam_module.h
└── vendor/         # json.hpp（nlohmann 单头，编入）
```

**读产品逻辑从 `src/app/{main,live,runner}.cpp` 开始，运维命令从 `src/app/tool_main.cpp` 开始。**
上板脚本（`des.env`、`des_app.sh` / `des_tool.sh` / `can_up.sh` / `des_service.sh` / `shutdown.sh`、回归 `pl_check.sh` / `soak.sh` / `display_check.sh`）不在 `ps/`：唯一源 [scripts/deploy/out](../scripts/deploy/README.md)。

## `des_app`（产品运行时：`run` / `live`）

| 模式 | 作用 |
|---|---|
| `des_app live [-n 秒] [--belt mm/s] [--dump x.png]` | **合成主循环**：核 0 取帧 + HDMI 60fps + 叠加（跟踪框重投影、blob、判定线），核 1 定位 / 跟踪 / 调度；`des_service.sh` `MODE=live` 跑的也是它 |
| `des_app run [-n 秒] [--can can0] [--go] [--belt mm/s]` | **总装（自启跑它）**：live 全部功能 + 停带（BeltDriver）+ 机械臂线程（ArmExec，`--go` 才动）+ CAN「PLC」联动（五态状态机、命令应答、10Hz 状态 / 1Hz 计数 / 每件结果）。没开 CAN 直接运行 |
| `des_app --version` | 打印程序名 + 构建日期 / 时间（板上核对跑的是哪次构建） |

其他参数见「开关速查」。**运维 / 测试模式不在 `des_app`**：`des_app probe` 会报错并提示用 `des_tool`。

## `des_tool`（人工运维 / 现场工具，14 模式）

| 模式 | 作用 |
|---|---|
| `probe` | 取帧时序：PS / PL 帧率、丢帧、重提交延迟 |
| `locate` | 取帧 + 定位，打印 (x, y, 角度, 长, 宽)，导出调试图 |
| `track` | 取帧 + 定位 + 跟踪 + 调度（不动机械臂），打印建轨 / 判定 / 停带计划；`--belt` 真走 |
| `display` | 相机 → HDMI 60fps（`PARK_PTR` 切帧存，退出恢复 fb0）；`display_check.sh` 用它 |
| `collect --cls <类别> [--full N]` | 采训练裁切图（一次一类，按零件分目录；`--full` 加存整帧 + YOLO 框），见 [train/README](../train/README.md) |
| `measure` | 逐件测量，打印外径 / 孔径 / 偏心 / 齿数 / 缺陷；`-n` >1 出均值 / σ / 极差；`--out` 存原始帧供 `--image` 复测 |
| `calib-cam --board 9x6 --square <mm>` | 棋盘格平放皮带 → 像素→台面单应，写 `calib.json`；坐标系与操作见 [视觉与检测](../docs/视觉与检测.md)「相机标定」 |
| `calib-belt` | 零件放上游，短走 / 长走 / 退回 → mm/步、方向、往返误差、横向漂移、辊径，写 `calib.json` `belt` 段 |
| `calib-enc` | 编码器标定：空皮带往返走（`-n` 次，默认 3），出 mm/计数，写 `calib.json` `encoder` 段 |
| `calib-beam --od <卡尺外径>` | 已知外径齿轮过光束 3 轮 → 写 `calib.json` `beam` 段；操作见 [到货联调清单](../docs/到货联调清单.md) §3.9 |
| `calib-flat` | 平场：空白 A4 走过视野 → 写 `flat.json`，换相机 / 灯光 / 曝光后重做（见 [视觉与检测](../docs/视觉与检测.md)「平场」） |
| `calib-arm [--marker-h mm]` | 手眼标定（交互）：标记物 → 夹爪碰标记 → 读关节角，≥3 点拟合底座位置 / 朝向，写 `calib.json` `arm` 段 |
| `arm scan / read / teach / torque-on / torque-off / home / goto x y [z] / pick x y [角度] [料盒] [--go]` | 机械臂上电调试：扫舵机 / 读位置角度 / 示教 / 回零 / 走点 / 取放（`--go` 才动） |
| `ik <x> <y> [z] [角度]` | 台面坐标 → 关节角 / 舵机码（纯计算，不碰 PL） |

启动器 `des_tool.sh <模式> …`：取本目录 `des.env` 路径、缺 `/dev/mipicam` 时 `insmod`、已有实例在跑时拒绝启动。

## 回归脚本（手动上板，不在清单里）

| 脚本 | 作用 |
|---|---|
| `soak.sh [秒] [目录]` | 长时间运行：`des_app run` + `canplc` 当 PLC，每分钟记内存 / 负载 / CAN / PL 帧号，出 summary.txt |
| `pl_check.sh` | 换比特流后的上板回归（后台跑 `des_tool probe`），自动暂停 / 恢复自启服务 |
| `display_check.sh [秒]` | HDMI 黑屏三段对照（控制台 / `des_tool display` / 正常运行），人工看屏、记录 VDMA 状态 |
| `des_service.sh start\|stop\|restart\|status` | 开机自启 + 守护 `des_app run --can can0`，崩溃 2s 重启（退避到 30s，5 分钟内退 5 次停），日志 `data/service.log`；设置在 `autostart.conf`；装自启 `ln -s /root/des/des_service.sh /etc/init.d/S90des`（经用户同意） |

全部源在 `scripts/deploy/out/`；`des_service.sh` 随清单上板，其余手动传。

## 命令速查

- **产品 / 演示**：`des_app run`；`live` 去掉带 / 臂 / CAN。
- **回归**：改相机 / 显示代码或换比特流后跑 `probe` / `display`。
- **标定**（一次性，换相机 / 带 / 臂位重做）：`calib-cam` → `calib-belt` → `calib-arm`，写 `calib.json`（各模式自动加载）。
- **验收 / 出数**：`des_tool measure -n` / `collect`、`soak.sh`。

## 开关速查

参数解析在 `src/app/cli_common.cpp`；usage 在同文件（`des_app`）与 `tool_main.cpp`（`des_tool` 14 模式）。

| 开关 | 用在哪 | 含义 |
|---|---|---|
| `-c <cfg.json>` | 全部 | 配置；缺省 `$DES_CONFIG`，再缺省当前目录 `des_app.json` |
| `-n <N>` | 除 `ik` / `arm *` | 秒（`probe` / `display` / `track` / `live` / `run` / `collect`）、帧数（`locate` / `measure`）、点数（`calib-arm`） |
| `--vmax N` `--exp <µs>` `--gain <dB×10>` | 用相机的模式 | 只影响本次运行（VMAX / 曝光 / 增益），不写回配置 |
| `--dump <png>` / `--image <png>` | `locate` / `live` / `calib-cam` / `measure` | 存调试图 / 读图离线复测 |
| `--out <文件\|目录>` | `calib-*` / `measure` / `collect` | 标定 json / 原始帧 / 采集目录（`collect` 缺省 `$DES_COLLECT`） |
| `--belt <mm/s>` | `track` / `live` / `collect` / `run` | 皮带速度：前三者带会走、抓取只打印计划（空跑），`run` 是给状态机的运行速度 |
| `--go` | `run` / `arm home\|goto\|pick` | 真动机械臂；不加只打印 |
| `--can <if>` | `run` | CAN 接口（如 `can0`）；不写不联 PLC 直接运行 |
| `--no-rt` | 全部 | 不进 SCHED_FIFO、不 `mlockall` |
| `--board <列>x<行>` `--square <mm>` `--travel …` `--exhaustive` | `calib-cam` | 棋盘格内角数 / 格边长 / 走向 / 搜索更慢更全 |
| `--steps <N>` / `--marker-h <mm>` | `calib-belt` / `calib-arm` | 长走步数（0 = 走到约 75% 视野）/ 标记物高出带面高度 |
| `--cls <名>` `--every N` `--max N` `--full N` | `collect` | 类别名 / 采样间隔 / 每件上限 / 整帧 + YOLO 框间隔 |
| `--version` | 两个入口 | 打印程序名 + 构建日期 / 时间后退出 |
| 裸数字 | `ik <x> <y> [z] [角度]`、`arm goto\|pick <x> <y> …` | 台面坐标 mm、角度 deg、料盒号 |

## 实现要点

- 坐标约定见 `src/sched/geom.hpp`：图像 px → 台面 mm（`px_to_table` 单应）；件在带上 `s = x − B`。停带 = 写「停在 B = 抓取线 − s」，PL 梯形减速；目标太晚时 PL 停在其后，调度器按刹车距离转复检、不倒车。
- 取帧：厂家帧 DMA「提交一块写一帧」，完成即重提（4 块环形缓冲）；每帧带锁存带位置（`CAP_*`），旧比特流退回「完成后读」。帧率与丢帧见 [测试与验证](../docs/测试与验证.md)。
- 定位与几何测量接入 live / run（`track_measure` 取离光轴最近帧，结果进跟踪表与 CAN；`measure.rule` 尺寸判定默认开），方法见 [视觉与检测](../docs/视觉与检测.md)「几何测量」。
- 网口与配置：`des_app run` 起 Modbus-TCP 从站（`plc.modbus_port`，0 = 关，表 `ps/src/hw/modbus_tcp.hpp`，协议见 [软件与UI](../docs/软件与UI.md)「Modbus-TCP」），NetFeed 上传并入 `part_log`；配置 = `src/des_app.json`（标定值，TODO 待实物）、`src/recipes.json`（配方，换型停机切换）、类别表 `scripts/deploy/out/label_list.json`。

## src/vision/ 的定位与改造边界

`src/vision/` 来自安路官方 face_detection demo，是检测流水线改造起点（已改成零件检测）。只放编进二进制的 4 个 `.cpp`：取帧 `mipi_camera`（`/dev/mipicam` DMA）、推理 `executor`（`NPUExecutor`，ArmNN + Alnpu）、`flow_common_types`、`logger_forward`；显示不走厂家 `ihdmi`（HDMI 在 `src/ui/display.cpp`）。其余厂家件（前处理 / 流水 / 后处理）未编入，留档已删（git 历史 / 厂家 SDK 可找回）。

## 编译与部署

构建步骤、`.env` 与 SDK 依赖见 [开发环境与构建](../docs/开发环境与构建.md)「PS：构建」与 [scripts/README](../scripts/README.md)。要点：

- **`make -C ps all` 出 11 个产物**：`des_app`、`des_tool`、`desreg`、`canplc` + 7 个 `test_*`。只有 `des_app` 进部署清单，其余手动 `scp`；`test_npu` / `test_servo_gap` 要显式点名。
- 两个入口共享源码各编一次成 `build/obj/*.o`（`-MMD -MP`）；源文件由 Makefile `wildcard` 自动收集（增删 `.cpp` 免改 Makefile），只有 `OPS_SRC`（`calib.cpp` / `measure_tool.cpp` / `tool_util.cpp`）显式列给 `des_tool`，不链进产品；第二遍 `make` 应是 no-op。
- `des_app` / `des_tool` 需要厂家 NPU demo 的 `libs/`（OpenCV 4.7 + ArmNN），Makefile 变量 `SDK` 指向，不入库；Windows 解压丢 `.so` 软链接先跑 `sh scripts/env/sdk_fix_sonames.sh <SDK> <readelf>`。
- 运行时库路径已写进 rpath（`/opt/armnn_lib/lib`、`/opt/ffmpeg_opencv4.7.0_aarch64/lib`），板上不用 `LD_LIBRARY_PATH`；`/dev/mipicam` 独占，三个程序同时只能跑一个。

## 工具（第④层）

`tools/desreg`：读写 PL 寄存器（状态 / `r <off>` / `w <off> <val>` / `led on|off|auto` / `lat [n]` 读延迟 / `cmdlat`·`steplat`·`steptgt` 脉冲延迟 / 舵机 `ping|sread|swrite|sync|svsync|sstress` / 步进 `step status|vel|move|stop|halt|off|zero` / `vis` 视觉统计 / `pwm` 挡板脉宽 / `io [watch n]` / `485 cfg|tx|bc|soak|listen|clr`；全表见 `desreg.c` 文件头）。写前核对 `DES_ID`，比特流无 `des_regs` 时拒绝写（否则窗口别名到相机 DMA 寄存器，会写乱内存）。

`tools/canplc`：扮「产线 PLC」（`canplc <if> start|stop|pause|resume|reset|speed <mm/s>|recipe <n>|monitor [sec]`），回环与 `soak.sh` 陪测对端，也是 `pc/plc_sim` 的协议参考。

## 约定

- `src/hw/des_regs.h` 与 [PL设计](../docs/PL设计.md)、PL 端 `pl/soc_base/src/des/` 对应模块同一次提交更新；访问 PL 用 `src/hw/des_hw`（舵机事务、步进、帧计数）。
- 类别表统一从 `scripts/deploy/out/label_list.json` 加载；时间戳、计数、统计接口随功能一起写。
- **头文件平铺**：无跨目录 `#include`，靠 `ps/Makefile` 的 `INC` 解析 —— **各目录 basename 必须唯一**，重名会静默命中先搜到的（`build/obj/` 平铺 `.o` 同样依赖）。
- 移动文件时同步改：`ps/Makefile` 的 `INC` / `OPS_SRC`、本文档，及 `docs/` + `pc/` + `train/` 按路径引用的契约（`des_regs.h`、`can_proto.h`、CSV 列、`roi_crop` 几何、`runner.cpp` 状态机）。
- 新增命令先问属于哪层：产品进 `main.cpp`（`run` / `live`），运维进 `tool_main.cpp` 并更新两张表，断言进 `tests/`，调试进 `tools/`。
