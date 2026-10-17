# ps/ — 处理系统软件（PS）

双核 A35 上的 Linux 应用。职责见 [docs/软件与UI](../docs/软件与UI.md)，开发环境见 [docs/开发环境与构建](../docs/开发环境与构建.md)。

## 四类产物（本文件的判定表是唯一源）

判据是**用途与生命周期**，不是名字。同一段库代码可以同时服务几层，但**入口二进制**按层分开：

| 层 | 产物 | 谁在什么场景执行 | 失败长什么样 | 构建目标 | 上板方式 |
|---|---|---|---|---|---|
| ① 产品运行时 | **`des_app`**（只有 `run` / `live`） | 开机自启、无人值守；演示 | 服务重启 / 停线 | `build/des_app` | 进部署清单 [manifest.tsv](../scripts/deploy/manifest.tsv) → `/root/des/` |
| ② 人工运维 / 现场工具 | **`des_tool`**（`probe` / `locate` / `track` / `display` / `collect` / `measure` / `calib-cam` / `calib-belt` / `calib-arm` / `arm` / `ik`） | 人看着输出、有交互；到货 / 标定 / 排障 / 采训练图 | 人读打印、自己决定下一步 | `build/des_tool` | **不进清单**，手动 `scp` 到 `/root/bin/`（启动器 `des_tool.sh` → `/root/des/`） |
| ③ 自动断言测试 | **`test_*`**：`test_scheduler` / `test_calib` / `test_measure` / `test_belt_hil` / `test_can` / `test_npu` / `test_servo_gap` | 回归 / 硬件在环；无交互 | 退出码 = 失败项数 | `build/test_*` | 手动 `scp` 到 `/root/bin/` |
| ④ 底层调试与协议模拟 | **`desreg`** / **`canplc`** | 绕过业务语义读写寄存器 / 总线帧；扮 PLC 对端 | 人看打印的寄存器值与延迟 | `build/desreg` / `build/canplc` | 手动 `scp` 到 `/root/bin/` |

`des_app` 与 `des_tool` 共用同一批对象（`build/obj/*.o`，见「编译」），区别只在**入口与可执行命令集**：产品二进制里没有任何会动机械臂 / 皮带的运维命令。**`des_app` / `des_tool` 运行期间 `desreg` 只用 `r` 只读**（舵机总线 / 步进只能有一个主人），写操作统一由 `des_app` / `des_tool` 执行。

## 主次地图（先看这张）

| 层级 | 目录 | 是什么 | 部署到哪 |
|---|---|---|---|
| ★ 入口 | `src/app/main.cpp` | `des_app` 产品入口：只 `run` / `live` | 编出的 `des_app` + 配置 → `/root/des/` |
| | `src/app/tool_main.cpp` | `des_tool` 运维入口：11 个人工模式 | 手动 `scp` → `/root/bin/` |
| 共用 | `src/{app,hw,sched,measure,ui,vision}/` | 两个入口共用的实现（编排 / 存储 / 调度 / 测量 / 界面 / 取帧推理） | 编进上面两个二进制（静态链接） |
| 支撑 | `kernel/` | `mipicam.ko`（相机 DMA 驱动，厂家源码改造起点） | 板上已有厂家预编译模块；改驱动才重编 |
| | `tools/` | 第④层命令行：`desreg`（寄存器调试）、`canplc`（扮 PLC） | `/root/bin/` |
| | `tests/` | 第③层：离线算法回归 + 上板硬件在环 | 不进产物，需要时 scp 到 `/root/bin/` |
| 非我们写的 | `vendor/` | nlohmann json 单头（编入）+ 厂家 demo 未编入件 | 不进产物（除 json.hpp） |
| 产物 | `build/` | `make` 输出（`**/build/` 已忽略） | — |

一句话：**读产品逻辑从 `src/app/{main,live,runner}.cpp` 开始，运维命令从 `src/app/tool_main.cpp` 开始，其余目录是两个入口共用的实现与支撑。**

