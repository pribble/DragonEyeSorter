# ps/ — 处理系统软件（PS）

双核 A35 上的 Linux 应用。职责见 [docs/06-软件与UI](../docs/06-软件与UI.md)，开发环境见 [docs/09-开发环境与构建](../docs/09-开发环境与构建.md)，第三方代码边界见 [docs/10-第三方代码引用规范](../docs/10-第三方代码引用规范.md)。

## 布局

```
ps/
├── Makefile        # 自研程序的交叉编译（desreg / test_scheduler / des_app），见下
├── common/         # 自研：寄存器头 des_regs.h（与 docs/03 同源）、des_hw（/dev/mem 上的舵机总线 / 步进 / 帧计数 C 接口）、label_list 加载
├── app/            # 自研主程序 des_app：取帧（camera_rig）、视觉定位（locate）、配置（config + des_app.json）
├── scheduler/      # 自研纯逻辑库（无硬件 / OpenCV 依赖）：跟踪表 tracker、停带 / 抓取 / 翻板调度 picker、机械臂 IK arm
├── tests/          # test_scheduler：调度库单元测试（板上或任意 Linux 跑）
├── vision/         # 检测流水线：安路 MIT demo 源码（face_detection）改造起点，后期改成零件检测
│                   #   相机/HDMI 接口、前处理、NPU 推理封装、YOLOv8 后处理、demo 主程序
├── kernel_modules/ # mipicam 驱动（MIT）；GPL 驱动不入库，见目录 README
├── tools/          # desreg 寄存器调试工具（基于 common/des_hw）
├── bus/            # TODO CAN（SocketCAN）、RS485 Modbus
├── ui-qt/          # TODO HDMI 本地界面（Qt 5.15.8，板上运行库）
└── storage/        # TODO SQLite（amalgamation 编入）+ NG 图片
```

## des_app（PS 主程序骨架）

| 模式 | 作用 |
|---|---|
| `des_app probe [-n 秒] [--vmax N] [--exp µs]` | 取帧时序：PS 实收帧率、PL 传感器帧率、丢帧（相邻两次取帧的 PL 帧号差）、重新提交缓冲延迟 |
| `des_app locate [-n 帧] [--dump x.png]` | 取帧 + 定位，打印每个件的 (x, y, 角度, 长, 宽)，导出阈值 / 外接矩形调试图 |
| `des_app track [-n 秒] [--belt mm/s]` | 取帧 + 定位 + 跟踪表 + 调度（不动机械臂），打印建轨 / 判定 / 停带计划 |
| `des_app ik <x> <y> [z] [角度]` | 台面坐标 → 关节角 / 舵机码（空跑） |
| `des_app display [-n 秒]` | 相机 → HDMI 60fps：每帧 BGRA→BGR 进 CMA 显示缓冲 + 写显示 VDMA `START_1` 翻页（实测转换 6.2ms/帧，60fps）；退出恢复 fb0 |

- 坐标约定见 `scheduler/geom.hpp`：图像 px → 台面 mm（单应矩阵，配置 `px_to_table`）；件在带上的坐标 `s = x − B`（B = 带走过的距离），停带 / 反转都不用特殊处理。
- 停带 = 给步进位置模式写「停在 B = 抓取线 − s」，减速由 PL 梯形曲线完成（`stepper_ctrl`）。⚠ 目标不能给在身后（会卡在 moving），调度器已按刹车距离判断来不及就转复检。
- 取帧：厂家帧 DMA 是「提交一块写一帧」，完成后立即重新提交下一块（4 块环形缓冲）。**实测 SCHED_FIFO 与普通调度都 0 丢帧**，重新提交 0.2µs —— 厂家 demo 的 28fps 是它的串行流水造成的，不是硬件限制。
  PL v4 起相机 59.83fps，`des_app.json` 的 `camera.vmax = 2276` 补到 **60.02fps**（20s 1201 帧 0 丢帧）。PL v5 起（10-10 已上板）每帧带 PL 锁存的拍摄时刻带位置（`CAP_*`，实测与「速度 × 读出时间」一致），旧比特流自动退回「完成后读」。
- 定位：1/4 降采样灰度（480×270）+ Otsu（下限钳位）+ 开运算 + 外轮廓 + minAreaRect，板上 6.8ms/帧。1/2 降采样要 21ms（DMA 缓冲读像素慢），超帧预算。
- 配置：`app/des_app.json`（所有标定值，标 TODO 的待实物标定）；类别规则从 `models/label_list.json` 读。板上部署到 `/root/des/`（`des_app`、`des_app.json`、`label_list.json`）。

