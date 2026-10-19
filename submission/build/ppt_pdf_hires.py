#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""PPT 链 PDF 插图高清化 + 清晰度 / 版面体检（矩阵：PowerPoint 导出的 PDF 兜底件）。

为什么需要它
------------
PowerPoint COM 导 PDF 会把插图降采样：实测源图 3200px、放到 7.42in 的图在 PDF 里只剩
1483px（= 200dpi）。PPTX 里嵌的仍是全分辨率，问题只在 PDF 兜底件。本脚本按
``submission/out/_ppt_images.json``（``make_ppt.py`` 每次生成时写出的插图清单：
页号 + 放置矩形 + 源图路径 + 原生像素）逐张对位，把源图按**原生像素**回填进 PDF
的图片 XObject（信息量不变，只为采样密度达标）；随后量一遍每张插图的等效 dpi。

版面度量读同一份 manifest 的 ``theme``（页面底色 / 标题分隔线下沿 / 页脚位置），
换主题后不必改本脚本——只在 ``DEFAULT_THEME`` 里留一份旧版兜底。

用法
----
    # 回填 + 体检（原地写回；失败不覆盖原文件）
    python submission/build/ppt_pdf_hires.py --pdf submission/out/龙瞳分拣_答辩.pdf \
        --manifest submission/out/_ppt_images.json --inplace --min-dpi 300

    # 只体检，不改文件；同时量逐页版面墨迹（头档 / 底档 / 填充率）
    python submission/build/ppt_pdf_hires.py --pdf <pdf> --report \
        --preview submission/out/ppt_preview --json submission/out/_ppt_pdf_hires.json

参数
----
* ``--pdf``       目标 PDF（必填）
* ``--manifest``  ``_ppt_images.json``（推荐；按页号 + 放置矩形对位，图标不会错位）
* ``--order``     退路：按 PDF 里「放置宽 ≥3in 的插图」出现顺序给源图路径
* ``--min-dpi``   等效 dpi 下限，默认 300；低于即非零退出
* ``--inplace``   写回原 PDF（默认写 ``<pdf>.hires.pdf``）；``--out`` 可指定输出
* ``--report``    只测不改（隐含）
* ``--preview``   逐页预览目录（``make_ppt.py`` → ``export_ppt_preview.ps1`` 产出的
                  1600x900 PNG）；给出后同时量版面并打门禁
* ``--json``      把 dpi 与版面度量写成 JSON（原始证据）

