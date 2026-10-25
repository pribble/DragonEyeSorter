# ps/ — 处理系统软件（PS）

双核 A35 上的 Linux 应用。职责边界见 [软件与UI](../docs/软件与UI.md)，构建与部署命令见 [开发环境与构建](../docs/开发环境与构建.md)（本文不重复）。

## 四类产物（本表是唯一源）

判据是**用途与生命周期**，不是名字。同一批库代码可服务几层，但**入口二进制**按层分开：

| 层 | 产物 | 谁在什么场景执行 | 失败长什么样 | 构建目标 | 上板方式 |
|---|---|---|---|---|---|
| ① 产品运行时 | **`des_app`**（只有 `run` / `live`） | 开机自启、无人值守；演示 | 服务重启 / 停线 | `build/des_app` | 进部署清单 [manifest.tsv](../scripts/deploy/manifest.tsv) → `/root/des/` |
| ② 人工运维 / 现场工具 | **`des_tool`**（`probe` / `locate` / `track` / `display` / `collect` / `measure` / `calib-cam` / `calib-belt` / `calib-arm` / `arm` / `ik`） | 人看着输出、有交互；到货 / 标定 / 排障 / 采训练图 | 人读打印、自己决定下一步 | `build/des_tool` | **不进清单**，手动 `scp` 到 `/root/bin/`（启动器 `des_tool.sh` → `/root/des/`） |
| ③ 自动断言测试 | **`test_*`**：scheduler / modbus / rs485 / calib / measure / belt_hil / can / npu / servo_gap | 回归 / 硬件在环；无交互 | 退出码 = 失败项数 | `build/test_*` | 手动 `scp` 到 `/root/bin/` |
| ④ 底层调试与协议模拟 | **`desreg`** / **`canplc`** | 绕过业务语义读写寄存器 / 总线帧；扮 PLC 对端 | 人看打印的寄存器值与延迟 | `build/desreg` / `build/canplc` | 手动 `scp` 到 `/root/bin/` |

`des_app` 与 `des_tool` 共用同一批对象（`build/obj/*.o`），区别只在**入口与可执行命令集**：产品二进制里没有任何会动机械臂 / 皮带的运维命令。**`des_app` / `des_tool` 运行期间 `desreg` 只用 `r` 只读**（舵机总线 / 步进只能有一个主人），写操作统一由 `des_app` / `des_tool` 执行。

## 代码地图

| 位置 | 内容 |
|---|---|
| ★ 入口 | `src/app/main.cpp`（`des_app`：只 `run` / `live`）、`src/app/tool_main.cpp`（`des_tool`：13 个模式） |
| 共用实现 | `src/{app,hw,sched,measure,ui,vision}/` 编进两个入口（静态链接） |
| 支撑 | `kernel/`（`mipicam.ko`）、`tools/`（第④层）、`tests/`（第③层）、`vendor/`（非自研，只有 `json.hpp` 编入）、`build/`（`make` 输出，已忽略） |

```
ps/
├── Makefile        # 唯一构建入口
├── src/app/        # 入口与编排：main / tool_main / cli_common / tool_util（仅 des_tool）/
│                   #   runner（CAN + Modbus 状态机总装）/ live（60fps 主循环）/ camera_rig / locate /
│                   #   config / part_log（含网口上传）/ belt_ctl / arm_io（机械臂执行 + 手动调试）/
│                   #   npu（NPU 分类 + 异步线程）/ collector（采集，含异步 JPEG 写盘）/
│                   #   util（时钟 / 文件 / 格式化）/ json_io / bg_queue
├── src/hw/         # 硬件契约：des_hw + des_regs（↔ PL）/ can_link + can_proto（↔ PLC）/
│                   #   modbus_tcp（Modbus-TCP 从站 + 寄存器表）
├── src/sched/      # 纯逻辑库（无硬件 / OpenCV 依赖）：part_types / tracker / picker（含挡板 / 带速自适应）/ arm（IK）/ geom
├── src/measure/    # 几何测量与标定：measure_gear / measure_rack / fit / track_measure / roi_crop / flat_field /
│                   #   measure_tool + calib（后两个仅 des_tool）
├── src/ui/         # 界面：hmi / ui_input / ui_text（FreeType 中文 + 绘制辅助）/ display（HDMI 输出 + 3/4 缩放）
├── src/vision/     # 厂家 face_detection demo 的 4 个改造件（见下）
├── src/des_app.json# 配置模板（板上与二进制同目录）
├── tools/          # desreg、canplc
├── tests/          # test_scheduler / test_modbus / test_rs485 / test_calib / test_measure / test_belt_hil /
│                   #   test_can / test_npu / test_servo_gap
├── kernel/         # mipicam 驱动源码 + 唯一一份 mipicam_module.h
└── vendor/         # json.hpp（nlohmann 单头，编入；厂家 demo 留档已删）
```