上板脚本（`des.env` + `des_app.sh` / `des_tool.sh` / `can_up.sh` / `des_service.sh` + 回归用的 `pl_check.sh` / `soak.sh` / `display_check.sh`）**不在 `ps/` 里**：它们的唯一源就是上板清单那一份 [scripts/deploy/out](../scripts/deploy/README.md)（2026-10-16 由 `ps/board/` 迁入），改脚本直接改那里。

## 目录

```
ps/
├── Makefile        # 唯一构建入口（交叉编译自研程序，见「编译」）
├── src/            # 两个入口共用的源码，按角色分层（头文件平铺，靠 INC 解析）
│   ├── app/        #   入口与编排：main（des_app 产品入口）/ tool_main（des_tool 运维入口）/ cli_common（共享 CLI）/
│   │               #   runner（CAN 状态机总装）/ live（60fps 主循环）/ camera_rig（取帧）/ locate（定位）/
│   │               #   config / part_log（存储）/ belt_ctl（停带）/ arm_exec（机械臂执行）/ arm_manual（界面手掰）/
│   │               #   npu_cls + npu_async / collector / img_writer
│   ├── hw/         #   硬件与总线访问：des_hw + des_regs（↔ PL 契约）/ can_link + can_proto（↔ PLC 契约）
│   ├── sched/      #   纯逻辑库（无硬件 / OpenCV 依赖）：tracker / picker / arm（IK）/ flap / geom
│   ├── measure/    #   几何测量与标定：measure / track_measure / roi_crop（产品与运维共用）/ measure_tool / calib（仅 des_tool）
│   ├── ui/         #   界面：hmi / ui_input / text_ft / hdmi_out（OpenCV 自绘 + FreeType，不用 Qt）
│   ├── vision/     #   相机取帧 + 推理封装（厂家 face_detection demo 的 4 个改造件）
│   └── des_app.json#   配置模板（板上与二进制同目录）
├── tools/          # 第④层：desreg 寄存器调试、canplc（扮「PLC」，回环 / 联调）
├── tests/          # 第③层：test_scheduler（调度库单元）、test_calib（标定，合成图）、test_measure（齿轮 / 齿条测量，合成图）、
│                   #   test_belt_hil（传送带控制硬件在环，板上 PL v5）、test_can（CAN 协议与收发，回环自测）、
│                   #   test_npu（NPU 推理核对与耗时）、test_servo_gap（舵机应答间隔表征，无断言）
├── kernel/         # mipicam 驱动源码 + 独立 Makefile（唯一一份 mipicam_module.h 也在这里）；板上其余驱动用厂家预编译 .ko
└── vendor/         # 非我们写的：third_party/json.hpp（编入）+ demo/（厂家 demo 未编入件，改造起点）
```

## des_app（产品运行时：`run` / `live`）

| 模式 | 作用 |
|---|---|
| `des_app live [-n 秒] [--belt mm/s] [--dump x.png]` | **合成主循环**：核 0 取帧 + HDMI 60fps + 叠加（跟踪框按当前帧带位置重投影、原始 blob、判定线、状态栏），核 1 定位 + 跟踪表 + 调度。实测 20s 1200 帧 59.96fps，工作线程处理 99.9% 的帧。`des_service.sh` 的 `MODE=live` 跑的也是它（降级 / 演示档） |
| `des_app run [-n 秒] [--can can0] [--go] [--belt mm/s]` | **总装（自启服务跑的就是它）**：live 的 60fps 显示 + 定位 / 跟踪 / 调度，加停带控制（BeltDriver）、机械臂线程（ArmExec，不加 `--go` 按航点时间空跑）、CAN「PLC」联动（状态机 空闲 / 运行 / 暂停 / 急停 / 故障，命令应答，10Hz 状态、1Hz 计数、每件结果）。没开 CAN 直接进入运行 |
| `des_app --version` | 打印程序名 + 构建日期 / 时间（板上核对跑的是哪次构建） |

`des_app` 的其他参数（`-c` / `--vmax` / `--exp` / `--gain` / `--no-rt`）见下面「开关速查」。**运维 / 测试模式不在 `des_app` 里**：`des_app probe` 之类会明确报错并提示用 `des_tool`。

