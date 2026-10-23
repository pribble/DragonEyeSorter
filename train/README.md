# train/ — 零件分类模型（NPU，赛题硬性项④）

**路线**：PS 用 OpenCV 定位（已在 60fps 主循环里）→ 每件按自身方向转正裁 **56×56mm → 224×224**（`ps/src/measure/roi_crop`，约 0.25mm/像素）→ NPU 跑 **YOLOv8n-cls@224（激活换成 ReLU）** 分 4 类（`models/label_list.json`：良品 / 缺料 / 飞边 / 黑点）→ 过视野时分类 3~6 次、概率软投票（`Tracker::vote`）→ 去向。

不用整帧检测：整帧约 0.18mm/像素，缩到 416 后约 0.84mm/像素，1mm 的黑点 / 缺料只剩 1 个像素；裁切后同样的缺陷约 4 个像素，计算量约为整帧 YOLOv8n@416 的 1/7。**训练和推理用同一个裁切函数**（`roi_crop`），归一化一致（RGB、÷255、均值 0）。

**为什么用 ReLU**（8 类合成数据，板上 NPU 实测）：NPU 对 SiLU 是近似计算 —— SiLU 版 94.0%，ReLU 版 **96.25%**（PC 上 uint8 96.46%，logits 最大偏差 1.6，SiLU 5.0），推理同为 7.8ms。

**量化结论**（合成数据，ReLU 模型）：MinMax / Entropy / Percentile 三种校准结果相同 → 损失来自权重 per-tensor 量化；per-channel 在 PC 上 97.7%，但**板上 NPU 推理中止**（只支持 per-tensor，与厂家模型一致）。要补这 2 个点：跨层均衡（ReLU 下严格等价）或量化感知训练，等实物数据再看值不值。

**ArmNN 已知问题**：量化后必须做形状推断（`export_npu.py` 已做），缺张量形状时板上解析器直接段错误、不打日志。**准确率以板上 NPU 实测为准**：`NPU_EVAL_DIR=<val 目录> test_npu <model.json> <refs> 100`。