**读产品逻辑从 `src/app/{main,live,runner}.cpp` 开始，运维命令从 `src/app/tool_main.cpp` 开始。**
上板脚本（`des.env` + `des_app.sh` / `des_tool.sh` / `can_up.sh` / `des_service.sh` / `shutdown.sh` + 回归用 `pl_check.sh` / `soak.sh` / `display_check.sh`）**不在 `ps/` 里**：唯一源是 [scripts/deploy/out](../scripts/deploy/README.md)，改脚本直接改那里。

## `des_app`（产品运行时：`run` / `live`）

| 模式 | 作用 |
|---|---|
| `des_app live [-n 秒] [--belt mm/s] [--dump x.png]` | **合成主循环**：核 0 取帧 + HDMI 60fps + 叠加（跟踪框按当前帧带位置重投影、原始 blob、判定线、状态栏），核 1 定位 + 跟踪表 + 调度。`des_service.sh` 的 `MODE=live` 跑的也是它（降级 / 演示档） |
| `des_app run [-n 秒] [--can can0] [--go] [--belt mm/s]` | **总装（自启服务跑的就是它）**：live 的 60fps 显示 + 定位 / 跟踪 / 调度，加停带控制（BeltDriver）、机械臂线程（ArmExec，不加 `--go` 按航点时间空跑）、CAN「PLC」联动（状态机空闲 / 运行 / 暂停 / 急停 / 故障，命令应答，10Hz 状态、1Hz 计数、每件结果）。没开 CAN 直接进入运行 |
| `des_app --version` | 打印程序名 + 构建日期 / 时间（板上核对跑的是哪次构建） |

其他参数（`-c` / `--vmax` / `--exp` / `--gain` / `--no-rt`）见「开关速查」。**运维 / 测试模式不在 `des_app` 里**：`des_app probe` 之类会明确报错并提示用 `des_tool`。

## `des_tool`（人工运维 / 现场工具：13 个模式）

| 模式 | 作用 |
|---|---|
| `probe [-n 秒] [--vmax N] [--exp µs]` | 取帧时序：PS 实收帧率、PL 传感器帧率、丢帧、重提交延迟；`pl_check.sh` 后台跑的就是它 |
| `locate [-n 帧] [--dump x.png]` | 取帧 + 定位，打印每件 (x, y, 角度, 长, 宽)，导出阈值 / 外接矩形调试图 |
| `track [-n 秒] [--belt mm/s]` | 取帧 + 定位 + 跟踪表 + 调度（不动机械臂），打印建轨 / 判定 / 停带计划；`--belt` 会让皮带真走 |
| `display [-n 秒]` | 相机 → HDMI 60fps：BGRA→BGR 进 CMA 显示缓冲 + 切显示 VDMA 帧存（`PARK_PTR`，帧边界生效）；退出恢复 fb0。`display_check.sh` 第 2 段用它 |
| `collect --cls <类别> [--belt mm/s] [--every N] [--max N] [--full N]` | live + 采训练裁切图（一次一类，按零件分目录；`--full` 另存整帧 + 自动 YOLO 框），见 [train/README](../train/README.md) |
| `measure [-n 帧] [--image f.png] [--out raw.png] [--dump report.png]` | 每个零件测一遍（定位 → ROI → 测量），打印外径 / 孔径 / 偏心 / 齿数 / 缺陷；`-n` > 1 时零件不动、统计均值 / σ / 极差；`--out` 存原始帧供 `--image` 离线复测 |
| `calib-cam --board 9x6 --square <mm> [--travel right] [--image f.png] [--dump x.png]` | 棋盘格平放皮带上 → 像素→台面毫米单应矩阵，写 `calib.json`；坐标系与操作见 [视觉与检测](../docs/视觉与检测.md)「相机标定」 |
| `calib-belt [--belt mm/s] [--steps N]` | 相机标定后：零件放视野上游 → 皮带短走 / 长走 / 退回，出 mm/步、方向、往返误差、横向漂移、等效辊径，写 `calib.json` 的 `belt` 段 |
| `calib-arm [-n 4] [--marker-h mm]` | 手眼标定（交互）：标记物放视野 → 相机定位 → 手掰夹爪尖碰标记 → 读关节角；≥3 点拟合底座位置 / 朝向（+ 带面高度），写 `calib.json` 的 `arm` 段 |
| `arm scan / read / teach / torque-on / torque-off / home / goto x y / pick x y [角度] [料盒] [--go]` | 机械臂上电调试：扫舵机、读位置与角度、手掰示教、回零、走点、整套取放。默认只打印舵机码，加 `--go` 才动 |
| `ik <x> <y> [z] [角度]` | 台面坐标 → 关节角 / 舵机码（纯计算，不碰 PL） |

