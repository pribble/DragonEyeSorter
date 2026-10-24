# ps/vendor — 不是我们写的代码

| 文件 | 内容 | 进 des_app？ |
|---|---|---|
| `json.hpp` | nlohmann/json 单头（923 KB），配置 / 标定 / 界面 / NPU 模型 JSON 都用它 | ✅ 被 `src/app` 与 `src/vision` 引用 |

> 本目录不属于产物：改我们的功能不必读它。原先留档的厂家 demo 未编入件（前处理、后处理、8 缓冲流水等）已从仓库删除（git 历史可找回）；已改造编进 `des_app` 的 4 个件在 `ps/src/vision/`（那是我们的代码，只是出身是 demo）。
