# ps/ — 处理系统软件（PS）

双核 A35 上的 Linux 应用。职责见 [docs/06-软件与UI.md](../docs/06-软件与UI.md)，开发环境见 [docs/09-开发环境与构建.md](../docs/09-开发环境与构建.md)。

## 布局

```
ps/
├── common/         # AXI-Lite 寄存器头文件（与 docs/03 同源）、/dev/mem 访问封装、label_list 加载、配置
├── scheduler/      # 跟踪表、抓取调度、停带 / 翻板决策、机械臂 IK
├── vision/         # 取帧、视觉定位 (x, y, θ)、ROI
├── inference/      # NPU 推理封装（ArmNN + Alnpu backend）
├── bus/            # CAN（SocketCAN）、RS485 Modbus
├── ui-qt/          # HDMI 本地界面（Qt 5.15，板上已有运行库）
└── storage/        # SQLite（amalgamation 编入）+ NG 图片
```

## 编译与运行

- 交叉编译器：FD 自带 `toolchain\gcc\aarch64_linux\bin\aarch64-linux-gnu-gcc`（glibc 2.25，与板上一致）。**不要用 WSL apt 装的 aarch64 gcc**（glibc 2.35，板上跑不了）。
- 板上以 root 运行；PL 寄存器经 `/dev/mem` mmap 访问（已验证可用）。
- 部署：`scp` 到板上（根分区持久化，拷一次一直在）。
- 摄像头 / NPU 接口参考厂家 `demo/sdk/face_detection.tar.gz`（`yolo_demo_hdmi_camera` 源码）。

## 工具

`tools/desreg`：读写 PL 寄存器（`desreg` 状态 / `r <off>` / `w <off> <val>` / `led on|off|auto` / `lat [n]` 测 PS→PL 读延迟）。
写之前会核对 `DES_ID`，比特流里没有 `des_regs` 时拒绝写 —— 否则该窗口别名到厂家相机 DMA 寄存器，写入会让相机 DMA 写乱内存。

```bat
D:\ANLU\FD\toolchain\gcc\aarch64_linux\bin\aarch64-linux-gnu-gcc.exe -O2 -Wall -o ps\tools\desreg ps\tools\desreg.c
scp -i %USERPROFILE%\.ssh\id_ed25519_anlu ps\tools\desreg root@192.168.137.100:/root/bin/
```

## 约定

- `common/` 中的寄存器偏移必须与 `docs/03-PL设计.md` 同源，改动同一次提交完成。
- 类别表从 `models/label_list.json` 加载，禁止手抄。
- 时间戳、计数、统计接口随功能一起写（测试报告用）。