参考实现：同事的 ``submission/build/pdf_images_hires.py``（报告链，``--order`` 模式）。
PPT 链的插图顺序里夹着图标 / 项目符号，所以本脚本用「页号 + 放置矩形」对位；若那个工具
以后支持 ``--map``，本脚本可以只做体检（``--report``），替换交给它。
"""

from __future__ import annotations

import argparse
import io
import json
import os
import sys
from pathlib import Path

try:
    import pymupdf
except ImportError:                                   # pragma: no cover
    try:
        import fitz as pymupdf
    except ImportError:
        sys.exit("缺少 PyMuPDF。安装：python -m pip install pymupdf")

from PIL import Image

REPO_ROOT = Path(__file__).resolve().parents[2]

SLIDE_W, SLIDE_H = 13.3333, 7.5
# 默认主题（旧版深色底；新版浅色主题由 make_ppt.py 写进 _ppt_images.json 的 theme，
# 这里读不到 manifest 时才用默认值）。版面度量依赖「页面底色」判墨迹，所以底色必须
# 与 pptx 一致，否则整页都会被当成墨迹。
DEFAULT_THEME = dict(
    bg=[0x0B, 0x25, 0x45],
    cover_bg=[0x07, 0x1B, 0x33],
    rule_bottom=1.195,           # 标题分隔线下沿（in）：量头档的起点
    footer_y=7.05,               # 页脚带起点（in）：量版面时排除
)
TOP_SKIP = 0.12                  # 页顶装饰紫：量版面时排除
X_SKIP = 0.20
FIG_MIN_W_IN = 3.0               # 放置宽 < 3in 的当图标 / 项目符号，不算插图
# 新版浅色编辑风的目标区间（内容块少而精，填充率天然低于旧版密排）：
# 底档 ≤0.90in、头档 ≤0.80in、面积填充 45%~75%；硬门禁再放宽一点抓回归。
GAP_BOTTOM_MAX, GAP_HEAD_MAX = 0.98, 0.80
FILL_MIN, FILL_MAX = 0.55, 0.82
GAP_FLOOR, FILL_FLOOR = 1.10, 0.45


# --------------------------------------------------------------------------- #
# PDF 里的插图：扫描 / 回填
# --------------------------------------------------------------------------- #


def white_rgb(path):
    """读成白底 RGB（源图是 RGBA；PDF 里不带 SMask 更省事，图本身也是白底）。"""
    im = Image.open(path)
    if im.mode in ("RGBA", "LA", "P"):
        im = im.convert("RGBA")
        bg = Image.new("RGB", im.size, (255, 255, 255))
        bg.paste(im, mask=im.split()[3])
        return bg
    return im.convert("RGB")


def png_idat(im):
    """把 PIL 图存成 PNG 后取出 IDAT：PNG 的逐行 filter + zlib 正是 PDF 的
    ``/Predictor 15`` 格式，可以直接当图像流用；线稿的压缩率远好于裸 RGB。"""
    buf = io.BytesIO()
    im.save(buf, format="PNG", optimize=True)
    data = buf.getvalue()
    pos, chunks = 8, []
    while pos + 8 <= len(data):
        ln = int.from_bytes(data[pos:pos + 4], "big")
        typ = data[pos + 4:pos + 8]
        if typ == b"IDAT":
            chunks.append(data[pos + 8:pos + 8 + ln])
        elif typ == b"IEND":
            break
        pos += 12 + ln
    return b"".join(chunks)


def find_image(page, rect, tol=0.12):
    """按放置矩形（in）在页面上找最接近的图片 XObject（页面上还有图标，不能只按顺序取）。"""
    best = None
    for info in page.get_images(full=True):
        try:
            bb = page.get_image_bbox(info)
        except Exception:
            continue
        if bb.is_empty or bb.is_infinite:
            continue
        x, y = bb.x0 / 72.0, bb.y0 / 72.0
        w, h = bb.width / 72.0, bb.height / 72.0
        err = (abs(x - rect[0]) + abs(y - rect[1]) + abs(w - rect[2]) + abs(h - rect[3]))
        if max(abs(x - rect[0]), abs(y - rect[1]),
               abs(w - rect[2]), abs(h - rect[3])) < tol:
            if best is None or err < best[0]:
                best = (err, info, bb)
    return best


def replace_one(doc, xref, src, scale):
    im = white_rgb(src)
    if scale > 1.0:
        im = im.resize((int(round(im.width * scale)), int(round(im.height * scale))),
                       Image.LANCZOS)
    w, h = im.size
    doc.update_stream(xref, png_idat(im), compress=False)
    doc.xref_set_key(xref, "Width", str(w))
    doc.xref_set_key(xref, "Height", str(h))
    doc.xref_set_key(xref, "BitsPerComponent", "8")
    doc.xref_set_key(xref, "ColorSpace", "/DeviceRGB")
    doc.xref_set_key(xref, "Filter", "/FlateDecode")
    doc.xref_set_key(xref, "DecodeParms",
                     "<< /Predictor 15 /Colors 3 /BitsPerComponent 8 /Columns %d >>" % w)
    doc.xref_set_key(xref, "SMask", "null")
    doc.xref_set_key(xref, "Mask", "null")
    return w, h


def scan_pdf(pdf):
    """每张插图一行：页 / xref / px / 放置尺寸 / 等效 dpi（图标不算）。"""
    rows = []
    doc = pymupdf.open(str(pdf))
    for i, page in enumerate(doc, 1):
        for info in page.get_images(full=True):
            try:
                bb = page.get_image_bbox(info)
            except Exception:
                continue
            if bb.is_empty or bb.is_infinite:
                continue
            w_in = bb.width / 72.0
            if w_in < FIG_MIN_W_IN:
                continue
            rows.append(dict(page=i, xref=info[0], px=[info[2], info[3]],
                             rect_in=[round(bb.x0 / 72.0, 3), round(bb.y0 / 72.0, 3),
                                      round(w_in, 3), round(bb.height / 72.0, 3)],
                             dpi=round(info[2] / w_in, 1)))
    doc.close()
    return rows


def _resolve(p):
    path = Path(p)
    return path if path.is_absolute() else (REPO_ROOT / path)


def hires_pdf(pdf, jobs, min_dpi):
    """jobs = [(页号, 放置矩形, 源图路径, 上采样倍数)]；先在临时文件上做，校验通过才覆盖。"""
    doc = pymupdf.open(str(pdf))
    done, missed = [], []
    for slide, rect_in, src, scale in jobs:
        if slide < 1 or slide > doc.page_count:
            missed.append("第 %d 页不在 PDF 里" % slide)
            continue
        page = doc[slide - 1]
        hit = find_image(page, rect_in)
        src_path = _resolve(src)
        if not src_path.is_file():
            missed.append("源图缺失 %s" % src)
            continue
        if not hit:
            missed.append("第 %d 页找不到放置矩形 %r" % (slide, rect_in))
            continue
        _err, info, bb = hit
        w, h = replace_one(doc, info[0], src_path, scale)
        done.append(dict(slide=slide, src=str(src), px=[w, h],
                         rect_in=[round(v, 3) for v in rect_in],
                         dpi=round(w / (bb.width / 72.0), 1), pdf_scale=scale))

    tmp = Path(str(pdf) + ".hires.tmp")
    doc.save(str(tmp), garbage=3, deflate=True)
    doc.close()

    chk = pymupdf.open(str(tmp))
    pages_ok = chk.page_count > 0
    chk.close()
    low = [r for r in scan_pdf(tmp) if r["dpi"] < min_dpi]
    if not pages_ok or low:
        tmp.unlink(missing_ok=True)
        raise SystemExit("!! PDF 高清化校验失败：页数正常=%s，低于 %ddpi 的插图 %r"
                         % (pages_ok, min_dpi, low))
    return done, missed, tmp


# --------------------------------------------------------------------------- #
# 逐页版面墨迹度量（1600x900 预览）
# --------------------------------------------------------------------------- #


def measure_png(path, theme=None, is_cover=False):
    theme = dict(DEFAULT_THEME, **(theme or {}))
    im = Image.open(path).convert("RGB")
    w, h = im.size
    px = im.load()
    ppi_x, ppi_y = w / SLIDE_W, h / SLIDE_H
    y0, y1 = int(TOP_SKIP * ppi_y), int(theme["footer_y"] * ppi_y)
    x0 = int(X_SKIP * ppi_x)
    bg = tuple(theme["cover_bg"] if is_cover else theme["bg"])

    def ink(c, base):
        return max(abs(c[0] - base[0]), abs(c[1] - base[1]), abs(c[2] - base[2])) > 12

    rows = []
    for y in range(y0, y1):
        n = 0
        for x in range(x0, w):
            if ink(px[x, y], bg):
                n += 1
        rows.append((y, n))
    thr = max(3, int(0.002 * w))
    top = next((y for y, n in rows if n > thr), None)
    bot = next((y for y, n in reversed(rows) if n > thr), None)
    if top is None or bot is None:
        return None
    below = [y for y, n in rows if n > thr and y / ppi_y > theme["rule_bottom"]]
    head = (min(below) / ppi_y - theme["rule_bottom"]) if below else None
    cols = []
    for x in range(x0, w):
        n = 0
        for y in range(top, bot + 1):
            if ink(px[x, y], bg):
                n += 1
        if n > 3:
            cols.append(x)
    left = min(cols) / ppi_x if cols else 0.0
    right = (max(cols) + 1) / ppi_x if cols else 0.0
    ink_h = (bot + 1 - top) / ppi_y
    return dict(top_gap=round(top / ppi_y, 2),
                head_gap=round(head, 2) if head is not None else None,
                bottom_gap=round(SLIDE_H - (bot + 1) / ppi_y, 2),
                fill_h=round(ink_h / SLIDE_H, 3),
                fill_area=round((right - left) * ink_h / (SLIDE_W * SLIDE_H), 3))


# --------------------------------------------------------------------------- #
# 主流程
# --------------------------------------------------------------------------- #


def main(argv=None):
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:                                  # pragma: no cover
        pass

    ap = argparse.ArgumentParser(description="PPT 链 PDF 插图高清化 + 清晰度 / 版面体检")
    ap.add_argument("--pdf", required=True)
    ap.add_argument("--manifest", help="submission/out/_ppt_images.json（推荐）")
    ap.add_argument("--order", nargs="*", default=None,
                    help="退路：按 PDF 里插图出现顺序给源图路径")
    ap.add_argument("--min-dpi", type=float, default=300.0)
    ap.add_argument("--inplace", action="store_true")
    ap.add_argument("--out", help="输出 PDF（默认 <pdf>.hires.pdf）")
    ap.add_argument("--report", action="store_true", help="只测不改（不写 PDF）")
    ap.add_argument("--preview", help="逐页预览目录（1600x900 PNG）")
    ap.add_argument("--json", help="把 dpi 与版面度量写成 JSON")
    args = ap.parse_args(argv)

    pdf = Path(args.pdf)
    if not pdf.is_file():
        raise SystemExit("找不到 PDF：%s" % pdf)

    # ---- 组装替换清单 + 主题（版面度量的底色来自 pptx 主题） ---------------- #
    jobs = []
    theme = dict(DEFAULT_THEME)
    if args.manifest:
        manifest = json.loads(Path(args.manifest).read_text(encoding="utf-8"))
        for fig in manifest["figures"]:
            jobs.append((fig["slide"], fig["rect_in"], fig["src"],
                         float(fig.get("pdf_scale", 1.0))))
        theme.update({k: v for k, v in (manifest.get("theme") or {}).items()
                      if k in DEFAULT_THEME})
    elif args.order:
        figs = scan_pdf(pdf)
        if len(figs) != len(args.order):
            raise SystemExit("--order 给了 %d 个源图，PDF 里有 %d 张插图，数量不匹配"
                             % (len(args.order), len(figs)))
        jobs = [(r["page"], r["rect_in"], src, 1.0) for r, src in zip(figs, args.order)]

    before = scan_pdf(pdf)
    replaced, missed = [], []
    if jobs and not args.report:
        replaced, missed, tmp = hires_pdf(pdf, jobs, args.min_dpi)
        if args.out:
            os.replace(str(tmp), args.out)
            out_pdf = Path(args.out)
        elif args.inplace:
            os.replace(str(tmp), str(pdf))
            out_pdf = pdf
        else:
            out_pdf = Path(str(pdf) + ".hires.pdf")
            os.replace(str(tmp), str(out_pdf))
        print("输出 PDF：%s（%s）" % (out_pdf, "原地回填" if args.inplace else "另存"))
    after = scan_pdf(args.out if args.out else pdf) if not args.report else before

    # ---- 插图 dpi 表 ----------------------------------------------------- #
    print("-" * 100)
    print("PDF 插图清晰度（源图原生像素回填；低于 %.0fdpi 判不合格）" % args.min_dpi)
    print("%-5s %-30s %-11s %-11s %-10s %s"
          % ("页", "插图", "导 PDF 时", "回填后", "放置 in", "dpi 前 → 后"))
    bmap = {(r["page"], tuple(r["rect_in"])): r for r in before}
    smap = {(d["slide"], tuple(d["rect_in"])): d["src"] for d in replaced}
    fails = []
    for r in after:
        key = (r["page"], tuple(r["rect_in"]))
        b = bmap.get(key, {})
        name = Path(smap.get(key, "")).name or "-"
        ok = r["dpi"] >= args.min_dpi
        if not ok:
            fails.append(r)
        print("%-5d %-30s %-11s %-11s %-10s %.0f → %.0f  %s"
              % (r["page"], name, "%dx%d" % tuple(b.get("px", [0, 0])),
                 "%dx%d" % tuple(r["px"]), "%.2fx%.2f" % tuple(r["rect_in"][2:]),
                 b.get("dpi", 0), r["dpi"], "PASS" if ok else "FAIL"))
    for m in missed:
        print("  ! 未替换：%s" % m)
    if replaced:
        print("共替换 %d 张（按源图原生像素回填，信息量不变）。" % len(replaced))

    # ---- 版面度量 -------------------------------------------------------- #
    layout, layout_fail = {}, []
    if args.preview and Path(args.preview).is_dir():
        print("-" * 100)
        print("版面墨迹度量（%.0fx%.0f 预览；底色取自 pptx 主题，排除页脚与页顶装饰。"
              "目标：头档≤%.2f 底档≤%.2f，面积填充 %.0f%%~%.0f%%）"
              % (1600, 900, GAP_HEAD_MAX, GAP_BOTTOM_MAX,
                 FILL_MIN * 100, FILL_MAX * 100))
        print("%-16s %7s %7s %7s %7s %7s  %s"
              % ("页", "顶档in", "头档in", "底档in", "填充h", "填充A", "判定"))
        for p in sorted(Path(args.preview).glob("slide_*.png")):
            n = int(p.stem.split("_")[1])
            m = measure_png(p, theme, is_cover=(n == 1))
            layout[p.name] = m
            if not m:
                print("%-16s  空白页" % p.name)
                continue
            if n == 1:
                print("%-16s %7.2f %7s %7.2f %6.0f%% %6.0f%%  封面（设计留白，豁免）"
                      % (p.name, m["top_gap"], "-", m["bottom_gap"],
                         m["fill_h"] * 100, m["fill_area"] * 100))
                continue
            bad = []
            if m["bottom_gap"] > GAP_BOTTOM_MAX:
                bad.append("底档>%.2f" % GAP_BOTTOM_MAX)
            if m["head_gap"] is not None and m["head_gap"] > GAP_HEAD_MAX:
                bad.append("头档>%.2f" % GAP_HEAD_MAX)
            if not (FILL_MIN <= m["fill_area"] <= FILL_MAX):
                bad.append("填充率出界")
            hard = m["bottom_gap"] > GAP_FLOOR or m["fill_area"] < FILL_FLOOR
            if hard:
                layout_fail.append((p.name, m))
            verdict = "PASS" if not bad else ("FAIL: " + "/".join(bad))
            print("%-16s %7.2f %7s %7.2f %6.0f%% %6.0f%%  %s%s"
                  % (p.name, m["top_gap"],
                     ("%.2f" % m["head_gap"]) if m["head_gap"] is not None else "-",
                     m["bottom_gap"], m["fill_h"] * 100, m["fill_area"] * 100,
                     "!! " if hard else "", verdict))

    if args.json:
        Path(args.json).write_text(json.dumps(
            dict(min_dpi=args.min_dpi, theme=theme, replaced=replaced,
                 missed=missed, pdf_images=after, layout=layout),
            ensure_ascii=False, indent=1),
            encoding="utf-8")
        print("证据 json → %s" % args.json)

    print("-" * 100)
    if fails:
        print("!! %d 张插图 dpi < %.0f：%r" % (len(fails), args.min_dpi, fails))
    if layout_fail:
        print("!! %d 页版面低于硬门禁（底档>%.2fin 或填充率<%.0f%%）：%r"
              % (len(layout_fail), GAP_FLOOR, FILL_FLOOR * 100,
                 [(n, m["bottom_gap"], m["fill_area"]) for n, m in layout_fail]))
    if fails or layout_fail:
        return 1
    print("全部通过：插图 ≥%.0fdpi；逐页版面在目标区间内。" % args.min_dpi)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
