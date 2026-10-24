# ps/kernel — mipicam 内核模块（仅需要改驱动时才重编）

板上 rootfs 已带厂家预编译模块（`/opt/bin/*.ko`）；本目录是唯一预期会改的驱动 **`mipicam.ko`**（`/dev/mipicam`，DMA 取帧 + 中断等待；源码来自安路 face_detection SDK，MIT），只在需要帧起始时间戳给 PL 帧锁存对账时动。cma_mem / hard_npu / soft_npu / jpu 用板上预编译 `.ko`，源码在厂家 face_detection SDK 的 `src/linux_driver/`（不入库，需要时从厂家资料包取）。

`mipicam_module.h`（ioctl ABI）**只有这一份**：用户态 `src/vision/mipi_camera.cpp` 通过 `ps/Makefile` 的 `-Ikernel` 用同一个文件，改 ioctl 两边不会分叉。

重编（需米联客 Linux 开发包内核树，获取与验证见 [开发环境与构建](../../docs/开发环境与构建.md)）：

```bash
cd ps/kernel
make build KERNELDIR=<内核树路径> CROSS_PREFIX=<aarch64工具链前缀>
# 产物 mipicam.ko；板上： rmmod mipicam && insmod mipicam.ko
```

**已知问题（厂家代码，刻意不改）**：读写寄存器 ioctl 不检查偏移、mmap 不检查长度、`devm_ioremap_resource` 返回值不查、打开计数与中断标志没加锁。设备只有 root 能开、且同一时刻只有一个程序在用（des_app / des_tool 启动时互斥），实测 2h 60fps 只跳 13 帧；改它要重编内核模块并替换板上 `.ko`，风险大于收益。真要改驱动（如上面的帧时间戳）时一起修。