## des_tool（人工运维 / 现场工具：11 个模式）

| 模式 | 作用 |
|---|---|
| `des_tool probe [-n 秒] [--vmax N] [--exp µs]` | 取帧时序：PS 实收帧率、PL 传感器帧率、丢帧（相邻两次取帧的 PL 帧号差）、重提交延迟；`pl_check.sh` 后台跑的就是它 |
| `des_tool locate [-n 帧] [--dump x.png]` | 取帧 + 定位，打印每件 (x, y, 角度, 长, 宽)，导出阈值 / 外接矩形调试图 |
| `des_tool track [-n 秒] [--belt mm/s]` | 取帧 + 定位 + 跟踪表 + 调度（不动机械臂），打印建轨 / 判定 / 停带计划；`--belt` 会让皮带真走 |
| `des_tool display [-n 秒]` | 相机 → HDMI 60fps：每帧 BGRA→BGR 进 CMA 显示缓冲 + 切显示 VDMA 帧存（`PARK_PTR`，帧边界生效；地址只在开机提交一次，docs/软件与UI.md）（实测 6.2ms/帧，60fps）；退出恢复 fb0。`display_check.sh` 第 2 段用它 |
| `des_tool collect --cls <类别> [--belt mm/s] [--every N] [--max N] [--full N]` | live + 采训练裁切图（一次一类，按零件分目录；`--full` 另存整帧 + 自动 YOLO 框），见 train/README |
| `des_tool measure [-n 帧] [--image f.png] [--out raw.png] [--dump report.png]` | 每个齿轮 / 齿条都测一遍（定位 → ROI → 测量），打印外径 / 孔径 / 偏心 / 齿数 / 缺陷或长度 / 齿距；`-n` > 1 时零件不动、统计各量均值 / σ / 极差（重复性，测试报告用）；`--out` 存原始帧供 `--image` 离线复测；`--dump` 出标注图（拟合圆、齿条框、r(θ) 曲线）。实物到货后对卡尺真值 |
| `des_tool calib-cam --board 9x6 --square <mm> [--travel right] [--image f.png] [--dump x.png]` | 棋盘格平放皮带上 → 像素→台面毫米单应矩阵，写 `calib.json`（`load_config` 自动合并）。坐标系与操作见 docs/视觉与检测.md「相机标定」；合成图测试 `build/test_calib` |
| `des_tool calib-belt [--belt mm/s] [--steps N]` | 相机标定后：零件放视野上游 → 皮带短走 / 长走 / 退回，出 mm/步、方向、往返误差、横向漂移、等效辊径，写 `calib.json` 的 `belt` 段（docs/视觉与检测.md「传送带标定」） |
| `des_tool calib-arm [-n 4] [--marker-h mm]` | 手眼标定（交互）：标记物放视野 → 相机定位 → 手掰夹爪尖碰标记 → 读关节角；≥3 点拟合底座在台面的位置 / 朝向（+ 带面高度），写 `calib.json` 的 `arm` 段（docs/运动控制与分拣.md「上电调试步骤」第 6 步） |
| `des_tool arm scan / read / teach / torque-on / torque-off / home / goto x y / pick x y [角度] [料盒] [--go]` | 机械臂上电调试：扫舵机、读位置与角度、手掰示教、回零、走点、整套取放。默认只打印舵机码，加 `--go` 才动（步骤见 docs/运动控制与分拣.md「上电调试步骤」） |
| `des_tool ik <x> <y> [z] [角度]` | 台面坐标 → 关节角 / 舵机码（纯计算，不碰 PL） |
| `des_tool --version` | 打印程序名 + 构建日期 / 时间（与 `des_app --version` 同格式） |

上板启动器：`des_tool.sh <模式> …`（源 `scripts/deploy/out/`，手动 scp 到 `/root/des/`）——与 `des_app.sh` 同一套规则：先取本目录 `des.env` 的路径、缺 `/dev/mipicam` 时 `insmod`、`des_app` / `des_tool` 已在跑时拒绝启动；`des_tool` 本体在 `/root/bin/`（与 `desreg` / `canplc` / `test_*` 一致）。

