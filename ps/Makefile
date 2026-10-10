# ps/Makefile — cross build of our own PS programs for the board (glibc 2.25 / aarch64).
#
# Windows (Git Bash):  D:/ANLU/FD/toolchain/build-tools/bin/make.exe -C ps [target]
# Linux:               make -C ps CROSS=<path>/aarch64-linux-gnu- SDK=<demo>/libs
#
# CROSS  toolchain prefix. Use the FD one (Linaro 7.4, glibc 2.25 = board); NOT a distro aarch64 gcc (newer glibc).
# SDK    vendor NPU demo libs dir (demo.zip -> face_detection/libs): armnn_lib/ + ffmpeg_opencv4.7.0_aarch64/.
#        Same files as /opt/armnn_lib and /opt/ffmpeg_opencv4.7.0_aarch64 on the board. Kept out of the repo (113 MB).
#
# Targets: desreg (C, no deps), test_scheduler (C++, no deps), test_calib / des_app (C++, need SDK), all, clean.
# Deploy:  scp -i ~/.ssh/id_ed25519_anlu build/<prog> root@192.168.137.100:/root/bin/

CROSS ?= D:/ANLU/FD/toolchain/gcc/aarch64_linux/bin/aarch64-linux-gnu-
SDK   ?= D:/ANLU/work/face_detection_src/app/face_detection/libs
BUILD ?= build

CC  := $(CROSS)gcc
CXX := $(CROSS)g++

CFLAGS   ?= -O2 -g -Wall -Wextra
CXXFLAGS ?= -O2 -g -Wall -Wextra -std=c++17

ARMNN  := $(SDK)/armnn_lib
OPENCV := $(SDK)/ffmpeg_opencv4.7.0_aarch64
# board paths baked in as rpath, so no LD_LIBRARY_PATH is needed at run time
RPATH  := -Wl,-rpath,/opt/armnn_lib/lib:/opt/ffmpeg_opencv4.7.0_aarch64/lib

SCHED_SRC := scheduler/tracker.cpp scheduler/picker.cpp scheduler/arm.cpp
# camera capture reuses the vendor-derived driver wrapper in vision/ (MIT); no NPU code is linked yet
VISION_SRC := vision/mipi_camera.cpp vision/logger_forward.cpp vision/flow_common_types.cpp
APP_SRC := app/main.cpp app/camera_rig.cpp app/locate.cpp app/config.cpp app/hdmi_out.cpp app/live.cpp app/calib.cpp
OPENCV_LIBS := -lopencv_calib3d -lopencv_features2d -lopencv_flann -lopencv_imgproc -lopencv_imgcodecs -lopencv_core

.PHONY: all clean
all: $(BUILD)/desreg $(BUILD)/test_scheduler $(BUILD)/test_calib $(BUILD)/des_app

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/desreg: tools/desreg.c common/des_hw.c common/des_hw.h common/des_regs.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tools/desreg.c common/des_hw.c

$(BUILD)/test_scheduler: tests/test_scheduler.cpp $(SCHED_SRC) $(wildcard scheduler/*.hpp) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Ischeduler -o $@ tests/test_scheduler.cpp $(SCHED_SRC)

$(BUILD)/des_app: $(APP_SRC) $(SCHED_SRC) $(VISION_SRC) common/des_hw.c $(wildcard app/*.hpp scheduler/*.hpp) | $(BUILD)
	$(CC) $(CFLAGS) -c -o $(BUILD)/des_hw.o common/des_hw.c
	$(CXX) $(CXXFLAGS) -Iapp -Ischeduler -Icommon -Ivision \
	    -I$(ARMNN)/include -I$(OPENCV)/include/opencv4 \
	    -o $@ $(APP_SRC) $(SCHED_SRC) $(VISION_SRC) $(BUILD)/des_hw.o \
	    -L$(ARMNN)/lib -L$(OPENCV)/lib -Wl,-rpath-link,$(ARMNN)/lib:$(OPENCV)/lib $(RPATH) \
	    $(OPENCV_LIBS) -larmnn -lfmt -lpthread

$(BUILD)/test_calib: tests/test_calib.cpp app/calib.cpp app/calib.hpp scheduler/geom.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Iapp -Ischeduler -Ivision -I$(OPENCV)/include/opencv4 \
	    -o $@ tests/test_calib.cpp app/calib.cpp \
	    -L$(OPENCV)/lib -Wl,-rpath-link,$(OPENCV)/lib $(RPATH) $(OPENCV_LIBS) -lpthread

clean:
	rm -rf $(BUILD)
