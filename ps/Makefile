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
# 目录：src/ 我们自己的源码（产品运行时与运维工具共用，按角色分 app / hw / sched / measure / ui / vision）、
#       tools/ 底层命令行（desreg / canplc）、tests/ 自动断言测试、kernel/ 驱动、vendor/ 非我们写的。
#       两个入口：src/app/main.cpp -> des_app（产品运行时 run / live），
#                 src/app/tool_main.cpp -> des_tool（人工运维 14 个模式）。
#       上板脚本不在这里：唯一源是 ../scripts/deploy/out/（与部署清单同一份，见那里的 README）。
#       **头文件平铺**（全仓库无跨目录 include），靠下面的 INC 解析 —— 各目录 basename 必须唯一。
#
# 源文件用 wildcard 自动收集：**增删 .cpp / 改名不用改本文件**。三个例外是入口、des_tool 专用件和 tools/ +
# tests/ 的各个独立二进制（每个二进制自己列源）。
#   - 共享件：src/{app,sched,measure,ui,vision}/*.cpp（去掉入口与 OPS_SRC） + src/hw/*.c/*.cpp
#   - OPS_SRC：只链进 des_tool 的标定 / 测量报告 / 运维辅助，产品二进制里不能出现
#
# Targets: desreg (C, no deps), test_scheduler (C++, no deps), test_calib / test_measure / test_npu (C++, need SDK),
#          test_belt_hil (board, PL v5+), test_can / canplc (SocketCAN), test_modbus (loopback TCP), des_app / des_tool (C++, need SDK), all, clean.
#          注意：test_npu / test_servo_gap 不在 all 里（要显式点名，见 ps/README.md）。
# Deploy:  ../scripts/deploy/ 里是上板清单与部署器（对比 / 部署 / 暂存）：make -C ps all 结束会顺手刷一份
#          ../scripts/deploy/out/（要上板的那一份：主程序与配置刷进去，脚本与 des.env 就地不动；
#          Linux/WSL 上这行会提示找不到 powershell，可忽略）。des_tool 与 desreg / canplc / test_* 一样不进清单，手动 scp。

# 从 .env 文件读取配置（如果存在）
-include ../.env.make

# 不给主机路径默认值：缺配置就报错并给出下一步，而不是默默去用某台机器上的目录
ENV_HINT = 先在仓库根把 .env.example 复制为 .env 并填好路径（见 docs/环境与上手.md），再运行 scripts/env/generate_env_make.sh；也可命令行覆盖，如 make CROSS=<工具链前缀> ARMNN_LIB=<armnn_lib 目录> OPENCV_LIB=<ffmpeg_opencv4.7.0_aarch64 目录>

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
# vendor/ 是 nlohmann json.hpp（app / measure / ui / vision 都用），tests 放共享断言宏。
INC := -Isrc/app -Isrc/hw -Isrc/sched -Isrc/measure -Isrc/ui -Isrc/vision -Ikernel -Itests -Ivendor