## 测试 / 回归脚本（手动上板，不在清单里）

| 脚本 | 作用 |
|---|---|
| `soak.sh [秒] [目录]`（`scripts/deploy/out/soak.sh`） | 长时间运行：des_app run + canplc 当 PLC，每分钟记内存 / 负载 / CAN / PL 帧号，结束出 summary.txt（测试报告「连续运行」） |
| `pl_check.sh`（`scripts/deploy/out/pl_check.sh`） | 换比特流后的上板回归（18 项，后台跑 `des_tool probe`），自动暂停 / 恢复自启服务 |
| `display_check.sh [秒]`（`scripts/deploy/out/display_check.sh`） | HDMI 周期性黑屏三段对照（控制台 / `des_tool display` / 正常运行），人工看屏、自动记录 VDMA 状态 |
| `des_service.sh start\|stop\|restart\|status`（`scripts/deploy/out/des_service.sh`，**产品运行时，随部署清单上板**） | 开机自启 + 守护：`des_app run --can can0`，崩溃 2s 后重启（退避到 30s，5 分钟内退出 5 次就停），日志 `data/service.log`；设置在 `autostart.conf`（首次运行生成：ENABLED / MODE / CAN / CAN_MODE / ARGS）。带 CAN 时上电是「空闲」，PLC 或界面按启动才动；`--go` 写进 ARGS 机械臂才真动。**装开机自启要改 /etc**：`ln -s /root/des/des_service.sh /etc/init.d/S90des`（经用户同意再装） |

### 谁在用这些命令（免得把精力花在边角模式上）

| 位置 | 命令 | 什么时候用 |
|---|---|---|
| ★ 产品链，天天跑 | `des_app run`（自启服务跑的就是 `des_app run --can can0`）；`des_app live` = 它去掉带 / 臂 / CAN 的版本，演示用 | 上料分拣、演示、整机联调 |
| ★ 每次改动的回归 | `des_tool probe`、`des_tool display` | 换比特流 / 改相机或显示代码后验 60fps、0 丢帧、BGRA→BGR 开销；`pl_check.sh` 后台跑的也是它 |
| ★ 到货标定（一次性，换相机 / 换带 / 换臂位才重做） | `des_tool calib-cam` → `calib-belt` → `calib-arm` | 结果写 `calib.json`，之后产品与运维模式都自动加载 |
| 到货后的验收 / 出数 | `des_tool measure -n`、`des_tool collect`、`soak.sh` | 重复性与卡尺误差、采训练图、2h 稳定性 |
| 舵机 / 机械臂首次上电 | `des_tool arm scan` / `read` / `teach` / `torque-*`，再 `torque-on` / `home` / `goto` / `pick` | 见 [到货联调清单](../docs/到货联调清单.md) §2；运动子命令不加 `--go` 只打印舵机帧 |
| 调参 / 排障备用（产品模式能覆盖一部分，价值在"一次只看一件事"） | `des_tool locate`（阈值 + 单帧定位耗时）、`track`（不带 HDMI 的跟踪表 / 停带计划）、`ik`（纯逆解，不碰 PL） | 定位或调度出问题时分段隔离；`locate` / `track` 本来就是 bring-up 链上的一环（probe → locate → track → arm） |

**没有"用不上"的模式，但有几条只在特定场合跑**：`locate` / `track` / `display` 分别是定位、调度、显示链路的隔离工具（`display` 是 HDMI 黑屏对照实验的第 2 段），日常被 `live` / `run` 覆盖；回归脚本同理 —— 不进部署清单，要用时手动 `scp`。

### 开关速查（参数解析在 `src/app/cli_common.cpp`；usage 文本在 `cli_common.cpp`（des_app 的 run/live）与 `src/app/tool_main.cpp`（des_tool 的 11 模式））

两个入口共用同一套解析（`cli_common.cpp`），下表"用在哪"指模式；`des_app` 只接受 `run` / `live`，其余模式在 `des_tool`。

