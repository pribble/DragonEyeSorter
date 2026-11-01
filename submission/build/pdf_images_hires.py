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
        --order submission/figures/a.png submission/figures/b.png ... [--inplace]

    --inplace          直接写回原 PDF（否则默认写 <同名>.hires.pdf，原文件不动）
    --out <路径>        指定输出 PDF（与 --inplace 二选一）
    --min-dpi <数字>    等效 dpi 下限，默认 300；0 = 不做阈值判定

判定与退出码：
    0  全部插图等效 dpi ≥ --min-dpi，且替换数量与 PDF 里的图像数量一致
    1  有插图低于下限、或数量对不上（--order 数量 ≠ PDF 内图像数量）
    2  缺 PyMuPDF / 参数用法错误

    `--order` 的图必须与 PDF 里的**正文插图**一一对应且顺序一致（按页、按页内自上而下）。
    报告里插图与图注一一对应，没有图标类图片，所以顺序就是正文里 `![](...)` 的顺序。
    页眉里的官方模板 logo（同一图像对象出现在 ≥ 一半页面上）不参与 `--order` 对账：
    给 `--header <png>` 时用**原地改写图像对象**的方式换回源像素（所有页同步生效，
    对象数不变——不能走 `Page.replace_image`，它会留孤儿对象并打乱对账）；不给则
    页眉保持 Word 导出的 96 dpi。数量对不上时脚本不猜、不部分替换，直接报错退出。

