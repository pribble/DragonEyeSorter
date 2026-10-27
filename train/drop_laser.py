#!/usr/bin/env python3
"""Copy one `des_tool collect` session into a class folder, leaving out crops lit by the through-beam laser.

The laser photo-eye crosses the belt a few mm above it. A part passing it gets a small red spot (10-28, beam aligned:
about 9x7 px at the tooth tips, only in the 1~3 crops taken at the beam). Sessions collected before the laser was
installed (10-27) have none, so the spot would only ever appear in the later classes - a cue the model could learn
instead of the defect. Decision (user, 10-28): keep every such crop out of the training AND test sets (the originals
stay on the board, the dropped names go to a list), check with the laser on at the end, and only if that shows an
effect add them back - then with laser-lit crops of every class.

Rule: >= 5 px with R - max(G, B) > 30, or any px > 60 (crops away from the beam stay <= 30).

    python drop_laser.py --src <session dir> --dst <collect7>/<session>/<id>_<label> [--list <file>]
"""
import argparse
import glob
import os
import re
import shutil
import sys

import numpy as np
from PIL import Image

RED, RED_MIN_PX, RED_ANY = 30, 5, 60


def redness(path):
    a = np.asarray(Image.open(path).convert("RGB")).astype(np.int16)
    r = a[..., 0] - np.maximum(a[..., 1], a[..., 2])
    return int((r > RED).sum()), int((r > RED_ANY).sum()), int(r.max())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", required=True, help="one collect session (its *.jpg, searched recursively)")
    ap.add_argument("--dst", required=True, help="class folder to fill, e.g. collect7/val_flashL/5_gear_flash_l")
    ap.add_argument("--list", help="dropped names (default <dst parent>.dropped_laser.txt)")
    a = ap.parse_args()

    if os.path.isdir(a.dst) and os.listdir(a.dst):
        sys.exit(f"{a.dst} is not empty, not overwriting")
    files = sorted(glob.glob(os.path.join(a.src, "**", "*.jpg"), recursive=True))
    if not files:
        sys.exit(f"no crops under {a.src}")
    names = [os.path.basename(f) for f in files]
    if len(set(names)) != len(names):
        sys.exit("duplicate crop names in the session")

    drop, by_idx = [], {}
    for f, name in zip(files, names):
        n_red, n_any, peak = redness(f)
        m = re.search(r"_(\d+)\.jpg$", name)  # <session>_<label>_<part>_<crop index>.jpg
        if m:
            i = int(m.group(1))
            by_idx[i] = max(by_idx.get(i, 0), peak)
        if n_red >= RED_MIN_PX or n_any >= 1:
            drop.append(name)

    os.makedirs(a.dst, exist_ok=True)
    gone = set(drop)
    for f, name in zip(files, names):
        if name not in gone:
            shutil.copy2(f, a.dst)
    lst = a.list or os.path.dirname(os.path.normpath(a.dst)) + ".dropped_laser.txt"
    with open(lst, "w") as fh:
        fh.write(f"# {a.src}: crops dropped for the laser red spot (>= {RED_MIN_PX} px R-max(G,B)>{RED} or any >{RED_ANY})\n")
        fh.writelines(n + "\n" for n in drop)

    print(f"{len(files)} crops: kept {len(files) - len(drop)} -> {a.dst}, dropped {len(drop)} -> {lst}")
    print("peak redness by crop index:", " ".join(f"{i}:{by_idx[i]}" for i in sorted(by_idx)))


if __name__ == "__main__":
    main()
