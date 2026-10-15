# ps/ — 处理系统软件（PS）

双核 A35 上的 Linux 应用。职责见 [docs/软件与UI](../docs/软件与UI.md)，开发环境见 [docs/开发环境与构建](../docs/开发环境与构建.md)。

## 主次地图（先看这张）

| 层级 | 目录 | 是什么 | 部署到哪 |
|---|---|---|---|
| ★ **产品** | `src/` | **编进 `des_app` 的全部源码**——只有它决定产物 | 编出的 `des_app` + `des_app.json` → `/root/des/` |
| 支撑 | `kernel/` | `mipicam.ko`（相机 DMA 驱动，厂家源码改造起点） | 板上已有厂家预编译模块；改驱动才重编 |
| | `board/` | 上板脚本：启动器、CAN 起接口、回归、长跑 | `/root/des/` |
| | `tools/` | 人敲的命令行：`desreg`（寄存器调试）、`canplc`（扮 PLC） | `/root/bin/` |
| | `tests/` | `test_*`：离线算法回归 + 上板硬件在环 | 不进产物，需要时 scp 到 `/root/bin/` |
| 非我们写的 | `vendor/` | nlohmann json 单头（编入）+ 厂家 demo 未编入件 | 不进产物（除 json.hpp） |
| 产物 | `build/` | `make` 输出（`**/build/` 已忽略） | — |

一句话：**读代码从 `src/` 开始，其余目录都是围绕它的支撑/工具/测试。**

## 目录

```
ps/
├── Makefile        # 唯一构建入口（交叉编译自研程序，见「编译」）
├── src/            # ★ 产品源码：只放编进 des_app 的，再按角色分层（头文件平铺，靠 INC 解析）
│   ├── app/        #   模式与线程编排：main / runner（CAN 状态机总装）/ live（60fps 主循环）/
│   │               #   camera_rig（取帧）/ locate（定位）/ config / part_log（存储）/
│   │               #   belt_ctl（停带）/ arm_exec（机械臂执行）/ npu_cls + npu_async / collector / img_writer
│   ├── hw/         #   硬件与总线访问：des_hw + des_regs（↔ PL 契约）/ can_link + can_proto（↔ PLC 契约）
│   ├── sched/      #   纯逻辑库（无硬件 / OpenCV 依赖）：tracker / picker / arm（IK）/ flap / geom
│   ├── measure/    #   几何测量与标定：measure / track_measure / roi_crop / measure_tool / calib
│   ├── ui/         #   界面：hmi / ui_input / text_ft / hdmi_out（OpenCV 自绘 + FreeType，不用 Qt）
│   ├── vision/     #   相机取帧 + 推理封装（厂家 face_detection demo 的 4 个改造件）
│   └── des_app.json#   配置模板（板上与二进制同目录）
├── board/          # 上板脚本：des_app.sh（insmod + 启动）/ can_up.sh / pl_check.sh / soak.sh
├── tools/          # 人敲的命令行：desreg 寄存器调试、canplc（扮「PLC」，回环 / 联调）
├── tests/          # test_scheduler（调度库单元）、test_calib（标定，合成图）、test_measure（齿轮 / 齿条测量，合成图）、
│                   #   test_belt_hil（传送带控制硬件在环，板上 PL v5）、test_can（CAN 协议与收发，回环自测）、
│                   #   test_npu（NPU 推理核对与耗时）、bench_cma（CMA 读速基准，手编，无规则）
├── kernel/         # mipicam 驱动源码 + 独立 Makefile（唯一一份 mipicam_module.h 也在这里）；板上其余驱动用厂家预编译 .ko
└── vendor/         # 非我们写的：third_party/json.hpp（编入）+ demo/（厂家 demo 未编入件，改造起点）
```

## des_app（PS 主程序）