| 开关 | 用在哪 | 含义 |
|---|---|---|
| `-c <cfg.json>` | 全部 | 配置文件；缺省 `$DES_CONFIG`（`des.env` 给），再缺省当前目录 `des_app.json` |
| `-n <N>` | 除 `ik` / `arm *` | 秒（`probe` / `display` / `track` / `live` / `run` / `collect`）、帧数（`locate` / `measure`）、标定点数（`calib-arm`） |
| `--vmax N` `--exp <µs>` `--gain <dB×10>` | 用相机的模式 | 只影响本次运行：覆盖 VMAX（帧率）/ 曝光 / 增益，不写回配置 |
| `--dump <png>` | `locate` / `live` / `calib-cam` / `measure` | 存调试图（定位叠加、角点、测量标注） |
| `--image <png>` | `calib-cam` / `measure` | 不连相机，读一张图离线复测 |
| `--out <文件\|目录>` | `calib-*` / `measure` / `collect` | 标定结果 json / 原始帧 png / 采集目录（`collect` 缺省用 `$DES_COLLECT`） |
| `--belt <mm/s>` | `track` / `live` / `collect` / `run` | 皮带速度：前三者带会走、但抓取只打印计划（空跑），`run` 是给状态机的运行速度 |
| `--go` | `run` / `arm home\|goto\|pick` | 真动机械臂；不加只打印舵机帧（干跑） |
| `--can <if>` | `run` | CAN 接口（如 `can0`）；不写就不联 PLC，直接进运行态 |
| `--no-rt` | 全部 | 不进 SCHED_FIFO、不 `mlockall`（排查调度 / 内存问题时用） |
| `--board <列>x<行>` `--square <mm>` `--travel …` `--exhaustive` | `calib-cam` | 棋盘格内角数 / 格边长 mm / 零件走向 / 角点搜索更慢更全（超时 20s → 120s） |
| `--steps <N>` | `calib-belt` | 长走步数（0 = 走到视野约 75%） |
| `--marker-h <mm>` | `calib-arm` | 标记物顶面高出带面的高度（顺带拟合 `arm.belt_z`） |
| `--cls <名>` `--every N` `--max N` `--full N` | `collect` | 类别名 / 每 N 帧采一张 / 每件最多 N 张 / 每 N 帧另存整帧 + YOLO 框 |
| `--version` | 两个入口 | 打印程序名 + 构建日期 / 时间，然后退出 |
| 裸数字 | `ik <x> <y> [z] [角度]`、`arm goto\|pick <x> <y> …` | 台面坐标 mm、角度 deg、料盒号 |

- 坐标约定见 `src/sched/geom.hpp`：图像 px → 台面 mm（单应矩阵，配置 `px_to_table`），件在带上 `s = x − B`（B = 带走过的距离），停带 / 反转都无需特殊处理；停带 = 给步进位置模式写「停在 B = 抓取线 − s」，减速由 PL 梯形曲线完成（`stepper_ctrl`）。目标写得太晚（已在身后）时 PL 减速停在它之后（`AT_TARGET` = 0），调度器按刹车距离判断来不及就转复检、不倒车。
- 取帧：厂家帧 DMA「提交一块写一帧」，完成后立即重提下一块（4 块环形缓冲）。**实测 SCHED_FIFO 与普通调度都 0 丢帧**，重提 0.2µs —— demo 的 28fps 是串行流水造成的，不是硬件限制。
  PL v4 起相机 59.83fps，`src/des_app.json` 的 `camera.vmax = 2276` 补到 **60.02fps**（20s 1201 帧 0 丢帧）。PL v5 起每帧带 PL 锁存的拍摄时刻带位置（`CAP_*`，实测与「速度 × 读出时间」一致），旧比特流自动退回「完成后读」。
