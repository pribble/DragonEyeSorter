# ps/Makefile — cross build of our own PS programs for the board (glibc 2.25 / aarch64).
#
# Windows (Git Bash):  <FD_ROOT>/toolchain/build-tools/bin/make.exe -C ps [target]
# Linux:               make -C ps CROSS=<FD_ROOT>/toolchain/gcc/aarch64_linux/bin/aarch64-linux-gnu-
#
# 推荐: 先配置 .env 文件，然后直接 make
#
# CROSS  toolchain prefix. Use the FD one (Linaro 7.4, glibc 2.25 = board); NOT a distro aarch64 gcc (newer glibc).
# ARMNN_LIB / OPENCV_LIB  两个第三方库目录，各自一个变量、各自一个根：
#        ARMNN_LIB  = ArmNN 运行时 + Alnpu 后端（含 libfmt.a）
#        OPENCV_LIB = OpenCV 4.7 + FFmpeg（aarch64 预编译）
#        与板上 /opt/armnn_lib、/opt/ffmpeg_opencv4.7.0_aarch64 同名同级；两者都不入库（合计 63 MB）。
#
# 本文件不写主机路径：FD_ROOT / ARMNN_LIB / OPENCV_LIB / FREETYPE_INC / BOARD_LIBS 一律来自 .env（本机、不入库）或命令行。
#
# 目录：src/ 产品源码（只放编进 des_app 的，再按角色分 app / hw / sched / measure / ui / vision）、
#       board/ 上板脚本、tools/ 人敲的命令行、tests/ 测试、kernel/ 驱动、vendor/ 非我们写的。
#       **头文件平铺**（全仓库无跨目录 include），靠下面的 INC 解析 —— 各目录 basename 必须唯一。
#
# Targets: desreg (C, no deps), test_scheduler (C++, no deps), test_calib / test_measure / test_npu / des_app (C++, need SDK),
#          test_belt_hil (board, PL v5+), test_can / canplc (SocketCAN), all, clean.
# Deploy:  scp -i ~/.ssh/id_ed25519_anlu build/<prog> root@192.168.137.100:/root/bin/
#          des_app 连配置一起放 /root/des/（见 README「部署」）。

# 从 .env 文件读取配置（如果存在）
-include ../.env.make

# 不给主机路径默认值：缺配置就报错并给出下一步，而不是默默去用某台机器上的目录
ENV_HINT = 先在仓库根把 .env.example 复制为 .env 并填好路径（见 docs/环境与上手.md），再运行 scripts/generate_env_make.sh；也可命令行覆盖，如 make CROSS=<工具链前缀> ARMNN_LIB=<armnn_lib 目录> OPENCV_LIB=<ffmpeg_opencv4.7.0_aarch64 目录>

ifneq ($(MAKECMDGOALS),clean)
ifeq ($(origin CROSS),undefined)
ifeq ($(strip $(FD_ROOT)),)
$(error FD_ROOT 未设置。$(ENV_HINT))
endif
endif
endif

CROSS ?= $(FD_ROOT)/toolchain/gcc/aarch64_linux/bin/aarch64-linux-gnu-
BUILD ?= build

CC  := $(CROSS)gcc
CXX := $(CROSS)g++

CFLAGS   ?= -O2 -g -Wall -Wextra
CXXFLAGS ?= -O2 -g -Wall -Wextra -std=c++17

ARMNN  := $(ARMNN_LIB)
OPENCV := $(OPENCV_LIB)
# board paths baked in as rpath, so no LD_LIBRARY_PATH is needed at run time
RPATH  := -Wl,-rpath,/opt/armnn_lib/lib:/opt/ffmpeg_opencv4.7.0_aarch64/lib

# 头文件搜索路径（平铺 include）。kernel/ 只为了 src/vision/mipi_camera.cpp 拿到唯一一份 ioctl 头，
# vendor/third_party 是 nlohmann json.hpp（app / measure / ui / vision 都用）。
INC := -Isrc/app -Isrc/hw -Isrc/sched -Isrc/measure -Isrc/ui -Isrc/vision -Ikernel -Ivendor/third_party

