# ps/kernel_modules — mipicam 内核模块（仅需要改驱动时才重编）

板上 rootfs 已带厂家预编译模块（`/opt/bin/*.ko`）；本目录是唯一预期会改的驱动 **`mipicam.ko`**（`/dev/mipicam`，DMA 取帧 + 中断等待；源码来自安路 face_detection SDK，MIT），只在需要帧起始时间戳给 PL 帧锁存对账时动。cma_mem / hard_npu / soft_npu / jpu 用板上预编译 .ko，源码在厂家 face_detection SDK 的 `src/linux_driver/`（不入库，需要时从厂家资料包取）。

重编（需米联客 Linux 开发包内核树，见 docs/开发环境与构建.md）：

```bash
cd ps/kernel_modules
make build KERNELDIR=<内核树路径> CROSS_PREFIX=<aarch64工具链前缀>
# 产物 mipicam.ko；板上： rmmod mipicam && insmod mipicam.ko
```
