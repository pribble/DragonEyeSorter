# mipicam 内核模块（源码来自安路 face_detection SDK，MIT）
# 板上已加载厂家预编译同名模块；仅当需要改驱动时才重编。
# 重编用米联客 Linux 开发包的内核树（uisrc-lab-anlogic，见 docs/09）：
#   make KERNELDIR=<开发包内核树> CROSS_PREFIX=<工具链前缀>
CROSS_PREFIX ?= aarch64-linux-gnu-
CURRENT_PATH := $(shell pwd)
ARCH := arm64
obj-m := mipicam.o

build: kernel_modules

kernel_modules:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) modules ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_PREFIX)

clean:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) clean