- 定位：1/4 降采样灰度（480×270）+ Otsu（下限钳位）+ 开运算 + 外轮廓 + minAreaRect，板上 6.8ms/帧。1/2 降采样要 21ms（DMA 缓冲读像素慢），超帧预算。
- 几何测量（P1）：`src/measure/measure`，齿轮外径 / 根径 / 孔径 / 偏心 / 规格齿数 / 缺料·缺齿·飞边，齿条长度 / 齿距 / 同上，含视差校正，4~5ms / 件；方法与合成图精度见 docs/视觉与检测.md「几何测量」，测试 `build/test_measure`（57 项）。✅ 已接入 live / run（`src/measure/track_measure`：每件取离光轴最近那帧，结果进跟踪表与 CAN；尺寸 / 规格判定 `measure.rule` 默认关）。
- 配置：`src/des_app.json`（所有标定值，标 TODO 的待实物标定）；产品配方 `src/recipes.json`（齿数 / 外径 / 孔径 / 偏心 / 齿条长度及公差，PLC 的 CAN RECIPE 或界面参数页切换，停机换型）；类别表从 `models/label_list.json` 读（由 `src/app/config.cpp` 解析）。板上部署到 `/root/des/`，走 [scripts/deploy](../scripts/deploy/README.md) 的清单（`des_app`、`des_app.json`、`recipes.json`、`label_list.json` + NPU 模型 + 自启链脚本 `des_service.sh` / `can_up.sh` / `des_app.sh`）。

## src/vision/ 的定位与改造边界

`src/vision/` 来自安路官方 face_detection demo，是**检测流水线的改造起点**，已改成齿轮/齿条零件检测。只放编进二进制的 4 个 `.cpp`：取帧 `mipi_camera`（`/dev/mipicam` DMA，`icamera::enumerate_device` 的定义并在这里，不再依赖未编的 `icamera_enum.cpp`）、推理 `executor`（`NPUExecutor`，ArmNN+Alnpu）、`flow_common_types`、`logger_forward`；显示不走厂家的 `ihdmi`（HDMI 输出是 `src/ui/hdmi_out.cpp`）。厂家 demo 的其余现成件（前处理 `preprocess`、8 缓冲流水 `BufferedNPUExecutor`、后处理 `getFastDecodeOutputYolov8WorkLoad` 走 JSON）在 [vendor/demo](vendor/demo/README.md)，**没编入**；删掉了哪些、怎么取回见该 README。

## 编译、构建与部署

- 交叉编译器 FD 自带 `toolchain\gcc\aarch64_linux\bin\aarch64-linux-gnu-gcc`（glibc 2.25，与板上一致）；板上以 root 运行，PL 寄存器经 `/dev/mem` mmap 访问（已验证可用）；部署 `scp` 到板上（根分区持久化）。

```bash
# Windows Git Bash（FD 自带 make 与交叉 gcc）；Linux 上传 CROSS= / SDK= 覆盖
<FD_ROOT>/toolchain/build-tools/bin/make.exe -C ps all   # → ps/build/{des_app,des_tool,desreg,test_*,canplc}
# 只重编两个应用：make -C ps build/des_app build/des_tool
# 上板走 scripts/deploy（清单 + 对比 / 部署 / 暂存）：make -C ps all 结束会自动刷新 out/
.\scripts\deploy\deploy.ps1 -DryRun   # 板上现在跑的是哪一份
.\scripts\deploy\deploy.ps1           # 部署（要更新 des_app 时加 -WithService，自己停/起自启服务）
# 手工传也行（产品件目标路径就是清单里的那两条）：scp ps/build/des_app ps/src/des_app.json $BOARD_HOST:/root/des/
# 运维件：scp ps/build/des_tool $BOARD_HOST:/root/bin/ ; scp scripts/deploy/out/des_tool.sh $BOARD_HOST:/root/des/
# 板上运行：/root/des/des_app.sh run --can can0 / /root/des/des_tool.sh probe -n 10   （mipicam.ko 不在开机模块里，两个启动器都会先 insmod）
```