SCHED_SRC := src/sched/tracker.cpp src/sched/picker.cpp src/sched/arm.cpp src/sched/flap.cpp
# 相机取帧复用厂家 demo 改造件（MIT）；src/vision 只放编进二进制的 4 个 .cpp，未编的参考件在 vendor/demo/
VISION_SRC := src/vision/mipi_camera.cpp src/vision/logger_forward.cpp src/vision/flow_common_types.cpp src/vision/executor.cpp
APP_SRC := src/app/main.cpp src/app/camera_rig.cpp src/app/locate.cpp src/app/config.cpp \
           src/app/live.cpp src/app/runner.cpp src/app/belt_ctl.cpp src/app/arm_exec.cpp \
           src/app/part_log.cpp src/app/img_writer.cpp src/app/collector.cpp \
           src/app/npu_cls.cpp src/app/npu_async.cpp \
           src/measure/calib.cpp src/measure/measure.cpp src/measure/track_measure.cpp \
           src/measure/measure_tool.cpp src/measure/roi_crop.cpp \
           src/ui/hdmi_out.cpp src/ui/text_ft.cpp src/ui/ui_input.cpp src/ui/hmi.cpp
HDRS := $(wildcard src/app/*.hpp src/hw/*.h src/sched/*.hpp src/measure/*.hpp src/ui/*.hpp src/vision/*.hpp)
OPENCV_LIBS := -lopencv_calib3d -lopencv_features2d -lopencv_flann -lopencv_imgproc -lopencv_imgcodecs -lopencv_core

.PHONY: all clean check-sdk check-board

# SDK / FreeType / 板上库来自 .env；缺变量或目录不存在时先报错，别让编译器去报一堆找不到头文件。
# 用 make 自己的 wildcard + error（不经过 shell）：从 PowerShell 调 make 时配方由 cmd.exe 执行
# （FD 的 sh.exe 不在 PATH 上），`test -d` 这类 POSIX 写法会直接报 "'test' is not recognized"。
check-sdk:
	@$(if $(wildcard $(ARMNN_LIB)),echo ARMNN_LIB ok,$(error ARMNN_LIB 未设置或目录不存在：$(ARMNN_LIB)。$(ENV_HINT)))
	@$(if $(wildcard $(OPENCV_LIB)),echo OPENCV_LIB ok,$(error OPENCV_LIB 未设置或目录不存在：$(OPENCV_LIB)。$(ENV_HINT)))
check-board:
	@$(if $(wildcard $(FREETYPE_INC)),echo FREETYPE_INC ok,$(error FREETYPE_INC 未设置或目录不存在：$(FREETYPE_INC)。$(ENV_HINT)))
	@$(if $(wildcard $(BOARD_LIBS)),echo BOARD_LIBS ok,$(error BOARD_LIBS 未设置或目录不存在：$(BOARD_LIBS)。$(ENV_HINT)))

all: $(BUILD)/desreg $(BUILD)/test_scheduler $(BUILD)/test_calib $(BUILD)/test_measure $(BUILD)/test_belt_hil $(BUILD)/test_can $(BUILD)/canplc $(BUILD)/des_app

$(BUILD):
	-@mkdir $(BUILD)

$(BUILD)/desreg: tools/desreg.c src/hw/des_hw.c src/hw/des_hw.h src/hw/des_regs.h | $(BUILD)
	$(CC) $(CFLAGS) $(INC) -o $@ tools/desreg.c src/hw/des_hw.c

$(BUILD)/test_scheduler: tests/test_scheduler.cpp $(SCHED_SRC) $(wildcard src/sched/*.hpp) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ tests/test_scheduler.cpp $(SCHED_SRC)

$(BUILD)/test_measure: tests/test_measure.cpp src/measure/measure.cpp src/measure/track_measure.cpp \
    src/measure/roi_crop.cpp src/app/part_log.cpp $(SCHED_SRC) \
    $(wildcard src/measure/*measure*.hpp src/measure/roi_crop.hpp src/app/part_log.hpp src/sched/*.hpp) | check-sdk $(BUILD)
	$(CXX) $(CXXFLAGS) $(INC) -I$(OPENCV)/include/opencv4 -o $@ tests/test_measure.cpp src/measure/measure.cpp \
	    src/measure/track_measure.cpp src/measure/roi_crop.cpp src/app/part_log.cpp $(SCHED_SRC) \
	    -L$(OPENCV)/lib -Wl,-rpath-link,$(OPENCV)/lib $(RPATH) -lopencv_imgcodecs -lopencv_imgproc -lopencv_core -lpthread

$(BUILD)/canplc: tools/canplc.c src/hw/can_link.c src/hw/can_link.h src/hw/can_proto.h | $(BUILD)
	$(CC) $(CFLAGS) $(INC) -o $@ tools/canplc.c src/hw/can_link.c

$(BUILD)/test_can: tests/test_can.c src/hw/can_link.c src/hw/can_link.h src/hw/can_proto.h | $(BUILD)
	$(CC) $(CFLAGS) $(INC) -o $@ tests/test_can.c src/hw/can_link.c

# hardware-in-the-loop: needs the board (PL v5+), no motor
$(BUILD)/test_belt_hil: tests/test_belt_hil.cpp src/app/belt_ctl.cpp src/app/belt_ctl.hpp $(SCHED_SRC) $(BUILD)/des_hw.o | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ tests/test_belt_hil.cpp src/app/belt_ctl.cpp $(SCHED_SRC) \
	    $(BUILD)/des_hw.o

# shared objects: one rule each, so parallel make (-j) never has two recipes writing the same .o
$(BUILD)/des_hw.o: src/hw/des_hw.c src/hw/des_hw.h src/hw/des_regs.h | $(BUILD)
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/hw/des_hw.c
$(BUILD)/can_link.o: src/hw/can_link.c src/hw/can_link.h src/hw/can_proto.h | $(BUILD)
	$(CC) $(CFLAGS) $(INC) -c -o $@ src/hw/can_link.c

# check-sdk / check-board are order-only (after |): they run first but, being phony, must not force a relink
$(BUILD)/des_app: $(APP_SRC) $(SCHED_SRC) $(VISION_SRC) $(BUILD)/des_hw.o $(BUILD)/can_link.o $(HDRS) | check-sdk check-board $(BUILD)
	$(CXX) $(CXXFLAGS) $(INC) \
	    -I$(ARMNN)/include -I$(OPENCV)/include/opencv4 -I$(FREETYPE_INC) \
	    -o $@ $(APP_SRC) $(SCHED_SRC) $(VISION_SRC) $(BUILD)/des_hw.o $(BUILD)/can_link.o \
	    -L$(ARMNN)/lib -L$(OPENCV)/lib -L$(BOARD_LIBS) -Wl,-rpath-link,$(ARMNN)/lib:$(OPENCV)/lib:$(BOARD_LIBS) $(RPATH) \
	    $(OPENCV_LIBS) -larmnn -larmnnOnnxParser -lfreetype -lfmt -lpthread

# NPU part classifier on the board vs the PC reference (train/eval_onnx.py --refs)
$(BUILD)/test_npu: tests/test_npu.cpp src/app/npu_cls.cpp src/app/npu_cls.hpp $(VISION_SRC) | check-sdk $(BUILD)
	$(CXX) $(CXXFLAGS) $(INC) -I$(ARMNN)/include -I$(OPENCV)/include/opencv4 \
	    -o $@ tests/test_npu.cpp src/app/npu_cls.cpp $(VISION_SRC) \
	    -L$(ARMNN)/lib -L$(OPENCV)/lib -Wl,-rpath-link,$(ARMNN)/lib:$(OPENCV)/lib $(RPATH) \
	    -lopencv_imgcodecs -lopencv_imgproc -lopencv_core -larmnn -larmnnOnnxParser -lfmt -lpthread

$(BUILD)/test_calib: tests/test_calib.cpp src/measure/calib.cpp src/measure/calib.hpp src/sched/geom.hpp | check-sdk $(BUILD)
	$(CXX) $(CXXFLAGS) $(INC) -I$(OPENCV)/include/opencv4 \
	    -o $@ tests/test_calib.cpp src/measure/calib.cpp \
	    -L$(OPENCV)/lib -Wl,-rpath-link,$(OPENCV)/lib $(RPATH) $(OPENCV_LIBS) -lpthread

clean:
	-@rmdir /S /Q $(BUILD) || rm -rf $(BUILD)
