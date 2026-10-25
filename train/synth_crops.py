#!/usr/bin/env python3
"""Synthetic part crops for the classes in models/label_list.json (5: ok / short_l / short_h / dirty_l / dirty_h),
for the TOOLCHAIN SMOKE TEST only.

Same geometry as ps/src/measure/roi_crop: 56 mm square -> 224 px (0.25 mm/px), part centered, long axis along x.
White POM gear (m1, 30 teeth, OD 32, bore 6) / rack (50 mm, 15 teeth, 15 mm high) on a dark belt, with the three
defect kinds drawn the way they are made by hand (物料与缺陷样本.md): short = tooth tips cut off, flash = material
filling a tooth gap, dirty = dark spots on the face. Lighting / blur / noise jitter.

It does NOT replace real data: it proves train -> export -> quantize -> ArmNN on the board -> des_app works, and
gives the NPU latency, before the parts arrive.

    python synth_crops.py --out data/synth --train 300 --val 60
writes <out>/{train,val}/<id>_<label>/*.png (folder names sort in label_list order -> model output = class id)
"""
import argparse
import json
import math
import os

import cv2
import numpy as np

PX_PER_MM = 4.0          # 224 px / 56 mm
SS = 4                   # supersampling
N = 224


def load_labels(path):
    with open(path, encoding="utf-8") as f:
        labels = json.load(f)["labels"]
    return [(l["id"], l["label"], l["kind"]) for l in sorted(labels, key=lambda l: l["id"])]


def to_px(pts_mm, c_px):
    """mm (x right, y up) around the crop center -> supersampled pixel coords (int32 for fillPoly)"""
    p = np.asarray(pts_mm, dtype=np.float64)
    x = (c_px[0] + p[:, 0] * PX_PER_MM) * SS
    y = (c_px[1] - p[:, 1] * PX_PER_MM) * SS
    return np.stack([x, y], 1).round().astype(np.int32)


def gear_outline(rng, short_teeth=(), fill_gaps=(), z=30, ra=16.0, rr=13.75):
    p = 2 * math.pi / z
    phase = rng.uniform(0, p)
    tip = np.full(z, ra)
    for k, cut in short_teeth:
        tip[k] = ra - cut
    fill = np.zeros(z)
    for k, h in fill_gaps:
        fill[k] = h
    pts = []
    for i in range(1440):
        th = 2 * math.pi * i / 1440
        rel = th - phase
        k = int(math.floor(rel / p + 0.5))
        a = rel - k * p
        k %= z
        aa = abs(a)
        t = tip[k]
        if aa <= 0.13 * p:
            r = t
        elif aa < 0.32 * p:
            r = t + (rr - t) * (aa - 0.13 * p) / (0.19 * p)
        else:
            r = rr
        if aa > 0.13 * p:
            gk = k if a > 0 else (k - 1) % z
            if fill[gk] > 0:
                r = max(r, fill[gk])
        pts.append((r * math.cos(th), r * math.sin(th)))
    return pts


def rack_outline(short_teeth=(), fill_gaps=(), n=15, length=50.0):
    hb, ht = 12.75, 15.0
    tip = np.full(n, ht)
    for k, cut in short_teeth:
        tip[k] = ht - cut
    fill = np.zeros(n)
    for k, h in fill_gaps:
        fill[k] = h
    loc = [(0, 0), (length, 0), (length, hb)]
    for k in range(n - 1, -1, -1):
        uc = 2.5 + k * math.pi
        yr = fill[k] if fill[k] > 0 else hb
        yl = fill[k - 1] if k > 0 and fill[k - 1] > 0 else hb
        loc += [(uc + 1.0, yr), (uc + 0.5, tip[k]), (uc - 0.5, tip[k]), (uc - 1.0, yl)]
    loc.append((0, hb))
    return [(x - length / 2, y - ht / 2) for x, y in loc]