- **`make all` 出 9 个产物**：`des_app`、`des_tool`、`desreg`、`canplc` + 5 个 `test_*`（scheduler / calib / measure / belt_hil / can）。只有 `des_app` 属于板端产品运行时（进部署清单）；`des_tool` / `desreg` / `canplc` / `test_*` 要用时手动 `scp`。`test_npu` 与 `test_servo_gap` **不在 `all` 里**，要显式点名（`make -C ps build/test_npu` 需模型 JSON + refs；`build/test_servo_gap`）。
- 构建方式：两个入口共享的源码各编一次成 `build/obj/*.o`（`-MMD -MP` 自动头文件依赖），`des_app` = `main.o` + 共享对象 + `des_hw.o`/`can_link.o`，`des_tool` = `tool_main.o` + 共享对象 + 标定 / 测量报告对象（`calib.o`/`measure_tool.o`，**不链进产品**）+ `des_hw.o`/`can_link.o`；`test_*` 的配方保持各自列源、互不影响。第二遍 `make` 应是 no-op。
- `des_app` / `des_tool` 需要厂家 NPU demo 的 `libs/`（OpenCV 4.7 + ArmNN，CMA 分配在 libarmnn 里），Makefile 变量 `SDK` 指向它；不入库（113MB）。Windows 解压会丢 `.so` 软链接，先跑 `sh scripts/sdk_fix_sonames.sh <SDK> <readelf>` 按 SONAME 补齐。无 SDK 的单测（`test_scheduler` / `test_can` / `test_servo_gap` / `test_belt_hil`）保持各自直接列源编译，不需要 SDK 变量。
- 运行时库路径已写进 rpath（`/opt/armnn_lib/lib`、`/opt/ffmpeg_opencv4.7.0_aarch64/lib`），板上不用设 `LD_LIBRARY_PATH`；`/dev/mipicam` 独占打开，`des_app` / `des_tool` / 厂家 demo 同一时刻只能跑一个。

## 工具（第④层）

`tools/desreg`：读写 PL 寄存器（`desreg` 状态 / `r <off>` / `w <off> <val>` / `led on|off|auto` / `lat [n]` 测 PS→PL 读延迟 / 舵机 `ping|sread|swrite|sync` / 步进 `step ...`）。写前核对 `DES_ID`，比特流里没有 `des_regs` 时拒绝写 —— 否则该窗口别名到厂家相机 DMA 寄存器，写入会写乱内存。⚠ `des_app` / `des_tool` 运行期间 `desreg` 只用 `r` 只读（舵机总线 / 步进是单主资源，写操作统一由 `des_app` / `des_tool` 执行）。

`tools/canplc`：扮「产线 PLC」的命令行（`start|stop|pause|resume|reset|speed|recipe|monitor`），板上回环测试与 `soak.sh` 的陪测对端，也是 `pc/plc_sim` 的协议参考实现。

## 约定

- `src/hw/des_regs.h` 与 `docs/PL设计.md`、PL 端 `pl/soc_base/src/des/des_regs.v` / `servo_bus.v` / `stepper_ctrl.v` 同一次提交更新；访问 PL 用 `src/hw/des_hw`（舵机事务、步进、帧计数都在里面）。
- 类别表从 `models/label_list.json` 统一加载；时间戳、计数、统计接口随功能一起写（测试报告用）。
- **头文件平铺**：全仓库没有跨目录 `#include`，靠 `ps/Makefile` 的 `INC`（`-Isrc/app -Isrc/hw -Isrc/sched -Isrc/measure -Isrc/ui -Isrc/vision -Ikernel -Ivendor/third_party`）解析 —— 因此**各目录 basename 必须唯一**，重名会静默命中先搜到的那个（`build/obj/` 平铺 `.o` 也依赖这条）。
- 移动文件时同步改：`ps/Makefile` 的源列表 / `INC` / `SHARED_SRC` / `OPS_SRC`、本文档、以及 `docs/`+`pc/`+`train/` 里按路径引用的契约（`des_regs.h`、`can_proto.h`、`part_log.cpp` 的 CSV 列、`roi_crop` 的裁切几何、`runner.cpp` 的状态机）。
- 新增命令时先问"它属于哪一层"：产品运行时加进 `main.cpp` 的 `run` / `live`，人工运维加进 `tool_main.cpp` 并更新本文档的两张表，自动断言加进 `tests/`（退出码=失败项数），底层调试加进 `tools/`。