| 模式 | 作用 |
|---|---|
| `des_app probe [-n 秒] [--vmax N] [--exp µs]` | 取帧时序：PS 实收帧率、PL 传感器帧率、丢帧（相邻两次取帧的 PL 帧号差）、重提交延迟 |
| `des_app locate [-n 帧] [--dump x.png]` | 取帧 + 定位，打印每件 (x, y, 角度, 长, 宽)，导出阈值 / 外接矩形调试图 |
| `des_app track [-n 秒] [--belt mm/s]` | 取帧 + 定位 + 跟踪表 + 调度（不动机械臂），打印建轨 / 判定 / 停带计划 |
| `des_app ik <x> <y> [z] [角度]` | 台面坐标 → 关节角 / 舵机码（空跑） |
| `des_app live [-n 秒] [--belt mm/s] [--dump x.png]` | **合成主循环**：核 0 取帧 + HDMI 60fps + 叠加（跟踪框按当前帧带位置重投影、原始 blob、判定线、状态栏），核 1 定位 + 跟踪表 + 调度。实测 20s 1200 帧 59.96fps，工作线程处理 99.9% 的帧 |
| `des_app run [-n 秒] [--can can0] [--go] [--belt mm/s]` | **总装**：live 的 60fps 显示 + 定位 / 跟踪 / 调度，加停带控制（BeltDriver）、机械臂线程（ArmExec，不加 `--go` 按航点时间空跑）、CAN「PLC」联动（状态机 空闲 / 运行 / 暂停 / 急停 / 故障，命令应答，10Hz 状态、1Hz 计数、每件结果）。没开 CAN 直接进入运行 |
| `canplc <if> start\|stop\|pause\|resume\|reset\|speed <mm/s>\|recipe <n>\|monitor [秒]` | 扮「PLC」的命令行（`tools/canplc.c`，板上回环测试 + PC 端程序参考实现） |
| `des_app arm scan / read / teach / torque-on / torque-off / home / goto x y / pick x y [角度] [料盒] [--go]` | 机械臂上电调试：扫舵机、读位置与角度、手掰示教、回零、走点、整套取放。默认只打印舵机码，加 `--go` 才动（步骤见 docs/运动控制与分拣.md「上电调试步骤」） |
| `des_app calib-cam --board 9x6 --square <mm> [--travel right] [--image f.png] [--dump x.png]` | 棋盘格平放皮带上 → 像素→台面毫米单应矩阵，写 `calib.json`（`load_config` 自动合并）。坐标系与操作见 docs/视觉与检测.md「相机标定」；合成图测试 `build/test_calib` |
| `des_app calib-belt [--belt mm/s] [--steps N]` | 相机标定后：零件放视野上游 → 皮带短走 / 长走 / 退回，出 mm/步、方向、往返误差、横向漂移、等效辊径，写 `calib.json` 的 `belt` 段（docs/视觉与检测.md「传送带标定」） |
| `des_app calib-arm [-n 4] [--marker-h mm]` | 手眼标定（交互）：标记物放视野 → 相机定位 → 手掰夹爪尖碰标记 → 读关节角；≥3 点拟合底座在台面的位置 / 朝向（+ 带面高度），写 `calib.json` 的 `arm` 段（docs/运动控制与分拣.md「上电调试步骤」第 6 步） |
| `des_app collect --cls <类别> [--belt mm/s] [--every N] [--max N] [--full N]` | live + 采训练裁切图（一次一类，按零件分目录；`--full` 另存整帧 + 自动 YOLO 框），见 train/README |
| `des_app measure [-n 帧] [--image f.png] [--out raw.png] [--dump report.png]` | 每个齿轮 / 齿条都测一遍（定位 → ROI → 测量），打印外径 / 孔径 / 偏心 / 齿数 / 缺陷或长度 / 齿距；`-n` > 1 时零件不动、统计各量均值 / σ / 极差（重复性，测试报告用）；`--out` 存原始帧供 `--image` 离线复测；`--dump` 出标注图（拟合圆、齿条框、r(θ) 曲线）。实物到货后对卡尺真值 |
| `des_app display [-n 秒]` | 相机 → HDMI 60fps：每帧 BGRA→BGR 进 CMA 显示缓冲 + 写显示 VDMA `START_1` 再写 `V_SIZE` 提交（只写地址不生效，docs/软件与UI.md）（实测 6.2ms/帧，60fps）；退出恢复 fb0 |
| `soak.sh [秒] [目录]`（`board/soak.sh`，部署到 /root/des/） | 长时间运行：des_app run + canplc 当 PLC，每分钟记内存 / 负载 / CAN / PL 帧号，结束出 summary.txt（测试报告「连续运行」） |
| `pl_check.sh`（`board/pl_check.sh`，部署到 /root/des/） | 换比特流后的上板回归（19 项，后台跑 `des_app probe`） |

