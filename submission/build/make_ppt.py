#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成决赛答辩 PPT：submission/out/龙瞳分拣_答辩.pptx（编辑风重设计版）

设计目标
--------
* 内容：每页一个**断言式标题**，下面 3~5 个「结论 + 证据」支撑点；数字只来自
  ``docs/测试与验证.md`` 与 ``docs/技术文档.md``，未实测标「待测 ⚠」。
* 排版：13.333 × 7.5 in（16:9）。统一版式骨架：页眉（kicker + 标题 + 分隔线）→
  正文区（左右边距 0.62 in，内容底 6.96 in）→ 页脚。每种内容配一种版式：
  封面 / 痛点对比 / 指标看板 / 左图右文 / 全幅示意图 / 流水线 / 数据看板 /
  卡片矩阵 / 总结。所有元素都是原生 pptx 形状与文本框，可在 PowerPoint 里逐项改。
* 配色：浅灰画布 + 白卡 + 墨色文字 + 青绿强调 + 琥珀警示；深墨色只用于封面与
  深色面板（色值以本文件 ``C_*`` 常量为唯一源；投影检查见
  ``submission/PPT制作指南.md`` §3 人工看图清单）。
* 插图：``docs/figures/*.png``（figkit 按报告落版 1:1 作画）整图嵌入，按源图原生
  像素放置（原生 dpi ≥ 300），不再做「局部放大」像素裁切。

唯一源
------
* 页纲：``docs/参赛材料骨架.md`` §4（16 页，与 ``slide_specs()`` 页序一致）
* 数字：``docs/测试与验证.md``「已实测数据汇总」+ ``docs/技术文档.md``
* 插图：``docs/figures/*.png``（由 ``docs/figures/make_figures.py`` 生成，不手工画）
* 演讲备注：每页 notes，带数字出处；PPT 正文不写仓库路径。

产物
----
* ``submission/out/龙瞳分拣_答辩.pptx``：现场演示件（可继续在 PowerPoint 里编辑）
* ``submission/out/_ppt_images.json``：插图清单（页号 / 放置矩形 / 原生像素）+
  ``theme`` 主题度量，供 ``export_ppt_preview.ps1`` 的 PDF 高清化与版面量取自检使用

用法
----
    python submission/build/make_ppt.py              # 生成 + 读回自检（有问题退出码 1）
    python submission/build/make_ppt.py --no-verify  # 只生成

脚本用 ``pathlib`` 相对 ``__file__`` 定位仓库根，不写任何主机绝对路径。
"""

from __future__ import annotations

import argparse
import json
import math
import shutil
import sys
import unicodedata
from pathlib import Path

try:
    from pptx import Presentation
    from pptx.dml.color import RGBColor
    from pptx.enum.shapes import MSO_SHAPE
    from pptx.enum.text import MSO_ANCHOR, PP_ALIGN
    from pptx.oxml.ns import qn
    from pptx.util import Inches, Pt
except ImportError:  # pragma: no cover - 环境问题，给出可操作提示
    sys.exit(
        "缺少 python-pptx。安装：python -m pip install python-pptx\n"
        "（环境要求见 submission/PPT制作指南.md §1「PPT 链怎么跑」）"
    )

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    sys.exit("缺少 Pillow。安装：python -m pip install Pillow")


# --------------------------------------------------------------------------- #
# 路径与常量
# --------------------------------------------------------------------------- #

REPO_ROOT = Path(__file__).resolve().parents[2]
FIGURES_DIR = REPO_ROOT / "docs" / "figures"
FIG_META_PATH = FIGURES_DIR / "fig_meta.json"
OUT_DIR = REPO_ROOT / "submission" / "out"
OUT_PPTX = OUT_DIR / "龙瞳分拣_答辩.pptx"
OUT_MANIFEST = OUT_DIR / "_ppt_images.json"
OLD_ZOOM_DIR = OUT_DIR / "zoom"          # 旧版像素放大裁切图；新版不再生成，重跑时清掉

SLIDE_W = 13.3333
SLIDE_H = 7.5

# 作品标识（封面用；赛事口径见 submission/提交材料清单.md §1）
WORK_NAME = "龙瞳分拣"
WORK_NAME_EN = "DragonEyeSorter"
EVENT_LINE = "2026 全国大学生嵌入式芯片与系统设计竞赛"
TRACK_LINE = "FPGA 创新设计赛道 · 安路选题四"
DATE_LINE = "2026 年 10 月"
TECH_LINE = "NPU 缺陷分类 · 跟踪抓取 · PL 硬实时控制"


# --------------------------------------------------------------------------- #
# 设计令牌（全篇唯一一份；改这里就改全篇）
# --------------------------------------------------------------------------- #

# 浅色画布 + 墨色文字 + 青绿强调 + 琥珀警示
C_BG = RGBColor(0xF7, 0xF8, 0xFA)          # 画布（浅灰）
C_CARD = RGBColor(0xFF, 0xFF, 0xFF)        # 卡片
C_CARD_ALT = RGBColor(0xF1, 0xF5, 0xF9)    # 次级卡片 / 表头底
C_LINE = RGBColor(0xE1, 0xE8, 0xF0)        # 分隔线 / 卡片描边
C_INK = RGBColor(0x0E, 0x1B, 0x2B)         # 标题 / 主要文字
C_BODY = RGBColor(0x3F, 0x52, 0x66)        # 正文
C_MUTED = RGBColor(0x5F, 0x72, 0x85)       # 次要 / 页脚
C_FAINT = RGBColor(0x6B, 0x7A, 0x8B)       # 更弱的注释（页脚 / 图注，对比度 ≥4:1）
C_TEAL = RGBColor(0x0D, 0x94, 0x88)        # 强调（青绿）
C_TEAL_DEEP = RGBColor(0x0B, 0x6E, 0x69)
C_TEAL_TINT = RGBColor(0xE7, 0xF3, 0xF1)   # 青绿浅底
C_AMBER = RGBColor(0xB4, 0x53, 0x09)       # 警示 / 待测
C_AMBER_TINT = RGBColor(0xFD, 0xF3, 0xE5)
C_NAVY = RGBColor(0x10, 0x2A, 0x43)        # 深色面板
C_NAVY_2 = RGBColor(0x17, 0x39, 0x59)      # 深色面板上的次级块
C_WHITE = RGBColor(0xFF, 0xFF, 0xFF)

# 封面专用（深墨色）
C_COVER_BG = RGBColor(0x08, 0x17, 0x26)
C_COVER_RING = [RGBColor(0x14, 0x2D, 0x45), RGBColor(0x17, 0x34, 0x50),
                RGBColor(0x1A, 0x3C, 0x59), RGBColor(0x1B, 0x4A, 0x58),
                RGBColor(0x18, 0x62, 0x66), RGBColor(0x0D, 0x94, 0x88)]
C_COVER_MUTED = RGBColor(0x7E, 0x97, 0xAE)
C_COVER_SOFT = RGBColor(0xA9, 0xBF, 0xD2)
C_COVER_TEAL = RGBColor(0x2D, 0xD4, 0xBF)
C_COVER_FOOT = RGBColor(0x7C, 0x93, 0xA8)

FONT_CN = "微软雅黑"
FONT_MONO = "Consolas"

# 版式网格（英寸）
MARGIN = 0.62
CONTENT_W = SLIDE_W - 2 * MARGIN           # 12.0933
KICKER_Y = 0.40
TITLE_Y = 0.64
TITLE_H = 0.74
RULE_Y = 1.46
BODY_TOP = 1.74
BODY_BOTTOM = 6.96
FOOT_RULE_Y = 7.05
FOOT_TEXT_Y = 7.11

SIZE_TITLE = 28
SIZE_KICKER = 11
SIZE_FOOT = 9
SIZE_BODY = 13
SIZE_SMALL = 11.5
SIZE_TINY = 10

LINE_FACTOR = 1.33                          # 微软雅黑单倍行高 ≈ 1.33×字号（渲染校准）
LATIN_EM = 0.56                             # 拉丁字符相对中文字宽的估算系数

# 版面门禁（生成后读回自检；渲染侧门禁见 submission/build/ppt_pdf_hires.py）
GAP_BOTTOM_MAX = 0.55                        # 内容底沿距页脚线的最大留白（in）
TEXT_FIT_TOL = 0.08                          # 文本高度估算容差（in）
FIG_DPI_MIN = 300


# --------------------------------------------------------------------------- #
# 通用低层原语
# --------------------------------------------------------------------------- #


def _rel(path):
    p = Path(path)
    try:
        return p.resolve().relative_to(REPO_ROOT).as_posix()
    except ValueError:                       # pragma: no cover - 极端情况
        return p.name


def _add_shape(slide, shape_type, x, y, w, h, *, fill=None, line=None, line_w=1.0):
    shp = slide.shapes.add_shape(shape_type, Inches(x), Inches(y), Inches(w), Inches(h))
    try:
        shp.shadow.inherit = False
    except Exception:                        # pragma: no cover - 少数形状没有 shadow
        pass
    if fill is None:
        shp.fill.background()
    else:
        shp.fill.solid()
        shp.fill.fore_color.rgb = fill
    if line is None:
        shp.line.fill.background()
    else:
        shp.line.color.rgb = line
        shp.line.width = Pt(line_w)
    return shp


def _round(shp, radius):
    try:
        shp.adjustments[0] = radius
    except Exception:                        # pragma: no cover
        pass


def _card(slide, x, y, w, h, *, fill=C_CARD, line=C_LINE, radius=0.055, line_w=1.0):
    shp = _add_shape(slide, MSO_SHAPE.ROUNDED_RECTANGLE, x, y, w, h,
                     fill=fill, line=line, line_w=line_w)
    _round(shp, radius)
    return shp


def _hairline(slide, x, y, w, *, color=C_LINE, weight=0.010):
    _add_shape(slide, MSO_SHAPE.RECTANGLE, x, y, w, weight, fill=color)


def _set_fill_alpha(shp, pct):
    """给纯色填充加透明度（0~100，100 = 不透明）。用于封面/背景装饰。"""
    try:
        srgb = shp.fill.fore_color._xFill
        for old in srgb.findall(qn("a:alpha")):
            srgb.remove(old)
        alpha = srgb.makeelement(qn("a:alpha"), {"val": str(int(pct * 1000))})
        srgb.append(alpha)
    except Exception:                        # pragma: no cover
        pass


def _style(run, *, size, color, bold=False, italic=False, mono=False,
           spc=None, font=None):
    name = font or (FONT_MONO if mono else FONT_CN)
    run.font.size = Pt(size)
    run.font.bold = bold
    run.font.italic = italic
    run.font.color.rgb = color
    run.font.name = name
    rPr = run._r.get_or_add_rPr()
    if rPr.find(qn("a:ea")) is None:
        try:
            ea = rPr.makeelement(qn("a:ea"), {"typeface": name})
            rPr.insert_element_before(ea, "a:cs", "a:sym", "a:hlinkClick",
                                      "a:hlinkMouseOver", "a:rtl", "a:extLst")
        except Exception:                    # pragma: no cover - 老版本 python-pptx
            pass
    if spc:
        rPr.set("spc", str(int(round(spc * 100))))
    return run


def _para_runs(p, runs, *, size, color, bold, mono=False, spc=None, font=None):
    """把 runs 写进段落；run 文本里的 ``\\n`` 转成真正的行内换行（<a:br/>），
    避免中文词被自动断行拆开。"""
    for spec in runs:
        if isinstance(spec, str):
            spec = {"t": spec}
        parts = str(spec["t"]).split("\n")
        for i, part in enumerate(parts):
            if i:
                p.add_line_break()
            if not part:
                continue
            run = p.add_run()
            run.text = part
            _style(run, size=spec.get("size", size), color=spec.get("color", color),
                   bold=spec.get("bold", bold), italic=spec.get("italic", False),
                   mono=spec.get("mono", mono), spc=spec.get("spc", spc),
                   font=spec.get("font", font))


def _add_paras(slide, x, y, w, h, paras, *, anchor=MSO_ANCHOR.TOP,
               margins=(0.0, 0.0, 0.0, 0.0), wrap=True):
    """多段落文本框。paras = [{runs:[...], align, space_before, space_after,
    line_spacing}, ...]，run 的公共样式来自顶层参数。"""
    box = slide.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
    tf = box.text_frame
    tf.word_wrap = wrap
    tf.vertical_anchor = anchor
    tf.margin_left = Inches(margins[0])
    tf.margin_right = Inches(margins[1])
    tf.margin_top = Inches(margins[2])
    tf.margin_bottom = Inches(margins[3])
    tf.clear()
    for i, spec in enumerate(paras):
        p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
        p.alignment = spec.get("align", PP_ALIGN.LEFT)
        if spec.get("line_spacing"):
            p.line_spacing = spec["line_spacing"]
        p.space_before = Pt(spec.get("space_before", 0))
        p.space_after = Pt(spec.get("space_after", 0))
        _para_runs(p, spec.get("runs", []),
                   size=spec.get("size", SIZE_BODY),
                   color=spec.get("color", C_BODY),
                   bold=spec.get("bold", False),
                   mono=spec.get("mono", False),
                   spc=spec.get("spc"),
                   font=spec.get("font"))
    return box


def _add_text(slide, x, y, w, h, text, *, size=SIZE_BODY, color=C_BODY,
              bold=False, mono=False, align=PP_ALIGN.LEFT, anchor=MSO_ANCHOR.TOP,
              line_spacing=None, spc=None, font=None, wrap=True):
    """单段落文本框（最常用）。"""
    return _add_paras(
        slide, x, y, w, h,
        [dict(runs=[{"t": text}], size=size, color=color, bold=bold, mono=mono,
              align=align, line_spacing=line_spacing, spc=spc, font=font)],
        anchor=anchor, wrap=wrap)


def _add_text_shapes(slide, x, y, w, h, runs, *, size=SIZE_BODY, color=C_BODY,
                     bold=False, align=PP_ALIGN.LEFT, anchor=MSO_ANCHOR.MIDDLE,
                     mono=False, spc=None, line_spacing=None):
    """单段落、多 run 的文本（同一行里混排强调）。"""
    return _add_paras(slide, x, y, w, h,
                      [dict(runs=runs, size=size, color=color, bold=bold,
                            align=align, line_spacing=line_spacing, spc=spc,
                            mono=mono)],
                      anchor=anchor)


def _line_h(size, line_spacing=1.0):
    return size * LINE_FACTOR * line_spacing / 72.0


def _text_em(text):
    """估算文字宽度（单位 = 一个中文字宽）。"""
    return sum(1.0 if unicodedata.east_asian_width(c) in "WFA" else LATIN_EM
               for c in text)


def _est_lines(text, size, width_in, extra_em=0.0):
    per_line = max(0.5, (width_in * 72.0) / size)
    return max(1, int(math.ceil((_text_em(text) + extra_em) / per_line - 1e-6)))


def _est_text_h(text, size, width_in, line_spacing=1.0):
    return _est_lines(text, size, width_in) * _line_h(size, line_spacing)


def _fit_size(text, width_in, sizes, *, bold=False, extra_em=0.0):
    """从 sizes 里挑第一个放得下（单行）的字号。"""
    for size in sizes:
        if _text_em(text) * size / 72.0 + extra_em <= width_in:
            return size
    return sizes[-1]


def _chip(slide, x, y, w, h, text, *, fill=C_TEAL_TINT, line=None, color=C_TEAL,
          size=12, bold=True, radius=0.5, mono=False):
    shp = _add_shape(slide, MSO_SHAPE.ROUNDED_RECTANGLE, x, y, w, h,
                     fill=fill, line=line)
    _round(shp, radius)
    tf = shp.text_frame
    tf.word_wrap = False
    tf.vertical_anchor = MSO_ANCHOR.MIDDLE
    tf.margin_left = 0
    tf.margin_right = 0
    tf.margin_top = 0
    tf.margin_bottom = 0
    p = tf.paragraphs[0]
    p.alignment = PP_ALIGN.CENTER
    run = p.add_run()
    run.text = text
    _style(run, size=size, color=color, bold=bold, mono=mono)
    return shp


def _arrow(slide, x, y, w, h, *, color=None, shape=MSO_SHAPE.RIGHT_ARROW):
    return _add_shape(slide, shape, x, y, w, h,
                      fill=color or C_TEAL_TINT, line=None)


# --------------------------------------------------------------------------- #
# 页眉 / 页脚 / 提示条
# --------------------------------------------------------------------------- #


def _header(slide, spec):
    kicker = spec["kicker"]
    _add_text(slide, MARGIN, KICKER_Y, CONTENT_W, 0.24, kicker,
              size=SIZE_KICKER, color=C_TEAL, bold=True, spc=1.4)
    title = spec["title"]
    size = _fit_size(title, CONTENT_W - 0.10, (SIZE_TITLE, 26, 24))
    _add_text(slide, MARGIN, TITLE_Y, CONTENT_W, TITLE_H, title,
              size=size, color=C_INK, bold=True, anchor=MSO_ANCHOR.MIDDLE)
    _hairline(slide, MARGIN, RULE_Y, CONTENT_W)
    _add_shape(slide, MSO_SHAPE.RECTANGLE, MARGIN, RULE_Y - 0.008, 1.05, 0.026,
               fill=C_TEAL)


def _footer(slide, page_no, total):
    _hairline(slide, MARGIN, FOOT_RULE_Y, CONTENT_W)
    _add_text(slide, MARGIN, FOOT_TEXT_Y, 7.5, 0.24,
              "%s · %s" % (WORK_NAME, WORK_NAME_EN),
              size=SIZE_FOOT, color=C_MUTED)
    _add_text(slide, SLIDE_W - MARGIN - 1.6, FOOT_TEXT_Y, 1.6, 0.24,
              "%02d / %02d" % (page_no, total),
              size=SIZE_FOOT, color=C_MUTED, align=PP_ALIGN.RIGHT)


def _takeaway(slide, y, text, *, h=0.54, size=15):
    _card(slide, MARGIN, y, CONTENT_W, h, fill=C_TEAL_TINT, line=None, radius=0.18)
    _add_shape(slide, MSO_SHAPE.ROUNDED_RECTANGLE, MARGIN, y, 0.075, h,
               fill=C_TEAL, line=None)
    _add_text(slide, MARGIN + 0.28, y, CONTENT_W - 0.5, h, text,
              size=size, color=C_TEAL_DEEP, bold=True, anchor=MSO_ANCHOR.MIDDLE)


def _warn_note(slide, x, y, w, h, text, *, size=SIZE_SMALL):
    _card(slide, x, y, w, h, fill=C_AMBER_TINT, line=None, radius=0.16)
    _add_text(slide, x + 0.20, y, w - 0.32, h, text,
              size=size, color=C_AMBER, bold=False, anchor=MSO_ANCHOR.MIDDLE)


def _section_label(slide, x, y, text, w=4.0):
    _add_text(slide, x, y, w, 0.22, text, size=SIZE_KICKER, color=C_MUTED,
              bold=True, spc=1.0)


# --------------------------------------------------------------------------- #
# 插图
# --------------------------------------------------------------------------- #


def _image_size(name):
    path = FIGURES_DIR / name
    if not path.is_file():
        raise FileNotFoundError(
            "插图缺失：%s（应由 docs/figures/make_figures.py 生成）" % path)
    with Image.open(path) as im:
        return im.width, im.height


def _add_figure(slide, name, *, x, y, w, h, border=True):
    """在 (x,y,w,h) 框内等比放下插图并居中；返回插图清单条目。

    整图按源图原生像素嵌入：3200px 宽（报告落版 447.92pt）放在左图右文页 7.40in
    宽时原生 dpi ≈ 432，放在全幅页 10.27~10.38in 时 ≈ 308~312，都满足 FIG_DPI_MIN；
    不再做像素放大裁切。
    """
    px_w, px_h = _image_size(name)
    ratio = px_w / px_h
    if w / h > ratio:
        fh = h
        fw = h * ratio
    else:
        fw = w
        fh = w / ratio
    fx = x + (w - fw) / 2.0
    fy = y + (h - fh) / 2.0
    if border:
        _add_shape(slide, MSO_SHAPE.RECTANGLE, fx - 0.010, fy - 0.010,
                   fw + 0.020, fh + 0.020, fill=None, line=C_LINE, line_w=0.9)
    slide.shapes.add_picture(str(FIGURES_DIR / name), Inches(fx), Inches(fy),
                             Inches(fw), Inches(fh))
    return dict(src=_rel(FIGURES_DIR / name), rect_in=[round(v, 4) for v in
                                                      (fx, fy, fw, fh)],
                px=[int(px_w), int(px_h)])


def _add_region_highlight(slide, name, fig, region, *, color=None):
    """在已放置的插图上，用 fig_meta.json 的 region 矩形画一个重点框。"""
    meta = _figure_meta(name)
    rect = meta["regions"][region]
    x0, y0, x1, y1 = (float(v) for v in rect)
    fx, fy, fw, fh = fig["rect_in"]
    px_w, px_h = float(meta["png_w"]), float(meta["png_h"])
    shp = _add_shape(slide, MSO_SHAPE.ROUNDED_RECTANGLE,
                     fx + x0 / px_w * fw, fy + y0 / px_h * fh,
                     (x1 - x0) / px_w * fw, (y1 - y0) / px_h * fh,
                     fill=None, line=color or C_TEAL, line_w=1.6)
    _round(shp, 0.03)
    return shp


_FIG_META = None


def _figure_meta(name):
    global _FIG_META
    if _FIG_META is None:
        path = FIG_META_PATH
        if not path.is_file():
            raise SystemExit("缺少 %s（应由 docs/figures/figkit.py 写）。先跑："
                             "python docs/figures/make_figures.py" % _rel(path))
        try:
            _FIG_META = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError) as exc:
            raise SystemExit("%s 读取失败：%s" % (_rel(path), exc))
    fig = _FIG_META.get("figures", {}).get(Path(name).stem)
    if fig is None:
        raise SystemExit("%s 里没有插图 %s" % (_rel(FIG_META_PATH), name))
    return fig


# --------------------------------------------------------------------------- #
# 组件：指标卡 / 条目标题 / 要点行
# --------------------------------------------------------------------------- #


def _kpi_card(slide, x, y, w, h, *, label, value, unit="", note="",
              value_color=C_INK, accent=C_TEAL, note_size=9.5):
    _card(slide, x, y, w, h)
    _add_shape(slide, MSO_SHAPE.ROUNDED_RECTANGLE, x, y, 0.075, h,
               fill=accent, line=None)
    _add_text(slide, x + 0.24, y + 0.16, w - 0.40, 0.22, label,
              size=10, color=C_MUTED, bold=True, spc=0.6)
    avail = w - 0.40
    v_size = 28
    for cand in (28, 26, 24, 22):
        need_em = _text_em(value) + (_text_em(unit) * 12.0 / cand if unit else 0)
        if need_em * cand / 72.0 <= avail:
            v_size = cand
            break
    else:                                    # pragma: no cover - 极端长值
        v_size = 20
    runs = [{"t": value, "size": v_size, "color": value_color, "bold": True}]
    if unit:
        runs.append({"t": " " + unit, "size": 12, "color": C_MUTED})
    _add_text_shapes(slide, x + 0.24, y + 0.40, avail, 0.62, runs,
                     anchor=MSO_ANCHOR.TOP)
    if note:
        note_y = y + min(1.12, h - 0.54)
        _add_text(slide, x + 0.24, note_y, avail, 0.44, note,
                  size=note_size, color=C_FAINT, line_spacing=0.98)


def _feature_items(slide, x, y, w, items, *, lead_size=15.5, detail_size=12.5,
                   gap=0.20, lead_color=C_INK, detail_color=C_BODY):
    """「加粗小标题 + 说明」竖排条目；返回结束 y。"""
    cy = y
    for item in items:
        lead = item[0]
        detail = item[1] if len(item) > 1 else ""
        lead_h = _line_h(lead_size)
        _add_text(slide, x, cy, w, lead_h + 0.02, lead,
                  size=lead_size, color=lead_color, bold=True)
        cy += lead_h + 0.03
        if detail:
            lines = detail if isinstance(detail, (list, tuple)) else [detail]
            dh = sum(_est_text_h(t, detail_size, w) for t in lines) + 0.06
            _add_paras(slide, x, cy, w, dh,
                       [dict(runs=[{"t": t}], size=detail_size,
                             color=detail_color) for t in lines])
            cy += dh + gap
        else:
            cy += gap * 0.6
    return cy


def _dot_list(slide, x, y, w, items, *, size=SIZE_BODY, gap=0.10,
              color=C_BODY, dot_color=C_TEAL, line_spacing=1.0):
    """圆点列表（每项一行或两行），返回结束 y。"""
    cy = y
    for text in items:
        h = _est_text_h(text, size, w - 0.26, line_spacing) + 0.04
        _add_shape(slide, MSO_SHAPE.OVAL, x, cy + _line_h(size) * 0.36 - 0.035,
                   0.072, 0.072, fill=dot_color, line=None)
        _add_text(slide, x + 0.24, cy, w - 0.26, h, text,
                  size=size, color=color, line_spacing=line_spacing)
        cy += h + gap
    return cy


# --------------------------------------------------------------------------- #
# 各页内容
# --------------------------------------------------------------------------- #

SRC_METRICS = "数字出处：docs/测试与验证.md「已实测数据汇总」"
SRC_TECH = "内容出处：docs/技术文档.md"


def slide_specs():
    """16 页的页面数据：kicker / title / kind / 各版式字段 / notes。"""
    return [
        # ---------------------------------------------------------------- 1
        dict(
            kind="cover",
            kicker=EVENT_LINE,
            title=WORK_NAME,
            notes=[
                "开场：自报作品名与赛道，不出现学校与指导老师信息。",
                "一句话定位：安路飞龙 F3P SoC 单板完成「看—判—量—跟—分」。",
                "节奏：决赛介绍 5~8 分钟，本页 15~25 秒。",
                "赛事口径见 submission/提交材料清单.md §1；封面禁项见 §6 检查表。",
            ],
        ),
        # ---------------------------------------------------------------- 2
        dict(
            kind="pain",
            kicker="02 · 场景与痛点",
            title="检测与分拣，不该是两道工序",
            pains=[
                ("01", "小缺陷靠人眼", "缺料 / 飞边 / 黑点 / 尺寸超差 / 混料，都要逐件核对。"),
                ("02", "人工目检是瓶颈", "慢、一致性差、漏检难追溯，节拍上不去。"),
                ("03", "视觉站只判不分", "判完再人工或专机分拣，多一次上下料。"),
            ],
            flow=[("01", "看", "IMX415 1080p60 采集，PL 预处理"),
                  ("02", "判", "YOLOv8n-cls 分类，7.8ms / 件"),
                  ("03", "量", "亚像素几何测量，4~5ms / 件"),
                  ("04", "跟", "帧起始锁存带坐标，跟踪不吃延迟"),
                  ("05", "分", "良品停带抓取，不良品挡板分流")],
            takeaway="一块板完成看—判—量—跟—分：不停线、不落地、可追溯",
            domain="赛题领域：流水线视觉检测（领域 3）＋ 多轴运动与机电控制（领域 1）",
            notes=[
                "痛点分两层：检测本身（小缺陷、多规格多颜色）与分拣（判完还要动）。",
                "人工目检的慢与不一致是产线节拍瓶颈；独立分拣设备增加一次上下料。",
                "本作品落点：一块板把检测与分拣串成闭环。",
                "应用领域对应赛题「流水线视觉检测」（领域 3），融合多轴运动与机电控制（领域 1）。",
                SRC_TECH + " §1.1、§1.2。",
            ],
        ),
        # ---------------------------------------------------------------- 3
        dict(
            kind="overview",
            kicker="03 · 系统总览",
            title="单板闭环：三层分工，指标可查",
            statement=[{"t": "一块安路飞龙 F3P 单板", "bold": True, "color": C_INK},
                       {"t": "：双核 Cortex-A35 + NPU + 自研 PL，完成全链路。",
                        "color": C_BODY}],
            kpis=[dict(label="6 轴 SYNC WRITE 一帧", value="3298.9", unit="µs",
                       note="理论 3298.6µs ｜ PL 硬件时间戳"),
                  dict(label="采集帧率", value="60.02", unit="fps",
                       note="IMX415 1080p ｜ 20s 0 丢帧"),
                  dict(label="NPU 单次推理", value="7.8", unit="ms",
                       note="224×224 UINT8 ｜ p95 7.84ms"),
                  dict(label="CAN 命令往返", value="359", unit="µs",
                       note="板上回环 ｜ 341~825µs")],
            layers=[
                ("PL · 硬实时", "脉冲、总线帧、IO 采样逐比特确定；帧头与校验由 PL 现算",
                 "6 轴 SYNC 3298.9µs · 首脉冲 10.6µs"),
                ("PS · Linux", "定位 / 测量 / 跟踪 / 调度 / 界面与存储，\n实时线程分离",
                 "显示 60fps · 定位 5.9ms/帧 · 读 PL 188ns"),
                ("NPU · 512 MAC", "硬核卷积 + 软核算子；ArmNN / Alnpu 部署 UINT8 模型",
                 "单次 7.8ms · 合成集 95.4%（实物待测）"),
            ],
            note="⚠ 口径：只列已实测；分类准确率、实物测量误差待实物数据集，全篇标「待测」",
            notes=[
                "总览页只讲三件事：什么板子、三层怎么分工、现在测到了什么。",
                "指标行只念已实测的；分类准确率与实物测量误差还没测，后面标「待测 ⚠」，不念数字。",
                SRC_METRICS + "；指标总表同 docs/技术文档.md §1.3。",
                "SYNC 与 CAN 两行分别是硬性①与硬性⑤的证据，答辩时专家常追问条件，条件随数字一起念。",
            ],
        ),
        # ---------------------------------------------------------------- 4
        dict(
            kind="figure_side",
            kicker="04 · 系统架构",
            title="时序关键放 PL，迭代逻辑放 PS，卷积放 NPU",
            figure="fig_architecture.png",
            side_label="分工依据",
            items=[
                ("PL · 硬实时", ["脉冲、总线帧、IO 采样逐比特确定",
                                 "帧头与校验由 PL 现算"]),
                ("PS · Linux", ["定位 / 测量 / 跟踪 / 调度 / 存储 / 界面",
                                "实时线程与工作线程分离"]),
                ("NPU · 512 MAC", ["硬核卷积 + 软核算子",
                                   "ArmNN / Alnpu 部署 UINT8 模型"]),
            ],
            side_stats="PL 基址 0x8030_1000 ｜ AXI-Lite 单次读 188ns",
            takeaway="评分要求「功能不能全放 PS」：时序关键件全在 PL",
            notes=[
                "主线一句话：把「必须确定延迟」的事情交给 PL，把「必须能改」的事情交给 PS，把卷积交给 NPU。",
                "PL 侧：舵机总线主机、步进控制、拍摄锁存、视觉预处理、导流挡板 PWM、现场输入。",
                "PS 侧：取帧显示线程（实时优先级）、工作线程（定位 / 测量 / 跟踪 / 调度 / 存储）、NPU 异步线程。",
                "分工理由逐条对着赛题评分口径讲：全部功能仅 PS 实现要扣分，所以脉冲、总线帧、IO 采样都在 PL。",
                SRC_TECH + " §2.2、§4.1；评分口径见 docs/ref/安路赛道选题指南_2026.pdf p31~33。",
                "图源：docs/figures/fig_architecture.png（按源图原生 3200px 嵌入）。",
            ],
        ),
        # ---------------------------------------------------------------- 5
        dict(
            kind="figure_full",
            kicker="05 · 核心机制",
            title="位置基准锁在帧起始，推理晚到也不产生位置误差",
            lead="帧起始锁存带位置，卷帘快门逐行插值 —— 处理延迟再大，也不变成位置误差。",
            figure="fig_timebase.png",
            caption="时间基准：拍摄瞬间锁存带位置，按图像行插值",
            notes=[
                "这一页是全系统最关键的设计：位置基准不跟着处理延迟走。",
                "帧写模块开始写帧时锁存 FREE_CNT、帧号与步进位置 POS；写完再锁一次得到 CAP_*。",
                "相机是卷帘快门，不同图像行曝光时刻不同；PS 在两次锁存值之间按行线性插值，得该件被拍到时的带位置 B。",
                "零件不在带上滑动，带坐标 s = x − B 就不变，所以停带、变速、反转都不用特殊处理 —— 创新点 1。",
                "实测：锁存带位置差与「带速 × 读出时间」一致，帧号差全为 1。出处 docs/log/2026-10-10-相机60fps与PS骨架.md；"
                + SRC_TECH + " §2.3、§3.3。",
                "图源：docs/figures/fig_timebase.png。",
            ],
        ),
        # ---------------------------------------------------------------- 6
        dict(
            kind="figure_side",
            kicker="06 · PL 实现",
            title="舵机总线与步进：协议、时序、精度都在 PL 里",
            figure="fig_pl_regs.png",
            region="modules",
            side_label="两个自研模块",
            items=[
                ("协议主机", ["PS 只写帧主体；PL 补 FF FF 帧头与校验和",
                              "解析 FF F5 应答与超时；开漏 + BSS138（舵机侧 3.7V）"]),
                ("SYNC WRITE", ["一帧六轴同起跑：实测 3298.9µs",
                                "理论 3298.6µs；PING 520.9µs"]),
                ("步进控制", ["Q0.32 速度、梯形加减速、刹车距离",
                              "首脉冲 10.6µs，move 3200 无累积误差"]),
            ],
            figure_note="重点框＝0x100 / 0x200 两段；其余段：0x000 des_regs、0x300 des_vision、"
                        "0x400 des_pwm、0x500 des_io",
            takeaway="协议、时序、精度都在 PL 硬件里",
            warn="⚠ 多舵机动作起点差待逻辑分析仪实测",
            notes=[
                "硬性①的主场：PS 只写帧主体，帧头与校验和由 PL 现算，应答与超时也在 PL 判。",
                "开漏发送只主动拉低，高电平由 BSS138 电平转换模块上拉（舵机侧空闲 3.7V，不能直连 3.3V IO）。",
                "SYNC WRITE 一帧实测 3298.9µs（理论 3298.6µs）；协议文档 SYNC 示例校验值印作 0x8B，按公式应为 0xBA，硬件按公式实现并实测正确。",
                "步进：速度用 Q0.32 定点表示，无累积误差；move 3200 后 POS 与累计步数都精确为 3200。",
                "⚠ 多舵机动作起点差（同步误差）用逻辑分析仪测，尚未实测，不猜。",
                "出处 docs/log/2026-10-10-自研PL逻辑上板.md；" + SRC_TECH + " §3.1、§3.2。",
                "图源：docs/figures/fig_pl_regs.png。",
            ],
        ),
        # ---------------------------------------------------------------- 7
        dict(
            kind="pipeline",
            kicker="07 · PL 实现",
            title="des_vision：逐像素预处理，零额外延迟",
            stages=[("MIPI 接收", ["4 lane · 1080p60", "字节时钟 111.1MHz"]),
                    ("ISP + 预处理", ["去马赛克 · 白平衡 · 灰度", "高斯 · 二值 · Sobel"]),
                    ("ROI 统计", ["面积 · 外接框 · 质心", "PL 尺寸粗测"]),
                    ("VDMA 输出", ["128-bit AXI-Stream", "每拍 4 像素"])],
            stats=[("6 种模式", "原图 / 灰度 / 高斯 / 二值 / Sobel / 边缘"),
                   ("4 像素 / 拍", "128-bit，不反压、不改帧时序"),
                   ("6 拍", "3 级流水；原图模式 1 拍直通"),
                   ("127MHz", "布线后 fmax（约束 125MHz）")],
            bullets=["两行缓冲 + 3 级流水，逐像素运算不占 CPU",
                     "阈值 / 模式由 PS 写寄存器，帧起始生效",
                     "60fps 下直接看到 PL 灰度 / 滤波 / 边缘结果",
                     "ROI 粗测与 PS 亚像素测量互为对照"],
            takeaway="逐像素、固定运算放 PL：零额外延迟，HDMI 直接看到硬件处理结果",
            notes=[
                "硬性①与领域 3 的结合：逐像素、固定运算放 PL，不占 CPU、零额外延迟。",
                "128 位 AXI-Stream 每拍 4 个 BGRA 像素；两行行缓冲 + 3 级流水共 6 拍延迟。",
                "灰度按 BT.601：Y = (77R + 150G + 29B) >> 8；二值 / 边缘阈值由 PS 写寄存器。",
                "ROI 统计给出「PL 尺寸粗测」，与 PS 亚像素测量并列记录，互为对照。",
                "仿真 32×12 帧、6 种模式、790 项全过；LANES=2 后备参数同样通过。",
                SRC_TECH + " §3.4；仿真与上板见 docs/log/2026-10-11-软件闭环_NPU_界面_PL视觉.md §9。",
            ],
        ),
        # ---------------------------------------------------------------- 8
        dict(
            kind="figure_full",
            kicker="08 · 运动与分拣",
            title="只为良品停带：跟踪、刹车、抓取一条线",
            lead="良品停带抓取，不良品由侧边挡板分流；来不及停稳或有冲突就转复检，绝不倒车。",
            figure="fig_pick_sequence.png",
            stats="8/8 停准 · 停带误差 ≤0.04mm · 节拍 22.7~48.9 件/min",
            notes=[
                "这是演示视频的主镜头，也是创新点 2：只为良品停一次带。",
                "调度器先读 PL 的 BRAKE 寄存器判断来不来得及停稳，来不及就转复检 —— 失效安全，不越过去再倒车。",
                "挡板在零件到达拦截点前约 110mm 合上、滑出后约 190mm 打开；不拦的件走到带尾复检箱。",
                "硬件在环实测：50 / 100 / 150mm/s 下 8 件良品全停准抓取、4 件不良到带尾，停带误差 ≤0.04mm，节拍 22.7 / 39.3 / 48.9 件每分钟。",
                "出处 docs/log/2026-10-15-审查队友提交.md；" + SRC_TECH + " §4.4。抓取成功率、机械臂重复定位精度待实物标定 → 待测 ⚠。",
                "图源：docs/figures/fig_pick_sequence.png。",
            ],
        ),
        # ---------------------------------------------------------------- 9
        dict(
            kind="figure_side",
            kicker="09 · 视觉测量",
            title="亚像素测量 + 三项补偿，给尺寸与混料兜底",
            figure="fig_layout.png",
            side_label="测量三步",
            items=[
                ("怎么测", ["只在离光轴最近一帧测一次",
                            "沿轮廓法线找 50% 灰度过零点 → 亚像素，4~5ms"]),
                ("怎么补", ["单应映射 + 视差校正（侧壁可见模型，实测 1.018）",
                            "+ 卷帘快门逐行补偿"]),
                ("怎么判", ["齿距中位数推规格齿数",
                            "与 NPU 双判定互校，冲突进复检"]),
            ],
            side_stats=["实物外径重复性 σ 0.004mm（中心静止 20 次）",
                        "对卡尺偏置已定；换件交叉验证待测 ⚠"],
            takeaway="测量是 NPU 之外的第二意见：尺寸超差与规格混料都靠它兜底",
            notes=[
                "测量解决的是 NPU 解决不了的问题：尺寸超差与规格混料（30 齿缺 2 齿 vs 28 齿）。",
                "三步补偿是可讲的技术点：单应映射、视差校正（侧壁可见模型 k=(2H−h)/(2(H−h))，3 个 10mm 齿轮实测 1.018）、卷帘快门逐行补偿。",
                "合成图（0.15mm/像素）精度：外径 ≤0.003mm、齿距 ≤0.001mm，4~5ms / 件 —— 这是合成图口径，必须说明。",
                "实物（30 齿、曝光 800µs、画面中心静止 20 次）：外径重复性 σ 0.004mm；窄齿尖模糊使外径读小 0.41mm，用卡尺标准件定偏置后 31.961 vs 卡尺 31.963；换件交叉验证待测 ⚠。",
                "曝光由 3000µs 降到 800µs：去饱和光晕、50mm/s 拖影降到 0.04mm；离中心越远侧壁越多进轮廓（75mm 外径 +0.6、150mm +5mm）→ 尺寸只取每件离中心最近一帧。",
                SRC_TECH + " §4.3、§5.4；合成精度见 docs/log/2026-10-11-软件闭环_NPU_界面_PL视觉.md，实物测量见 §4.3。",
                "图源：docs/figures/fig_layout.png。",
            ],
        ),
        # ---------------------------------------------------------------- 10
        dict(
            kind="ai",
            kicker="10 · AI 与 NPU",
            title="为板端 NPU 改造模型：ReLU 换掉 SiLU，7.8ms 一次",
            steps=[("01", "合成数据", "4 类 · 480 张验证"),
                   ("02", "训练", "YOLOv8n-cls · 224×224"),
                   ("03", "量化", "UINT8 QDQ · per-tensor"),
                   ("04", "部署", "ArmNN + Alnpu 后端")],
            acc_title="同一模型的三处对照（4 类合成验证集 480 张，口径一致）",
            acc=[dict(label="PC · fp32", value="97.5", unit="%",
                      note="训练侧参考上限", color=C_MUTED),
                 dict(label="板上 NPU（实测）", value="95.4", unit="%",
                      note="480 张；OK/NG 96.0%", color=C_TEAL),
                 dict(label="PC · UINT8", value="95.6", unit="%",
                      note="量化后参考", color=C_MUTED)],
            whys=[("为什么 ROI 分类", "整帧缩到 416 后约 0.95mm/像素；按件裁 224×224 后 0.25mm/像素，"
                                     "小缺陷约 4 倍像素，计算量约 1/7"),
                  ("为什么 per-tensor", "per-channel 在 PC 上更准，但板上 NPU 推理中止，只能 per-tensor"),
                  ("为什么去 Softmax", "Softmax 放 PS 做；导出强制形状推断，\n否则 ArmNN 解析器段错误")],
            warn="⚠ 95.4% 是 4 类合成集口径，不代表实物；实物验证集准确率待测",
            notes=[
                "硬性④：模型、量化、部署三段都要说清，重点是「为什么这么改」。",
                "为什么 ROI 分类：整帧缩到 416 后约 0.95mm/像素，1mm 黑点或缺料只剩约 1 像素；按零件方向裁 56×56mm → 224×224 后约 0.25mm/像素，计算量约为整帧检测的 1/7。",
                "早期 8 类（含齿条）合成集上 SiLU 94.0%、ReLU 96.25%（PC 96.46%），logits 偏差 5.0 —— 板上 NPU 对 SiLU 是近似计算，改 ReLU 有效。",
                "改用齿轮 4 类（良品 / 缺料 / 飞边 / 黑点）重训后：PC fp32 97.5%、uint8 95.6%，板上 NPU 95.4%（OK/NG 96.0%），单次 7.80ms。",
                "per-channel 量化在 PC 上更准，但板上 NPU 推理中止，因此用 per-tensor；导出脚本强制做形状推断（否则 ArmNN 解析器段错误）。",
                "ⓘ 95.4% 是 4 类合成验证集 480 张的口径，不代表实物；实物验证集准确率待测 ⚠，答辩时不要念成实物精度。",
                "出处 train/README.md；" + SRC_TECH + " §5.1、§5.2；" + SRC_METRICS + "。",
            ],
        ),
        # ---------------------------------------------------------------- 11
        dict(
            kind="hmi",
            kicker="11 · 人机与存储",
            title="界面与存储都在 PS，60fps 链路不被拖累",
            tabs=["总览", "曲线", "参数", "记录", "机械臂"],
            kpis=[dict(label="HDMI 输出", value="1080p60", unit="", note="PL 处理结果直接上屏"),
                  dict(label="显示链路", value="13.0~13.9", unit="ms", note="每帧（run 模式实测）"),
                  dict(label="定位耗时", value="5.9", unit="ms", note="每帧，2h 统计平均"),
                  dict(label="存储", value="43", unit="列 CSV", note="不良件另存裁切图")],
            items=[("不用 Qt", "板上 Qt 只有独立镜像形式，会与视频链路抢资源；\n改 OpenCV 自绘 + FreeType",
                    "5 页：总览 / 曲线 / 参数 / 记录 / 机械臂"),
                   ("写盘不阻塞", "后台线程写 CSV / 图片，工作线程不等 SD 卡",
                    "43 列 CSV + 不良件裁切图，按日期分目录"),
                   ("可追溯", "测量值、带坐标与姿态、NPU 投票与耗时全记录",
                    "时间 = 本机时钟 + PLC 对时偏移")],
            takeaway="界面与存储都在 PS，仍保住 60fps 视频链路",
            notes=[
                "硬性②：界面与存储都在 PS，但要用实测数字说明没有牺牲实时性。",
                "不用 Qt 的原因：板上 Qt 只有独立镜像形式，会与 60fps 视频链路抢资源；改为 OpenCV 自绘 + FreeType 渲染中文。",
                "参数页改动即时生效并保存；记录页可翻每件裁切图与测量值；另有命名管道脚本通道，用于自动化测试与文档配图。",
                "存储：43 列 CSV 含测量值、带坐标与姿态、PL 粗测、NPU 投票次数与耗时；按日期分目录；时间 = 本机时钟 + PLC 下发的时间偏移。",
                "出处 docs/log/2026-10-16-HDMI周期性黑屏.md；" + SRC_METRICS + "；" + SRC_TECH + " §4.6、§4.7。",
            ],
        ),
        # ---------------------------------------------------------------- 12
        dict(
            kind="can",
            kicker="12 · 工业总线",
            title="CAN 联动 PLC，同一套命令再开放 Modbus-TCP",
            ends=[("产线 PLC", "PC PLC 模拟器 / USB-CAN"), ("F3P · des_app", "SocketCAN · 状态机 · 三路命令源")],
            bus="CAN 2.0A · 500 kbit/s · 11 位 ID",
            down=["启停 / 联锁暂停 / 恢复 / 复位", "调速（0.1mm/s）",
                  "配方换型（运行中拒绝）", "对时（unix 秒 + 毫秒）"],
            up=["STATUS 10Hz · 状态 / 带速 / 报警", "每件 PART：类别 / 去向 / 置信度 / 尺寸",
                "COUNTS 1Hz · ALARM 报警帧", "另开 Modbus-TCP 从站 502：同一套命令与状态"],
            stats=[("359", "µs", "CAN 命令往返（341~825µs，1000/1000）"),
                   ("1.04", "ms", "Modbus-TCP 读 24 个输入寄存器往返"),
                   ("0.13", "%", "2h 回环 PLC 侧 STATUS 丢帧（复测中）")],
            takeaway="界面 / CAN / 网口三路同一套规则：命令只排队，控制循环按同一规则执行",
            warn="⚠ USB-CAN 双节点实物收发与长时间误码待测（单节点 v12 路由已通）",
            notes=[
                "硬性⑤：工业总线不只做 CAN，还按工厂实际把同一套命令开放成 Modbus-TCP 从站。",
                "状态机：空闲 / 运行 / 暂停 / 急停 / 故障；PLC 命令与界面按钮同一套规则（未启动时「恢复」被拒、运行中不能换配方）。",
                "实测：板上回环命令往返平均 359µs（341~825µs）、1000/1000；200~2000 帧/s 零丢失零错序；Modbus-TCP 读 24 个输入寄存器平均往返 1.04ms，单元测试 29 项通过。",
                "CAN 实物：官方 demo 块没开 PS CAN0（接收接 0、发送悬空、无约束），现成比特流下 CAN 不通；v12 把 PS CAN0 硬核收发经块设计引到 F20 / F19 并约束（接收上拉），单节点发帧后错误计数 128、进被动错误不离线（无应答的标准表现）。",
                "Modbus 实物启停（皮带真走）：启动 → 改带速 50→30→80mm/s → 联锁暂停 → 恢复 → 停止全部正确，4 种非法写全被拒，写入 → 执行确认 14~24ms。",
                "配方换型：切到 28 齿配方后，同批零件里 28 齿件由报废变良品、30 齿件变规格混料，运行中拒绝换配方。",
                "⚠ 实物 USB-CAN 双节点收发与长时间误码待测；CAN 2h 回环里 PLC 侧 STATUS 丢 82 帧（0.13%），已定位为背靠背连发，复测中。",
                "出处 docs/log/2026-10-11-软件闭环_NPU_界面_PL视觉.md、docs/log/2026-10-20-编码器与Modbus实物启停.md；" + SRC_METRICS + "；" + SRC_TECH + " §6。",
            ],
        ),
        # ---------------------------------------------------------------- 13
        dict(
            kind="metrics",
            kicker="13 · 测试数据",
            title="四类指标都有实测，条件与口径一起念",
            rows=[
                ("控制同步",
                 [("SYNC 帧", "3298.9µs"), ("首脉冲", "10.6µs"), ("停带误差", "≤0.04mm")],
                 "HIL 6/6；理论 3298.6µs"),
                ("采集延迟",
                 [("采集帧率", "60.02fps"), ("显示每帧", "13.0~13.9ms"), ("读 PL 寄存器", "188ns")],
                 "IMX415 1080p；20s 0 丢帧"),
                ("AI 推理",
                 [("NPU 单次", "7.8ms"), ("合成集准确率", "95.4%"), ("实物外径 σ", "0.004mm")],
                 "4 类 480 张；中心静止 20 次"),
                ("总线稳定性",
                 [("CAN 往返", "359µs"), ("Modbus 往返", "1.04ms"), ("实物收发", "待测 ⚠")],
                 "板上回环；341~825µs"),
            ],
            note="⚠ 待测项（答辩主动说明）：实物分类准确率 / 换件对卡尺交叉验证 / 多舵机同步误差 / "
                 "机械臂重复定位 / 跟踪误差 / 接实物 2h",
            notes=[
                "硬性⑥：四类指标逐类念，条件随数字一起念（条件不同的数不可比）。",
                "已实测：SYNC 3298.9µs、步进命令→首脉冲 10.6µs、停带误差 ≤0.04mm、60.02fps 0 丢帧、显示 13.0~13.9ms、PS 读 PL 188ns、NPU 7.8ms、实物外径重复性 σ 0.004mm、CAN 359µs、Modbus 1.04ms。",
                "待测 ⚠（答辩要主动说明，别等专家问）：实物分类准确率、换件对卡尺交叉验证、多舵机同步误差（逻辑分析仪）、机械臂重复定位、跟踪误差、接实物 2h。",
                "口径诚实是评分项：指令分辨率与实测精度分开、采集 / 显示实时性与按物料触发推理分开。",
                SRC_METRICS + "；缺口清单见同文件「还缺（按优先级）」。",
            ],
        ),
        # ---------------------------------------------------------------- 14
        dict(
            kind="reliability",
            kicker="14 · 可靠性与安全",
            title="失效安全：宁可进复检箱，不放过一个不良品",
            stats=[("60.02 fps", "2h 连续运行",
                    "432122 帧仅跳 13 帧（0.003%）；工作线程处理 99.9% 帧"),
                   ("88.5 → 99.9 MB", "内存占用",
                    "最后 50 分钟不增长；显示平均 9.29ms，无崩溃")],
            safety=[("急停双路", "常闭切动力电源；常开报 PL 锁存，软件进急停并 CAN 报警"),
                    ("1ms 去抖", "急停按下保持 1ms 才算，锁存后直接停住步进，需人工复位"),
                    ("20µs 捕获", "对射 / 限位去抖 20µs；挡光边沿\n锁存时间、带位置与编码器"),
                    ("失效安全", "漏检 / 跟丢 / 挡板故障 → 复检箱，不良品不会被放行")],
            takeaway="安全分两条：切断动力（硬件）与上报锁存（软件），互不依赖",
            warn="⚠ 该次 2h 是无零件软件浸泡；接实物 2h 与舵机总线长时间丢帧率待测",
            notes=[
                "安全分两条：切电（常闭触点串在动力电源上，同时切断步进与全部舵机）与上报（常开触点报 PL 锁存，软件进急停、CAN 发报警，需人工复位）。",
                "失效安全的设计意图：漏检、跟丢、挡板故障都只会进复检箱，不会把不良品当良品放行。",
                "长时间：软件 2h 浸泡 60.02fps、跳帧 0.003%、工作线程处理 99.9% 的帧、显示平均 9.29ms（全屏布局），无崩溃。",
                "⚠ 该次 2h 是无零件的软件浸泡；接实物的 2h 与舵机总线长时间丢帧率待测。",
                "出处 docs/log/2026-10-11-软件闭环_NPU_界面_PL视觉.md（soak.sh 7200）；" + SRC_TECH + " §3.6、§7；" + SRC_METRICS + "。",
            ],
        ),
        # ---------------------------------------------------------------- 15
        dict(
            kind="innovations",
            kicker="15 · 创新点",
            title="五个创新点，每个都有可验证的证据",
            cards=[("01", "帧起始锁存 + 带坐标跟踪", "位置误差与推理延迟解耦；帧号差全为 1，锁存位置差 = 带速 × 读出时间"),
                   ("02", "只为良品停带，不良品侧向分流", "按硬件刹车距离判定能否停准；HIL 8 件全停准，停带误差 ≤0.04mm"),
                   ("03", "亚像素测量 + NPU 双判定", "一方 OK 一方 NG 即复检；合成图外径 ≤0.003mm、齿距 ≤0.001mm"),
                   ("04", "面向 NPU 的模型改造", "4 类合成集板上 95.4%（OK/NG 96.0%），PC uint8 95.6%\nReLU 改造 + per-tensor 量化 + 导出强制形状推断，板上通过"),
                   ("05", "全自研 PL 外设与寄存器通道", "不改官方块设计引出寄存器窗口；协议 / 步进 / 视觉 / 挡板 / 急停均有逐项仿真\n仿真 790 + 19 + 11 + 28 项全过；PL 基址 0x8030_1000")],
            takeaway="位置误差与推理延迟解耦 —— 判得慢，不影响分得准",
            notes=[
                "五个创新点逐个用一句话讲，各配一个可验证的证据，不堆形容词。",
                "点 1 证据：帧号差全为 1、锁存带位置差与带速 × 读出时间一致。",
                "点 2 证据：硬件在环 8 件全停准，停带误差 ≤0.04mm，来不及停转复检。",
                "点 3 证据：一方判 OK 另一方判缺陷时进复检，避免单点误判放行。",
                "点 4 证据：4 类合成验证集板上 95.4%（OK/NG 96.0%）、PC uint8 95.6%；ReLU 改造在早期 8 类数据上把 94.0% 提到 96.25%。",
                "点 5 证据：不改官方块设计，把 fvdma_ctrl 地址窗口加宽引出寄存器总线；PL 基址与分段见 docs/技术文档.md §2.4；仿真 790 + 19 + 11 + 28 项全过。",
                "出处 docs/技术文档.md §9；各项数字见 docs/测试与验证.md。",
            ],
        ),
        # ---------------------------------------------------------------- 16
        dict(
            kind="summary",
            kicker="16 · 总结",
            title="单板闭环已经跑通，下一步是实物数据集与标定",
            columns=[("已完成", C_TEAL, ["PL 硬实时外设与总线",
                                        "PS 定位 / 测量 / 调度 / 存储 / 界面",
                                        "NPU 分类，7.8ms / 次",
                                        "CAN + Modbus-TCP 双路对接",
                                        "四类指标出数"]),
                     ("待补齐 ⚠", C_AMBER, ["实物数据集与混淆矩阵",
                                            "对卡尺的测量误差与重复性",
                                            "多舵机同步误差",
                                            "机械臂重复定位 / 抓取成功率",
                                            "接实物 2h 连续运行"]),
                     ("下一步", C_MUTED, ["采实物数据集并重训模型",
                                          "接实物复跑 2h 与标定",
                                          "按实测微调挡板拦截点",
                                          "决赛现场演示与 FPGA 编程考核"])],
            closing="判得准、跟得住、分得开 —— 一块板跑通全链路",
            thanks="谢谢，请专家指正",
            notes=[
                "收尾按「做完了什么 / 还差什么 / 接下来做什么」三段说。",
                "做到了：PL 硬实时外设与总线、PS 定位测量调度存储界面、NPU 分类、CAN 与 Modbus 双路对接、四类指标出数。",
                "还没测：实物数据集准确率与混淆矩阵、对卡尺的测量误差与重复性、多舵机同步误差、机械臂重复定位、接实物 2h。",
                "收束到一句话：位置误差与推理延迟解耦，所以「判得慢」不影响「分得准」。",
                "材料分工：报告正文 docs/技术文档.md，分镜 docs/演示视频脚本.md，实测数字 docs/测试与验证.md。",
            ],
        ),
    ]


# --------------------------------------------------------------------------- #
# 版式：封面
# --------------------------------------------------------------------------- #


def build_cover(prs, spec, page_no, total):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    _add_shape(slide, MSO_SHAPE.RECTANGLE, 0, 0, SLIDE_W, SLIDE_H, fill=C_COVER_BG)

    # 右侧「龙瞳」同心环装饰（纯形状，可编辑）
    cx, cy = 10.05, 3.55
    rings = [(5.90, 0.9), (5.05, 1.1), (4.20, 1.3), (3.40, 1.5),
             (2.55, 1.8), (1.75, 2.0)]
    for i, (d, lw) in enumerate(rings):
        _add_shape(slide, MSO_SHAPE.OVAL, cx - d / 2, cy - d / 2, d, d,
                   fill=None, line=C_COVER_RING[min(i, len(C_COVER_RING) - 1)],
                   line_w=lw)
    pupil = _add_shape(slide, MSO_SHAPE.OVAL, cx - 0.46, cy - 0.46, 0.92, 0.92,
                       fill=C_COVER_RING[-1], line=C_COVER_TEAL, line_w=1.2)
    _set_fill_alpha(pupil, 55)
    _add_shape(slide, MSO_SHAPE.OVAL, cx - 0.12, cy - 0.12, 0.24, 0.24,
               fill=C_COVER_TEAL, line=None)

    # 左侧文字块
    _add_text(slide, 1.05, 1.60, 8.6, 0.26, EVENT_LINE,
              size=12, color=C_COVER_MUTED, bold=False, spc=0.6)
    _add_text(slide, 1.02, 1.92, 8.6, 1.42, WORK_NAME,
              size=66, color=C_WHITE, bold=True)
    _add_text(slide, 1.05, 3.44, 8.0, 0.40, WORK_NAME_EN,
              size=18, color=C_COVER_TEAL, spc=2.6)
    _add_shape(slide, MSO_SHAPE.RECTANGLE, 1.07, 3.92, 2.15, 0.045,
               fill=C_COVER_TEAL)
    _add_text(slide, 1.05, 4.08, 8.6, 0.44, "塑胶传动件在线质检分拣系统",
              size=22, color=C_COVER_SOFT)
    _add_text(slide, 1.05, 4.86, 8.6, 0.34, TECH_LINE,
              size=13, color=C_COVER_MUTED)
    _add_text(slide, 1.05, 5.56, 8.6, 0.34, TRACK_LINE,
              size=14, color=C_COVER_MUTED)
    _add_text(slide, 1.05, 6.06, 8.6, 0.30, DATE_LINE,
              size=12.5, color=C_COVER_FOOT)

    _add_text(slide, 1.05, 7.13, 7.0, 0.22, "DragonEyeSorter · 龙瞳分拣",
              size=9, color=C_COVER_FOOT)
    _add_text(slide, SLIDE_W - MARGIN - 1.6, 7.13, 1.6, 0.22,
              "01 / %02d" % total, size=9, color=C_COVER_FOOT,
              align=PP_ALIGN.RIGHT)
    return []


# --------------------------------------------------------------------------- #
# 版式：痛点对比（P2）
# --------------------------------------------------------------------------- #


def build_pain(prs, spec, page_no, total):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    _add_shape(slide, MSO_SHAPE.RECTANGLE, 0, 0, SLIDE_W, SLIDE_H, fill=C_BG)
    _header(slide, spec)

    # 左：三条产线现状
    _section_label(slide, MARGIN, 1.72, "产线现状")
    cy = 2.02
    for num, lead, detail in spec["pains"]:
        _add_text(slide, MARGIN, cy, 0.55, 0.32, num,
                  size=13, color=C_TEAL, bold=True, spc=1.0)
        _add_text(slide, MARGIN + 0.52, cy - 0.04, 5.55, 0.34, lead,
                  size=17, color=C_INK, bold=True)
        _add_text(slide, MARGIN + 0.52, cy + 0.36, 5.55, 0.62, detail,
                  size=12.5, color=C_BODY, line_spacing=1.05)
        cy += 1.16
    _add_text(slide, MARGIN, 5.72, 6.1, 0.30, spec["domain"],
              size=11.5, color=C_MUTED)

    # 右：深色面板（单板闭环 5 步）
    px, pw = 7.02, 5.69
    _card(slide, px, 1.74, pw, 4.42, fill=C_NAVY, line=None, radius=0.035)
    _add_text(slide, px + 0.34, 1.96, pw - 0.6, 0.22, "DragonEyeSorter · 单板闭环",
              size=10.5, color=C_COVER_TEAL, bold=True, spc=1.2)
    _add_text(slide, px + 0.34, 2.24, pw - 0.6, 0.42, "一块板，一条闭环",
              size=22, color=C_WHITE, bold=True)
    cy = 2.86
    for num, stage, detail in spec["flow"]:
        _add_shape(slide, MSO_SHAPE.ROUNDED_RECTANGLE, px + 0.34, cy, 0.46, 0.42,
                   fill=C_NAVY_2, line=None)
        _add_text(slide, px + 0.34, cy, 0.46, 0.42, stage,
                  size=14, color=C_COVER_TEAL, bold=True,
                  align=PP_ALIGN.CENTER, anchor=MSO_ANCHOR.MIDDLE)
        _add_text(slide, px + 0.98, cy, pw - 1.3, 0.42, detail,
                  size=11, color=C_COVER_SOFT, anchor=MSO_ANCHOR.MIDDLE)
        cy += 0.60

    _takeaway(slide, 6.34, spec["takeaway"])
    _footer(slide, page_no, total)
    return []


# --------------------------------------------------------------------------- #
# 版式：系统总览（P3）
# --------------------------------------------------------------------------- #


def build_overview(prs, spec, page_no, total):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    _add_shape(slide, MSO_SHAPE.RECTANGLE, 0, 0, SLIDE_W, SLIDE_H, fill=C_BG)
    _header(slide, spec)

    _add_text_shapes(slide, MARGIN, 1.70, CONTENT_W, 0.46,
                     [{"t": r["t"], "size": r.get("size", 18),
                       "color": r.get("color", C_BODY),
                       "bold": r.get("bold", False)} for r in spec["statement"]],
                     size=18, anchor=MSO_ANCHOR.MIDDLE)

    # 4 张指标卡
    gap = 0.22
    tw = (CONTENT_W - 3 * gap) / 4.0
    for i, kpi in enumerate(spec["kpis"]):
        _kpi_card(slide, MARGIN + i * (tw + gap), 2.26, tw, 1.72, **kpi)

    # 3 张分层卡
    gap2 = 0.22
    lw = (CONTENT_W - 2 * gap2) / 3.0
    for i, (lead, detail, proof) in enumerate(spec["layers"]):
        x = MARGIN + i * (lw + gap2)
        _card(slide, x, 4.32, lw, 1.62, fill=C_CARD_ALT, line=None, radius=0.05)
        _add_text(slide, x + 0.26, 4.44, lw - 0.5, 0.26,
                  lead, size=15, color=C_INK, bold=True)
        _add_text(slide, x + 0.26, 4.74, lw - 0.5, 0.52, detail,
                  size=11, color=C_BODY, line_spacing=1.02)
        _add_text(slide, x + 0.26, 5.44, lw - 0.5, 0.24, proof,
                  size=9.2, color=C_TEAL_DEEP, line_spacing=1.0)

    _warn_note(slide, MARGIN, 6.14, CONTENT_W, 0.48, spec["note"])
    _footer(slide, page_no, total)
    return []


# --------------------------------------------------------------------------- #
# 版式：左图右文（P4 / P6 / P9）
# --------------------------------------------------------------------------- #


def build_figure_side(prs, spec, page_no, total):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    _add_shape(slide, MSO_SHAPE.RECTANGLE, 0, 0, SLIDE_W, SLIDE_H, fill=C_BG)
    _header(slide, spec)

    fig_w = 7.40
    fig_h = 4.72 if spec.get("figure_note") else 5.12
    fig = _add_figure(slide, spec["figure"], x=MARGIN, y=1.76, w=fig_w, h=fig_h)
    if spec.get("region"):
        _add_region_highlight(slide, spec["figure"], fig, spec["region"])
    if spec.get("figure_note"):
        _add_text(slide, MARGIN, 6.56, 7.40, 0.30, spec["figure_note"],
                  size=9.5, color=C_MUTED)

    rx, rw = 8.28, SLIDE_W - 8.28 - MARGIN
    _section_label(slide, rx, 1.72, spec.get("side_label", "要点"))
    end_y = _feature_items(slide, rx, 2.10, rw, spec["items"],
                           lead_size=16, detail_size=11.5, gap=0.20)
    take_y = 5.45
    if spec.get("side_stats"):
        stats_y = max(end_y + 0.10, 4.96)
        _hairline(slide, rx, stats_y, rw)
        stats = spec["side_stats"]
        lines = stats if isinstance(stats, list) else [stats]
        _add_paras(slide, rx, stats_y + 0.08, rw, 0.60,
                   [dict(runs=[{"t": t}], size=10, color=C_MUTED) for t in lines])
        take_y = max(stats_y + 0.78, 5.62)
    else:
        take_y = max(end_y + 0.16, max(5.05, BODY_BOTTOM - 1.55))
    take_h = 0.64
    if spec.get("takeaway"):
        th = 0.50 if spec.get("warn") else take_h
        _card(slide, rx, take_y, rw, th, fill=C_TEAL_TINT, line=None, radius=0.14)
        _add_shape(slide, MSO_SHAPE.ROUNDED_RECTANGLE, rx, take_y, 0.07, th,
                   fill=C_TEAL, line=None)
        _add_text(slide, rx + 0.24, take_y, rw - 0.40, th, spec["takeaway"],
                  size=12, color=C_TEAL_DEEP, bold=True,
                  anchor=MSO_ANCHOR.MIDDLE, line_spacing=1.05)
        warn_y = take_y + th + 0.12
    else:
        warn_y = 6.26
    if spec.get("warn"):
        _warn_note(slide, rx, warn_y, rw, 0.52, spec["warn"], size=10)
    _footer(slide, page_no, total)
    return [fig]


# --------------------------------------------------------------------------- #
# 版式：全幅示意图（P5 / P8）
# --------------------------------------------------------------------------- #


def build_figure_full(prs, spec, page_no, total):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    _add_shape(slide, MSO_SHAPE.RECTANGLE, 0, 0, SLIDE_W, SLIDE_H, fill=C_BG)
    _header(slide, spec)

    lead_w = 7.6 if spec.get("stats") else CONTENT_W
    _add_text(slide, MARGIN, 1.60, lead_w, 0.40, spec["lead"],
              size=14.5, color=C_BODY, anchor=MSO_ANCHOR.MIDDLE)
    if spec.get("stats"):
        _add_text(slide, SLIDE_W - MARGIN - 4.9, 1.60, 4.9, 0.40,
                  spec["stats"], size=11, color=C_TEAL, bold=True,
                  align=PP_ALIGN.RIGHT, anchor=MSO_ANCHOR.MIDDLE)

    fig_h = 4.52 if spec.get("caption") else 4.86
    fig_y = 2.08 if spec.get("caption") else 2.02
    fig = _add_figure(slide, spec["figure"], x=MARGIN, y=fig_y,
                      w=CONTENT_W, h=fig_h)
    if spec.get("caption"):
        _add_text(slide, MARGIN, 6.70, CONTENT_W, 0.22, spec["caption"],
                  size=SIZE_TINY, color=C_MUTED, align=PP_ALIGN.CENTER)
    _footer(slide, page_no, total)
    return [fig]


# --------------------------------------------------------------------------- #
# 版式：流水线（P7 des_vision）
# --------------------------------------------------------------------------- #


def build_pipeline(prs, spec, page_no, total):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    _add_shape(slide, MSO_SHAPE.RECTANGLE, 0, 0, SLIDE_W, SLIDE_H, fill=C_BG)
    _header(slide, spec)

    # 4 段流程卡
    n = len(spec["stages"])
    gap = 0.34
    cw = (CONTENT_W - (n - 1) * gap) / n
    for i, (title, detail) in enumerate(spec["stages"]):
        x = MARGIN + i * (cw + gap)
        _card(slide, x, 1.74, cw, 1.30, fill=C_CARD, line=C_LINE)
        _add_text(slide, x + 0.22, 1.86, cw - 0.4, 0.20, "%02d" % (i + 1),
                  size=10, color=C_TEAL, bold=True, spc=1.0)
        _add_text(slide, x + 0.22, 2.06, cw - 0.4, 0.30, title,
                  size=14.5, color=C_INK, bold=True)
        _add_paras(slide, x + 0.22, 2.40, cw - 0.4, 0.56,
                   [dict(runs=[{"t": t}], size=9.5, color=C_MUTED) for t in detail])
        if i < n - 1:
            _arrow(slide, x + cw + 0.06, 2.27, gap - 0.12, 0.24)

    # 4 个小指标（与流程卡同网格：同起点、同宽、同间距）
    gap2 = 0.34
    tw = (CONTENT_W - 3 * gap2) / 4.0
    for i, (value, label) in enumerate(spec["stats"]):
        x = MARGIN + i * (tw + gap2)
        _card(slide, x, 3.24, tw, 1.12, fill=C_CARD_ALT, line=None, radius=0.06)
        _add_text(slide, x + 0.22, 3.36, tw - 0.4, 0.40, value,
                  size=20, color=C_INK, bold=True)
        _add_text(slide, x + 0.22, 3.76, tw - 0.4, 0.52, label,
                  size=9.5, color=C_MUTED, line_spacing=1.0)

    # 要点 2×2
    bw = (CONTENT_W - 0.30) / 2.0
    for i, text in enumerate(spec["bullets"]):
        col, row = i % 2, i // 2
        x = MARGIN + col * (bw + 0.30)
        y = 4.66 + row * 0.78
        _add_shape(slide, MSO_SHAPE.RECTANGLE, x, y + 0.20, 0.055, 0.22,
                   fill=C_TEAL)
        _add_text(slide, x + 0.20, y + 0.10, bw - 0.2, 0.42, text,
                  size=13, color=C_BODY, line_spacing=1.0)

    _takeaway(slide, 6.16, spec["takeaway"])
    _footer(slide, page_no, total)
    return []


# --------------------------------------------------------------------------- #
# 版式：AI 与 NPU（P10）
# --------------------------------------------------------------------------- #


def build_ai(prs, spec, page_no, total):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    _add_shape(slide, MSO_SHAPE.RECTANGLE, 0, 0, SLIDE_W, SLIDE_H, fill=C_BG)
    _header(slide, spec)

    # 4 步流水
    n = len(spec["steps"])
    gap = 0.34
    cw = (CONTENT_W - (n - 1) * gap) / n
    for i, (num, title, detail) in enumerate(spec["steps"]):
        x = MARGIN + i * (cw + gap)
        _card(slide, x, 1.72, cw, 1.02)
        _add_text(slide, x + 0.22, 1.82, cw - 0.4, 0.20, num,
                  size=10, color=C_TEAL, bold=True, spc=1.0)
        _add_text(slide, x + 0.22, 2.02, cw - 0.4, 0.28, title,
                  size=13.5, color=C_INK, bold=True)
        _add_text(slide, x + 0.22, 2.32, cw - 0.4, 0.30, detail,
                  size=9.5, color=C_MUTED, line_spacing=1.0)
        if i < n - 1:
            _arrow(slide, x + cw + 0.06, 2.10, gap - 0.12, 0.24)

    # 精度对比：3 张卡 + 中间箭头
    _add_text(slide, MARGIN, 2.94, CONTENT_W, 0.24, spec["acc_title"],
              size=10.5, color=C_MUTED, bold=True)
    acc = spec["acc"]
    agap = 0.34
    aw = (CONTENT_W - (len(acc) - 1) * agap) / len(acc)
    for i, item in enumerate(acc):
        x = MARGIN + i * (aw + agap)
        _kpi_card(slide, x, 3.24, aw, 1.62,
                  label=item["label"], value=item["value"], unit=item["unit"],
                  note=item["note"], value_color=item["color"],
                  accent=C_TEAL if i == 1 else C_LINE)
        if i == 1:
            _chip(slide, x + aw - 0.94, 3.32, 0.80, 0.26, "已部署",
                  fill=C_TEAL, color=C_WHITE, size=8.5, radius=0.5)
        if i < len(acc) - 1:
            _arrow(slide, x + aw + 0.06, 3.90, agap - 0.12, 0.26)

    # 三个「为什么」
    n3 = len(spec["whys"])
    gap3 = 0.22
    w3 = (CONTENT_W - (n3 - 1) * gap3) / n3
    for i, (lead, detail) in enumerate(spec["whys"]):
        x = MARGIN + i * (w3 + gap3)
        _card(slide, x, 5.02, w3, 1.08, fill=C_CARD_ALT, line=None, radius=0.05)
        _add_text(slide, x + 0.22, 5.13, w3 - 0.4, 0.26, lead,
                  size=12.5, color=C_INK, bold=True)
        _add_text(slide, x + 0.22, 5.41, w3 - 0.4, 0.62, detail,
                  size=9.5, color=C_BODY, line_spacing=1.02)

    _warn_note(slide, MARGIN, 6.58, CONTENT_W, 0.34, spec["warn"], size=10)
    _footer(slide, page_no, total)
    return []


# --------------------------------------------------------------------------- #
# 版式：HMI 与存储（P11）
# --------------------------------------------------------------------------- #


def build_hmi(prs, spec, page_no, total):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    _add_shape(slide, MSO_SHAPE.RECTANGLE, 0, 0, SLIDE_W, SLIDE_H, fill=C_BG)
    _header(slide, spec)

    # 五页标签
    tabs = spec["tabs"]
    n = len(tabs)
    gap = 0.14
    tw = (CONTENT_W - (n - 1) * gap) / n
    for i, tab in enumerate(tabs):
        x = MARGIN + i * (tw + gap)
        if i == 0:
            _chip(slide, x, 1.74, tw, 0.52, tab, fill=C_TEAL, color=C_WHITE,
                  size=13, radius=0.14)
        else:
            _chip(slide, x, 1.74, tw, 0.52, tab, fill=C_CARD, line=C_LINE,
                  color=C_BODY, size=13, radius=0.14, bold=False)

    # 4 个指标卡
    gap2 = 0.22
    kw = (CONTENT_W - 3 * gap2) / 4.0
    for i, kpi in enumerate(spec["kpis"]):
        _kpi_card(slide, MARGIN + i * (kw + gap2), 2.50, kw, 1.58, **kpi)

    # 3 张特性卡
    n3 = len(spec["items"])
    gap3 = 0.22
    w3 = (CONTENT_W - (n3 - 1) * gap3) / n3
    for i, (lead, detail, note) in enumerate(spec["items"]):
        x = MARGIN + i * (w3 + gap3)
        _card(slide, x, 4.32, w3, 1.56, fill=C_CARD, line=C_LINE)
        _add_text(slide, x + 0.24, 4.44, w3 - 0.44, 0.28, lead,
                  size=13.5, color=C_INK, bold=True)
        _add_text(slide, x + 0.24, 4.74, w3 - 0.44, 0.62, detail,
                  size=10, color=C_BODY, line_spacing=1.05)
        _add_text(slide, x + 0.24, 5.42, w3 - 0.44, 0.34, note,
                  size=9.2, color=C_TEAL_DEEP, line_spacing=1.0)

    _takeaway(slide, 6.10, spec["takeaway"])
    _footer(slide, page_no, total)
    return []


# --------------------------------------------------------------------------- #
# 版式：CAN / Modbus（P12）
# --------------------------------------------------------------------------- #


def build_can(prs, spec, page_no, total):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    _add_shape(slide, MSO_SHAPE.RECTANGLE, 0, 0, SLIDE_W, SLIDE_H, fill=C_BG)
    _header(slide, spec)

    # 拓扑：产线 PLC ⇄ CAN 总线 ⇄ F3P
    ends = spec["ends"]
    card_w = 4.30
    x1 = MARGIN
    x2 = SLIDE_W - MARGIN - card_w
    for x, (title, detail) in ((x1, ends[0]), (x2, ends[1])):
        _card(slide, x, 1.78, card_w, 0.94, fill=C_CARD, line=C_LINE)
        _add_text(slide, x + 0.26, 1.87, card_w - 0.5, 0.30, title,
                  size=15.5, color=C_INK, bold=True)
        _add_text(slide, x + 0.26, 2.18, card_w - 0.5, 0.30, detail,
                  size=10, color=C_MUTED, line_spacing=1.0)
    mid_x = x1 + card_w
    mid_w = x2 - mid_x
    _add_text(slide, mid_x, 1.82, mid_w, 0.26, spec["bus"],
              size=11, color=C_TEAL_DEEP, bold=True,
              align=PP_ALIGN.CENTER, spc=0.6)
    _arrow(slide, mid_x + 0.42, 2.16, mid_w - 0.84, 0.30, color=C_TEAL,
           shape=MSO_SHAPE.LEFT_RIGHT_ARROW)
    _add_text(slide, mid_x, 2.50, mid_w, 0.22, "命令 / 状态 / 每件结果",
              size=9, color=C_FAINT, align=PP_ALIGN.CENTER)

    # 下发 / 回传
    _card(slide, MARGIN, 3.22, 5.92, 1.72, fill=C_CARD, line=C_LINE)
    _add_text(slide, MARGIN + 0.26, 3.36, 5.4, 0.24, "PLC → 本机：命令",
              size=11.5, color=C_TEAL, bold=True, spc=0.8)
    _dot_list(slide, MARGIN + 0.26, 3.66, 5.4, spec["down"], size=10.2, gap=0.04)

    _card(slide, 6.80, 3.22, 5.91, 1.72, fill=C_CARD, line=C_LINE)
    _add_text(slide, 7.06, 3.36, 5.4, 0.24, "本机 → PLC：状态与结果",
              size=11.5, color=C_TEAL, bold=True, spc=0.8)
    _dot_list(slide, 7.06, 3.66, 5.4, spec["up"], size=10.2, gap=0.04)

    # 实测数字
    gap3 = 0.30
    sw = (CONTENT_W - 2 * gap3) / 3.0
    for i, (value, unit, note) in enumerate(spec["stats"]):
        x = MARGIN + i * (sw + gap3)
        _add_text_shapes(slide, x, 5.12, sw, 0.46,
                         [{"t": value, "size": 24, "bold": True, "color": C_INK},
                          {"t": "  " + unit, "size": 11, "color": C_MUTED}],
                         anchor=MSO_ANCHOR.MIDDLE)
        _add_text(slide, x, 5.60, sw, 0.30, note,
                  size=9.5, color=C_MUTED, line_spacing=1.0)

    # 结论 + 警示并排
    take_w = CONTENT_W - 3.72
    _card(slide, MARGIN, 6.18, take_w, 0.58, fill=C_TEAL_TINT, line=None,
          radius=0.18)
    _add_shape(slide, MSO_SHAPE.ROUNDED_RECTANGLE, MARGIN, 6.18, 0.075, 0.58,
               fill=C_TEAL, line=None)
    _add_text(slide, MARGIN + 0.26, 6.18, take_w - 0.42, 0.58, spec["takeaway"],
              size=12, color=C_TEAL_DEEP, bold=True, anchor=MSO_ANCHOR.MIDDLE)
    _warn_note(slide, MARGIN + take_w + 0.22, 6.18, 3.50, 0.58,
               spec["warn"], size=9)
    _footer(slide, page_no, total)
    return []


# --------------------------------------------------------------------------- #
# 版式：测试数据（P13）
# --------------------------------------------------------------------------- #


def build_metrics(prs, spec, page_no, total):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    _add_shape(slide, MSO_SHAPE.RECTANGLE, 0, 0, SLIDE_W, SLIDE_H, fill=C_BG)
    _header(slide, spec)

    col_cat, col_cond = 1.78, 3.05
    col_main = CONTENT_W - col_cat - col_cond
    x_cat = MARGIN
    x_main = MARGIN + col_cat
    x_cond = MARGIN + col_cat + col_main

    # 表头
    _add_text(slide, x_cat, 1.70, col_cat, 0.24, "类别", size=10, color=C_MUTED, bold=True, spc=0.8)
    _add_text(slide, x_main, 1.70, col_main, 0.24, "已实测指标", size=10, color=C_MUTED, bold=True, spc=0.8)
    _add_text(slide, x_cond, 1.70, col_cond, 0.24, "条件 / 口径", size=10, color=C_MUTED, bold=True, spc=0.8)
    _hairline(slide, MARGIN, 1.98, CONTENT_W, color=C_INK, weight=0.014)

    row_h = 1.02
    y0 = 1.98
    for i in range(len(spec["rows"])):
        if i % 2 == 1:
            _add_shape(slide, MSO_SHAPE.RECTANGLE, MARGIN, y0 + i * row_h,
                       CONTENT_W, row_h, fill=C_CARD, line=None)
    y = y0
    for i, (cat, metrics, cond) in enumerate(spec["rows"]):
        _add_text(slide, x_cat, y + 0.30, col_cat - 0.2, 0.28, cat,
                  size=15, color=C_INK, bold=True)
        _add_shape(slide, MSO_SHAPE.RECTANGLE, x_cat, y + 0.68, 1.05, 0.032,
                   fill=C_TEAL)
        # 指标：名字 + 数值混排
        runs = []
        for j, (label, value) in enumerate(metrics):
            if j:
                runs.append({"t": "　｜　", "size": 10, "color": C_LINE})
            pending = "待测" in value
            runs.append({"t": label + " ", "size": 12, "color": C_BODY})
            runs.append({"t": value, "size": 17, "bold": True,
                         "color": C_AMBER if pending else C_INK})
        _add_text_shapes(slide, x_main, y + 0.32, col_main - 0.2, 0.62, runs,
                         size=12, anchor=MSO_ANCHOR.TOP)
        _add_text(slide, x_cond, y + 0.36, col_cond - 0.1, 0.56, cond,
                  size=10, color=C_MUTED, line_spacing=1.0)
        y += row_h
    for i in range(len(spec["rows"]) + 1):
        _hairline(slide, MARGIN, y0 + i * row_h, CONTENT_W,
                  color=C_LINE if i else C_INK,
                  weight=0.010 if i else 0.014)

    _warn_note(slide, MARGIN, 6.20, CONTENT_W, 0.48, spec["note"], size=10.5)
    _footer(slide, page_no, total)
    return []


# --------------------------------------------------------------------------- #
# 版式：可靠性与安全（P14）
# --------------------------------------------------------------------------- #


def build_reliability(prs, spec, page_no, total):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    _add_shape(slide, MSO_SHAPE.RECTANGLE, 0, 0, SLIDE_W, SLIDE_H, fill=C_BG)
    _header(slide, spec)

    # 两张长稳卡
    gap = 0.22
    sw = (CONTENT_W - gap) / 2.0
    for i, (value, label, note) in enumerate(spec["stats"]):
        x = MARGIN + i * (sw + gap)
        _card(slide, x, 1.74, sw, 1.66)
        _add_text(slide, x + 0.28, 1.88, sw - 0.5, 0.24, label,
                  size=10.5, color=C_MUTED, bold=True, spc=0.6)
        _add_text(slide, x + 0.28, 2.14, sw - 0.5, 0.56, value,
                  size=27, color=C_INK, bold=True)
        _add_text(slide, x + 0.28, 2.76, sw - 0.5, 0.50, note,
                  size=10, color=C_FAINT, line_spacing=1.05)

    # 安全设计
    _section_label(slide, MARGIN, 3.62, "安全设计")
    gap2 = 0.16
    cw = (CONTENT_W - 3 * gap2) / 4.0
    for i, (lead, detail) in enumerate(spec["safety"]):
        x = MARGIN + i * (cw + gap2)
        _card(slide, x, 3.94, cw, 1.22, fill=C_CARD_ALT, line=None, radius=0.06)
        _add_text(slide, x + 0.22, 4.06, cw - 0.4, 0.26, lead,
                  size=13, color=C_INK, bold=True)
        _add_text(slide, x + 0.22, 4.36, cw - 0.4, 0.74, detail,
                  size=9.5, color=C_BODY, line_spacing=1.02)

    _takeaway(slide, 5.40, spec["takeaway"], size=14)
    _warn_note(slide, MARGIN, 6.12, CONTENT_W, 0.50, spec["warn"], size=10.5)
    _footer(slide, page_no, total)
    return []


# --------------------------------------------------------------------------- #
# 版式：创新点（P15）
# --------------------------------------------------------------------------- #


def _add_evidence(slide, x, y, w, h, text):
    """创新点卡的证据段：\n 分隔多行，手动断行避免断在词里。"""
    lines = text.split("\n")
    _add_paras(slide, x, y, w, h,
               [dict(runs=[{"t": t}], size=10.5, color=C_BODY) for t in lines])


def build_innovations(prs, spec, page_no, total):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    _add_shape(slide, MSO_SHAPE.RECTANGLE, 0, 0, SLIDE_W, SLIDE_H, fill=C_BG)
    _header(slide, spec)

    cards = spec["cards"]
    # 第一行 3 张，第二行 2 张
    top = cards[:3]
    bottom = cards[3:]
    gap = 0.255
    w3 = (CONTENT_W - 2 * gap) / 3.0
    for i, (num, title, evidence) in enumerate(top):
        x = MARGIN + i * (w3 + gap)
        _card(slide, x, 1.72, w3, 1.76)
        _add_text(slide, x + 0.26, 1.82, 0.7, 0.30, num,
                  size=17, color=C_TEAL, bold=True, spc=1.5)
        _add_text(slide, x + 0.26, 2.12, w3 - 0.5, 0.32, title,
                  size=15.5, color=C_INK, bold=True, line_spacing=1.0)
        _add_text(slide, x + 0.26, 2.50, 1.2, 0.18, "证据",
                  size=8.5, color=C_MUTED, bold=True, spc=1.6)
        _add_evidence(slide, x + 0.26, 2.70, w3 - 0.5, 0.70, evidence)

    w2 = (CONTENT_W - gap) / 2.0
    for i, (num, title, evidence) in enumerate(bottom):
        x = MARGIN + i * (w2 + gap)
        _card(slide, x, 3.66, w2, 1.76)
        _add_text(slide, x + 0.26, 3.76, 0.7, 0.28, num,
                  size=17, color=C_TEAL, bold=True, spc=1.5)
        _add_text(slide, x + 0.26, 4.06, w2 - 0.5, 0.32, title,
                  size=16, color=C_INK, bold=True)
        _add_text(slide, x + 0.26, 4.44, 1.2, 0.18, "证据",
                  size=8.5, color=C_MUTED, bold=True, spc=1.6)
        _add_evidence(slide, x + 0.26, 4.64, w2 - 0.5, 0.80, evidence)

    _takeaway(slide, 5.98, spec["takeaway"], h=0.60, size=16)
    _footer(slide, page_no, total)
    return []


# --------------------------------------------------------------------------- #
# 版式：总结（P16）
# --------------------------------------------------------------------------- #


def build_summary(prs, spec, page_no, total):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    _add_shape(slide, MSO_SHAPE.RECTANGLE, 0, 0, SLIDE_W, SLIDE_H, fill=C_BG)
    _header(slide, spec)

    cols = spec["columns"]
    gap = 0.255
    w = (CONTENT_W - (len(cols) - 1) * gap) / len(cols)
    for i, (head, color, items) in enumerate(cols):
        x = MARGIN + i * (w + gap)
        _add_shape(slide, MSO_SHAPE.RECTANGLE, x, 1.74, w, 0.055, fill=color)
        _add_text(slide, x, 1.88, w, 0.32, head, size=14, color=C_INK, bold=True)
        _dot_list(slide, x, 2.34, w - 0.1, items, size=12.5, gap=0.28,
                  dot_color=color)

    # 收束条
    _card(slide, MARGIN, 5.55, CONTENT_W, 1.15, fill=C_NAVY, line=None, radius=0.10)
    _add_text(slide, MARGIN + 0.34, 5.55, CONTENT_W - 2.6, 1.15,
              spec["closing"], size=18, color=C_WHITE, bold=True,
              anchor=MSO_ANCHOR.MIDDLE)
    _add_text(slide, SLIDE_W - MARGIN - 2.4, 5.55, 2.0, 1.15,
              spec["thanks"], size=14, color=C_COVER_TEAL,
              align=PP_ALIGN.RIGHT, anchor=MSO_ANCHOR.MIDDLE)
    _footer(slide, page_no, total)
    return []


# --------------------------------------------------------------------------- #
# 组装
# --------------------------------------------------------------------------- #

BUILDERS = {
    "cover": build_cover,
    "pain": build_pain,
    "overview": build_overview,
    "figure_side": build_figure_side,
    "figure_full": build_figure_full,
    "pipeline": build_pipeline,
    "ai": build_ai,
    "hmi": build_hmi,
    "can": build_can,
    "metrics": build_metrics,
    "reliability": build_reliability,
    "innovations": build_innovations,
    "summary": build_summary,
}


def _set_notes(slide, lines):
    tf = slide.notes_slide.notes_text_frame
    tf.clear()
    for i, line in enumerate(lines):
        p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
        p.space_after = Pt(4)
        run = p.add_run()
        run.text = line
        run.font.size = Pt(12)
        run.font.name = FONT_CN


THEME = dict(
    bg=[0xF7, 0xF8, 0xFA],
    cover_bg=[0x08, 0x17, 0x26],
    rule_bottom=round(RULE_Y + 0.03, 3),
    body_top=BODY_TOP,
    content_bottom=BODY_BOTTOM,
    footer_y=FOOT_RULE_Y,
    slide_w=SLIDE_W,
    slide_h=SLIDE_H,
)


def build(out_path=OUT_PPTX):
    specs = slide_specs()
    total = len(specs)
    prs = Presentation()
    prs.slide_width = Inches(SLIDE_W)
    prs.slide_height = Inches(SLIDE_H)

    manifest = []
    if OLD_ZOOM_DIR.is_dir():                # 旧版放大裁切图已弃用，重跑时清掉
        shutil.rmtree(OLD_ZOOM_DIR)

    for i, spec in enumerate(specs, start=1):
        builder = BUILDERS[spec["kind"]]
        figs = builder(prs, spec, i, total)
        for fig in figs:
            fig["slide"] = i
            fig["role"] = "figure"
            manifest.append(fig)
        _set_notes(prs.slides[-1], spec["notes"])

    out_path = Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    prs.save(str(out_path))
    OUT_MANIFEST.write_text(
        json.dumps(dict(slide_w=SLIDE_W, slide_h=SLIDE_H, theme=THEME,
                        figures=manifest), ensure_ascii=False, indent=1),
        encoding="utf-8")
    return out_path, prs, specs


# --------------------------------------------------------------------------- #
# 自检（读回生成的 pptx：结构 / 禁项 / 越界 / 文本溢出估算 / 插图 dpi）
# --------------------------------------------------------------------------- #

# 禁项：学校名称与指导老师信息（submission/提交材料清单.md §6 检查表）。
# 「全国大学生嵌入式芯片与系统设计竞赛」是官方赛事名，必须原样出现 → 先整串剔除再查。
FORBIDDEN = ("大学", "学院", "指导老师", "指导教师", "导师", "校徽")
EVENT_ALLOWLIST = (EVENT_LINE, "全国大学生嵌入式芯片与系统设计竞赛")


def _shape_text(shapes):
    texts = []
    for shape in shapes:
        if shape.has_text_frame:
            texts.append(shape.text_frame.text)
        if getattr(shape, "has_table", False) and shape.has_table:
            for row in shape.table.rows:
                for cell in row.cells:
                    texts.append(cell.text)
    return "\n".join(texts)


def _text_need_height(tf, width_in):
    """按字号与字宽估算文本框需要的总高度（含分段间距）。"""
    total = 0.0
    for p in tf.paragraphs:
        runs = [r for r in p.runs if r.text]
        if not runs:
            total += _line_h(12) * 0.6
            continue
        size = max((r.font.size.pt if r.font.size else 12) for r in runs)
        width_units = sum(_text_em(r.text)
                          * ((r.font.size.pt if r.font.size else 12) / size)
                          for r in runs)
        ls = p.line_spacing if isinstance(p.line_spacing, float) else 1.0
        per_line = max(0.5, width_in * 72.0 / size)
        lines = max(1, int(math.ceil(width_units / per_line - 1e-6)))
        total += lines * _line_h(size, ls)
        sb = p.space_before.pt if p.space_before is not None else 0.0
        sa = p.space_after.pt if p.space_after is not None else 0.0
        total += (sb + sa) / 72.0
    return total


def verify(path, specs):
    from pptx import Presentation as _P

    prs = _P(str(path))
    width_in = prs.slide_width / 914400
    height_in = prs.slide_height / 914400
    slides = list(prs.slides)

    print("=" * 100)
    print("文件      : %s" % path)
    print("相对路径  : %s" % _rel(path))
    print("尺寸      : %.3f x %.3f in（16:9 = %.4f，期望 %.4f）"
          % (width_in, height_in, width_in / height_in, 16 / 9))
    print("页数      : %d（期望 %d）" % (len(slides), len(specs)))
    print("-" * 100)

    problems = []
    pics_total = notes_total = chars_total = 0
    for idx, slide in enumerate(slides, start=1):
        shapes = list(slide.shapes)
        pics = [s for s in shapes if s.shape_type == 13]
        pics_total += len(pics)
        joined = _shape_text(shapes)

        notes = slide.notes_slide.notes_text_frame.text.strip()
        notes_total += 1 if notes else 0
        chars_total += len(notes)
        if not notes:
            problems.append("第 %d 页没有 speaker notes" % idx)

        audit = joined
        for allowed in EVENT_ALLOWLIST:
            audit = audit.replace(allowed, "")
        for bad in FORBIDDEN:
            if bad in audit:
                problems.append("第 %d 页正文出现禁项「%s」" % (idx, bad))
        for bad in ("C:\\", "D:\\", "C:/", "D:/"):
            if bad in joined:
                problems.append("第 %d 页出现主机绝对路径" % idx)

        # 形状越界（整幅背景/装饰形状允许出血，不算越界）
        for shape in shapes:
            if shape.left is None or shape.width is None:
                continue
            sw = shape.width / 914400
            sh = shape.height / 914400
            if sw >= width_in - 0.05 and sh >= height_in - 0.05:
                continue
            left = shape.left / 914400
            top = shape.top / 914400
            right = left + sw
            bottom = top + sh
            if left < -0.02 or top < -0.02 or right > width_in + 0.02 \
                    or bottom > height_in + 0.02:
                problems.append("第 %d 页形状越界：L=%.2f T=%.2f R=%.2f B=%.2f"
                                % (idx, left, top, right, bottom))

        # 文本溢出估算
        for shape in shapes:
            if not shape.has_text_frame or not shape.text_frame.text.strip():
                continue
            tf = shape.text_frame
            ml = Pt(tf.margin_left.pt).inches if tf.margin_left is not None else 0.0
            mr = Pt(tf.margin_right.pt).inches if tf.margin_right is not None else 0.0
            mt = Pt(tf.margin_top.pt).inches if tf.margin_top is not None else 0.0
            mb = Pt(tf.margin_bottom.pt).inches if tf.margin_bottom is not None else 0.0
            avail_w = shape.width / 914400 - ml - mr
            avail_h = shape.height / 914400 - mt - mb
            need = _text_need_height(tf, avail_w)
            if need > avail_h + TEXT_FIT_TOL:
                problems.append(
                    "第 %02d 页文本可能溢出：需 %.2fin / 有 %.2fin（%s）"
                    % (idx, need, avail_h, tf.text.replace("\n", " / ")[:34]))

        # 插图原生 dpi
        for shape in pics:
            try:
                px_w = shape.image.size[0]
            except Exception:                # pragma: no cover
                continue
            dpi = px_w / (shape.width / 914400)
            if dpi < FIG_DPI_MIN:
                problems.append("第 %02d 页插图原生 dpi %.0f < %d"
                                % (idx, dpi, FIG_DPI_MIN))

        # 内容底沿：不能压到页脚线（整幅背景与页脚带内的形状除外）
        content_bottom = 0.0
        for shape in shapes:
            if shape.top is None or shape.height is None:
                continue
            sw = shape.width / 914400
            sh = shape.height / 914400
            top = shape.top / 914400
            if (sw >= width_in - 0.05 and sh >= height_in - 0.05) \
                    or top >= FOOT_RULE_Y - 0.001:
                continue
            bottom = top + sh
            if bottom > FOOT_RULE_Y + 0.005:
                content_bottom = max(content_bottom, bottom)
        if content_bottom > FOOT_RULE_Y + 0.005:
            problems.append("第 %d 页有内容压过页脚线：底沿 %.2fin" % (idx, content_bottom))

        print("第 %02d 页  形状 %2d（图片 %d）  备注 %3d 字  %s"
              % (idx, len(shapes), len(pics), len(notes),
                 notes.splitlines()[0][:40] if notes else "!! 无备注"))

    print("-" * 100)
    print("合计      : 幻灯片 %d 页；插图 %d 张；有备注 %d 页；备注合计 %d 字"
          % (len(slides), pics_total, notes_total, chars_total))
    if problems:
        print("自检问题  : %d 项" % len(problems))
        for p in problems:
            print("  ! %s" % p)
    else:
        print("自检问题  : 0（尺寸 / 页数 / 备注 / 禁项 / 主机路径 / 越界 / "
              "文本溢出估算 / 插图 dpi / 页脚安全区）")
    print("=" * 100)
    return len(slides), pics_total, notes_total, problems


def main(argv=None):
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:                        # pragma: no cover
        pass

    ap = argparse.ArgumentParser(description="生成龙瞳分拣答辩 PPT（16:9，16 页）")
    ap.add_argument("--out", default=str(OUT_PPTX),
                    help="输出 pptx 路径（默认 submission/out/龙瞳分拣_答辩.pptx）")
    ap.add_argument("--no-verify", action="store_true", help="跳过读回自检")
    args = ap.parse_args(argv)

    path, _prs, specs = build(Path(args.out))
    print("已生成：%s" % _rel(path))
    print("插图清单：%s" % _rel(OUT_MANIFEST))

    if not args.no_verify:
        _n, _p, _nt, problems = verify(path, specs)
        return 0 if not problems else 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
