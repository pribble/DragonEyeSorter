#!/usr/bin/env python3
"""Merge `des_tool collect` sessions into a training set: <out>/{train,val}/<id>_<label>/.

Crops of the same physical part are near-duplicates, so the split is by PART, never by image: otherwise validation
accuracy is inflated. Two ways:
  --val-sessions <substr> ...  whole sessions whose name contains one of the substrings go to val (best: collect the
                               held-out sample numbers of 物料与缺陷样本.md §6.1 in their own sessions)
  otherwise                    a random --val-frac of the tracks of every class (seeded)
A part passed twice through the camera in one session is two tracks, so the random split can still leak a little;
the session split cannot.

    python prepare_dataset.py --src /path/to/collect --out data/real --val-sessions val_
"""
import argparse
import collections
import glob
import os
import random
import shutil


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", nargs="+", required=True, help="collect roots (each holds <session>/<id>_<label>/*.jpg)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--val-sessions", nargs="*", default=[])
    ap.add_argument("--val-frac", type=float, default=0.2)
    ap.add_argument("--seed", type=int, default=0)
    a = ap.parse_args()

    parts = collections.defaultdict(list)          # (class folder, session, track) -> files
    for root in a.src:
        for f in glob.glob(os.path.join(root, "*", "*_*", "*.jpg")):
            cls_dir = os.path.basename(os.path.dirname(f))
            session = os.path.basename(os.path.dirname(os.path.dirname(f)))
            if cls_dir == "full" or not cls_dir.split("_", 1)[0].isdigit():
                continue
            # file: <session>_<track>_<k>.jpg
            stem = os.path.splitext(os.path.basename(f))[0]
            track = stem.rsplit("_", 2)[-2]
            parts[(cls_dir, session, track)].append(f)
    if not parts:
        raise SystemExit("no crops found")

    rng = random.Random(a.seed)
    by_cls = collections.defaultdict(list)
    for key in parts:
        by_cls[key[0]].append(key)
    split = {}
    for cls_dir, keys in by_cls.items():
        keys.sort()
        if a.val_sessions:
            for k in keys:
                split[k] = "val" if any(s in k[1] for s in a.val_sessions) else "train"
        else:
            rng.shuffle(keys)
            nval = max(1, round(len(keys) * a.val_frac)) if len(keys) > 1 else 0
            for i, k in enumerate(keys):
                split[k] = "val" if i < nval else "train"

    if os.path.exists(a.out):
        shutil.rmtree(a.out)
    stats = collections.defaultdict(lambda: [0, 0, 0, 0])   # train parts, train images, val parts, val images
    for key, files in parts.items():
        s = split[key]
        d = os.path.join(a.out, s, key[0])
        os.makedirs(d, exist_ok=True)
        for f in files:
            shutil.copy2(f, os.path.join(d, os.path.basename(f)))
        st = stats[key[0]]
        st[0 if s == "train" else 2] += 1
        st[1 if s == "train" else 3] += len(files)
    print(f"{'class':<16s} {'train parts':>11s} {'images':>7s} {'val parts':>9s} {'images':>7s}")
    for c in sorted(stats):
        print(f"{c:<16s} {stats[c][0]:>11d} {stats[c][1]:>7d} {stats[c][2]:>9d} {stats[c][3]:>7d}")
    missing = [c for c in sorted(stats) if stats[c][2] == 0]
    if missing:
        print("WARNING: no validation parts for", ", ".join(missing))


if __name__ == "__main__":
    main()
