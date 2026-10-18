# -*- coding: utf-8 -*-
"""作品报告 PDF 插图高清化 / 清晰度体检（通用工具，只依赖 PyMuPDF）。

**为什么要这一步**：Word 导出 PDF 时会把插图重采样到 96 dpi（实测 3200px 宽的原图在
14.5cm 宽的图框里只剩 547px）。图内小字因此发虚，打印与屏幕放大都看不清。
本脚本把 PDF 里每张插图的图像对象**按原图重放**（`Page.replace_image`），显示尺寸
（图框）一点不动，只把像素换回源图分辨率，等效 dpi 随之从 96 提到 ~500。

两种用法（互斥）：

    # ① 体检：只测量打印，不改文件。给别的链当门禁用。
    python submission/build/pdf_images_hires.py --pdf out/报告.pdf --report

    # ② 高清化：按**文档出现顺序**把 --order 里的图逐张替换
    python submission/build/pdf_images_hires.py --pdf out/报告.pdf \
        --order docs/figures/a.png docs/figures/b.png ... [--inplace]

    --inplace          直接写回原 PDF（否则默认写 <同名>.hires.pdf，原文件不动）
    --out <路径>        指定输出 PDF（与 --inplace 二选一）
    --min-dpi <数字>    等效 dpi 下限，默认 300；0 = 不做阈值判定

判定与退出码：
    0  全部插图等效 dpi ≥ --min-dpi，且替换数量与 PDF 里的图像数量一致
    1  有插图低于下限、或数量对不上（--order 数量 ≠ PDF 内图像数量）
    2  缺 PyMuPDF / 参数用法错误

    `--order` 的图必须与 PDF 里的插图**一一对应且顺序一致**（按页、按页内自上而下）。
    报告里插图与图注一一对应，没有图标类图片，所以顺序就是正文里 `![](...)` 的顺序。
    数量对不上时脚本不猜、不部分替换，直接报错退出。

依赖 PyMuPDF（`python -c "import pymupdf"` 能过即可）。本文件不写主机绝对路径：
本机若当前解释器缺 PyMuPDF，脚本会在运行时探测 `python` / `py -3`，只把**探测结果**
打印出来给你照抄，不落盘、不写进文件。
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

# 控制台按 UTF-8 输出（Windows 默认 GBK 遇到中文 / µ / ≥ 会报 UnicodeEncodeError）
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):  # pragma: no cover
        pass

MIN_DPI_DEFAULT = 300.0
ASPECT_WARN = 0.02          # 宽高比偏差 > 2%：图会被拉伸，只警告不判失败


def load_pymupdf():
    """导入 PyMuPDF；缺了就打印明确的安装 / 换解释器提示，返回 None。"""
    try:
        import pymupdf  # noqa: F401
        return pymupdf
    except ImportError:
        pass
    print("[error] 当前 Python 没有 PyMuPDF：%s" % sys.executable, file=sys.stderr)
    print("        装一个：python -m pip install pymupdf", file=sys.stderr)
    found = []
    for exe, pre in (("python", []), ("py", ["-3"])):
        try:
            r = subprocess.run([exe] + pre + ["-c", "import pymupdf, sys; print(sys.executable)"],
                               capture_output=True, text=True, timeout=60)
        except (OSError, subprocess.SubprocessError):
            continue
        if r.returncode == 0 and r.stdout.strip():
            found.append((exe, pre, r.stdout.strip().splitlines()[-1]))
    if found:
        exe, pre, real = found[0]
        print("        本机这个解释器有 PyMuPDF：%s" % real, file=sys.stderr)
        print("        换成它跑（不要写进脚本）：%s %s <本脚本> <参数>"
              % (exe, " ".join(pre)), file=sys.stderr)
    else:
        print("        本机 PATH 上的 python / py -3 都没有，先 pip install pymupdf",
              file=sys.stderr)
    return None


def page_images(doc):
    """按文档出现顺序列出插图：逐页、页内按显示位置自上而下。

    返回 [{page, xref, px_w, px_h, disp_w_pt, disp_cm, dpi, aspect, bbox}]
    """
    out = []
    for pno, page in enumerate(doc, 1):
        infos = []
        for info in page.get_image_info(xrefs=True):
            xref = info.get("xref") or 0
            if not xref:
                continue                      # 没有 xref 的不是可替换的位图对象
            x0, y0, x1, y1 = info["bbox"]
            infos.append((y0, x0, xref, info.get("width") or 0,
                          info.get("height") or 0, (x0, y0, x1, y1)))
        for y0, x0, xref, w, h, bbox in sorted(infos, key=lambda t: (round(t[0], 1), t[1])):
            disp_w = bbox[2] - bbox[0]
            out.append({
                "page": pno, "xref": xref, "px_w": int(w), "px_h": int(h),
                "disp_w_pt": disp_w, "disp_cm": disp_w / 28.3465,
                "dpi": (int(w) / (disp_w / 72.0)) if disp_w else 0.0,
                "aspect": (int(w) / int(h)) if h else 0.0, "bbox": bbox,
            })
    return out


def fmt_row(idx, item, after=None):
    s = "#%d p%02d xref=%-4d %s" % (idx, item["page"], item["xref"],
                                    "%4dpx 宽 @ %3.0f dpi（显示宽 %.2fcm）"
                                    % (item["px_w"], item["dpi"], item["disp_cm"]))
    if after is not None:
        s += "  →  %4dpx 宽 @ %3.0f dpi" % (after["px_w"], after["dpi"])
    return s


def distinct(items):
    """去重成「图像对象」列表：同一个 xref 在多页复用只算一张。"""
    seen, uniq = set(), []
    for it in items:
        if it["xref"] in seen:
            continue
        seen.add(it["xref"])
        uniq.append(it)
    return uniq


def report(pdf_path: Path, min_dpi: float) -> int:
    pm = load_pymupdf()
    if pm is None:
        return 2
    with pm.open(str(pdf_path)) as doc:
        items = page_images(doc)
        uniq = distinct(items)
        print("PDF  : %s" % pdf_path)
        print("        %d 页，%.2f MB，图片对象 %d 个（出现 %d 次）"
              % (doc.page_count, pdf_path.stat().st_size / 1e6, len(uniq), len(items)))
        low = []
        for i, it in enumerate(uniq, 1):
            tag = "" if it["dpi"] >= min_dpi else "   [低于 %.0f dpi]" % min_dpi
            print(fmt_row(i, it) + tag)
            if it["dpi"] < min_dpi:
                low.append(i)
        if min_dpi > 0 and low:
            print("[FAIL] %d 张插图等效 dpi < %.0f：%s" % (len(low), min_dpi, low))
            return 1
        if min_dpi > 0:
            print("[ok] 全部 %d 张插图等效 dpi ≥ %.0f（最低 %.0f）"
                  % (len(uniq), min_dpi, min(it["dpi"] for it in uniq)))
    return 0


def hires(pdf_path: Path, order, min_dpi: float, out_path: Path, inplace: bool) -> int:
    pm = load_pymupdf()
    if pm is None:
        return 2
    for src in order:
        if not src.is_file():
            print("[error] --order 里的图不存在：%s" % src, file=sys.stderr)
            return 1
    target = pdf_path if inplace else out_path
    if target.resolve() == pdf_path.resolve() and not inplace:
        print("[error] 输出路径与输入相同，请加 --inplace 或换 --out", file=sys.stderr)
        return 2

    print("PDF  : %s（%.2f MB）" % (pdf_path, pdf_path.stat().st_size / 1e6))
    with pm.open(str(pdf_path)) as doc:
        before = distinct(page_images(doc))
        if len(before) != len(order):
            print("[error] 数量对不上：PDF 里 %d 张插图，--order 给了 %d 张"
                  % (len(before), len(order)), file=sys.stderr)
            for i, it in enumerate(before, 1):
                print("        PDF #%d：p%02d xref=%d %dx%dpx"
                      % (i, it["page"], it["xref"], it["px_w"], it["px_h"]), file=sys.stderr)
            for i, src in enumerate(order, 1):
                print("        --order #%d：%s" % (i, src.name), file=sys.stderr)
            return 1

        print("替换前：%d 张插图" % len(before))
        for i, it in enumerate(before, 1):
            print("        " + fmt_row(i, it))
        # 替换：逐张按顺序，同一 xref 只替换一次（多页复用时各页同步变清晰）
        pairs, done = [], {}
        for idx, (it, src) in enumerate(zip(before, order), 1):
            px = pm.Pixmap(str(src))
            if it["aspect"] and px.width and px.height:
                src_aspect = px.width / px.height
                if abs(src_aspect - it["aspect"]) / it["aspect"] > ASPECT_WARN:
                    print("[!] #%d 宽高比不一致：源图 %.3f vs PDF 显示框 %.3f —— 图会被拉伸"
                          % (idx, src_aspect, it["aspect"]))
            if it["xref"] not in done:      # 同一 xref 多页复用时只替换一次
                doc[it["page"] - 1].replace_image(it["xref"], filename=str(src))
                done[it["xref"]] = True
            pairs.append((idx, it, src, (px.width, px.height)))
        tmp = target.with_suffix(target.suffix + ".tmp")
        doc.save(str(tmp), garbage=3, deflate=True)
    shutil.move(str(tmp), str(target))
    print("写出  : %s（%.2f MB）" % (target, target.stat().st_size / 1e6))

    with pm.open(str(target)) as doc2:
        after_all = distinct(page_images(doc2))
        print("替换后：%d 张插图" % len(after_all))
        low, mismatch = [], []
        for (idx, old, src, px), new in zip(pairs, after_all):
            print("        " + fmt_row(idx, old, new) + "   [%s]" % src.name)
            if px != (new["px_w"], new["px_h"]):
                mismatch.append(idx)
            if min_dpi > 0 and new["dpi"] < min_dpi:
                low.append(idx)
        if mismatch:
            print("[FAIL] 替换后像素尺寸与源图不符：%s" % mismatch)
            return 1
        if low:
            print("[FAIL] %d 张插图等效 dpi < %.0f：%s（源图分辨率不够或图框太宽）"
                  % (len(low), min_dpi, low))
            return 1
        if min_dpi > 0:
            print("[ok] %d 张插图全部 ≥ %.0f dpi（最低 %.0f，替换前最低 %.0f）"
                  % (len(after_all), min_dpi, min(it["dpi"] for it in after_all),
                     min(it["dpi"] for it in before)))
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description="作品报告 PDF 插图高清化 / 清晰度体检（PyMuPDF）")
    ap.add_argument("--pdf", required=True, help="目标 PDF")
    ap.add_argument("--order", nargs="+", default=None,
                    help="按文档出现顺序排列的源图（与 PDF 内插图一一对应）")
    ap.add_argument("--report", action="store_true", help="只测量打印，不改文件")
    ap.add_argument("--min-dpi", type=float, default=MIN_DPI_DEFAULT,
                    help="等效 dpi 下限，默认 %(default)s；0 = 不判定")
    ap.add_argument("--inplace", action="store_true", help="直接写回原 PDF")
    ap.add_argument("--out", default=None, help="输出 PDF（默认 <同名>.hires.pdf）")
    args = ap.parse_args(argv)

    pdf_path = Path(args.pdf).resolve()
    if not pdf_path.is_file():
        print("[error] 找不到 PDF：%s" % pdf_path, file=sys.stderr)
        return 2
    if args.report and args.order:
        print("[error] --report 与 --order 互斥", file=sys.stderr)
        return 2
    if not args.report and not args.order:
        print("[error] 要么给 --order 做替换，要么给 --report 只体检", file=sys.stderr)
        return 2

    if args.report:
        return report(pdf_path, args.min_dpi)
    out_path = Path(args.out).resolve() if args.out else \
        pdf_path.with_name(pdf_path.stem + ".hires.pdf")
    return hires(pdf_path, [Path(p).resolve() for p in args.order],
                 args.min_dpi, out_path, args.inplace)


if __name__ == "__main__":
    sys.exit(main())
