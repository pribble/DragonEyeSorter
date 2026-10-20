# ps/vendor — 不是我们写的代码（两类）

| 目录 | 内容 | 进 des_app？ |
|---|---|---|
| `third_party/json.hpp` | nlohmann/json 单头（923 KB），配置 / 标定 / 界面 / NPU 模型 JSON 都用它 | ✅ 被 `src/app` 与 `src/vision` 引用 |
| `demo/` | 安路官方 face_detection demo 的**未编入**部分：前处理、YOLOv8 后处理、8 缓冲流水推理、厂家 fb0 显示路径、示例 JSON | ❌ 一行都没编，留着当改造起点 |

> 本目录不属于产物：改我们的功能不必读它。要启用其中某个 `.cpp`，照 [demo/README](demo/README.md) 加进 `ps/Makefile` 的 `VISION_SRC`。已编进 `des_app` 的 4 个 demo 改造件在 `ps/src/vision/`（那是我们的代码，只是出身是 demo）。
