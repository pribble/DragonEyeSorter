# ps/kernel_modules — 内核模块（仅需要改驱动时才重编）

板上 rootfs 已带厂家预编译模块（`/opt/bin/*.ko`），日常开发直接加载现成的即可。

| 目录 | 来源 | 板上模块 | 说明 |
|---|---|---|---|
| `mipicam_driver/` | 安路 face_detection SDK | `mipicam.ko`（/dev/mipicam，DMA 取帧 + 中断等待） | 唯一预期会改的驱动（如需帧起始时间戳给 PL 帧锁存对账） |

cma_mem / hard_npu / soft_npu / jpu 驱动用厂家预编译 .ko，源码在本机
`../reference_src/face_detection_sdk/src/linux_driver/`。

重编方法（需米联客 Linux 开发包的内核树，见 docs/09）：

```bash
cd mipicam_driver
make -C . build KERNELDIR=<内核树路径> CROSS_PREFIX=<aarch64工具链前缀>
# 板上： rmmod mipicam && insmod mipicam.ko
