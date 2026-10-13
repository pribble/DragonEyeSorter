# ps/Makefile — cross build of our own PS programs for the board (glibc 2.25 / aarch64).
#
# Windows (Git Bash):  <FD_ROOT>/toolchain/build-tools/bin/make.exe -C ps [target]
# Linux:               make -C ps CROSS=<FD_ROOT>/toolchain/gcc/aarch64_linux/bin/aarch64-linux-gnu-
#
# 推荐: 先配置 .env 文件，然后直接 make
#
# CROSS  toolchain prefix. Use the FD one (Linaro 7.4, glibc 2.25 = board); NOT a distro aarch64 gcc (newer glibc).
# SDK    vendor NPU demo libs dir (demo.zip -> face_detection/libs): armnn_lib/ + ffmpeg_opencv4.7.0_aarch64/.
#        Same files as /opt/armnn_lib and /opt/ffmpeg_opencv4.7.0_aarch64 on the board. Kept out of the repo (113 MB).
#
# 本文件不写主机路径：FD_ROOT / SDK_LIBS / FREETYPE_INC / BOARD_LIBS 一律来自 .env（本机、不入库）或命令行。
#
# Targets: desreg (C, no deps), test_scheduler (C++, no deps), test_calib / test_measure / des_app (C++, need SDK), all, clean.
# Deploy:  scp -i ~/.ssh/id_ed25519_anlu build/<prog> root@192.168.137.100:/root/bin/

# 从 .env 文件读取配置（如果存在）
-include ../.env.make

# 不给主机路径默认值：缺配置就报错并给出下一步，而不是默默去用某台机器上的目录
ENV_HINT = 先在仓库根把 .env.example 复制为 .env 并填好路径（见 docs/环境与上手.md），再运行 scripts/generate_env_make.sh；也可命令行覆盖，如 make CROSS=<工具链前缀> SDK_LIBS=<libs>

ifneq ($(MAKECMDGOALS),clean)
ifeq ($(origin CROSS),undefined)
ifeq ($(strip $(FD_ROOT)),)
$(error FD_ROOT 未设置。$(ENV_HINT))
endif
endif
endif

CROSS ?= $(FD_ROOT)/toolchain/gcc/aarch64_linux/bin/aarch64-linux-gnu-
SDK   ?= $(SDK_LIBS)
BUILD ?= build

CC  := $(CROSS)gcc
CXX := $(CROSS)g++

CFLAGS   ?= -O2 -g -Wall -Wextra
CXXFLAGS ?= -O2 -g -Wall -Wextra -std=c++17

ARMNN  := $(SDK)/armnn_lib
OPENCV := $(SDK)/ffmpeg_opencv4.7.0_aarch64
# board paths baked in as rpath, so no LD_LIBRARY_PATH is needed at run time
RPATH  := -Wl,-rpath,/opt/armnn_lib/lib:/opt/ffmpeg_opencv4.7.0_aarch64/lib

SCHED_SRC := scheduler/tracker.cpp scheduler/picker.cpp scheduler/arm.cpp scheduler/flap.cpp
# camera capture reuses the vendor-derived driver wrapper in vision/ (MIT); no NPU code is linked yet
VISION_SRC := vision/mipi_camera.cpp vision/logger_forward.cpp vision/flow_common_types.cpp vision/executor.cpp
APP_SRC := app/main.cpp app/camera_rig.cpp app/locate.cpp app/config.cpp app/hdmi_out.cpp app/live.cpp app/calib.cpp app/belt_ctl.cpp app/arm_exec.cpp app/runner.cpp \
           app/measure.cpp app/track_measure.cpp app/measure_tool.cpp app/roi_crop.cpp app/part_log.cpp \
           app/img_writer.cpp app/collector.cpp app/npu_cls.cpp app/npu_async.cpp \
           app/text_ft.cpp app/ui_input.cpp app/hmi.cpp
OPENCV_LIBS := -lopencv_calib3d -lopencv_features2d -lopencv_flann -lopencv_imgproc -lopencv_imgcodecs -lopencv_core

.PHONY: all clean check-sdk check-board

# SDK / FreeType / 板上库来自 .env；缺变量或目录不存在时先报错，别让编译器去报一堆找不到头文件。
# 用 make 自己的 wildcard + error（不经过 shell）：从 PowerShell 调 make 时配方由 cmd.exe 执行
# （FD 的 sh.exe 不在 PATH 上），`test -d` 这类 POSIX 写法会直接报 "'test' is not recognized"。
check-sdk:
	@$(if $(wildcard $(SDK_LIBS)),echo SDK_LIBS ok,$(error SDK_LIBS 未设置或目录不存在：$(SDK_LIBS)。$(ENV_HINT)))
check-board:
	@$(if $(wildcard $(FREETYPE_INC)),echo FREETYPE_INC ok,$(error FREETYPE_INC 未设置或目录不存在：$(FREETYPE_INC)。$(ENV_HINT)))
	@$(if $(wildcard $(BOARD_LIBS)),echo BOARD_LIBS ok,$(error BOARD_LIBS 未设置或目录不存在：$(BOARD_LIBS)。$(ENV_HINT)))