# ---------------------------------------------------------------- sources (wildcard)
ENTRY_APP := src/app/main.cpp
ENTRY_TOOL := src/app/tool_main.cpp
# des_tool 专用（不链进产品 des_app）：标定（会写 calib.json）、人工测量报告、运维命令行辅助
OPS_SRC := $(wildcard src/measure/calib.cpp src/measure/measure_tool.cpp src/app/tool_util.cpp)
SCHED_SRC := $(wildcard src/sched/*.cpp)
VISION_SRC := $(wildcard src/vision/*.cpp)
# 产品运行时与运维工具共用的源码（不含两个入口、不含 OPS_SRC）：编译一次成 $(OBJ) 下的 .o
APP_SRC := $(filter-out $(ENTRY_APP) $(ENTRY_TOOL) $(OPS_SRC), \
             $(wildcard src/app/*.cpp) $(wildcard src/measure/*.cpp) $(wildcard src/ui/*.cpp) \
             $(wildcard src/hw/*.cpp) $(wildcard src/sched/*.cpp) $(wildcard src/vision/*.cpp))
APP_C := $(wildcard src/hw/*.c)
# 测试/工具复用 measure/ 下的共享件（去掉 OPS_SRC）
MEASURE_SRC := $(filter-out $(OPS_SRC), $(wildcard src/measure/*.cpp))

OBJ := $(BUILD)/obj
APP_OBJ := $(addprefix $(OBJ)/, $(notdir $(APP_SRC:.cpp=.o)) $(notdir $(APP_C:.c=.o)))
OPS_OBJ := $(addprefix $(OBJ)/, $(notdir $(OPS_SRC:.cpp=.o)))
APP_MAIN_OBJ := $(OBJ)/$(notdir $(ENTRY_APP:.cpp=.o))
TOOL_MAIN_OBJ := $(OBJ)/$(notdir $(ENTRY_TOOL:.cpp=.o))
SDK_INC := -I$(ARMNN)/include -I$(OPENCV)/include/opencv4 -I$(FREETYPE_INC)
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

all: $(BUILD)/desreg $(BUILD)/test_scheduler $(BUILD)/test_modbus $(BUILD)/test_rs485 $(BUILD)/test_calib $(BUILD)/test_measure $(BUILD)/test_belt_hil $(BUILD)/test_can $(BUILD)/canplc $(BUILD)/des_app $(BUILD)/des_tool
	-@powershell -NoProfile -ExecutionPolicy Bypass -File ../scripts/deploy/deploy.ps1 -Stage

$(BUILD):
	-@mkdir $(BUILD)

# 共享对象：一个源文件一个 .o（各目录 basename 唯一，所以 $(OBJ) 平铺不冲突），-MMD -MP 生成头文件依赖。
$(OBJ):
	-@mkdir $(BUILD)
	-@mkdir $(subst /,\,$(OBJ))

vpath %.cpp src/app src/sched src/measure src/ui src/vision src/hw
vpath %.c src/hw
$(OBJ)/%.o: %.cpp | $(OBJ)
	$(CXX) $(CXXFLAGS) $(INC) $(SDK_INC) -MMD -MP -c -o $@ $<
$(OBJ)/%.o: %.c | $(OBJ)
	$(CC) $(CFLAGS) $(INC) -MMD -MP -c -o $@ $<

-include $(wildcard $(OBJ)/*.d)

$(BUILD)/desreg: tools/desreg.c src/hw/des_hw.c src/hw/des_hw.h src/hw/des_regs.h | $(BUILD)
	$(CC) $(CFLAGS) $(INC) -o $@ tools/desreg.c src/hw/des_hw.c

$(BUILD)/test_scheduler: tests/test_scheduler.cpp $(SCHED_SRC) $(wildcard src/sched/*.hpp) $(wildcard tests/*.hpp) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ tests/test_scheduler.cpp $(SCHED_SRC)

# 测量：measure/ 下除 OPS_SRC 外的全部共享件 + 零件记录（轨迹测量要写进 PartRecord 的类型）
$(BUILD)/test_measure: tests/test_measure.cpp $(MEASURE_SRC) src/app/part_log.cpp src/app/util.cpp src/app/locate.cpp $(SCHED_SRC) \
    $(wildcard src/measure/*.hpp src/app/part_log.hpp src/app/locate.hpp src/app/config.hpp src/sched/*.hpp tests/*.hpp) | check-sdk $(BUILD)
	$(CXX) $(CXXFLAGS) $(INC) -I$(OPENCV)/include/opencv4 -o $@ tests/test_measure.cpp $(MEASURE_SRC) \
	    src/app/part_log.cpp src/app/util.cpp src/app/locate.cpp $(SCHED_SRC) \
	    -L$(OPENCV)/lib -Wl,-rpath-link,$(OPENCV)/lib $(RPATH) -lopencv_imgcodecs -lopencv_imgproc -lopencv_core -lpthread

# Modbus-TCP slave over loopback (no SDK; also builds with WSL g++)
$(BUILD)/test_modbus: tests/test_modbus.cpp src/hw/modbus_tcp.cpp $(wildcard src/hw/modbus_tcp.hpp) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ tests/test_modbus.cpp src/hw/modbus_tcp.cpp -lpthread

# RS485: Modbus-RTU PDUs + the lamp worker against a simulated relay module (no hardware; links des_hw only for its types)
$(BUILD)/test_rs485: tests/test_rs485.cpp src/hw/modbus_rtu.cpp src/app/rs485_io.cpp src/app/util.cpp $(OBJ)/des_hw.o \
                     $(wildcard src/hw/modbus_rtu.hpp src/app/rs485_io.hpp) | $(OBJ) $(BUILD)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ tests/test_rs485.cpp src/hw/modbus_rtu.cpp src/app/rs485_io.cpp src/app/util.cpp $(OBJ)/des_hw.o -lpthread

# can_link.h is header-only (static inline): no can_link.c exists
$(BUILD)/canplc: tools/canplc.c src/hw/can_link.h src/hw/can_proto.h | $(BUILD)
	$(CC) $(CFLAGS) $(INC) -o $@ tools/canplc.c

$(BUILD)/test_can: tests/test_can.c src/hw/can_link.h src/hw/can_proto.h | $(BUILD)
	$(CC) $(CFLAGS) $(INC) -o $@ tests/test_can.c

# servo bus: idle time a servo needs after its reply before the next frame (board, servos wired; stop des_app)
$(BUILD)/test_servo_gap: tests/test_servo_gap.c $(OBJ)/des_hw.o | $(OBJ) $(BUILD)
	$(CC) $(CFLAGS) $(INC) -o $@ tests/test_servo_gap.c $(OBJ)/des_hw.o

# hardware-in-the-loop: needs the board (PL v5+), no motor
$(BUILD)/test_belt_hil: tests/test_belt_hil.cpp src/app/belt_ctl.cpp src/app/belt_ctl.hpp src/app/util.cpp $(SCHED_SRC) $(OBJ)/des_hw.o | $(OBJ) $(BUILD)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ tests/test_belt_hil.cpp src/app/belt_ctl.cpp src/app/util.cpp $(SCHED_SRC) $(OBJ)/des_hw.o -lpthread

# check-sdk / check-board are order-only (after |): they run first but, being phony, must not force a relink
# 产品运行时（run / live）与运维工具（14 个模式）共用同一批对象，只有入口不同；des_tool 额外链标定 / 测量报告对象。
$(BUILD)/des_app: $(APP_MAIN_OBJ) $(APP_OBJ) | check-sdk check-board $(BUILD)
	$(CXX) $(CXXFLAGS) $(INC) $(SDK_INC) \
	    -o $@ $(APP_MAIN_OBJ) $(APP_OBJ) \
	    -L$(ARMNN)/lib -L$(OPENCV)/lib -L$(BOARD_LIBS) -Wl,-rpath-link,$(ARMNN)/lib:$(OPENCV)/lib:$(BOARD_LIBS) $(RPATH) \
	    $(OPENCV_LIBS) -larmnn -larmnnOnnxParser -lfreetype -lfmt -lpthread

$(BUILD)/des_tool: $(TOOL_MAIN_OBJ) $(APP_OBJ) $(OPS_OBJ) | check-sdk check-board $(BUILD)
	$(CXX) $(CXXFLAGS) $(INC) $(SDK_INC) \
	    -o $@ $(TOOL_MAIN_OBJ) $(APP_OBJ) $(OPS_OBJ) \
	    -L$(ARMNN)/lib -L$(OPENCV)/lib -L$(BOARD_LIBS) -Wl,-rpath-link,$(ARMNN)/lib:$(OPENCV)/lib:$(BOARD_LIBS) $(RPATH) \
	    $(OPENCV_LIBS) -larmnn -larmnnOnnxParser -lfreetype -lfmt -lpthread

# NPU part classifier on the board vs the PC reference (train/eval_onnx.py --refs)
$(BUILD)/test_npu: tests/test_npu.cpp $(wildcard src/app/npu*.cpp src/app/npu*.hpp) src/app/util.cpp $(VISION_SRC) | check-sdk $(BUILD)
	$(CXX) $(CXXFLAGS) $(INC) -I$(ARMNN)/include -I$(OPENCV)/include/opencv4 \
	    -o $@ tests/test_npu.cpp $(wildcard src/app/npu*.cpp) src/app/util.cpp $(VISION_SRC) \
	    -L$(ARMNN)/lib -L$(OPENCV)/lib -Wl,-rpath-link,$(ARMNN)/lib:$(OPENCV)/lib $(RPATH) \
	    -lopencv_imgcodecs -lopencv_imgproc -lopencv_core -larmnn -larmnnOnnxParser -lfmt -lpthread

$(BUILD)/test_calib: tests/test_calib.cpp src/measure/calib.cpp src/measure/calib.hpp src/app/util.cpp src/sched/geom.hpp | check-sdk $(BUILD)
	$(CXX) $(CXXFLAGS) $(INC) -I$(OPENCV)/include/opencv4 \
	    -o $@ tests/test_calib.cpp src/measure/calib.cpp src/app/util.cpp \
	    -L$(OPENCV)/lib -Wl,-rpath-link,$(OPENCV)/lib $(RPATH) $(OPENCV_LIBS) -lpthread

clean:
	-@rmdir /S /Q $(BUILD) || rm -rf $(BUILD)
