# ps/vendor/demo — 厂家 face_detection demo 的未编入件（改造起点）

代码来自安路官方 NPU demo `demo.zip → sdk/face_detection.tar.gz → src/npu_flow/`（2025-12 版）；上游三层目录已展平，`#include` 均为纯文件名。**本目录一行都不编进 `des_app`**。

## 已经改好并编入 `des_app` 的部分（在 ps/src/vision/）

`mipi_camera.cpp`（`/dev/mipicam` DMA 取帧，`icamera::enumerate_device` 的定义也在这里）、`executor.cpp`（`NPUExecutor`：ArmNN + Alnpu 同步推理）、`flow_common_types.cpp`、`logger_forward.cpp`；配套头也在 `ps/src/vision/`。ioctl ABI 头 `mipicam_module.h` 只有一份，在 `ps/kernel/`（`ps/Makefile` 的 `-Ikernel` 让两边共享）。显示不走厂家的 `ihdmi`：HDMI 输出是 `src/ui/display.cpp`（直写显示 VDMA）。

## 本目录内容（未编入）

| 文件 | 说明 |
|---|---|
| `preprocess.cpp` `.hpp` | resize 前处理；已知瓶颈，优化在这里做 |
| `postprocess.cpp` `.hpp` | YOLOv8 后处理 `getFastDecodeOutputYolov8WorkLoad`（类别 / 阈值走 JSON） |
| `buffered_executor.cpp` `.hpp` | `BufferedNPUExecutor` 8 缓冲流水推理；现在用同步 `executor.cpp` + 自研 `src/app/npu.cpp` |
| `hdmi.cpp` `.hpp`、`hdmi_framebuffer.cpp` `.hpp`、`ihdmi.hpp`、`axi_hdmi.h`、`fast_io.cpp` `.h` | 厂家 fb0 显示路径，不编入 |
| `example_yolo.json` | 后处理参数示例，接 `postprocess.cpp` 时的模板 |

这些只依赖两个库目录（`$ARMNN_LIB` 与 `$OPENCV_LIB`）；要启用就把对应 `.cpp` 加进 `ps/Makefile` 的 `VISION_SRC`（`-Isrc/vision` 已在 `INC` 里）。

## 已删除（需要时从厂家 demo 取回）

上游 demo 的应用层与 USB / DRM 分支已删：`main.cpp` / `solution.cpp`（五级流水 demo 主程序）、`main_helper` / `helper`、`cmdline.h`、`usb_camera`、`stream_input`（JPU 解码输入流）、`icamera_enum.cpp`、`ihdmi_enum.cpp`、`hdmi_drm`、`plot.hpp`、`al_type.h`、`npu_c_api.h`、`CMakeLists.txt`、`.gitattributes`。理由：引用仓库里没有的头（`udp_send.hpp`、JPU 的 `jdi.h` 等），或只服务已废的显示 / USB 路径。取回：从厂家 `demo/sdk/face_detection.tar.gz` 的 `src/npu_flow/` 解出对应文件。

## 待改造方向（若改检测路线）

类别换 `models/label_list.json`（现 4 类，NPU JSON 由它生成）；增加分拣判定输出（检测框 → verdict/action/dest → 调度器）；前处理 resize 优化。