上板启动器 `des_tool.sh <模式> …`（源 `scripts/deploy/out/`，手动 `scp` 到 `/root/des/`）：先取本目录 `des.env` 的路径、缺 `/dev/mipicam` 时 `insmod`、已有 `des_app` / `des_tool` 在跑时拒绝启动；`des_tool` 本体在 `/root/bin/`（与 `desreg` / `canplc` / `test_*` 一致）。

## 回归脚本（手动上板，不在清单里）

| 脚本 | 作用 |
|---|---|
| `soak.sh [秒] [目录]` | 长时间运行：`des_app run` + `canplc` 当 PLC，每分钟记内存 / 负载 / CAN / PL 帧号，结束出 summary.txt |
| `pl_check.sh` | 换比特流后的上板回归（后台跑 `des_tool probe`），自动暂停 / 恢复自启服务 |
| `display_check.sh [秒]` | HDMI 周期性黑屏三段对照（控制台 / `des_tool display` / 正常运行），人工看屏、自动记录 VDMA 状态 |
| `des_service.sh start\|stop\|restart\|status` | 开机自启 + 守护 `des_app run --can can0`，崩溃 2s 后重启（退避到 30s，5 分钟内退出 5 次就停），日志 `data/service.log`；设置在 `autostart.conf`。装自启：`ln -s /root/des/des_service.sh /etc/init.d/S90des`（经用户同意） |

全部源在 `scripts/deploy/out/`；`des_service.sh` 随部署清单上板，其余手动传。

## 命令速查

- **产品 / 演示**：`des_app run`（自启服务跑的就是 `des_app run --can can0`）；`des_app live` = 去掉带 / 臂 / CAN 的版本。
- **回归**：改完相机 / 显示代码或换比特流后跑 `des_tool probe`、`des_tool display`（`pl_check.sh` 后台跑的也是它）。
- **标定**（一次性，换相机 / 带 / 臂位才重做）：`calib-cam` → `calib-belt` → `calib-arm`，结果写 `calib.json`，产品与运维模式都自动加载。
- **验收 / 出数**：`des_tool measure -n`、`des_tool collect`、`soak.sh`。
- **机械臂首次上电**：`arm scan` / `read` / `teach` / `torque-*` → `torque-on` / `home` / `goto` / `pick`（见 [到货联调清单](../docs/到货联调清单.md) §2）；不加 `--go` 只打印舵机帧。
- **分段隔离**（"一次只看一件事"）：`locate`（阈值 + 单帧定位耗时）、`track`（跟踪表 / 停带计划）、`ik`（纯逆解）；日常被 `live` / `run` 覆盖，只在对应链路出问题时单独跑。

## 开关速查

参数解析在 `src/app/cli_common.cpp`，usage 文本在 `cli_common.cpp`（`des_app` 的 run / live）与 `src/app/tool_main.cpp`（`des_tool` 的 11 模式）。