依赖 PyMuPDF（`python -c "import pymupdf"` 能过即可）。本文件不写主机绝对路径：
本机若当前解释器缺 PyMuPDF，脚本会在运行时探测 `python` / `py -3`，只把**探测结果**
打印出来给你照抄，不落盘、不写进文件。
"""
from __future__ import annotations

import argparse
import io
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


def header_xrefs(doc, items):
    """页眉图的 xref 集合：同一个 xref 出现在 ≥ 一半页面上（≥3 页）即判定为页眉。

    官方模板版式后，每页页眉有一张「芯辰大海」logo（Word 导出时各页共用一个
    图像对象）。它不属于正文插图，不参与 --order 对账，也替换不得（替换会波及
    全部页并打乱共享对象）。
    """
    if doc.page_count < 4:
        return set()
    pages = {}
    for it in items:
        pages.setdefault(it["xref"], set()).add(it["page"])
    need = max(3, doc.page_count // 2)
    return {x for x, ps in pages.items() if len(ps) >= need}


def body_images(doc, items=None):
    """正文插图（按文档出现顺序，排除页眉图）。"""
    items = page_images(doc) if items is None else items
    hdr = header_xrefs(doc, items)
    return [it for it in items if it["xref"] not in hdr], hdr


def replace_header_image(doc, xref: int, src: Path) -> tuple[int, int]:
    """把页眉图（同一 xref 被多页引用）**原地**换成源图像素。

    不能走 ``Page.replace_image``：它先 ``insert_image`` 再 ``xref_copy`` 覆盖，
    会在页面资源里留下孤儿图像对象（实测替换 1 张后总对象数 15→16、后续对账错位）。
    这里直接改写 xref 指向的图像对象（宽 / 高 / 流），所有引用页同步生效、对象数不变。
    源图须为不带 alpha 的 RGB（logo_header.png 已白底化）。
    """
    import pymupdf                       # 懒加载：load_pymupdf() 已在此前验证可用
    pm = pymupdf.Pixmap(str(src))
    if pm.alpha:
        pm = pymupdf.Pixmap(pymupdf.csRGB, pm)          # 丢 alpha（源已白底化）
    if pm.colorspace and pm.colorspace.n != 3:
        pm = pymupdf.Pixmap(pymupdf.csRGB, pm)
    if pm.stride != pm.width * 3:
        raise ValueError("页眉源图像素行带 padding（stride %d ≠ %d），先转存为无填充 RGB"
                         % (pm.stride, pm.width * 3))
    # update_stream 默认自带 Flate 压缩，字典里声明 /Filter /FlateDecode 即可；
    # 自己先 zlib 压一遍会变成双重压缩、解码出噪声（10-31 首版踩过）。
    d = ("<</Type/XObject/Subtype/Image/Width %d/Height %d/ColorSpace /DeviceRGB"
         "/BitsPerComponent 8/Filter /FlateDecode>>" % (pm.width, pm.height))
    doc.update_object(xref, d)
    doc.update_stream(xref, pm.samples, compress=True)
    return pm.width, pm.height


def replace_with_jpeg(doc, page_no: int, xref: int, src: Path, quality: int) -> tuple[int, int]:
    """把指定 xref 的图像换成 JPEG —— 只给实物照片（ph_*）用。

    FlateDecode 存的是 RGB 原始采样，噪点照片几乎压不动（6 张实物图让 PDF 涨到 48MB）；
    改成 JPEG（quality 88、4:2:0）在 322 dpi 下肉眼看不出差别，PDF 能砍到十分之一。
    线稿图不走这里——无损 FlateDecode 才不会让字边缘出伪影。
    走 ``Page.replace_image``（而不是手写 /DCTDecode 字典）：字典 / 颜色空间 / Length
    由 PyMuPDF 自己写，手写那版实测渲染成黑块 + 噪点。
    """
    from PIL import Image
    import tempfile
    im = Image.open(src).convert("RGB")
    tmp = Path(tempfile.gettempdir()) / ("%s_q%d.jpg" % (src.stem, quality))
    im.save(tmp, "JPEG", quality=quality, optimize=True, subsampling=2)
    doc[page_no - 1].replace_image(xref, filename=str(tmp))
    return im.width, im.height


def report(pdf_path: Path, min_dpi: float) -> int:
    pm = load_pymupdf()
    if pm is None:
        return 2
    with pm.open(str(pdf_path)) as doc:
        all_items = page_images(doc)
        uniq, hdr = body_images(doc, all_items)
        print("PDF  : %s" % pdf_path)
        print("        %d 页，%.2f MB，图片对象 %d 个（出现 %d 次，页眉图 %d 个）"
              % (doc.page_count, pdf_path.stat().st_size / 1e6, len(uniq),
                 len(all_items), len(hdr)))
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


def hires(pdf_path: Path, order, min_dpi: float, out_path: Path, inplace: bool,
          header_src: Path | None = None, photo_jpeg: int = 0) -> int:
    pm = load_pymupdf()
    if pm is None:
        return 2
    for src in order:
        if not src.is_file():
            print("[error] --order 里的图不存在：%s" % src, file=sys.stderr)
            return 1
    if header_src is not None and not header_src.is_file():
        print("[error] --header 指定的页眉图不存在：%s" % header_src, file=sys.stderr)
        return 1
    target = pdf_path if inplace else out_path
    if target.resolve() == pdf_path.resolve() and not inplace:
        print("[error] 输出路径与输入相同，请加 --inplace 或换 --out", file=sys.stderr)
        return 2

    print("PDF  : %s（%.2f MB）" % (pdf_path, pdf_path.stat().st_size / 1e6))
    with pm.open(str(pdf_path)) as doc:
        before, hdr = body_images(doc)
        if hdr:
            print("页眉图：xref %s（出现在 ≥ 一半页面，按 --header 原地换源像素）"
                  % ", ".join(str(x) for x in sorted(hdr)))
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
                if photo_jpeg and src.name.startswith("ph_"):
                    replace_with_jpeg(doc, it["page"], it["xref"], src, photo_jpeg)
                else:
                    doc[it["page"] - 1].replace_image(it["xref"], filename=str(src))
                done[it["xref"]] = True
            pairs.append((idx, it, src, (px.width, px.height)))
        # 页眉图：原地改写图像对象（不能走 replace_image，会留孤儿对象），所有引用页生效
        hdr_px = {}
        if header_src is not None:
            for hx in sorted(hdr):
                w, h = replace_header_image(doc, hx, header_src)
                hdr_px[hx] = (w, h)
                print("        页眉 xref=%d 原地替换 → %dx%d px  [%s]"
                      % (hx, w, h, header_src.name))
        tmp = target.with_suffix(target.suffix + ".tmp")
        doc.save(str(tmp), garbage=3, deflate=True)
    shutil.move(str(tmp), str(target))
    print("写出  : %s（%.2f MB）" % (target, target.stat().st_size / 1e6))

    with pm.open(str(target)) as doc2:
        after_all, hdr2 = body_images(doc2)
        print("替换后：%d 张正文插图" % len(after_all))
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
        # 页眉复核：像素必须与源图一致（dpi 只到源图原生值，不做 300 门禁）
        if header_src is not None:
            src_pm = pm.Pixmap(str(header_src))
            for hx in sorted(hdr2):
                got = next((it for it in page_images(doc2) if it["xref"] == hx), None)
                if got is None or (got["px_w"], got["px_h"]) != (src_pm.width, src_pm.height):
                    print("[FAIL] 页眉图 xref=%d 替换后与源图不符" % hx)
                    return 1
                print("        页眉 xref=%d 复核 OK：%dx%d px @ %.0f dpi（源图原生）"
                      % (hx, got["px_w"], got["px_h"], got["dpi"]))
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
    ap.add_argument("--header", default=None,
                    help="页眉 logo 源图（官方模板 logo_header.png）：对出现在 ≥ 一半页面的"
                         "图像对象原地换源像素；不给则页眉图保持 Word 导出的 96 dpi")
    ap.add_argument("--report", action="store_true", help="只测量打印，不改文件")
    ap.add_argument("--min-dpi", type=float, default=MIN_DPI_DEFAULT,
                    help="等效 dpi 下限，默认 %(default)s；0 = 不判定")
    ap.add_argument("--inplace", action="store_true", help="直接写回原 PDF")
    ap.add_argument("--photo-jpeg", type=int, default=0, metavar="Q",
                    help="ph_*.png（实物照片）改用 JPEG 编码，Q=质量 1~100（0=关闭，默认）；"
                         "线稿仍走 FlateDecode 无损")
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
    header = Path(args.header).resolve() if args.header else None
    return hires(pdf_path, [Path(p).resolve() for p in args.order],
                 args.min_dpi, out_path, args.inplace, header, args.photo_jpeg)


if __name__ == "__main__":
    sys.exit(main())
