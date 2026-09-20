# ps/ — 处理系统软件（PS）

双核 A35 上的 Linux 应用。职责边界见 [docs/06-软件与UI.md](../docs/06-软件与UI.md)：调度状态机、NPU 推理、UI、存储、网络，不搬像素。

规划布局：

```
ps/
├── scheduler/      # 调度状态机与物料跟踪队列（C/C++）
├── inference/      # NPU 推理封装（ArmNN + Alnpu backend）
├── ui-qt/          # Qt 本地 UI（framebuffer/EGLFS，HDMI）
├── ui-web/         # Web UI 后端（REST + WebSocket + MJPEG）
└── common/         # AXI-Lite 寄存器头文件、label_list 加载、配置
```

约定：

- `common/` 中的寄存器偏移定义必须与 `docs/03-PL设计.md` 同源，改动同一次提交完成。
- 类别表从 `models/label_list.json` 加载，此处禁止手抄副本。