- 坐标约定见 `src/sched/geom.hpp`：图像 px → 台面 mm（单应矩阵，配置 `px_to_table`），件在带上 `s = x − B`（B = 带走过的距离），停带 / 反转都无需特殊处理；停带 = 给步进位置模式写「停在 B = 抓取线 − s」，减速由 PL 梯形曲线完成（`stepper_ctrl`）。目标写得太晚（已在身后）时 PL 减速停在它之后（`AT_TARGET` = 0），调度器按刹车距离判断来不及就转复检、不倒车。
- 取帧：厂家帧 DMA「提交一块写一帧」，完成后立即重提下一块（4 块环形缓冲）。**实测 SCHED_FIFO 与普通调度都 0 丢帧**，重提 0.2µs —— demo 的 28fps 是串行流水造成的，不是硬件限制。
  PL v4 起相机 59.83fps，`src/des_app.json` 的 `camera.vmax = 2276` 补到 **60.02fps**（20s 1201 帧 0 丢帧）。PL v5 起每帧带 PL 锁存的拍摄时刻带位置（`CAP_*`，实测与「速度 × 读出时间」一致），旧比特流自动退回「完成后读」。
- 定位：1/4 降采样灰度（480×270）+ Otsu（下限钳位）+ 开运算 + 外轮廓 + minAreaRect，板上 6.8ms/帧。1/2 降采样要 21ms（DMA 缓冲读像素慢），超帧预算。
- 几何测量（P1）：`src/measure/measure`，齿轮外径 / 根径 / 孔径 / 偏心 / 规格齿数 / 缺料·缺齿·飞边，齿条长度 / 齿距 / 同上，含视差校正，4~5ms / 件；方法与合成图精度见 docs/视觉与检测.md「几何测量」，测试 `build/test_measure`（57 项）。✅ 已接入 live / run（`src/measure/track_measure`：每件取离光轴最近那帧，结果进跟踪表与 CAN；尺寸 / 规格判定 `measure.rule` 默认关）。
- 配置：`src/des_app.json`（所有标定值，标 TODO 的待实物标定）；产品配方 `src/recipes.json`（齿数 / 外径 / 孔径 / 偏心 / 齿条长度及公差，PLC 的 CAN RECIPE 或界面参数页切换，停机换型）；类别表从 `models/label_list.json` 读（由 `src/app/config.cpp` 解析）。板上部署到 `/root/des/`（`des_app`、`des_app.json`、`label_list.json`）。

## src/vision/ 的定位与改造边界

`src/vision/` 来自安路官方 face_detection demo，是**检测流水线的改造起点**，已改成齿轮/齿条零件检测。只放编进 `des_app` 的 4 个 `.cpp`：取帧 `mipi_camera`（`/dev/mipicam` DMA，`icamera::enumerate_device` 的定义并在这里，不再依赖未编的 `icamera_enum.cpp`）、推理 `executor`（`NPUExecutor`，ArmNN+Alnpu）、`flow_common_types`、`logger_forward`；显示不走厂家的 `ihdmi`（HDMI 输出是 `src/ui/hdmi_out.cpp`）。厂家 demo 的其余现成件（前处理 `preprocess`、8 缓冲流水 `BufferedNPUExecutor`、后处理 `getFastDecodeOutputYolov8WorkLoad` 走 JSON）在 [vendor/demo](vendor/demo/README.md)，**没编入**；删掉了哪些、怎么取回见该 README。

