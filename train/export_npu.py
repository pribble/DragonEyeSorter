#!/usr/bin/env python3
"""best.pt -> ONNX (opset 13) without the final Softmax -> UINT8 QDQ per-tensor (onnxruntime quantize_static),
the same format as the vendor's fire_detect model that runs on the Alnpu backend (docs/02 模型与量化路线), plus the
model JSON that des_app reads (class names in id order, input normalization, crop spec).

Softmax is cut off (des_app applies it in C++): one operator less for the ArmNN ONNX parser / NPU to support.
Input normalization = the training one: RGB, x / 255, mean 0 (ultralytics classification defaults).

    python export_npu.py --weights runs/synth/weights/best.pt --calib data/synth/train --out ../models/part_cls
writes <out>_fp32.onnx, <out>_uint8.onnx, <out>.json
"""
import argparse
import glob
import json
import os
import random

import cv2
import numpy as np
import onnx
from onnxruntime.quantization import CalibrationDataReader, CalibrationMethod, QuantFormat, QuantType, quantize_static
from onnxruntime.quantization.shape_inference import quant_pre_process
from ultralytics import YOLO

HERE = os.path.dirname(os.path.abspath(__file__))


def preprocess(path, imgsz):
    """exactly what des_app does with a crop: BGR -> RGB, /255, NCHW float32"""
    bgr = cv2.imread(path, cv2.IMREAD_COLOR)
    if bgr.shape[0] != imgsz or bgr.shape[1] != imgsz:
        bgr = cv2.resize(bgr, (imgsz, imgsz), interpolation=cv2.INTER_AREA)
    rgb = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB).astype(np.float32) / 255.0
    return rgb.transpose(2, 0, 1)[None]


class Reader(CalibrationDataReader):
    def __init__(self, files, input_name, imgsz):
        self.it = iter(files)
        self.name = input_name
        self.imgsz = imgsz

    def get_next(self):
        f = next(self.it, None)
        return None if f is None else {self.name: preprocess(f, self.imgsz)}


def drop_softmax(src, dst):
    m = onnx.load(src)
    g = m.graph
    out = g.output[0]
    last = [n for n in g.node if out.name in n.output]
    if last and last[0].op_type == "Softmax":
        sm = last[0]
        logits = sm.input[0]
        g.node.remove(sm)
        out.name = logits      # graph output = the logits (same shape [1, nc])
        print(f"removed Softmax, output = {logits}")
    else:
        print("no final Softmax found, output unchanged")
    onnx.checker.check_model(m)
    onnx.save(m, dst)
    return m.graph.input[0].name, m.graph.output[0].name


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--weights", required=True)
    ap.add_argument("--calib", required=True, help="folder tree of training crops (calibration set)")
    ap.add_argument("--out", default=os.path.join(HERE, "..", "models", "part_cls"))
    ap.add_argument("--imgsz", type=int, default=224)
    ap.add_argument("--ncalib", type=int, default=200)
    ap.add_argument("--per-channel", action="store_true", help="per-channel weight scales: does NOT run on the board's NPU (aborts at inference, 2026-10-11)")
    ap.add_argument("--calib-method", default="MinMax", choices=["MinMax", "Entropy", "Percentile"],
                    help="activation range calibration (MinMax / Entropy / Percentile gave the same accuracy on the synthetic set)")
    ap.add_argument("--labels", default=os.path.join(HERE, "..", "models", "label_list.json"))
    a = ap.parse_args()

    model = YOLO(a.weights)
    names = model.names                      # {index: folder name "<id>_<label>"}
    onnx_path = model.export(format="onnx", imgsz=a.imgsz, opset=13, simplify=True, dynamic=False)
    fp32 = a.out + "_fp32.onnx"
    in_name, out_name = drop_softmax(onnx_path, fp32)

    pre = a.out + "_pre.onnx"
    quant_pre_process(fp32, pre)
    files = sorted(glob.glob(os.path.join(a.calib, "**", "*.png"), recursive=True) +
                   glob.glob(os.path.join(a.calib, "**", "*.jpg"), recursive=True))
    random.Random(0).shuffle(files)
    files = files[: a.ncalib]
    u8 = a.out + "_uint8.onnx"
    quantize_static(pre, u8, Reader(files, in_name, a.imgsz), quant_format=QuantFormat.QDQ,
                    activation_type=QuantType.QUInt8, weight_type=QuantType.QUInt8, per_channel=a.per_channel,
                    calibrate_method=getattr(CalibrationMethod, a.calib_method),
                    extra_options={"CalibPercentile": 99.99} if a.calib_method == "Percentile" else None)
    os.remove(pre)
    # The board's ArmNN ONNX parser needs the shape of EVERY tensor (value_info): without it, it segfaults while
    # parsing, before any log line (2026-10-11). The quantizer adds Q/DQ tensors without shapes -> infer them again,
    # as the vendor's fire_detect model has it (metadata "onnx.infer").
    q = onnx.shape_inference.infer_shapes(onnx.load(u8))
    onnx.save(q, u8)
    print(f"quantized with {len(files)} calibration crops -> {u8} ({len(q.graph.value_info)} tensor shapes)")

    with open(a.labels, encoding="utf-8") as f:
        ll = {l["id"]: l for l in json.load(f)["labels"]}
    classes = []
    for i in range(len(names)):
        cid, label = names[i].split("_", 1)
        if int(cid) != i or ll[int(cid)]["label"] != label:
            raise SystemExit(f"class folder {names[i]} does not match label_list id {i}")
        classes.append(label)
    meta = {
        "model": os.path.basename(u8),
        "task": "classify",
        "input": {"name": in_name, "shape": [1, 3, a.imgsz, a.imgsz], "layout": "NCHW", "color": "RGB",
                  "mean": [0.0, 0.0, 0.0], "scale": [1 / 255.0] * 3},
        "output": {"name": out_name, "kind": "logits", "softmax_in_app": True},
        "crop": {"side_mm": 56, "px": a.imgsz},
        "classes": classes,
        # same keys as the vendor demo JSON, for its tools
        "input_params": {"r_mean": 0.0, "g_mean": 0.0, "b_mean": 0.0,
                         "r_scale": 0.003921, "g_scale": 0.003921, "b_scale": 0.003921},
        "weights": os.path.abspath(a.weights),
    }
    with open(a.out + ".json", "w", encoding="utf-8") as f:
        json.dump(meta, f, ensure_ascii=False, indent=1)
    print(f"wrote {fp32}, {u8}, {a.out}.json")


if __name__ == "__main__":
    main()
