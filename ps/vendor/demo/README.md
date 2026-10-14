# ps/vendor/demo — 厂家 face_detection demo 的未编入件（改造起点）

代码来自安路官方 NPU demo `demo.zip → sdk/face_detection.tar.gz → src/npu_flow/`（2025-12 版）。上游 `npu_flow/utils/`、`npu_flow/utils/media/`、`npu_flow/yolo_demo_hdmi_camera/` 三层已展平，`#include` 均为纯文件名。

**本目录一行都不编进 `des_app`**：留下的是改造起点，用之前先看下表的依赖说明。

## 已经改好并编入 `des_app` 的部分（在 ps/src/vision/）

| 文件 | 作用 |
|---|---|
| `mipi_camera.cpp` | `/dev/mipicam` DMA 取帧：`SubmitAndStart(phys, virt)` / `WaitFinish()`；**`icamera::enumerate_device` 的定义也在这里** |
| `executor.cpp` | `NPUExecutor`：ArmNN + Alnpu 推理（同步） |
| `flow_common_types.cpp` | CMA 缓冲 / 张量公共类型 |
| `logger_forward.cpp` | 日志转发 |

配套头 `icamera.hpp`、`mipi_camera.hpp`、`i2c_helper.hpp`、`executor.hpp`、`flow_common_types.hpp`、`logger_forward.hpp` 也在 `ps/src/vision/`；ioctl ABI 头 `mipicam_module.h` 只有一份，在 `ps/kernel/`（`ps/Makefile` 的 `-Ikernel` 让两边共享）。

取帧两种写法等价：`src/app/camera_rig.cpp` 用 `make_shared<camera_t>()`，也可用 `icamera::enumerate_device("/dev/mipicam", "1920x1080")`（上游把该工厂放在 `icamera_enum.cpp`、而 Makefile 不编那个文件，照 demo 调用会链接失败，已把定义并进 `mipi_camera.cpp`；USB 相机分支已删，只认 `/dev/mipicam`）。

## 本目录内容（未编入）

| 文件 | 说明 |
|---|---|
| `preprocess.cpp` `.hpp` | resize 前处理；实测 37ms 的已知瓶颈，优化在这里做 |
| `postprocess.cpp` `.hpp` | YOLOv8 后处理 `getFastDecodeOutputYolov8WorkLoad`（类别 / 阈值走 JSON） |
| `buffered_executor.cpp` `.hpp` | `BufferedNPUExecutor` 8 缓冲流水推理；现在用同步 `executor.cpp` + 自研 `src/app/npu_async.cpp` |
| `hdmi.cpp` `.hpp`、`hdmi_framebuffer.cpp` `.hpp`、`ihdmi.hpp`、`axi_hdmi.h`、`fast_io.cpp` `.h` | 厂家 fb0 显示路径。**不编入**：HDMI 输出走 `src/ui/hdmi_out.cpp`（直写显示 VDMA）；`ihdmi::enumerate_device` 的声明已随 `ihdmi_enum.cpp` 一起删，不留「声明有、定义无」 |
| `example_yolo.json` | 后处理参数示例，接 `postprocess.cpp` 时的模板 |

这些只依赖 SDK（`$SDK_LIBS` 的 armnn / OpenCV），要启用就把对应 `.cpp` 加进 `ps/Makefile` 的 `VISION_SRC`（个别文件 include `src/vision/` 的头，`-Isrc/vision` 已在 `INC` 里）。

## 已删除（需要时从厂家 demo 取回）

上游 demo 的应用层与 USB / DRM 分支：`main.cpp`、`solution.cpp` `.hpp`（五级流水 demo 主程序）、`main_helper.cpp` `.hpp`、`helper.cpp` `.hpp`、`cmdline.h`、`ThreadDataInProcessing.hpp`、`usb_camera.cpp` `.hpp`、`stream_input.cpp` `.hpp`（JPU 解码输入流）、`icamera_enum.cpp`、`ihdmi_enum.cpp`、`hdmi_drm.cpp` `.hpp`、`plot.hpp`、`al_type.h`、`npu_c_api.h`、`CMakeLists.txt`、`.gitattributes`（厂家的 LFS 规则，仓库里没有 `.so` / `.a` 需要它）。

删除理由：它们引用仓库里没有的头（`udp_send.hpp`、`JW_Encoder.h`、JPU 的 `jdi.h` / `jpuapi.h` / `JW_Wrapper.h`），或只服务已废的显示 / USB 路径——一行都没编进我们的程序。取回：从厂家 `demo/sdk/face_detection.tar.gz` 的 `src/npu_flow/` 解出对应文件（上游的 `drm/` 目录入库时即未收录）。

## 待改造方向（零件检测）

- 类别换成 `models/label_list.json` 的 8 类（齿轮/齿条 × 良品/缺料/飞边/黑点），NPU JSON 由 label_list 生成，不手抄（AGENTS.md 约定 1）
- 增加分拣判定输出：检测框 → label_list 的 verdict/action/dest → 调度器
- 前处理 resize（实测 37ms）优化
