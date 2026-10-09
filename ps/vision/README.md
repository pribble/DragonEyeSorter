# ps/vision/ — 检测流水线（改造起点：安路官方 face_detection demo，MIT）

我们的齿轮/齿条零件检测流水线在这里开发。现有代码来自安路官方 NPU demo
`demo.zip → sdk/face_detection.tar.gz → src/npu_flow/`（2025-12 版），作为
改造起点直接修改，不是冻结的第三方依赖——目标是把它改成零件检测。

许可：安路 Inc. 版权，`SPDX-License-Identifier: MIT`（原文件头保留）。MIT 义务：
保留版权头；大改的文件头注明 "derived from Anlogic face_detection，修改：…"；
改动足够大可以整个重写，重写后即为我们自有代码（仍可 MIT）。

原上游为 `npu_flow/utils/`、`npu_flow/utils/media/`、`npu_flow/yolo_demo_hdmi_camera/`
三层，入库时已展平为本目录一层，`#include` 均为纯文件名，无需改动。

## 现有内容速查

- 取帧：`icamera::enumerate_device("/dev/mipicam", "1920x1080")` → `SubmitAndStart(phys, virt)` / `WaitFinish()`（mipi_camera / stream_input）
- 显示：`ihdmi::enumerate_device("/dev/fb0" | "/dev/dri/card0", ...)` → `show(phys, virt, ch)`（hdmi_framebuffer 路径，hdmi_drm 未编入）
- 推理：`BufferedNPUExecutor(model, {Alnpu,CpuAcc,CpuRef})`，8 缓冲流水，输入量化参数从 ONNX 读
- 后处理：`getFastDecodeOutputYolov8WorkLoad(tensors, h, w, cls, prob, nms)`，类别/阈值在 JSON
- 计时：solution.cpp 每任务 time_infos → 测试埋点框架
- demo 主程序：`main.cpp` + `solution.cpp`（相机→resize→NPU→YOLO→HDMI 五级流水）

## 入库时与上游的差异

- 删除 `drm/`（libdrm 二进制 + 内核头，板上已有；需要时从原 demo 取）
- 删除 `udp_send.hpp`（UDP 推流演示用不到）

## 待改造方向（零件检测）

- 类别换成 `models/label_list.json` 的 8 类（齿轮/齿条 × OK/短射/飞边/脏污），
  NPU JSON 由 label_list 生成，不手抄（AGENTS.md 约定 1）
- `face_detection`/`yolo_demo_hdmi_camera` 等命名后续随改造重命名
- 增加分拣判定输出：检测框 → label_list 的 verdict/action/dest → 调度器
- 前处理 resize（实测 37ms）是已知瓶颈，优化也在本目录做
