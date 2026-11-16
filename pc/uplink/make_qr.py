"""Make the dashboard QR codes with OpenCV, then decode each PNG back to check it.
usage: make_qr.py <out_dir> <name1> <text1> [<name2> <text2> ...]"""
import os
import sys

import cv2
import numpy as np


def make(text, path, px=12, border=4):
    enc = cv2.QRCodeEncoder_create()
    img = enc.encode(text)                      # 1 pixel per module, 0 = dark, 255 = light
    if img is None or img.size == 0:
        raise SystemExit('encode failed: ' + path)
    img = np.pad(img, border, constant_values=255)
    img = cv2.resize(img, (img.shape[1] * px, img.shape[0] * px), interpolation=cv2.INTER_NEAREST)
    ok, buf = cv2.imencode('.png', img)
    if not ok:
        raise SystemExit('png failed: ' + path)
    with open(path, 'wb') as f:                 # imwrite cannot take a non-ASCII path on Windows
        f.write(buf.tobytes())
    back = cv2.imdecode(np.frombuffer(open(path, 'rb').read(), np.uint8), cv2.IMREAD_GRAYSCALE)
    got, _, _ = cv2.QRCodeDetector().detectAndDecode(back)
    return got == text, back.shape


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    pairs = sys.argv[2:]
    for name, text in zip(pairs[0::2], pairs[1::2]):
        ok, shape = make(text, os.path.join(out, name))
        print(name, 'decodes back OK' if ok else 'DECODE MISMATCH', shape)


if __name__ == '__main__':
    main()
