#!/usr/bin/env python3
"""Diagnose which part of the classifier the board's ArmNN ONNX parser / Alnpu backend cannot take: build variants
of the fp32 model, quantize each exactly like export_npu.py, and write minimal JSONs; then try to load each on the
board with test_npu (a class-count mismatch error = it LOADED; a crash = it did not).
Result 2026-10-11: the crash was NOT an operator but missing tensor shapes (value_info) after quantization; with
shape inference every variant loads (export_npu.py does it now). Kept for the next unknown model problem.

  v1_opset   : the exported model with only the ai.onnx opset import (quantizer / slim add other domains)
  v2_gapconv : head GlobalAveragePool -> 1x1 Conv (Gemm weights) -> output [1, nc, 1, 1] (no Flatten / Gemm)
  v3_avgconv : head AveragePool 7x7 (explicit kernel) -> 1x1 Conv -> [1, nc, 1, 1]
  v4_body    : backbone only, ends at the last Conv before the pooling
"""
import argparse
import glob
import json
import os
import random

import numpy as np
import onnx
from onnx import helper, numpy_helper
from onnxruntime.quantization import CalibrationMethod, QuantFormat, QuantType, quantize_static
from onnxruntime.quantization.shape_inference import quant_pre_process

from export_npu import Reader


def only_ai_onnx(m):
    keep = [o for o in m.opset_import if o.domain in ("", "ai.onnx")]
    del m.opset_import[:]
    m.opset_import.extend(keep)
    return m


def conv_head(m, avg_kernel=None):
    g = m.graph
    gemm = [n for n in g.node if n.op_type == "Gemm"][0]
    flat = [n for n in g.node if n.op_type == "Flatten"][0]
    gap = [n for n in g.node if n.op_type == "GlobalAveragePool"][0]
    inits = {i.name: i for i in g.initializer}
    W = numpy_helper.to_array(inits[gemm.input[1]])     # [nc, C] (transB=1 in torch export)
    B = numpy_helper.to_array(inits[gemm.input[2]]) if len(gemm.input) > 2 else np.zeros(W.shape[0], np.float32)
    trans_b = next((a.i for a in gemm.attribute if a.name == "transB"), 0)
    if not trans_b:
        W = W.T
    nc, C = W.shape
    wname, bname = "head_conv_W", "head_conv_B"
    g.initializer.extend([numpy_helper.from_array(W.reshape(nc, C, 1, 1).astype(np.float32), wname),
                          numpy_helper.from_array(B.astype(np.float32), bname)])
    pool_out = gap.output[0]
    if avg_kernel:
        ap = helper.make_node("AveragePool", [gap.input[0]], [pool_out], kernel_shape=[avg_kernel, avg_kernel],
                              strides=[1, 1], name="head_avgpool")
        g.node.insert(list(g.node).index(gap), ap)
        g.node.remove(gap)
    out_name = "logits"
    conv = helper.make_node("Conv", [pool_out, wname, bname], [out_name], kernel_shape=[1, 1], name="head_conv")
    idx = list(g.node).index(flat)
    g.node.remove(flat)
    g.node.remove(gemm)
    g.node.insert(idx, conv)
    del g.output[:]
    g.output.extend([helper.make_tensor_value_info(out_name, onnx.TensorProto.FLOAT, [1, nc, 1, 1])])
    for i in list(g.initializer):
        if i.name in (gemm.input[1], gemm.input[2] if len(gemm.input) > 2 else ""):
            g.initializer.remove(i)
    return m


def body(m):
    g = m.graph
    gap = [n for n in g.node if n.op_type == "GlobalAveragePool"][0]
    src = gap.input[0]
    return onnx.utils.Extractor(m).extract_model([g.input[0].name], [src])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fp32", required=True)
    ap.add_argument("--calib", required=True)
    ap.add_argument("--out", default="out/variants")
    ap.add_argument("--json", required=True, help="model json of export_npu.py (classes / input)")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    meta = json.load(open(a.json, encoding="utf-8"))
    files = sorted(glob.glob(os.path.join(a.calib, "**", "*.png"), recursive=True))
    random.Random(0).shuffle(files)
    files = files[:100]
    base = onnx.load(a.fp32)
    variants = {
        "v1_opset": only_ai_onnx(onnx.load(a.fp32)),
        "v2_gapconv": only_ai_onnx(conv_head(onnx.load(a.fp32))),
        "v3_avgconv": only_ai_onnx(conv_head(onnx.load(a.fp32), avg_kernel=7)),
        "v4_body": only_ai_onnx(body(base)),
    }
    for name, m in variants.items():
        onnx.checker.check_model(m)
        fp = os.path.join(a.out, name + "_fp32.onnx")
        onnx.save(m, fp)
        pre = os.path.join(a.out, name + "_pre.onnx")
        quant_pre_process(fp, pre)
        u8 = os.path.join(a.out, name + "_uint8.onnx")
        quantize_static(pre, u8, Reader(files, m.graph.input[0].name, 224), quant_format=QuantFormat.QDQ,
                        activation_type=QuantType.QUInt8, weight_type=QuantType.QUInt8, per_channel=False,
                        calibrate_method=CalibrationMethod.MinMax)
        os.remove(pre)
        q = onnx.shape_inference.infer_shapes(only_ai_onnx(onnx.load(u8)))   # the board parser needs all shapes
        onnx.save(q, u8)
        j = dict(meta)
        j["model"] = os.path.basename(u8)
        with open(os.path.join(a.out, name + ".json"), "w", encoding="utf-8") as f:
            json.dump(j, f, indent=1)
        print(name, "->", u8, "ops", sorted({n.op_type for n in q.graph.node} - {"QuantizeLinear", "DequantizeLinear"}),
              "opsets", [(o.domain, o.version) for o in q.opset_import])


if __name__ == "__main__":
    main()
