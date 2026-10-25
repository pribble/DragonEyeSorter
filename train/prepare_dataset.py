#!/usr/bin/env python3
"""Merge `des_tool collect` sessions into a training set: <out>/{train,val}/<id>_<label>/.

Crops of the same physical part are near-duplicates, so the split is by PART, never by image: otherwise validation
accuracy is inflated. Two ways:
  --val-sessions <substr> ...  whole sessions whose name contains one of the substrings go to val (best: collect the
                               held-out sample numbers of 物料与缺陷样本.md §6.1 in their own sessions)
  otherwise                    a random --val-frac of the tracks of every class (seeded)
A part passed twice through the camera in one session is two tracks, so the random split can still leak a little;
the session split cannot.
  --max-train <n>              at most ~n training images per class: every track is kept, each with fewer crops
                               (evenly spaced along its pass; crops of one pass are near-duplicates anyway). The
                               collected crops are never deleted, the cap only applies to the copy in --out.

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
    ap.add_argument("--max-train", type=int, default=0, help="cap training images per class (0 = all)")
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

    if a.max_train > 0:   # thin the crops of over-represented classes, keeping every track
        crop_idx = lambda f: int(os.path.splitext(os.path.basename(f))[0].rsplit("_", 1)[-1])
        for cls_dir, keys in by_cls.items():
            tk = [k for k in keys if split[k] == "train"]
            total = sum(len(parts[k]) for k in tk)
            if not tk or total <= a.max_train:
                continue
            per = max(1, a.max_train // len(tk))
            for k in tk:
                fs = sorted(parts[k], key=crop_idx)
                if len(fs) > per:
                    parts[k] = [fs[round(i * (len(fs) - 1) / max(1, per - 1))] for i in range(per)] if per > 1 else [fs[len(fs) // 2]]
            print(f"{cls_dir}: {total} training crops -> {sum(len(parts[k]) for k in tk)} ({per} per track)")
    if os.path.exists(a.out):
        out_n = os.path.normcase(os.path.realpath(a.out))
        for root in a.src:   # the collected crops are never deleted: refuse an --out inside a collect root
            r_n = os.path.normcase(os.path.realpath(root))
            if out_n == r_n or out_n.startswith(r_n + os.sep):
                raise SystemExit("--out %s is inside collect root %s: refusing to delete the source crops" % (a.out, root))
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