all: $(BUILD)/desreg $(BUILD)/test_scheduler $(BUILD)/test_calib $(BUILD)/test_measure $(BUILD)/test_belt_hil $(BUILD)/test_can $(BUILD)/canplc $(BUILD)/des_app

$(BUILD):
	-@mkdir $(BUILD)

$(BUILD)/desreg: tools/desreg.c common/des_hw.c common/des_hw.h common/des_regs.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tools/desreg.c common/des_hw.c

$(BUILD)/test_scheduler: tests/test_scheduler.cpp $(SCHED_SRC) $(wildcard scheduler/*.hpp) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Ischeduler -o $@ tests/test_scheduler.cpp $(SCHED_SRC)

$(BUILD)/test_measure: tests/test_measure.cpp app/measure.cpp app/track_measure.cpp app/roi_crop.cpp app/part_log.cpp $(SCHED_SRC) \
    $(wildcard app/*measure*.hpp app/roi_crop.hpp app/part_log.hpp scheduler/*.hpp) check-sdk | $(BUILD)
	$(CXX) $(CXXFLAGS) -Iapp -Ischeduler -I$(OPENCV)/include/opencv4 -o $@ tests/test_measure.cpp app/measure.cpp \
	    app/track_measure.cpp app/roi_crop.cpp app/part_log.cpp $(SCHED_SRC) \
	    -L$(OPENCV)/lib -Wl,-rpath-link,$(OPENCV)/lib $(RPATH) -lopencv_imgcodecs -lopencv_imgproc -lopencv_core -lpthread

$(BUILD)/canplc: tools/canplc.c bus/can_link.c bus/can_link.h bus/can_proto.h | $(BUILD)
	$(CC) $(CFLAGS) -Ibus -o $@ tools/canplc.c bus/can_link.c

$(BUILD)/test_can: tests/test_can.c bus/can_link.c bus/can_link.h bus/can_proto.h | $(BUILD)
	$(CC) $(CFLAGS) -Ibus -o $@ tests/test_can.c bus/can_link.c

# hardware-in-the-loop: needs the board (PL v5+), no motor
$(BUILD)/test_belt_hil: tests/test_belt_hil.cpp app/belt_ctl.cpp app/belt_ctl.hpp $(SCHED_SRC) common/des_hw.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $(BUILD)/des_hw.o common/des_hw.c
	$(CXX) $(CXXFLAGS) -Iapp -Ischeduler -Icommon -o $@ tests/test_belt_hil.cpp app/belt_ctl.cpp $(SCHED_SRC) \
	    $(BUILD)/des_hw.o

$(BUILD)/des_app: $(APP_SRC) $(SCHED_SRC) $(VISION_SRC) common/des_hw.c $(wildcard app/*.hpp scheduler/*.hpp) check-sdk check-board | $(BUILD)
	$(CC) $(CFLAGS) -c -o $(BUILD)/des_hw.o common/des_hw.c
	$(CC) $(CFLAGS) -Ibus -c -o $(BUILD)/can_link.o bus/can_link.c
	$(CXX) $(CXXFLAGS) -Iapp -Ischeduler -Icommon -Ivision -Ibus \
	    -I$(ARMNN)/include -I$(OPENCV)/include/opencv4 -I$(FREETYPE_INC) \
	    -o $@ $(APP_SRC) $(SCHED_SRC) $(VISION_SRC) $(BUILD)/des_hw.o $(BUILD)/can_link.o \
	    -L$(ARMNN)/lib -L$(OPENCV)/lib -L$(BOARD_LIBS) -Wl,-rpath-link,$(ARMNN)/lib:$(OPENCV)/lib:$(BOARD_LIBS) $(RPATH) \
	    $(OPENCV_LIBS) -larmnn -larmnnOnnxParser -lfreetype -lfmt -lpthread

# NPU part classifier on the board vs the PC reference (train/eval_onnx.py --refs)
$(BUILD)/test_npu: tests/test_npu.cpp app/npu_cls.cpp app/npu_cls.hpp $(VISION_SRC) check-sdk | $(BUILD)
	$(CXX) $(CXXFLAGS) -Iapp -Ivision -I$(ARMNN)/include -I$(OPENCV)/include/opencv4 \
	    -o $@ tests/test_npu.cpp app/npu_cls.cpp $(VISION_SRC) \
	    -L$(ARMNN)/lib -L$(OPENCV)/lib -Wl,-rpath-link,$(ARMNN)/lib:$(OPENCV)/lib $(RPATH) \
	    -lopencv_imgcodecs -lopencv_imgproc -lopencv_core -larmnn -larmnnOnnxParser -lfmt -lpthread

$(BUILD)/test_calib: tests/test_calib.cpp app/calib.cpp app/calib.hpp scheduler/geom.hpp check-sdk | $(BUILD)
	$(CXX) $(CXXFLAGS) -Iapp -Ischeduler -Ivision -I$(OPENCV)/include/opencv4 \
	    -o $@ tests/test_calib.cpp app/calib.cpp \
	    -L$(OPENCV)/lib -Wl,-rpath-link,$(OPENCV)/lib $(RPATH) $(OPENCV_LIBS) -lpthread

clean:
	-@rmdir /S /Q $(BUILD) || rm -rf $(BUILD)