| 开关 | 用在哪 | 含义 |
|---|---|---|
| `-c <cfg.json>` | 全部 | 配置文件；缺省 `$DES_CONFIG`，再缺省当前目录 `des_app.json` |
| `-n <N>` | 除 `ik` / `arm *` | 秒（`probe` / `display` / `track` / `live` / `run` / `collect`）、帧数（`locate` / `measure`）、标定点数（`calib-arm`） |
| `--vmax N` `--exp <µs>` `--gain <dB×10>` | 用相机的模式 | 只影响本次运行：覆盖 VMAX / 曝光 / 增益，不写回配置 |
| `--dump <png>` / `--image <png>` | `locate` / `live` / `calib-cam` / `measure` | 存调试图 / 不连相机读一张图离线复测 |
| `--out <文件\|目录>` | `calib-*` / `measure` / `collect` | 标定结果 json / 原始帧 png / 采集目录（`collect` 缺省 `$DES_COLLECT`） |
| `--belt <mm/s>` | `track` / `live` / `collect` / `run` | 皮带速度：前三者带会走、抓取只打印计划（空跑），`run` 是给状态机的运行速度 |
| `--go` | `run` / `arm home\|goto\|pick` | 真动机械臂；不加只打印舵机帧（干跑） |
| `--can <if>` | `run` | CAN 接口（如 `can0`）；不写就不联 PLC，直接进运行态 |
| `--no-rt` | 全部 | 不进 SCHED_FIFO、不 `mlockall`（排查调度 / 内存问题） |
| `--board <列>x<行>` `--square <mm>` `--travel …` `--exhaustive` | `calib-cam` | 棋盘格内角数 / 格边长 / 零件走向 / 角点搜索更慢更全 |
| `--steps <N>` / `--marker-h <mm>` | `calib-belt` / `calib-arm` | 长走步数（0 = 走到视野约 75%）/ 标记物顶面高出带面的高度 |
| `--cls <名>` `--every N` `--max N` `--full N` | `collect` | 类别名 / 每 N 帧采一张 / 每件最多 N 张 / 每 N 帧另存整帧 + YOLO 框 |
| `--version` | 两个入口 | 打印程序名 + 构建日期 / 时间后退出 |
| 裸数字 | `ik <x> <y> [z] [角度]`、`arm goto\|pick <x> <y> …` | 台面坐标 mm、角度 deg、料盒号 |

## 实现要点

- 坐标约定见 `src/sched/geom.hpp`：图像 px → 台面 mm（单应矩阵，配置 `px_to_table`），件在带上 `s = x − B`（B = 带走过的距离），停带 / 反转无需特殊处理；停带 = 给步进位置模式写「停在 B = 抓取线 − s」，减速由 PL 梯形曲线完成；目标写得太晚（已在身后）时 PL 减速停在它之后，调度器按刹车距离判断来不及就转复检、不倒车。
- 取帧：厂家帧 DMA「提交一块写一帧」，完成后立即重提下一块（4 块环形缓冲）。每帧带锁存的拍摄时刻带位置（`CAP_*`），旧比特流自动退回「完成后读」。帧率与丢帧见 [测试与验证](../docs/测试与验证.md)。
- 定位：1/4 降采样灰度 + Otsu（下限钳位）+ 开运算 + 外轮廓 + minAreaRect；耗时见 [测试与验证](../docs/测试与验证.md)。
- 几何测量：`src/measure/measure`（外径 / 根径 / 孔径 / 偏心 / 规格齿数 / 缺料·缺齿·飞边，含视差校正），已接入 live / run（`track_measure`：每件取离光轴最近那帧，结果进跟踪表与 CAN；`measure.rule` 尺寸 / 规格判定默认关）。方法见 [视觉与检测](../docs/视觉与检测.md)「几何测量」。
- 网口：`des_app run` 起 Modbus-TCP 从站（`des_app.json` 的 `plc.modbus_port`，0 = 关；寄存器表 `ps/src/hw/modbus_tcp.hpp`，协议见 [软件与UI](../docs/软件与UI.md)「Modbus-TCP」）；网口实时上传（NetFeed）并入 `part_log`，供 PC 取记录（`pc/feed/des_feed.py`）。
- 配置：`src/des_app.json`（标定值，标 TODO 的待实物标定）、`src/recipes.json`（产品配方，换型停机切换）、类别表 `models/label_list.json`（`config.cpp` 解析）；部署到 `/root/des/` 走 [scripts/deploy](../scripts/deploy/README.md) 的清单。

## src/vision/ 的定位与改造边界

