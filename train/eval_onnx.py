#!/usr/bin/env python3
"""Accuracy of the fp32 and the uint8 ONNX models on a val folder (onnxruntime on the PC), the confusion matrix
(test report: 准确率 ≥85% simple level / 分级 ≥92% advanced level), and a reference set for the board:
--refs <dir> copies N val crops there with refs.json = {file: {label, fp32 logits, uint8 logits}}, so
`test_npu` can check the NPU against the PC numerically.

    python eval_onnx.py --model ../models/part_cls --val data/synth/val --refs ../models/part_cls_refs
"""
import argparse
import glob
import json
import os
import shutil

import numpy as np
import onnxruntime as ort

from export_npu import preprocess


def softmax(x):
    e = np.exp(x - x.max())
    return e / e.sum()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", required=True, help="prefix: <model>_fp32.onnx, <model>_uint8.onnx, <model>.json")
    ap.add_argument("--val", required=True)
    ap.add_argument("--refs", default="")
    ap.add_argument("--nrefs", type=int, default=32)
    a = ap.parse_args()

    with open(a.model + ".json", encoding="utf-8") as f:
        meta = json.load(f)
    classes = meta["classes"]
    imgsz = meta["input"]["shape"][2]
    sessions = {k: ort.InferenceSession(f"{a.model}_{k}.onnx", providers=["CPUExecutionProvider"])
                for k in ("fp32", "uint8")}
    inp = {k: s.get_inputs()[0].name for k, s in sessions.items()}

    files = []
    for d in sorted(glob.glob(os.path.join(a.val, "*"))):
        cid = int(os.path.basename(d).split("_", 1)[0])
        files += [(f, cid) for f in sorted(glob.glob(os.path.join(d, "*.png")) + glob.glob(os.path.join(d, "*.jpg")))]
    nc = len(classes)
    conf = {k: np.zeros((nc, nc), int) for k in sessions}
    agree = 0
    refs = {}
    pick = set(range(0, len(files), max(1, len(files) // a.nrefs))) if a.refs else set()
    if a.refs:
        os.makedirs(a.refs, exist_ok=True)
    for i, (f, cid) in enumerate(files):
        x = preprocess(f, imgsz)
        out = {k: s.run(None, {inp[k]: x})[0][0] for k, s in sessions.items()}
        for k in sessions:
            conf[k][cid, int(np.argmax(out[k]))] += 1
        agree += int(np.argmax(out["fp32"]) == np.argmax(out["uint8"]))
        if i in pick:
            name = f"{i:04d}_{classes[cid]}{os.path.splitext(f)[1]}"
            shutil.copy(f, os.path.join(a.refs, name))
            refs[name] = {"label": cid, "fp32": out["fp32"].tolist(), "uint8": out["uint8"].tolist()}

    for k in sessions:
        c = conf[k]
        acc = np.trace(c) / c.sum()
        print(f"\n== {k}: accuracy {acc:.4f} ({np.trace(c)}/{c.sum()})")
        print("rows = true, cols = predicted: " + " ".join(f"{j}" for j in range(nc)))
        for j in range(nc):
            r = c[j]
            print(f"  {j} {classes[j]:<12s} " + " ".join(f"{v:4d}" for v in r) + f"   recall {r[j] / max(1, r.sum()):.3f}")
        ok_ids = [j for j, n in enumerate(classes) if n.endswith("_ok")]
        okng = np.zeros((2, 2), int)   # simple level: OK / NG two-class view
        for t in range(nc):
            for p in range(nc):
                okng[int(t not in ok_ids), int(p not in ok_ids)] += c[t, p]
        print(f"  OK/NG two-class accuracy {np.trace(okng) / okng.sum():.4f}  (NG missed as OK: {okng[1, 0]})")
    print(f"\nfp32 / uint8 top-1 agreement {agree / len(files):.4f} on {len(files)} crops")
    if a.refs:
        with open(os.path.join(a.refs, "refs.json"), "w", encoding="utf-8") as f:
            json.dump({"classes": classes, "imgsz": imgsz, "refs": refs}, f, indent=1)
        print(f"wrote {len(refs)} reference crops -> {a.refs}")


if __name__ == "__main__":
    main()
