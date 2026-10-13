# train/ — 零件分类模型（NPU，赛题硬性项④）

**路线**：PS 用 OpenCV 定位（已在 60fps 主循环里）→ 每件按自身方向转正裁 **56×56mm → 224×224**（`ps/app/roi_crop`，约 0.25mm/像素）→ NPU 跑 **YOLOv8n-cls@224（激活换成 ReLU）** 分 8 类（`models/label_list.json`）→ 过视野时分类 3~6 次、概率软投票（`Tracker::vote`）→ 去向。

为什么不用整帧检测：4.2mm 镜头、相机高约 300mm 时整帧约 0.21mm/像素，缩到 416 输入后约 0.95mm/像素，1mm 的黑点 / 缺料只剩 1 个像素；裁切后同样的缺陷有约 4 个像素。分类模型计算量约为整帧 YOLOv8n@416 的 1/7。

**为什么用 ReLU**（合成数据：480 张验证集、板上 NPU 实测）：SiLU 版 NPU 94.0%，ReLU 版 **96.25%**，与 PC 上 uint8 模型（96.46%）几乎一致，logits 最大偏差 1.6（SiLU 5.0），推理同为 7.8ms —— NPU 对 SiLU 是近似计算。

**量化实验（合成数据，ReLU 模型）**：校准方法 MinMax / Entropy / Percentile 结果相同（uint8 96.46%）→ 损失来自权重 per-tensor 量化；per-channel 在 PC 上 97.7%，但**板上 NPU 推理中止**（只支持 per-tensor，与厂家模型一致）。要补这 2 个点：跨层均衡（ReLU 下严格等价）或量化感知训练，等实物数据再看值不值。

**ArmNN 坑**：量化后必须做形状推断（`export_npu.py` 已做），缺张量形状时板上解析器直接段错误、不打日志。**准确率以板上 NPU 实测为准**：`NPU_EVAL_DIR=<val 目录> test_npu <model.json> <refs> 100`。

**训练和推理用同一个裁切函数**（`roi_crop`），归一化一致（RGB、÷255、均值 0）——精度不掉的前提。

## 流程

```
采集（板上）  des_app collect --cls gear_ok          一次只放一类零件，每件自动存 ≤10 张裁切图
     ↓        /root/des/data/collect/<会话>_<类别>/<id>_<类别>/*.jpg
整理（PC）    prepare_dataset.py                      合并会话，按「零件」分 train / val（不按图片，防同件两边都有）
     ↓
训练（PC）    train_cls.py                            YOLOv8n-cls，ImageNet 预训练，温和增强（翻转、亮度；不随机擦除、几乎不随机裁剪）
     ↓
导出 + 量化   export_npu.py                           ONNX opset 13 → 去掉 Softmax → ORT quantize_static（QDQ / UINT8 / per-tensor，与厂家 fire_detect 同格式）+ 模型 JSON
     ↓
评估（PC）    eval_onnx.py                            fp32 / uint8 准确率、混淆矩阵、OK/NG 准确率，导出板上核对参考集
     ↓
上板          scp 模型 + JSON → /root/des/；test_npu 核对 NPU 与 PC 一致、测推理耗时；des_app live / run 自动加载
```

## 环境配置

训练环境 `~/des-train` 在 WSL 中创建（GPU 需 CUDA 支持）：

```bash
python3 -m venv ~/des-train
source ~/des-train/bin/activate
```

按硬件装依赖：

| 版本 | 安装 | 训练速度参考 |
|---|---|---|
| CPU（调试 / 无 GPU） | `pip install -r requirements.txt` | 合成数据 2400 张 × 12 轮：约 6.4 分钟 |
| GPU（推荐，CUDA 12.1） | `pip install -r requirements-gpu.txt` | RTX 4060 Laptop，合成数据 2400 张 × 12 轮：约 40 秒（含启动 1 分 14 秒） |

**注意事项**：
- GPU 版本要 NVIDIA GPU + CUDA 12.1（或兼容版本）
- WSL2 下 CUDA 可用（NVIDIA 驱动 ≥525.60.13）
- 两版 fp32 推理精度一致；量化后精度以板上 NPU 实测为准
- 训练完成度：CPU 98.33% top1，GPU 98.75%（GPU 不完全确定性）

### CUDA 环境检查

训练前验证 GPU 可用（WSL 里 `nvidia-smi` 能看到卡）；无 CUDA 时 `train_cls.py --device cpu` 强制用 CPU：

```bash
python -c "import torch; print(f'CUDA available: {torch.cuda.is_available()}')"
```

## 命令

`train_cls.py --device auto`（默认）有 CUDA 就用 GPU 0。

```bash
export YOLO_CONFIG_DIR=$HOME/.ultralytics-cfg
cd <仓库根>/train
# 真实数据（采集会话拷到 WSL 本地盘再训，/mnt/d 很慢）
~/des-train/bin/python prepare_dataset.py --src ~/des-work/collect --out ~/des-work/real --val-sessions val_
~/des-train/bin/python train_cls.py --data ~/des-work/real --epochs 40 --name real --project ~/des-work/runs
~/des-train/bin/python export_npu.py --weights ~/des-work/runs/real/weights/best.pt --calib ~/des-work/real/train --out ../models/part_cls
~/des-train/bin/python eval_onnx.py --model ../models/part_cls --val ~/des-work/real/val --refs out/part_cls_refs
# 板上
scp ../models/part_cls_uint8.onnx ../models/part_cls.json root@192.168.137.100:/root/des/
scp -r out/part_cls_refs root@192.168.137.100:/root/des/
ssh root@192.168.137.100 /root/bin/test_npu /root/des/part_cls.json /root/des/part_cls_refs 200
```

**冒烟测试**（零件没到时验链路）：`synth_crops.py` 生成 8 类合成裁切图（齿轮 / 齿条 + 缺料 / 飞边 / 黑点），走完上面全部步骤。合成数据**只验证工具链与 NPU 耗时**，不代表真实精度。

## 采集建议（每类目标 ≥300 张、≥30 个零件）

- 一个会话只放**一类**；验证集用**另外的零件编号**单独采（会话名带 `val_`，见 `物料与缺陷样本.md`（仓库外）§6.1），整理时 `--val-sessions val_`。
- 皮带低速（`--belt 30`），零件随机方向、靠中线；每件存 ≤10 张（`--max`）、间隔 6 帧（`--every`）。
- 光照、相机高度定了再采正式数据；改了光照要补采。
- 要检测模型备选时加 `--full 30`：每 30 帧存整帧 + 定位器自动生成的 YOLO 框。
