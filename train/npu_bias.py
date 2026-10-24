#!/usr/bin/env python3
"""Logit bias correction fitted against the board's NPU itself.

export_npu.py fits logit_offset = mean(fp32 - uint8) with the PC's uint8 simulation (onnxruntime), but the NPU's integer
arithmetic differs from it by up to ~1.7 per logit (2026-10-24, real 5-class model: PC uint8 + offset 95.6 %, NPU with
the same offset 91.7 %, good parts 86 -> 69 %). Here the offset comes from the NPU's own raw logits instead:

  board:  NPU_EVAL_DIR=<train subset tree> NPU_DUMP=train.csv test_npu <model>.json <refs> 1
          NPU_EVAL_DIR=<val tree>          NPU_DUMP=val.csv   test_npu <model>.json <refs> 1
  PC:     python npu_bias.py --model <prefix> --train-dump train.csv --train-root <train subset tree> \
                             --val-dump val.csv [--write]

offset = mean over the training crops of (PC fp32 logits - NPU raw logits); the validation dump is only scored (per crop
and per part = mean softmax over the crops of one track), never used to fit. --write puts the offset into <prefix>.json.
"""
import argparse
import csv
import json
import os
import re

import numpy as np
import onnxruntime as ort

from export_npu import preprocess


def read_dump(path):
    rows = []
    with open(path, newline="") as f:
        for r in csv.reader(f):
            rows.append((r[0], int(r[1]), np.array([float(v) for v in r[2:]], dtype=np.float32)))
    return rows


def softmax(z):
    e = np.exp(z - z.max(-1, keepdims=True))
    return e / e.sum(-1, keepdims=True)


def score(rows, off, classes, name):
    nc = len(classes)
    conf = np.zeros((nc, nc), int)
    tracks = {}
    for path, cid, z in rows:
        p = softmax(z + off)
        conf[cid, int(p.argmax())] += 1
        m = re.search(r"_(\d+)_\d+\.\w+$", path)
        key = (cid, os.path.dirname(path), m.group(1) if m else path)
        tracks.setdefault(key, []).append(p)
    vote_ok = sum(int(np.mean(ps, axis=0).argmax() == k[0]) for k, ps in tracks.items())
    low = sum(int(np.mean(ps, axis=0).max() < 0.5) for ps in tracks.values())
    print(f"== {name}: per-crop {np.trace(conf) / conf.sum():.4f} ({np.trace(conf)}/{conf.sum()}); "
          f"per-part vote {vote_ok}/{len(tracks)}; parts below p 0.5: {low}")
    for i in range(nc):
        print(f"   {i} {classes[i]:<14s} {conf[i].tolist()}  recall {conf[i, i] / max(1, conf[i].sum()):.3f}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", required=True, help="prefix: <model>_fp32.onnx and <model>.json")
    ap.add_argument("--train-dump", required=True)
    ap.add_argument("--train-root", required=True, help="the tree the train dump was made from (paths are relative)")
    ap.add_argument("--val-dump", default="")
    ap.add_argument("--write", action="store_true", help="store the offset as logit_offset in <model>.json")
    a = ap.parse_args()

    with open(a.model + ".json", encoding="utf-8") as f:
        meta = json.load(f)
    classes = meta["classes"]
    s32 = ort.InferenceSession(a.model + "_fp32.onnx", providers=["CPUExecutionProvider"])
    iname = s32.get_inputs()[0].name
    imgsz = meta["input"]["shape"][2]

    tr = read_dump(a.train_dump)
    d = np.array([s32.run(None, {iname: preprocess(os.path.join(a.train_root, p), imgsz)})[0][0] - z for p, _, z in tr])
    off = d.mean(0)
    print(f"offset from {len(tr)} training crops (fp32 - NPU): {np.round(off, 3).tolist()}  sd {np.round(d.std(0), 3).tolist()}")
    if a.val_dump:
        va = read_dump(a.val_dump)
        old = np.array(meta.get("logit_offset") or [0.0] * len(classes), dtype=np.float32)
        score(va, np.zeros_like(off), classes, "NPU raw")
        score(va, old, classes, "NPU + offset in the JSON (PC-fitted)")
        score(va, off, classes, "NPU + NPU-fitted offset")
    if a.write:
        meta["logit_offset"] = [round(float(v), 4) for v in off]
        meta["logit_offset_note"] = f"fitted on the board's NPU over {len(tr)} training crops (npu_bias.py)"
        with open(a.model + ".json", "w", encoding="utf-8") as f:
            json.dump(meta, f, ensure_ascii=False, indent=1)
        print("wrote logit_offset to", a.model + ".json")


if __name__ == "__main__":
    main()