## vision/ 的定位与改造边界

`vision/` 现有代码来自安路官方 face_detection demo（MIT），是**检测流水线的改造起点**，不是冻结的第三方依赖——我们直接在上面改成齿轮/齿条零件检测。原 demo 提供四个现成接口：
- 取帧 `icamera`（/dev/mipicam DMA）· 显示 `ihdmi`（/dev/fb0）· 推理 `BufferedNPUExecutor`（ArmNN+Alnpu）· 后处理 `getFastDecodeOutputYolov8WorkLoad`（类别/阈值走 JSON）

改造原则：保留安路版权头（MIT 义务），大改的文件头注明 "derived from Anlogic face_detection，修改：…"；改动足够大就整个重写，重写后即为我们自有代码。
- 分类加载用 `common/label_list.c`，模型用哪 8 类以 `models/label_list.json` 为唯一源，生成 NPU JSON 的脚本随后续 models/ 工具链一起写。

## 编译与运行

- 交叉编译器：FD 自带 `toolchain\gcc\aarch64_linux\bin\aarch64-linux-gnu-gcc`（glibc 2.25，与板上一致）。**不要用 WSL apt 装的 aarch64 gcc**（glibc 2.35，板上跑不了）。
- Qt 交叉编译用与板上一致的 5.15.8 头文件/库（从 rootfs 或 Buildroot 输出取；勿用厂家 05-1 demo 里的 5.13.2 包）。
- 板上以 root 运行；PL 寄存器经 `/dev/mem` mmap 访问（已验证可用）。
- 部署：`scp` 到板上（根分区持久化）。

## 构建

```bash
# Windows Git Bash（FD 自带 make 与交叉 gcc）；Linux 上传 CROSS= / SDK= 覆盖
D:/ANLU/FD/toolchain/build-tools/bin/make.exe -C ps            # → ps/build/{desreg,test_scheduler,des_app}
scp -i ~/.ssh/id_ed25519_anlu ps/build/des_app ps/app/des_app.sh ps/app/des_app.json models/label_list.json root@192.168.137.100:/root/des/
# 板上运行：/root/des/des_app.sh probe -n 10   （mipicam.ko 开机不自动加载，脚本缺模块时先 insmod）
```

- `des_app` 需要厂家 NPU demo 的 `libs/`（OpenCV 4.7 + ArmNN，CMA 分配在 libarmnn 里），Makefile 变量 `SDK` 指向它；不入库（113MB）。
  在 Windows 上解压会丢 `.so` 软链接，先跑一次 `sh scripts/sdk_fix_sonames.sh <SDK> <readelf>` 按 SONAME 补齐。
- 运行时库路径已写进 rpath（`/opt/armnn_lib/lib`、`/opt/ffmpeg_opencv4.7.0_aarch64/lib`），板上不用设 `LD_LIBRARY_PATH`。
- `des_app` 与厂家 demo 不能同时运行（`/dev/mipicam` 只允许一个进程打开）。

## 工具

`tools/desreg`：读写 PL 寄存器（`desreg` 状态 / `r <off>` / `w <off> <val>` / `led on|off|auto` / `lat [n]` 测 PS→PL 读延迟 / 舵机 `ping|sread|swrite|sync` / 步进 `step ...`）。
写之前会核对 `DES_ID`，比特流里没有 `des_regs` 时拒绝写 —— 否则该窗口别名到厂家相机 DMA 寄存器，写入会让相机 DMA 写乱内存。
⚠ `des_app` 运行时不要用 `desreg` 的写命令（舵机总线 / 步进是单主资源）。

## 约定

- `common/des_regs.h` 与 `docs/03-PL设计.md`、PL 端 `pl/rtl/des_regs.v` / `servo_bus.v` / `stepper_ctrl.v` 同一次提交更新（2026-10-10 统一；`common/pl_regs.h` 已被取代，保留待删）。
- 访问 PL 用 `common/des_hw`（舵机事务、步进、帧计数都在里面）。`common/pl_map` 的映射指针不是 `volatile`，循环里轮询寄存器可能被编译器优化掉，新代码别用。
- 类别表从 `models/label_list.json` 加载，禁止手抄。
- 时间戳、计数、统计接口随功能一起写（测试报告用）。