**官方工具链**：SDK 仓库 [gitee.com/anlogic/dr1m90_npu](https://gitee.com/anlogic/dr1m90_npu)（release 分支，板上同为 ArmNN + Alnpu）；PC 端量化链在 `npu_demo/scripts/`（onnxsim → batch 1 + 形状推断 → 拆超限 Conv / FC → opset 19 → 算子检查 → ORT `quantize_static`：uint8 = per-tensor 非对称，int8 = per-channel 对称）。我们的 `export_npu.py` 与之等价，差别只在没走 onnxsim / opset 19 / 拆分（我们的模型不需要拆）。

- 算子限制（`check_onnx_model.py`）：Conv 核 1/3/5/7 方形、步长 1/2（步长 2 只能 3×3）、pad = (k−1)/2、**深度可分离卷积不加速**、输入 ≤1023×1023；Gemm transB=1；MaxPool 核 2/5/9/13；硬核激活只有 ReLU / LeakyReLU / Mish / HardSwish（**没有 SiLU**）；InstanceNorm、Erf 不支持。
- 用官方检查器查我们的 ReLU 模型：Conv 26 / Relu 26 / Split 4 / Add 6 / Concat 4 / GlobalAveragePool 1 / Gemm 1 → **只有 GlobalAveragePool 不能加速**（尾部 1280×7×7 求平均回退 CPU，代价很小）。SiLU 版多出 Sigmoid 26 + Mul 26。
- ⬜ int8 per-channel 对称量化（P1）：要先让 `ps/src/app/npu_cls.cpp` 支持 int8 输入（现在只认 uint8 / float32）；官方 int8 路径是 per-channel + 对称（PC 上 97.7% vs per-tensor 96.46%），等实物数据集后再做。
- 可选（P2，收益很小）：GlobalAveragePool 并进全连接，让整网都在 NPU 上。

## 流程

```
采集（板上）  des_tool.sh collect --cls gear_ok     一次只放一类零件，每件自动存 ≤10 张裁切图
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

板上用 `des_tool.sh` 的运维模式采集（分层见 [ps/README](../ps/README.md)「四类产物」）；模型两文件与配置随 [scripts/deploy/README](../scripts/deploy/README.md) 的清单一起上板。

## 环境配置

训练环境 `~/des-train` 在 WSL 中创建（GPU 需 CUDA 支持）：

```bash
python3 -m venv ~/des-train
source ~/des-train/bin/activate
```

按硬件装依赖：

| 版本 | 安装 | 训练速度参考 |
|---|---|---|
| CPU（调试 / 无 GPU） | `pip install -r requirements.txt --extra-index-url https://download.pytorch.org/whl/cpu` | 合成数据 2400 张 × 12 轮：约 6.4 分钟 |
| GPU（推荐，CUDA 13.0 轮子） | `pip install -r requirements-gpu.txt` | RTX 4060 Laptop，合成数据 2400 张 × 12 轮：训练约 40 秒，端到端含启动约 1 分 14 秒 |

- 版本为实际跑通、模型已上板验证的环境（来源 `pip list`：torch 2.14.1 / ultralytics 8.4.175 / onnx 1.23.2 / ORT 1.23.2，Python 3.10）；换机重装以实际环境为准，版本变了重新核对导出与量化。
- GPU 版要 NVIDIA 驱动支持 CUDA 13（WSL 里 `nvidia-smi` 右上角 CUDA Version ≥ 13.0）；两版 fp32 推理精度一致，量化后精度以板上 NPU 实测为准；无 CUDA 时 `train_cls.py --device cpu` 强制用 CPU，检查命令 `python -c "import torch; print(torch.cuda.is_available())"`。
- 训练完成度：CPU 98.33% top1，GPU 98.75%（GPU 不完全确定性）。

## 命令

`train_cls.py --device auto`（默认）有 CUDA 就用 GPU 0。

```bash
export YOLO_CONFIG_DIR=$HOME/.ultralytics-cfg
cd <仓库根>/train
# 真实数据（采集会话拷到 WSL 本地盘再训，/mnt/d 很慢）
~/des-train/bin/python prepare_dataset.py --src ~/des-work/collect7 --out ~/des-work/real --val-sessions val_ --max-train 450
~/des-train/bin/python train_cls.py --data ~/des-work/real --epochs 40 --name real --project ~/des-work/runs
~/des-train/bin/python export_npu.py --weights ~/des-work/runs/real/weights/best.pt --calib ~/des-work/real/train --out ../models/part_cls
~/des-train/bin/python eval_onnx.py --model ../models/part_cls --val ~/des-work/real/val --refs out/part_cls_refs
# 板上
scp ../models/part_cls_uint8.onnx ../models/part_cls.json $BOARD_HOST:/root/des/
scp -r out/part_cls_refs $BOARD_HOST:/root/des/
ssh $BOARD_HOST /root/bin/test_npu /root/des/part_cls.json /root/des/part_cls_refs 200
```

**冒烟测试**（零件没到时验链路）：`synth_crops.py` 按 `label_list.json` 生成合成裁切图（现为齿轮 4 类），走完上面全部步骤。现 4 类：600 + 120 张 / 类、12 轮 → 4 类合成验证集（480 张）：NPU **95.42%**、OK/NG 96.0%，PC fp32 97.5% / uint8 95.6%，单次 7.80ms。合成数据**只验证工具链与 NPU 耗时**，不代表真实精度。

## 采集建议（每类目标 ≥300 张、≥30 个零件）

- 一个会话只放**一类**；验证集用**另外的零件编号**单独采（会话名带 `val_`），整理时 `--val-sessions val_`。
- 皮带低速（`--belt 30`），零件随机方向、靠中线；每件存 ≤10 张（`--max`）、间隔 6 帧（`--every`）。光照、相机高度定了再采正式数据；改了光照要补采。
- 要检测模型备选时加 `--full 30`：每 30 帧存整帧 + 定位器自动生成的 YOLO 框。
