# ps/ — 处理系统软件（PS）

双核 A35 上的 Linux 应用。职责见 [docs/06-软件与UI](../docs/06-软件与UI.md)，开发环境见 [docs/09-开发环境与构建](../docs/09-开发环境与构建.md)，第三方代码边界见 [docs/10-第三方代码引用规范](../docs/10-第三方代码引用规范.md)。

## 布局

```
ps/
├── common/         # 自研：AXI-Lite 寄存器头（与 docs/03 同源）、/dev/mem 封装、label_list 加载
├── vision/         # 检测流水线：安路 MIT demo 源码（face_detection）改造起点，后期改成零件检测
│                   #   相机/HDMI 接口、前处理、NPU 推理封装、YOLOv8 后处理、demo 主程序
├── kernel_modules/ # mipicam 驱动（MIT）；GPL 驱动不入库，见目录 README
├── scheduler/      # TODO 跟踪表、抓取调度、停带/翻板决策、机械臂 IK
├── inference/      # TODO（并入 vision/ 推理封装，见其 README）
├── bus/            # TODO CAN（SocketCAN）、RS485 Modbus
├── ui-qt/          # TODO HDMI 本地界面（Qt 5.15.8，板上运行库）
└── storage/        # TODO SQLite（amalgamation 编入）+ NG 图片
```

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

## 约定

- `common/pl_regs.h` 与 `docs/03-PL设计.md`、PL 端 `pl/rtl/axi_lite_regs.v` 同一次提交更新。
- 类别表从 `models/label_list.json` 加载，禁止手抄。
- 时间戳、计数、统计接口随功能一起写（测试报告用）。