## 编译、构建与部署

- 交叉编译器 FD 自带 `toolchain\gcc\aarch64_linux\bin\aarch64-linux-gnu-gcc`（glibc 2.25，与板上一致）；板上以 root 运行，PL 寄存器经 `/dev/mem` mmap 访问（已验证可用）；部署 `scp` 到板上（根分区持久化）。

```bash
# Windows Git Bash（FD 自带 make 与交叉 gcc）；Linux 上传 CROSS= / SDK= 覆盖
<FD_ROOT>/toolchain/build-tools/bin/make.exe -C ps   # → ps/build/{desreg,test_*,canplc,des_app}
scp -i ~/.ssh/id_ed25519_anlu ps/build/des_app ps/board/des_app.sh ps/src/des_app.json ps/src/recipes.json models/label_list.json root@192.168.137.100:/root/des/
# 板上运行：/root/des/des_app.sh probe -n 10   （mipicam.ko 开机不自动加载，脚本缺模块时先 insmod）
```

- **`make all` 出 8 个产物**；`test_npu` 要显式点名（`make -C ps build/test_npu`，需模型 JSON + refs）；`tests/bench_cma.cpp` 目前无 Makefile 规则，要用手敲的 g++ 行（见文件头注释）。
- `des_app` 需要厂家 NPU demo 的 `libs/`（OpenCV 4.7 + ArmNN，CMA 分配在 libarmnn 里），Makefile 变量 `SDK` 指向它；不入库（113MB）。Windows 解压会丢 `.so` 软链接，先跑 `sh scripts/sdk_fix_sonames.sh <SDK> <readelf>` 按 SONAME 补齐。
- 运行时库路径已写进 rpath（`/opt/armnn_lib/lib`、`/opt/ffmpeg_opencv4.7.0_aarch64/lib`），板上不用设 `LD_LIBRARY_PATH`；`/dev/mipicam` 独占打开，`des_app` 与厂家 demo 二选一运行。

## 工具

`tools/desreg`：读写 PL 寄存器（`desreg` 状态 / `r <off>` / `w <off> <val>` / `led on|off|auto` / `lat [n]` 测 PS→PL 读延迟 / 舵机 `ping|sread|swrite|sync` / 步进 `step ...`）。写前核对 `DES_ID`，比特流里没有 `des_regs` 时拒绝写 —— 否则该窗口别名到厂家相机 DMA 寄存器，写入会写乱内存。⚠ `des_app` 运行期间 `desreg` 只用 `r` 只读（舵机总线 / 步进是单主资源，写操作统一由 `des_app` 执行）。

## 约定

- `src/hw/des_regs.h` 与 `docs/PL设计.md`、PL 端 `pl/soc_base/src/des/des_regs.v` / `servo_bus.v` / `stepper_ctrl.v` 同一次提交更新；访问 PL 用 `src/hw/des_hw`（舵机事务、步进、帧计数都在里面）。
- 类别表从 `models/label_list.json` 统一加载；时间戳、计数、统计接口随功能一起写（测试报告用）。
- **头文件平铺**：全仓库没有跨目录 `#include`，靠 `ps/Makefile` 的 `INC`（`-Isrc/app -Isrc/hw -Isrc/sched -Isrc/measure -Isrc/ui -Isrc/vision -Ikernel -Ivendor/third_party`）解析 —— 因此**各目录 basename 必须唯一**，重名会静默命中先搜到的那个。
- 移动文件时同步改：`ps/Makefile` 的源列表 / `INC`、本文档、以及 `docs/`+`pc/`+`train/` 里按路径引用的契约（`des_regs.h`、`can_proto.h`、`part_log.cpp` 的 CSV 列、`roi_crop` 的裁切几何、`runner.cpp` 的状态机）。