def render(rng, kind, defect):
    """returns BGR uint8 224x224; defect: ok / short / flash / dirty with an optional _l / _h severity
    suffix (5-class label_list: gear_short_l/h, gear_dirty_l/h) — _h draws the defect heavier"""
    sev = 0
    if defect.endswith("_l") or defect.endswith("_h"):
        sev = 1 if defect.endswith("_h") else 0
        defect = defect[:-2]
    big = np.zeros((N * SS, N * SS), np.uint8)
    c_px = (N / 2 + rng.uniform(-6, 6), N / 2 + rng.uniform(-6, 6))     # +-1.5 mm centering error
    if kind == "gear":
        z = 30
        short, fill = [], []
        if defect == "short":
            k0 = int(rng.integers(z))
            for j in range(int(rng.integers(2, 5)) if sev else int(rng.integers(1, 3))):
                short.append(((k0 + j) % z, rng.uniform(0.8, 2.25)))
        if defect == "flash":
            for _ in range(int(rng.integers(1, 3))):
                fill.append((int(rng.integers(z)), rng.uniform(14.4, 15.8)))
        outer = gear_outline(rng, short, fill)
        cv2.fillPoly(big, [to_px(outer, c_px)], 255, cv2.LINE_8, 0)
        bore = [(3.0 * math.cos(t), 3.0 * math.sin(t)) for t in np.linspace(0, 2 * math.pi, 120, endpoint=False)]
        cv2.fillPoly(big, [to_px(bore, c_px)], 0, cv2.LINE_8, 0)
        face_r = 13.0
    else:
        n = 15
        short, fill = [], []
        if defect == "short":
            k0 = int(rng.integers(n))
            for j in range(int(rng.integers(2, 4)) if sev else int(rng.integers(1, 3))):
                if k0 + j < n:
                    short.append((k0 + j, rng.uniform(0.8, 2.25)))
        if defect == "flash":
            for _ in range(int(rng.integers(1, 3))):
                fill.append((int(rng.integers(n - 1)), rng.uniform(13.5, 14.8)))
        outer = rack_outline(short, fill)
        if rng.random() < 0.5:                       # teeth up or down (axis ambiguity)
            outer = [(x, -y) for x, y in outer]
        if rng.random() < 0.5:
            outer = [(-x, y) for x, y in outer]
        outer = [(x * math.cos(a) - y * math.sin(a), x * math.sin(a) + y * math.cos(a))
                 for a in [rng.uniform(-0.03, 0.03)] for x, y in outer]
        cv2.fillPoly(big, [to_px(outer, c_px)], 255, cv2.LINE_8, 0)
        face_r = 6.0
    mask = cv2.resize(big, (N, N), interpolation=cv2.INTER_AREA).astype(np.float32) / 255.0

    # shading: two side bars -> brighter toward left / right, slight vertical gradient
    xs = np.linspace(-1, 1, N)[None, :]
    ys = np.linspace(-1, 1, N)[:, None]
    face = rng.uniform(170, 225) + rng.uniform(5, 25) * np.abs(xs) + rng.uniform(-10, 10) * ys
    belt = rng.uniform(25, 60) + rng.normal(0, 3, (N, N))
    img = belt * (1 - mask) + face * mask

    if defect == "dirty":                            # dark spots on the face
        for _ in range(int(rng.integers(3, 6)) if sev else int(rng.integers(1, 3))):
            ang, rad = rng.uniform(0, 2 * math.pi), rng.uniform(0, face_r)
            if kind == "rack":
                cx, cy = c_px[0] + rng.uniform(-22, 22) * PX_PER_MM, c_px[1] + rng.uniform(-4, 4) * PX_PER_MM
            else:
                cx, cy = c_px[0] + rad * math.cos(ang) * PX_PER_MM, c_px[1] + rad * math.sin(ang) * PX_PER_MM
            spot = np.zeros((N, N), np.float32)
            ax = (max(1, int(rng.uniform(0.4, 1.6) * PX_PER_MM)), max(1, int(rng.uniform(0.3, 1.2) * PX_PER_MM)))
            cv2.ellipse(spot, (int(cx), int(cy)), ax, rng.uniform(0, 180), 0, 360, 1.0, -1, cv2.LINE_AA)
            spot = cv2.GaussianBlur(spot, (0, 0), 0.8) * mask
            img = img * (1 - spot) + rng.uniform(35, 100) * spot

    img = cv2.GaussianBlur(img, (0, 0), rng.uniform(0.5, 1.1))
    img = img * rng.uniform(0.85, 1.15) + rng.uniform(-15, 15)
    img += rng.normal(0, rng.uniform(1.5, 4), img.shape)
    gray = np.clip(img, 0, 255).astype(np.uint8)
    bgr = cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR).astype(np.float32)
    bgr *= np.array([rng.uniform(0.95, 1.05) for _ in range(3)])[None, None, :]   # slight color cast
    return np.clip(bgr, 0, 255).astype(np.uint8)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--labels", default=os.path.join(os.path.dirname(__file__), "..", "models", "label_list.json"))
    ap.add_argument("--out", default="data/synth")
    ap.add_argument("--train", type=int, default=300)
    ap.add_argument("--val", type=int, default=60)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
    rng = np.random.default_rng(a.seed)
    labels = load_labels(a.labels)
    for split, count in (("train", a.train), ("val", a.val)):
        for cid, label, kind in labels:
            d = os.path.join(a.out, split, f"{cid}_{label}")
            os.makedirs(d, exist_ok=True)
            defect = label.split("_", 1)[1]        # ok / short / flash / dirty
            for i in range(count):
                cv2.imwrite(os.path.join(d, f"{split}_{i:04d}.png"), render(rng, kind, defect))
        print(f"{split}: {count} x {len(labels)} classes -> {a.out}/{split}")


if __name__ == "__main__":
    main()