`src/vision/` 来自安路官方 face_detection demo，是**检测流水线的改造起点**，已改成零件检测。只放编进二进制的 4 个 `.cpp`：取帧 `mipi_camera`（`/dev/mipicam` DMA，`icamera::enumerate_device` 的定义并在这里）、推理 `executor`（`NPUExecutor`，ArmNN + Alnpu）、`flow_common_types`、`logger_forward`；显示不走厂家的 `ihdmi`（HDMI 输出是 `src/ui/display.cpp`）。厂家 demo 的其余现成件（前处理 `preprocess`、8 缓冲流水 `BufferedNPUExecutor`、后处理）没编入，留档已从仓库删除（需要时从 git 历史或厂家 SDK 找回）。

## 编译与部署

构建步骤、`.env`、SDK 依赖与 sdk_fix_sonames 见 [开发环境与构建](../docs/开发环境与构建.md)「PS：构建」与 [scripts/README](../scripts/README.md)。要点：

- **`make -C ps all` 出 11 个产物**：`des_app`、`des_tool`、`desreg`、`canplc` + 7 个 `test_*`（scheduler / modbus / rs485 / calib / measure / belt_hil / can）。只有 `des_app` 属于板端产品运行时（进部署清单）；其余要用时手动 `scp`。`test_npu` 与 `test_servo_gap` 不在 `all` 里，要显式点名。
- 两个入口共享的源码各编一次成 `build/obj/*.o`（`-MMD -MP` 自动头文件依赖）；源文件由 Makefile 的 `wildcard` 自动收集（新增 / 删除 `.cpp` 不用改 Makefile，**唯一约束仍是 basename 唯一**），只有 `OPS_SRC`（`calib.cpp` / `measure_tool.cpp` / `tool_util.cpp`）显式列给 `des_tool`，**不链进产品**。第二遍 `make` 应是 no-op。
- `des_app` / `des_tool` 需要厂家 NPU demo 的 `libs/`（OpenCV 4.7 + ArmNN），Makefile 变量 `SDK` 指向它；不入库。Windows 解压丢 `.so` 软链接时先跑 `sh scripts/sdk_fix_sonames.sh <SDK> <readelf>`。
- 运行时库路径已写进 rpath（`/opt/armnn_lib/lib`、`/opt/ffmpeg_opencv4.7.0_aarch64/lib`），板上不用设 `LD_LIBRARY_PATH`；`/dev/mipicam` 独占打开，`des_app` / `des_tool` / 厂家 demo 同一时刻只能跑一个。

## 工具（第④层）

`tools/desreg`：读写 PL 寄存器（状态 / `r <off>` / `w <off> <val>` / `led on|off|auto` / `lat [n]` 测 PS→PL 读延迟 / 舵机 `ping|sread|swrite|sync` / 步进 `step ...`）。写前核对 `DES_ID`，比特流里没有 `des_regs` 时拒绝写 —— 否则该窗口别名到厂家相机 DMA 寄存器，写入会写乱内存。⚠ 运行期间只用 `r` 只读。

`tools/canplc`：扮「产线 PLC」的命令行（`canplc <if> start|stop|pause|resume|reset|speed <mm/s>|recipe <n>|monitor [sec]`），板上回环测试与 `soak.sh` 的陪测对端，也是 `pc/plc_sim` 的协议参考实现。

## 约定

- `src/hw/des_regs.h` 与 [PL设计](../docs/PL设计.md)、PL 端 `pl/soc_base/src/des/` 的对应模块同一次提交更新；访问 PL 用 `src/hw/des_hw`（舵机事务、步进、帧计数都在里面）。
- 类别表从 `models/label_list.json` 统一加载；时间戳、计数、统计接口随功能一起写。
- **头文件平铺**：全仓库没有跨目录 `#include`，靠 `ps/Makefile` 的 `INC` 解析 —— **各目录 basename 必须唯一**，重名会静默命中先搜到的那个（`build/obj/` 平铺 `.o` 也依赖这条）。
- 移动文件时同步改：`ps/Makefile` 的 `INC` / `OPS_SRC`（源列表自动收集）、本文档，以及 `docs/` + `pc/` + `train/` 里按路径引用的契约（`des_regs.h`、`can_proto.h`、`part_log.cpp` 的 CSV 列、`roi_crop` 的裁切几何、`runner.cpp` 的状态机）。
- 新增命令先问"它属于哪一层"：产品运行时加进 `main.cpp` 的 `run` / `live`，人工运维加进 `tool_main.cpp` 并更新本文档的两张表，自动断言加进 `tests/`（退出码 = 失败项数），底层调试加进 `tools/`。
