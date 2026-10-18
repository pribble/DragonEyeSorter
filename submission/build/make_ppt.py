#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成决赛答辩 PPT 骨架：submission/out/龙瞳分拣_答辩.pptx

唯一源
------
* 页纲：``docs/参赛材料骨架.md`` §4（15 页提纲，本脚本拆为 16 页：把「架构」再拆出
  「数据流与时间基准」一页）。
* 数字：``docs/技术文档.md`` §1.3 与 ``docs/测试与验证.md``
  「已实测数据汇总」；**未实测的一律写「待测 ⚠」**，不估、不编。
* 插图：``docs/figures/*.png``（由 ``docs/figures/make_figures.py`` 生成，不手工画）。

用法
----
    python submission/build/make_ppt.py            # 生成 + 读回自检（有问题退出码 1）
    python submission/build/make_ppt.py --no-verify

脚本用 ``pathlib`` 相对 ``__file__`` 定位仓库根，不写任何主机绝对路径。
本机 python-pptx 版本见 ``pip show python-pptx``；不锁定 python 解释器路径。

版式（可复跑度量）
------------------
每页内容块（标题 / 正文块 / 图 / 表，不含页脚）的 ink bbox 都记进 ``Page.rects``，
生成后打印成表：顶档、头档（标题分隔线到正文的间距）、底档、纵向填充率、面积填充率。
门禁（不过即退出码 1）：

* 顶档 ≤ 1.20in、头档 ≤ 0.60in、底档 ≤ 1.20in、面积填充率 65%~85%（封面豁免）；
* 表格拉满版心宽（≥ 版心 98%）、表内字号 ≥ 14pt、单元格不折行；
* 插图按源图像素放置 → 原生 dpi ≥ 300（本版 3200px / 7.4in ≈ 430dpi）。

密集插图页（第 4/5/6/8/9 页）
---------------------------
``docs/figures`` 的图是 3200px 宽、图内文字只有 7~9pt（SVG 口径），整图缩到 7.4in 宽后
图内文字等效只有 3~4pt——投影距离不可读。因此这几页改成「全貌缩略 + 局部放大面板」：

* 缩略图（左列，4.35in 宽）保留全貌，并用青框标出放大区；
* 放大面板（右列，7.41in）由 Pillow 从源 PNG 裁出关键区（``zoom.rect``，源图像素坐标），
  只写到 ``submission/out/zoom/``（不入库，每次重建）；
* ``zoom.rect`` 的依据写在每页 ``zoom["why"]`` 里；等效字号 = 7.5pt(SVG) × 源图缩放 ×
  面板宽/裁切宽，脚本按 ≥ 11pt 门禁；实测值打印在度量表里。

规矩
----
* 封面只有作品名 + 赛事 / 赛道 / 选题 + 日期，**不出现学校名称与指导老师姓名**；
* 每页都写 speaker notes，并注明数字出处（``docs/测试与验证.md`` / ``docs/log/``）；
* 生成物在 ``submission/out/``（已 gitignore），可随时重建；另写
  ``submission/out/_ppt_images.json``（每页插图清单：源图 / 放置矩形 / 原生 dpi / PDF 上采样倍数），
  供 ``export_ppt_preview.ps1`` 的 PDF 高清化步骤按页对位替换。
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import sys
import unicodedata
from pathlib import Path

try:
    from pptx import Presentation
    from pptx.dml.color import RGBColor
    from pptx.enum.shapes import MSO_SHAPE
    from pptx.enum.text import MSO_ANCHOR, PP_ALIGN
    from pptx.util import Emu, Inches, Pt
except ImportError:  # pragma: no cover - 环境问题，给出可操作提示
    sys.exit(
        "缺少 python-pptx。安装：python -m pip install python-pptx\n"
        "（本仓库运行环境已带 python-pptx，见 submission/PPT制作指南.md）"
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
OUT_DIR = REPO_ROOT / "submission" / "out"
ZOOM_DIR = OUT_DIR / "zoom"
OUT_PPTX = OUT_DIR / "龙瞳分拣_答辩.pptx"
OUT_MANIFEST = OUT_DIR / "_ppt_images.json"

SLIDE_W_IN = 13.3333
SLIDE_H_IN = 7.5

# 事件与作品标识（封面用；赛事口径见 submission/提交材料清单.md §1）
WORK_NAME = "龙瞳分拣"
WORK_NAME_EN = "DragonEyeSorter"
EVENT_LINE_1 = "2026 全国大学生嵌入式芯片与系统设计竞赛"
EVENT_LINE_2 = "FPGA 创新设计赛道 · 安路选题四"
DATE_LINE = "2026-10-18"

# 设计令牌（深蓝底 + 青色强调；投影对比度见 submission/PPT制作指南.md §4）
C_BG = RGBColor(0x0B, 0x25, 0x45)          # 深蓝底
C_BG_COVER = RGBColor(0x07, 0x1B, 0x33)    # 封面更深
C_STRIP = RGBColor(0x10, 0x33, 0x5C)       # 色带
C_ACCENT = RGBColor(0x22, 0xD3, 0xEE)      # 青色强调
C_ACCENT_DIM = RGBColor(0x1B, 0x8F, 0xA8)
C_TITLE = RGBColor(0xFF, 0xFF, 0xFF)
C_BODY = RGBColor(0xE8, 0xF0, 0xF8)
C_MUTED = RGBColor(0x93, 0xB2, 0xCE)
C_CARD = RGBColor(0xFF, 0xFF, 0xFF)        # 插图卡片底（图本身是白底，避免糊在深蓝上）
C_CARD_LINE = RGBColor(0xC7, 0xD6, 0xE5)
C_TBL_HEAD = RGBColor(0x14, 0x3C, 0x6B)
C_TBL_BODY = RGBColor(0x0E, 0x2E, 0x54)
C_TBL_LINE = RGBColor(0x2A, 0x55, 0x82)

FONT_CN = "微软雅黑"
FONT_MONO = "Consolas"

# --------------------------------------------------------------------------- #
# 版式令牌（英寸）：版面度量与自检都按这几个数算，改这里就改全篇
# --------------------------------------------------------------------------- #

MARGIN_X = 0.62
CONTENT_W = SLIDE_W_IN - 2 * MARGIN_X      # 12.0933 = 版心宽
TITLE_X = MARGIN_X
TITLE_Y = 0.38
TITLE_H = 0.62
HEADER_STRIP_Y = 0.44
RULE_Y = 1.16
RULE_H = 0.035
BODY_TOP = 1.44                            # 正文区上沿（分隔线下方 0.245in）
BODY_BOTTOM = 6.94                         # 正文区下沿 → 底档 = 7.5-6.94 = 0.56in
CAPTION_Y = 6.62                           # 有图注的页：图注占最后一行
CAPTION_H = 0.32
PANEL_BOTTOM = 6.56                        # 有图注的页：图区下沿
FOOTER_Y = 7.08
BLOCK_GAP = 0.34                           # 内容块之间的固定间距
LINE_FACTOR = 1.34                         # 微软雅黑单倍行高 ≈ 1.34×字号（渲染实测校准）

SIZE_TITLE = 28
SIZE_FOOTER = 10
SIZE_TABLE_HEAD = 16                       # 表头 ≥14pt（门禁），本版 16pt
SIZE_TABLE_BODY = 16                       # 表内正文 ≥14pt（门禁），本版 16pt
SIZE_TAKEAWAY = 20
SIZE_BULLET_WIDE = 24                      # 纯文字页（要点列宽 = 版心）
SIZE_BULLET_TABLE = 22                     # 表格页（要点在表格下方，占版心宽）
SIZE_BULLET_FIG = 18                       # 图页（要点在 4.35in 窄列）
SIZE_ZOOM_LABEL = 13
SIZE_THUMB_LABEL = 11

TABLE_ROW_MIN, TABLE_ROW_MAX = 0.48, 0.95  # 表格行高区间（16pt 字最小行高约 0.35in）
CAPTION_LEFT_W = 4.35                      # 图页左列宽（要点 + 全貌缩略）
PANEL_LEFT = MARGIN_X + CAPTION_LEFT_W + 0.33
PANEL_W = MARGIN_X + CONTENT_W - PANEL_LEFT

# 版面目标 / 门禁
GAP_TOP_MAX = 1.20
GAP_HEAD_MAX = 0.60
GAP_BOTTOM_MAX = 1.20
FILL_MIN, FILL_MAX = 0.65, 0.85
TABLE_W_MIN_RATIO = 0.98
FIG_DPI_MIN = 300
ZOOM_TEXT_PT_MIN = 11.0
SVG_PT_TO_PX = 200 / 72.0                  # docs/figures 的 PNG 由 dpi=200 导出：1pt(SVG) = 2.78px
FIG_BODY_PT = 7.5                          # 图内正文的最小字号（SVG 口径，见 make_figures.py）
LATIN_EM = 0.75                            # 拉丁 / 数字字符相对中文字宽的估算系数（实渲校准）
FIG_ROLES = {}                             # {(页,x,y): 插图清单条目}，见 verify()
ZOOM_NOTES = {}                            # {页: [锚点核对结果]}，打印原始证据用


# --------------------------------------------------------------------------- #
# 版式原语
# --------------------------------------------------------------------------- #


class Page:
    """一页幻灯片 + 内容块 ink 矩形登记表（供版面度量与自检使用）。"""

    def __init__(self, slide):
        self.slide = slide
        self.rects = []          # [(x, y, w, h)] 内容墨迹 bbox（不含页脚与装饰）
        self.figures = []        # 插图清单（写 _ppt_images.json 用）
        self.zoom_notes = []     # 放大区锚点核对结果（原始证据，打印用）

    # -- 登记 ------------------------------------------------------------- #
    def note(self, x, y, w, h):
        self.rects.append((float(x), float(y), float(w), float(h)))
        return (x, y, w, h)

    def note_figure(self, *, role, src, x, y, w, h, px_w, px_h, pdf_scale=1.0):
        self.figures.append(dict(
            role=role, src=src, rect_in=[round(float(v), 4) for v in (x, y, w, h)],
            px=[int(px_w), int(px_h)], pdf_scale=float(pdf_scale)))
        self.note(x, y, w, h)

    # -- 度量 ------------------------------------------------------------- #
    def metrics(self, *, has_caption=False):
        if not self.rects:
            return None
        top = min(r[1] for r in self.rects)
        bottom = max(r[1] + r[3] for r in self.rects)
        left = min(r[0] for r in self.rects)
        right = max(r[0] + r[2] for r in self.rects)
        body = [r for r in self.rects if r[1] > RULE_Y + RULE_H + 0.01]
        head = (min(r[1] for r in body) - (RULE_Y + RULE_H)) if body else None
        return dict(
            top_gap=top, head_gap=head, bottom_gap=SLIDE_H_IN - bottom,
            fill_h=(bottom - top) / SLIDE_H_IN,
            fill_area=(right - left) * (bottom - top) / (SLIDE_W_IN * SLIDE_H_IN),
        )


def _add_rect(slide, *, x, y, w, h, fill, line=None, shape=MSO_SHAPE.RECTANGLE,
              line_w=Pt(0.75)):
    shp = slide.shapes.add_shape(shape, Inches(x), Inches(y), Inches(w), Inches(h))
    shp.shadow.inherit = False
    if fill is None:
        shp.fill.background()
    else:
        shp.fill.solid()
        shp.fill.fore_color.rgb = fill
    if line is None:
        shp.line.fill.background()
    else:
        shp.line.color.rgb = line
        shp.line.width = line_w
    if shp.has_text_frame:
        shp.text_frame.word_wrap = True
    return shp


def _add_textbox(slide, *, x, y, w, h, anchor=MSO_ANCHOR.TOP):
    box = slide.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
    tf = box.text_frame
    tf.word_wrap = True
    tf.vertical_anchor = anchor
    tf.margin_left = 0
    tf.margin_right = 0
    tf.margin_top = 0
    tf.margin_bottom = 0
    tf.clear()
    return box, tf


def _style_run(run, *, size, color, bold=False, mono=False, italic=False):
    run.font.size = Pt(size)
    run.font.bold = bold
    run.font.italic = italic
    run.font.color.rgb = color
    run.font.name = FONT_MONO if mono else FONT_CN
    return run


def _paragraph(tf, *, first=False):
    return tf.paragraphs[0] if first else tf.add_paragraph()


def _line_h(size, line_spacing=1.0):
    """单倍行高（英寸）。微软雅黑实际行高 ≈ 1.34×字号（渲染实测校准）。"""
    return size * LINE_FACTOR * line_spacing / 72.0


def _add_title(page, text):
    slide = page.slide
    _add_rect(slide, x=TITLE_X, y=HEADER_STRIP_Y, w=0.075, h=0.46, fill=C_ACCENT)
    box, tf = _add_textbox(slide, x=TITLE_X + 0.19, y=TITLE_Y, w=CONTENT_W - 0.19,
                           h=TITLE_H, anchor=MSO_ANCHOR.MIDDLE)
    _style_run(tf.paragraphs[0].add_run(), size=SIZE_TITLE, color=C_TITLE, bold=True)
    tf.paragraphs[0].runs[0].text = text
    _add_rect(slide, x=TITLE_X, y=RULE_Y, w=CONTENT_W, h=RULE_H, fill=C_ACCENT_DIM)
    # 标题 ink：色条 + 标题行 + 分隔线
    page.note(TITLE_X, HEADER_STRIP_Y, 0.075, 0.46)
    page.note(TITLE_X + 0.19, TITLE_Y + 0.06, CONTENT_W - 0.19, TITLE_H - 0.12)
    page.note(TITLE_X, RULE_Y, CONTENT_W, RULE_H)


def _add_footer(slide, page_no, total):
    box, tf = _add_textbox(slide, x=TITLE_X, y=FOOTER_Y, w=CONTENT_W, h=0.28)
    p = tf.paragraphs[0]
    p.alignment = PP_ALIGN.RIGHT
    run = p.add_run()
    run.text = "%s · %s ｜ %02d / %02d" % (WORK_NAME, WORK_NAME_EN, page_no, total)
    _style_run(run, size=SIZE_FOOTER, color=C_MUTED)


def _add_bullets(page, bullets, *, x, y, w, size, space_after, line_spacing=1.0):
    box, tf = _add_textbox(page.slide, x=x, y=y, w=w,
                           h=len(bullets) * _line_h(size, line_spacing) + 0.10)
    for i, item in enumerate(bullets):
        p = _paragraph(tf, first=(i == 0))
        p.space_after = Pt(space_after)
        p.line_spacing = line_spacing
        mark = p.add_run()
        mark.text = "▪ "
        _style_run(mark, size=size, color=C_ACCENT, bold=True)
        body = p.add_run()
        body.text = item
        _style_run(body, size=size, color=C_BODY)
    ink_h = bullets_height(len(bullets), size, space_after, line_spacing)
    page.note(x, y, w, ink_h)
    return ink_h


def bullets_height(n, size, space_after_pt, line_spacing=1.0):
    """要点块 ink 高度：n 行 + (n-1) 段后距（最后一段的段后距不产生墨迹）。"""
    return n * _line_h(size, line_spacing) + max(0, n - 1) * space_after_pt / 72.0


def _solve_space_after(n, size, avail_in, *, lo=8.0, hi=30.0, line_spacing=1.0):
    """解出让 n 条要点正好占满 avail_in 的段后距（pt），并夹在 [lo, hi]。"""
    if n <= 1:
        return lo
    line = _line_h(size, line_spacing)
    s = (avail_in - n * line) / (n - 1) * 72.0
    return max(lo, min(hi, s))


def _add_takeaway(page, text, *, y, h=0.66, size=SIZE_TAKEAWAY):
    slide = page.slide
    _add_rect(slide, x=TITLE_X, y=y, w=0.075, h=h, fill=C_ACCENT)
    _add_rect(slide, x=TITLE_X, y=y, w=CONTENT_W, h=h, fill=C_STRIP)
    box, tf = _add_textbox(slide, x=TITLE_X + 0.30, y=y, w=CONTENT_W - 0.70,
                           h=h, anchor=MSO_ANCHOR.MIDDLE)
    run = tf.paragraphs[0].add_run()
    run.text = text
    _style_run(run, size=size, color=C_ACCENT, bold=True)
    page.note(TITLE_X, y, CONTENT_W, h)
    return h


def _add_caption(page, caption, *, y=CAPTION_Y, x=TITLE_X, w=CONTENT_W):
    """图注：按长度缩号（12/11/10pt），保证一行放得下、不顶到页脚。"""
    size = 12 if len(caption) <= 44 else (11 if len(caption) <= 56 else 10)
    box, tf = _add_textbox(page.slide, x=x, y=y, w=w, h=CAPTION_H,
                           anchor=MSO_ANCHOR.MIDDLE)
    p = tf.paragraphs[0]
    p.alignment = PP_ALIGN.CENTER
    run = p.add_run()
    run.text = caption
    _style_run(run, size=size, color=C_MUTED)
    page.note(x, y, w, CAPTION_H)


def image_size(name):
    path = FIGURES_DIR / name
    if not path.is_file():
        raise FileNotFoundError(
            "插图缺失：%s（应由 docs/figures/make_figures.py 生成）" % path)
    with Image.open(path) as im:
        return im.width, im.height


def _rel(path):
    p = Path(path)
    try:
        return p.resolve().relative_to(REPO_ROOT).as_posix()
    except ValueError:                             # pragma: no cover - 极端情况
        return p.name


_SVG_TEXT_RE = re.compile(r"<text\b([^>]*)>(.*?)</text>", re.S)
_SVG_TAG_RE = re.compile(r"<[^>]+>")
_SVG_TR_RE = re.compile(r'transform="translate\(([-0-9.eE]+)[ ,]+([-0-9.eE]+)\)"')


def svg_labels(stem):
    """读 docs/figures/<stem>.svg 的文字坐标（源图像素）。

    matplotlib 导出的 SVG 里文字是真的 ``<text>``：① ``transform="translate(x y)"``
    或 ② ``x=``/``y=`` 属性（轴标签那种）。SVG 坐标空间 = figure 英寸 × 72，
    源 PNG 由 dpi=200 导出 → 像素 = 坐标 × 200/72。
    """
    path = FIGURES_DIR / (stem + ".svg")
    if not path.is_file():
        return []
    svg = path.read_text(encoding="utf-8")
    out = []
    for m in _SVG_TEXT_RE.finditer(svg):
        attrs, inner = m.group(1), m.group(2)
        t = re.sub(r"\s+", " ", _SVG_TAG_RE.sub("", inner)).strip()
        tr = _SVG_TR_RE.search(attrs)
        if tr:
            x, y = float(tr.group(1)), float(tr.group(2))
        else:
            xa = re.search(r'\bx="([-0-9.eE]+)"', attrs)
            ya = re.search(r'\by="([-0-9.eE]+)"', attrs)
            if not (xa and ya):
                continue
            x, y = float(xa.group(1)), float(ya.group(1))
        out.append((x * SVG_PT_TO_PX, y * SVG_PT_TO_PX, t))
    return out


def check_zoom_anchors(name, zoom):
    """核对「裁切区里确实含关键标签」——防呆：目测估坐标很容易裁错地方。"""
    x0, y0, x1, y1 = zoom["rect"]
    labels = svg_labels(Path(name).stem)
    if not labels:
        return ["（%s 没有 SVG，跳过锚点核对）" % name]
    notes = []
    for anchor in zoom.get("anchors", []):
        hits = [(x, y, t) for x, y, t in labels if anchor in t]
        if not hits:
            raise ValueError("放大区锚点核对失败：%s 的 SVG 里找不到标签「%s」" % (name, anchor))
        x, y, t = hits[0]
        if not (x0 <= x <= x1 and y0 <= y <= y1):
            raise ValueError(
                "放大区锚点核对失败：%s 的「%s」在 px(%.0f,%.0f)，不在裁切区 %r 内"
                % (name, anchor, x, y, zoom["rect"]))
        notes.append("%s@px(%.0f,%.0f)" % (anchor, x, y))
    return notes


def make_zoom(name, rect):
    """从 docs/figures/<name> 裁出 rect=(x0,y0,x1,y1)（源图像素），写到 submission/out/zoom/。    只写 out/（不入库），每次重跑重建；白底合成，避免 RGBA 在 PDF 里需要 SMask。
    """
    x0, y0, x1, y1 = rect
    src = FIGURES_DIR / name
    with Image.open(src) as im:
        im = im.convert("RGBA")
        box = (int(x0), int(y0), int(x1), int(y1))
        crop = im.crop(box)
        bg = Image.new("RGB", crop.size, (255, 255, 255))
        bg.paste(crop, mask=crop.split()[3])
    ZOOM_DIR.mkdir(parents=True, exist_ok=True)
    out = ZOOM_DIR / ("%s_%d-%d-%d-%d.png" % (Path(name).stem, box[0], box[1], box[2], box[3]))
    bg.save(out, format="PNG", optimize=True)
    return out, bg.width, bg.height


def _add_table(page, data, *, x, y, w, row_h, col_ratio,
               size=SIZE_TABLE_BODY, head_size=SIZE_TABLE_HEAD):
    """表格。col_ratio 必须给：python-pptx 不会按 add_table 的 width 自动分配列宽，
    不给列宽时表格会缩成默认宽度（内容换行、纵向溢出页面）。"""
    rows, cols = len(data), len(data[0])
    gf = page.slide.shapes.add_table(rows, cols, Inches(x), Inches(y), Inches(w),
                                     Inches(row_h * rows))
    table = gf.table
    if abs(sum(col_ratio) - 1.0) > 1e-6:
        raise ValueError("col_ratio 必须归一化到 1.0，当前 = %r" % (sum(col_ratio),))
    for i, r in enumerate(col_ratio):
        table.columns[i].width = Emu(int(Inches(w) * r))
    for r, row in enumerate(data):
        table.rows[r].height = Inches(row_h)
        for c, text in enumerate(row):
            cell = table.cell(r, c)
            cell.fill.solid()
            cell.fill.fore_color.rgb = C_TBL_HEAD if r == 0 else C_TBL_BODY
            cell.margin_left = Inches(0.10)
            cell.margin_right = Inches(0.06)
            cell.margin_top = Inches(0.02)
            cell.margin_bottom = Inches(0.02)
            cell.vertical_anchor = MSO_ANCHOR.MIDDLE
            tf = cell.text_frame
            tf.word_wrap = True
            p = tf.paragraphs[0]
            run = p.add_run()
            run.text = text
            _style_run(run, size=head_size if r == 0 else size,
                       color=C_TITLE if r == 0 else C_BODY, bold=(r == 0))
    page.note(x, y, w, row_h * rows)
    return gf, row_h * rows


def col_ratios(data, *, pad_em=0.55, latin_em=LATIN_EM, min_ratio=0.09):
    """按列内文字宽度分配列宽比例（内容多的列宽、最后一列不留大块空白）。

    与自检门禁共用同一套字宽估算（``latin_em`` 默认 = ``LATIN_EM``），
    因此「分配出列宽 ≥ 门禁算出的需要宽度」——加宽一列不会把另一列挤到折行。
    """
    cols = len(data[0])
    widths = []
    for c in range(cols):
        em = max(_text_em(row[c], latin_em) for row in data)
        widths.append(em + pad_em)
    total = sum(widths)
    ratio = [max(min_ratio, v / total) for v in widths]
    total = sum(ratio)
    return [v / total for v in ratio]


def _set_notes(slide, lines):
    tf = slide.notes_slide.notes_text_frame
    tf.clear()
    for i, line in enumerate(lines):
        p = _paragraph(tf, first=(i == 0))
        p.space_after = Pt(4)
        run = p.add_run()
        run.text = line
        run.font.size = Pt(12)
        run.font.name = FONT_CN


# --------------------------------------------------------------------------- #
# 各页内容（页纲：docs/参赛材料骨架.md §4）
# 数字出处写进每页 notes；未实测写「待测 ⚠」。
# --------------------------------------------------------------------------- #

SRC_METRICS = "数字出处：docs/测试与验证.md「已实测数据汇总」"
SRC_TECH = "数字出处：docs/技术文档.md"
SRC_BELT = "数字出处：docs/log/2026-10-15-审查队友提交.md（硬件在环 6/6）"
SRC_CAM = "数字出处：docs/log/2026-10-10-相机60fps与PS骨架.md"
SRC_NPU = "数字出处：docs/log/2026-10-11-软件闭环_NPU_界面_PL视觉.md（test_npu 100 次）"
SRC_SERVO = "数字出处：docs/log/2026-10-10-自研PL逻辑上板.md"
SRC_HMI = "数字出处：docs/log/2026-10-16-HDMI周期性黑屏.md"
SRC_CAN = "数字出处：docs/log/2026-10-11-软件闭环_NPU_界面_PL视觉.md（板上回环）"
SRC_SOAK = "数字出处：docs/log/2026-10-11-软件闭环_NPU_界面_PL视觉.md（soak.sh 7200）"

# 局部放大区（源图像素坐标 x0,y0,x1,y1）+ 锚点标签（必须落在裁切区内，生成时核对）。
#
# 坐标怎么来的（可复跑，不是目测）：
#   ① docs/figures/*.svg 是 matplotlib 导出的**真文字**，每条文字都有 SVG 坐标
#      （1152x720pt 空间）；源 PNG 由 dpi=200 导出 → PNG 像素 = SVG 坐标 × 200/72 = ×2.7778；
#   ② 方框边界用「从锚点向外扩、遇连续白像素即出框」量出来（submission/out/_probe_box.py 的做法）；
#   ③ 裁切区按「面板 7.41in 宽 ÷ 裁切宽 px」定放大倍数：图内 7.5pt 文字放到面板上
#      等效 = 7.5 × 2.7778 × 7.41 ÷ 裁切宽px × 72，要 ≥11pt（门禁 ZOOM_TEXT_PT_MIN）。
#      这就是裁切宽上限 ≈ 970px、且裁切宽高比要 ≥ 面板宽高比 1.55（不然会被高度限制，
#      等效字号反而变小）的原因。
#   ④ 裁完还要看**墨迹密度**：整图里有些圆角矩形是空的（如架构图右侧那块橙色 NPU 块），
#      裁进来就是一个大空块。所以裁切区要选文字成片的地方，且避开空块（第 4 页的裁切区
#      右边界卡在橙色块左边界 1038 之前，就是为此）。
ZOOM_ARCH = dict(rect=(470, 1080, 1030, 1560),       # servo_bus + stepper_ctrl 两个自研模块段的寄存器窗口
                 anchors=["servo_bus 德晟协议主机", "stepper_ctrl"])
ZOOM_TIMEBASE = dict(rect=(40, 265, 1010, 600),      # 两次锁存刻度 + 卷帘读出/行插值公式框
                     anchors=["PL 锁存 帧号", "PL 锁存 POS1", "卷帘读出"])
ZOOM_PL_REGS = dict(rect=(1478, 366, 2460, 893),     # 0x1xx servo_bus / 0x2xx stepper_ctrl 两行窗口
                    anchors=["0x1xx servo_bus", "0x2xx stepper_ctrl"])
ZOOM_PICK = dict(rect=(1390, 770, 2380, 1400),       # 良品判定 → 停带目标 → 抓取线 → 机械臂动作
                 anchors=["良品过判定线", "停在抓取线", "下探"])
ZOOM_LAYOUT = dict(rect=(1650, 740, 2560, 1160),     # 相机视野参数 + 定位/测量/判定文字
                   anchors=["相机视野", "定位 · 亚像素测量"])


def slide_specs():
    """返回 16 页的 (标题, 要点, 图, 表, 备注, 图注)。"""
    return [
        # ---------------------------------------------------------------- 1
        dict(
            kind="cover",
            title=WORK_NAME,
            notes=[
                "开场：自报作品名与赛道，不出现学校与指导老师。",
                "一句话定位：塑胶齿轮 / 齿条在线质检分拣，安路飞龙 F3P SoC 单板完成看—判—量—跟—分。",
                "节奏：决赛介绍 5~8 分钟，本页 25~40 秒。",
                "赛事口径见 submission/提交材料清单.md §1；封面禁项见 §6 检查表。",
            ],
        ),
        # ---------------------------------------------------------------- 2
        dict(
            kind="bullets",
            title="场景与痛点",
            bullets=[
                "注塑塑胶传动件出模后需逐件质检",
                "缺料 / 飞边 / 黑点 / 尺寸超差 / 混料",
                "人工目检慢，一致性差、漏检难追溯",
                "常见视觉站只判不分，分拣靠人工",
                "要的是「看—判—量—跟—分」全链路",
            ],
            takeaway="一块板完成看—判—量—跟—分，不停线、不落地、可追溯",
            notes=[
                "痛点分两层：检测本身（小缺陷、多规格多颜色）与分拣（判完还要动）。",
                "人工目检的慢与不一致是产线节拍瓶颈；独立分拣设备增加一次上下料。",
                "本作品的落点：一块板把检测与分拣串成闭环。",
                "应用领域对应赛题「流水线视觉检测」（领域 3），融合多轴运动与机电控制（领域 1）。",
                "内容出处：docs/技术文档.md §1.1。",
            ],
        ),
        # ---------------------------------------------------------------- 3
        dict(
            kind="table",
            title="系统总览与指标",
            bullets=[
                "单板：双核 Cortex-A35 + NPU",
                "PL 硬实时、PS Linux、NPU 分类",
                "指标只念已实测的，未测标 ⚠",
            ],
            table=[
                ["类别", "指标", "实测值", "条件 / 来源"],
                ["控制同步", "6 轴 SYNC WRITE 一帧", "3298.9µs",
                 "理论 3298.6µs，PL 时间戳"],
                ["采集延迟", "采集帧率", "60.02fps",
                 "IMX415 1080p，20s 0 丢帧"],
                ["AI 推理", "NPU 单次推理", "7.8ms",
                 "224×224 UINT8，p95 7.84ms"],
                ["总线稳定性", "CAN 命令往返", "356µs（均）",
                 "板上回环，最大 429µs"],
            ],
            order="table_first",
            notes=[
                "总览页只讲三件事：什么板子、三层怎么分工、现在测到了什么。",
                "指标行只念已实测的；分类准确率与实物测量误差还没测，下一页起标「待测 ⚠」，不念数字。",
                SRC_METRICS + "；指标总表同 docs/技术文档.md §1.3。",
                "SYNC 与 CAN 两行分别是硬性①与硬性⑤的证据，答辩时专家常追问条件，条件随数字一起念。",
            ],
        ),
        # ---------------------------------------------------------------- 4
        dict(
            kind="figure",
            title="系统架构：PS / PL / NPU 分工",
            bullets=[
                "时序关键、逐比特 → PL",
                "算法迭代快 → PS（Linux）",
                "卷积矩阵运算 → NPU 软核",
                "AXI-Lite 寄存器窗口双向交换",
            ],
            figure="fig_architecture.png",
            zoom=dict(**ZOOM_ARCH,
                      label="局部放大：servo_bus / stepper_ctrl 自研模块段",
                      why="取 PL 列的自研模块段：servo_bus 段（补帧头 / 校验、SYNC WRITE、应答解析、"
                          "超时、时间戳）与 stepper_ctrl 段（梯形加减速、位置 / 速度模式、刹车距离、"
                          "首脉冲 10.6µs）——本页「时序关键放 PL」的举证区。右边界卡在橙色 NPU 块"
                          "（x≥1038）左边：那块是空框，裁进来只会是个大空块；左中部的 des_regs "
                          "长条（AXI-Lite 寄存器窗口）文字横跨到 x≈1363，含它就必须带上橙色空块，"
                          "所以在全貌缩略图里看它、不放大。裁 560×480px，图内 7.5pt 文字 → ≈14.9pt。"),
            caption="图 2-1 系统架构（左：全貌，青框＝右图；右：局部放大）",
            notes=[
                "主线一句话：把「必须确定延迟」的事情交给 PL，把「必须能改」的事情交给 PS，把卷积交给 NPU。",
                "PL 侧：舵机总线主机、步进控制、拍摄锁存、视觉预处理、导流挡板 PWM、现场输入。",
                "PS 侧：取帧显示线程（实时优先级）、工作线程（定位 / 测量 / 跟踪 / 调度 / 存储）、NPU 异步线程。",
                "分工的理由逐条对着赛题评分口径讲：全部功能仅 PS 实现要扣分，所以脉冲、总线帧、IO 采样都在 PL。",
                "内容出处：docs/技术文档.md §2.2、§4.1；评分口径见 docs/ref/安路赛道选题指南_2026.pdf p31~33。",
                "图源：docs/figures/fig_architecture.png（PPT 按源图原生 3200px 嵌入；右侧放大区由 make_ppt.py 的 zoom.rect 裁切，可调）。",
            ],
        ),
        # ---------------------------------------------------------------- 5
        dict(
            kind="figure",
            title="数据流与时间基准",
            bullets=[
                "FREE_CNT 与 POS 一个基准",
                "拍摄瞬间锁存帧号与带位置",
                "按图像行插值，带坐标 s = x-B",
                "结果晚到多少毫秒都不产生位置误差",
            ],
            figure="fig_timebase.png",
            zoom=dict(**ZOOM_TIMEBASE,
                      label="局部放大：两次锁存刻度 + 卷帘读出 16ms 行插值",
                      why="取时间轴左段：帧写开始 / 帧写完两处 PL 锁存刻度，以及「卷帘读出 ≈16ms、"
                          "第 r 行带位置 = POS0 + (POS1 − POS0)·r / H」公式框 —— 本页创新的机制区；"
                          "裁 970×335px，图内 7.5pt 文字 → 面板上 ≈11.5pt。"),
            caption="图 2-2 拍摄瞬间锁存带位置（左：全貌，青框＝右图；右：局部放大）",
            notes=[
                "这一页是全系统最关键的设计：位置基准不跟着处理延迟走。",
                "帧写模块开始写帧时锁存 FREE_CNT、帧号与步进位置 POS；写完再锁一次得到 CAP_*。",
                "相机是卷帘快门，不同图像行曝光时刻不同，PS 在两次锁存值之间按行线性插值，得该件被拍到时的带位置 B。",
                "零件不在带上滑动，带坐标 s 就不变，所以停带、变速、反转都不用特殊处理 —— 这是创新点 1。",
                "实测：锁存带位置差与「带速 × 读出时间」一致，帧号差全为 1。出处 docs/log/2026-10-10-相机60fps与PS骨架.md；" + SRC_TECH + " §2.3。",
                "图源：docs/figures/fig_timebase.png（右侧放大区 = 两次锁存刻度 + 卷帘读出 16ms 行插值公式）。",
            ],
        ),
        # ---------------------------------------------------------------- 6
        dict(
            kind="figure",
            title="PL：舵机总线主机 + 步进控制",
            bullets=[
                "德晟协议主机：补帧头与校验",
                "SYNC WRITE 一帧六轴同起跑",
                "解析 FF F5 应答与超时",
                "步进 Q0.32 速度与梯形加减速",
                "脉宽 5µs、换向 10µs、会刹车",
            ],
            figure="fig_pl_regs.png",
            zoom=dict(**ZOOM_PL_REGS,
                      label="局部放大：0x1xx servo_bus / 0x2xx stepper_ctrl 寄存器窗口",
                      why="整图缩到 7.41in 宽时图内文字等效只有 3.4pt（源图 3200px、SVG 字号 7.4pt），"
                          "投影距离不可读。裁 0x1xx servo_bus 与 0x2xx stepper_ctrl 两行寄存器窗口"
                          "（982×527px）——本页要点「补帧头与校验 / SYNC WRITE / FF F5 应答 / "
                          "Q0.32 梯形加减速」全在这两行里；放大后 7.4pt → 面板上 ≈11.2pt。"),
            caption="图 3-1 自研模块与寄存器窗口（左：全貌，青框＝右图；右：局部放大）",
            notes=[
                "硬性①的主场：PS 只写帧主体，帧头与校验和由 PL 现算，应答与超时也在 PL 判。",
                "开漏发送只主动拉低，高电平由 BSS138 电平转换模块上拉（舵机侧空闲 3.7V，不能直连 3.3V IO）。",
                "SYNC WRITE 一帧时长实测 3298.9µs（理论 3298.6µs），PING 520.9µs（理论 520.8µs）；协议文档 SYNC 示例校验值印作 0x8B，按公式应为 0xBA，硬件按公式实现并实测正确。",
                "步进：速度用 Q0.32 定点表示，无累积误差；move 3200 后 POS 与累计步数都精确为 3200。",
                "多舵机动作起点差（同步误差）用逻辑分析仪测，尚未实测 → 标「待测 ⚠」，不猜。",
                "讲图：先指左边全貌定位「寄存器窗口在 PL 哪一段」，再指右边放大行逐条念寄存器。",
                SRC_SERVO + "；" + SRC_TECH + " §3.1、§3.2。",
                "图源：docs/figures/fig_pl_regs.png（右侧放大区 = 0x1xx / 0x2xx 两行寄存器窗口，裁切参数见 make_ppt.py 的 ZOOM_PL_REGS）。",
            ],
        ),
        # ---------------------------------------------------------------- 7
        dict(
            kind="bullets",
            title="PL：des_vision 硬件预处理",
            bullets=[
                "6 种模式：原图 / 灰度 / 高斯 / 二值 / Sobel",
                "128 位 AXI-Stream，每拍处理 4 像素",
                "两行缓冲 + 3 级流水，延迟 1~6 拍",
                "ROI 统计面积 / 外接框 / 质心",
                "切模式在帧起始生效，60fps 不撕裂",
            ],
            takeaway="逐像素预处理零额外延迟，HDMI 直接看到 PL 处理结果",
            notes=[
                "硬性①与领域 3 的结合：逐像素、固定运算放 PL，不占 CPU、零额外延迟。",
                "像素域最高约 156MHz：实测 MIPI 字节时钟 111.1MHz，约束 125MHz、布线后 fmax 127MHz。",
                "灰度按 BT.601：Y = (77R + 150G + 29B) >> 8；二值 / 边缘的阈值由 PS 写寄存器。",
                "ROI 统计给出「PL 尺寸粗测」，与 PS 亚像素测量并列记录，互为对照。",
                "仿真 32×12 帧、6 种模式、790 项全过；LANES=2 后备参数同样通过。",
                SRC_TECH + " §3.4；仿真与上板见 docs/log/2026-10-11-软件闭环_NPU_界面_PL视觉.md §9。",
            ],
        ),
        # ---------------------------------------------------------------- 8
        dict(
            kind="figure",
            title="传送带跟踪与停带抓取",
            bullets=[
                "每件一个 ID，按带坐标关联观测",
                "查 BRAKE 刹车距离再停带",
                "停带前查夹爪占位冲突",
                "来不及停或冲突 → 复检",
                "不良品不停带，侧边导流挡板分流",
            ],
            figure="fig_pick_sequence.png",
            zoom=dict(**ZOOM_PICK,
                      label="局部放大：良品判定 → 停带目标 → 抓取线 → 机械臂动作",
                      why="取时序图中段：良品过判定线、写停带目标（PL 梯形减速，正好停准）、"
                          "停在抓取线（只为良品停）、下探 / 合爪 / 抬起动作块（990×630px）——"
                          "「停带抓取」这条要点的举证区；翻板分流不在这块里，看左侧全貌。"
                          "放大后 7.5pt 文字 → 面板上 ≈11.2pt。"),
            caption="图 4-1 停带抓取与挡板分流时序（左：全貌，青框＝右图；右：局部放大）",
            notes=[
                "这是演示视频的主镜头（V7），也是创新点 2：只为良品停一次带。",
                "调度器先读 PL 的 BRAKE 寄存器判断来不来得及停稳，来不及就转复检 —— 失效安全，不越过去再倒车。",
                "挡板在零件到达拦截点前约 110mm 合上、滑出后约 190mm 打开；不拦的件走到带尾复检箱。",
                "硬件在环实测：50 / 100 / 150mm/s 下 8 件良品全停准抓取、4 件不良到带尾，停带误差 ≤0.04mm，节拍 22.7 / 39.3 / 48.9 件每分钟。",
                SRC_BELT + "；" + SRC_TECH + " §4.4。抓取成功率、机械臂重复定位精度待实物标定 → 待测 ⚠。",
                "图源：docs/figures/fig_pick_sequence.png（右侧放大区 = 判定线 → 停带目标 → 抓取线 → 机械臂动作）。",
            ],
        ),
        # ---------------------------------------------------------------- 9
        dict(
            kind="figure",
            title="视觉测量：亚像素与补偿",
            bullets=[
                "离光轴最近一帧测一次，4~5ms",
                "沿轮廓法线找 50% 灰度过零点",
                "单应映射，补视差与卷帘快门",
                "齿距中位数推规格齿数，识别混料",
                "与 NPU 双判定互校，冲突复检",
            ],
            figure="fig_layout.png",
            zoom=dict(**ZOOM_LAYOUT,
                      label="局部放大：相机视野参数 + 定位 / 测量 / 判定区",
                      why="取布局图相机视野区：视野 398×224mm（4.2mm 镜头、离带面约 300mm、"
                          "0.21mm/像素）与「定位 · 亚像素测量（离光轴最近那帧）」「NPU 分类 3~6 次"
                          "软投票 → 判定」文字 —— 测量精度口径的举证区；裁 910×420px，"
                          "图内 7.5pt 文字 → 面板上 ≈12.4pt。"),
            caption="图 7-1 工位布局（左：全貌，青框＝右图；右：局部放大）",
            notes=[
                "测量解决的是 NPU 解决不了的问题：尺寸超差与规格混料（30 齿缺 2 齿 vs 28 齿）。",
                "三步补偿是可讲的技术点：单应映射、视差校正（零件顶面高于皮带平面）、卷帘快门逐行补偿。",
                "合成图（0.15mm/像素）精度：外径 ≤0.003mm、齿距 ≤0.001mm，4~5ms / 件 —— 这是合成图口径，必须说明。",
                "实物对卡尺的误差与重复性还没测 → 标的 <0.3mm 是目标不是结果，写「待测 ⚠」。",
                SRC_TECH + " §4.3、§5.4；合成精度见 docs/log/2026-10-11-软件闭环_NPU_界面_PL视觉.md。",
                "图源：docs/figures/fig_layout.png（右侧放大区 = 相机视野参数 + 定位 / 测量 / 判定文字）。",
            ],
        ),
        # ---------------------------------------------------------------- 10
        dict(
            kind="bullets",
            title="AI 模型与 NPU 部署",
            bullets=[
                "YOLOv8n-cls，输入 224×224",
                "ROI 分类替代整帧检测",
                "激活改 ReLU，合成集 94.0%→96.25%",
                "UINT8 QDQ per-tensor，去 Softmax",
                "单次 7.8ms，一件 3~6 次软投票",
            ],
            takeaway="96.25% 是合成集口径；实物验证集准确率待测 ⚠",
            notes=[
                "硬性④：模型、量化、部署三段都要说清，重点是「为什么这么改」。",
                "为什么 ROI 分类：整帧缩到 416 后约 0.95mm/像素，1mm 黑点只剩约 1 像素；按零件方向裁 56×56mm → 224×224 后约 0.25mm/像素，计算量约为整帧检测的 1/7。",
                "实测发现板上 NPU 对 SiLU 是近似计算（合成集 94.0%、logits 偏差 5.0），改 ReLU 后 96.25%，与 PC 上同一 UINT8 模型 96.46% 几乎一致。",
                "per-channel 量化在 PC 上更准，但板上 NPU 推理中止，因此用 per-tensor；导出脚本强制做形状推断（否则 ArmNN 解析器段错误）。",
                "ⓘ 96.25% 是合成裁切图 480 张的口径，**不代表实物**；实物验证集准确率待测 ⚠，答辩时不要念成实物精度。",
                SRC_NPU + "；" + SRC_TECH + " §5.2、§5.1；指标表见 " + SRC_METRICS + "。",
            ],
        ),
        # ---------------------------------------------------------------- 11
        dict(
            kind="bullets",
            title="HMI 界面与本地存储",
            bullets=[
                "HDMI 1080p60，帧内 13.0~13.9ms",
                "五页：总览 / 曲线 / 参数 / 记录 / 机械臂",
                "OpenCV 自绘 + FreeType，不用 Qt",
                "每件一行 43 列 CSV + 不良件裁切图",
                "定位 5.9ms / 帧，后台写盘不阻塞",
            ],
            takeaway="界面与存储都在 PS，仍保住 60fps 视频链路",
            notes=[
                "硬性②：界面与存储都在 PS，但要用实测数字说明没有牺牲实时性。",
                "不用 Qt 的原因：板上 Qt 只有独立镜像形式，会与 60fps 视频链路抢资源；改为 OpenCV 自绘 + FreeType 渲染中文。",
                "参数页改动即时生效并保存；记录页可翻每件裁切图与测量值；另有命名管道脚本通道，用于自动化测试与文档配图。",
                "存储：43 列 CSV 含测量值、带坐标与姿态、PL 粗测、NPU 投票次数与耗时；按日期分目录；时间 = 本机时钟 + PLC 下发的时间偏移。",
                SRC_HMI + "；" + SRC_METRICS + "；" + SRC_TECH + " §4.6、§4.7。",
            ],
        ),
        # ---------------------------------------------------------------- 12
        dict(
            kind="bullets",
            title="CAN 联动产线 PLC",
            bullets=[
                "CAN 2.0A 500kbit/s → 11 位 ID",
                "下发启停 / 调速 / 配方 / 对时",
                "10Hz 回状态，每件发一帧 PART",
                "同一套命令另开 Modbus-TCP 从站",
                "CAN / 界面 / 网口 三路同规则",
            ],
            takeaway="硬件收发 356µs，Modbus 1.04ms；实物 USB-CAN 待测 ⚠",
            notes=[
                "硬性⑤：工业总线不只做 CAN，还按工厂实际把同一套命令开放成 Modbus-TCP 从站。",
                "状态机：空闲 / 运行 / 暂停 / 急停 / 故障；PLC 命令与界面按钮同一套规则（未启动时「恢复」被拒、运行中不能换配方）。",
                "实测：板上回环命令往返平均 356µs（最大 429µs）、1000/1000；200~2000 帧/s 零丢失零错序；Modbus-TCP 读 24 个输入寄存器平均往返 1.04ms，单元测试 29 项通过。",
                "配方换型：切到 28 齿配方后，同批零件里 28 齿件由报废变良品、30 齿件变规格混料，运行中拒绝换配方。",
                "⚠ 实物 USB-CAN 收发与长时间误码待测；CAN 2h 回环里 PLC 侧 STATUS 丢 82 帧（0.13%），已定位为背靠背连发，复测中。",
                SRC_CAN + "；" + SRC_METRICS + "；" + SRC_TECH + " §6。",
            ],
        ),
        # ---------------------------------------------------------------- 13
        dict(
            kind="table",
            title="测试数据（四类指标）",
            bullets=[
                "依据：板上 CSV → part_stats.py",
                "未实测项一律标「待测 ⚠」，不进结论",
            ],
            table=[
                ["类别", "指标", "实测值"],
                ["控制同步", "6 舵机 SYNC 帧 / 步进首脉冲 / 停带误差",
                 "3298.9µs / 10.6µs / ≤0.04mm"],
                ["采集延迟", "采集帧率 / 显示每帧 / PS 读 PL 寄存器",
                 "60.02fps / 13.0~13.9ms / 188ns"],
                ["AI 推理", "NPU 单次推理 / 实物准确率",
                 "7.8ms / 待测 ⚠"],
                ["总线稳定性", "CAN 往返 / Modbus 往返 / 实测测量误差",
                 "356µs / 1.04ms / 待测 ⚠"],
            ],
            order="table_first",
            notes=[
                "硬性⑥：四类指标逐类念，条件随数字一起念（否条件不同的数不可比）。",
                "已实测：SYNC 帧 3298.9µs、步进命令→首脉冲 10.6µs、停带误差 ≤0.04mm、60.02fps 0 丢帧、显示 13.0~13.9ms、PS 读 PL 188ns、NPU 7.8ms、CAN 356µs、Modbus 1.04ms。",
                "待测 ⚠（答辩要主动说明，别等专家问）：实物分类准确率、实物测量误差对卡尺、多舵机同步误差（逻辑分析仪）、机械臂重复定位、跟踪误差、接实物 2h。",
                "口径诚实是评分项：指令分辨率与实测精度分开、采集 / 显示实时性与按物料触发推理分开。",
                SRC_METRICS + "；缺口清单见同文件「还缺（按优先级）」。",
            ],
        ),
        # ---------------------------------------------------------------- 14
        dict(
            kind="table",
            title="可靠性与安全",
            bullets=[
                "急停双路：常闭切电 + 常开报 PL",
                "急停保持 1ms 去抖后锁存停脉冲",
                "对射 / 限位去抖 20µs 并锁存位置",
                "失效安全：漏检 / 跟丢 → 复检箱",
            ],
            table=[
                ["项", "实测值", "条件"],
                ["2h 连续运行", "60.02fps，432122 帧仅跳 13 帧（0.003%）",
                 "soak.sh 7200 + CAN 回环"],
                ["内存", "88.5 → 99.9MB 后持平", "最后 50 分钟不增长"],
            ],
            order="table_first",
            notes=[
                "安全分两条：切电（常闭触点串在动力电源上，同时切断步进与全部舵机）与上报（常开触点报 PL 锁存，软件进急停、CAN 发报警，需人工复位）。",
                "失效安全的设计意图：漏检、跟丢、挡板故障都只会进复检箱，不会把不良品当良品放行。",
                "长时间：软件 2h 浸泡 60.02fps、跳帧 0.003%、工作线程处理 99.9% 的帧、显示平均 9.29ms（全屏布局），无崩溃。",
                "⚠ 该次 2h 是无零件的软件浸泡；接实物的 2h 与舵机总线长时间丢帧率待测。",
                SRC_SOAK + "；" + SRC_TECH + " §3.6、§7；" + SRC_METRICS + "。",
            ],
        ),
        # ---------------------------------------------------------------- 15
        dict(
            kind="bullets",
            title="创新点",
            bullets=[
                "帧起始锁存，位置误差与延迟解耦",
                "只为良品停带，不良品侧向分流",
                "亚像素测量与 NPU 分类双判定",
                "面向 NPU 改造：SiLU 换 ReLU",
                "全自研 PL 外设，逐项仿真",
            ],
            takeaway="位置误差与推理延迟解耦——判得慢不影响分得准",
            notes=[
                "五个创新点逐个用一句话讲，各配一个可验证的证据，不堆形容词。",
                "点 1 证据：帧号差全为 1、锁存带位置差与带速 × 读出时间一致。",
                "点 2 证据：硬件在环 8 件全停准，停带误差 ≤0.04mm，来不及停转复检。",
                "点 3 证据：一方判 OK 另一方判缺陷时进复检，避免单点误判放行。",
                "点 4 证据：SiLU 版板上 94.0%、ReLU 版 96.25%，与 PC 端 96.46% 几乎一致（合成集口径）。",
                "点 5 证据：不改官方块设计，把 fvdma_ctrl 地址窗口加宽引出寄存器总线；PL 基址与分段见 docs/技术文档.md §2.4、§9。",
            ],
        ),
        # ---------------------------------------------------------------- 16
        dict(
            kind="bullets",
            title="总结与展望",
            bullets=[
                "单板跑通：看—判—量—跟—分",
                "硬性六项均有数据，未测已标注",
                "P0 完成，P1 待实物数据集与标定",
                "待做：实物数据集与混淆矩阵",
                "待做：接实物复跑 2h 与标定",
            ],
            takeaway="初赛交报告 + 视频；决赛现场演示与提问，PPT 用本材料",
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
# 组装
# --------------------------------------------------------------------------- #


def build_cover(prs, spec, page_no, total):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    _add_rect(slide, x=0, y=0, w=SLIDE_W_IN, h=SLIDE_H_IN, fill=C_BG_COVER)
    _add_rect(slide, x=0, y=0, w=0.16, h=SLIDE_H_IN, fill=C_ACCENT)
    _add_rect(slide, x=0, y=6.95, w=SLIDE_W_IN, h=0.55, fill=C_STRIP)

    box, tf = _add_textbox(slide, x=1.20, y=1.75, w=11.0, h=1.20,
                           anchor=MSO_ANCHOR.MIDDLE)
    run = tf.paragraphs[0].add_run()
    run.text = spec["title"]
    _style_run(run, size=54, color=C_TITLE, bold=True)

    _add_rect(slide, x=1.24, y=3.06, w=2.10, h=0.055, fill=C_ACCENT)

    box, tf = _add_textbox(slide, x=1.20, y=3.28, w=11.0, h=0.55)
    run = tf.paragraphs[0].add_run()
    run.text = "%s ｜ %s" % (WORK_NAME_EN, "塑胶传动件在线质检分拣系统")
    _style_run(run, size=22, color=C_ACCENT)

    box, tf = _add_textbox(slide, x=1.20, y=4.30, w=11.0, h=1.10)
    for i, line in enumerate((EVENT_LINE_1, EVENT_LINE_2)):
        p = _paragraph(tf, first=(i == 0))
        p.space_after = Pt(6)
        run = p.add_run()
        run.text = line
        _style_run(run, size=20, color=C_BODY)

    box, tf = _add_textbox(slide, x=1.20, y=5.72, w=11.0, h=0.42)
    run = tf.paragraphs[0].add_run()
    run.text = DATE_LINE
    _style_run(run, size=18, color=C_MUTED)

    _add_footer(slide, page_no, total)
    return slide


def build_bullets_page(page, spec):
    """纯文字页：要点 +（可选）结论条，纵向占满版心。"""
    bullets = spec["bullets"]
    takeaway = spec.get("takeaway")
    size = SIZE_BULLET_WIDE
    band = BODY_BOTTOM - BODY_TOP
    extra = (0.66 + BLOCK_GAP) if takeaway else 0.0
    avail = band - extra
    space = _solve_space_after(len(bullets), size, avail, lo=10.0, hi=34.0)
    b_h = bullets_height(len(bullets), size, space)
    stack = b_h + extra
    y = BODY_TOP + max(0.0, (band - stack) / 2.0)
    _add_bullets(page, bullets, x=TITLE_X, y=y, w=CONTENT_W, size=size, space_after=space)
    if takeaway:
        _add_takeaway(page, takeaway, y=y + b_h + BLOCK_GAP)


def build_table_page(page, spec):
    """表格页：表格拉满版心宽、字号 16pt、行高按剩余空间撑开；要点在表格下方。"""
    bullets = spec["bullets"]
    data = spec["table"]
    rows = len(data)
    band = BODY_BOTTOM - BODY_TOP
    size = SIZE_BULLET_TABLE
    space = 20.0
    b_h = bullets_height(len(bullets), size, space) if bullets else 0.0
    avail = band - b_h - (BLOCK_GAP if bullets else 0.0)
    row_h = max(TABLE_ROW_MIN, min(TABLE_ROW_MAX, avail / rows))
    t_h = row_h * rows
    stack = t_h + (BLOCK_GAP + b_h if bullets else 0.0)
    y = BODY_TOP + max(0.0, (band - stack) / 2.0)
    _add_table(page, data, x=MARGIN_X, y=y, w=CONTENT_W, row_h=row_h,
               col_ratio=col_ratios(data))
    if bullets:
        _add_bullets(page, bullets, x=TITLE_X, y=y + t_h + BLOCK_GAP, w=CONTENT_W,
                     size=size, space_after=space)


def build_figure_page(page, spec):
    """插图页：左列 = 要点 + 全貌缩略（青框标出放大区），右列 = 局部放大面板。"""
    name = spec["figure"]
    bullets = spec["bullets"]
    zoom = spec.get("zoom")
    px_w, px_h = image_size(name)
    ratio = px_w / px_h

    # ---- 右列：局部放大 -------------------------------------------------- #
    if zoom:
        zoom_notes = check_zoom_anchors(name, zoom)
        page.zoom_notes = zoom_notes
        crop_path, cw, ch = make_zoom(name, zoom["rect"])
        rx, rw = PANEL_LEFT, PANEL_W
        label = zoom["label"]
        mag = px_w / cw
        box, tf = _add_textbox(page.slide, x=rx, y=BODY_TOP, w=rw, h=0.34,
                               anchor=MSO_ANCHOR.MIDDLE)
        run = tf.paragraphs[0].add_run()
        run.text = "%s ｜ 放大 ×%.1f" % (label, mag)
        _style_run(run, size=SIZE_ZOOM_LABEL, color=C_ACCENT, bold=True)
        page.note(rx, BODY_TOP, rw, 0.34)
        zy = BODY_TOP + 0.34
        zh = PANEL_BOTTOM - zy
        # PDF 兜底件要求 ≥300dpi：裁切图按 1:1 像素给出，替换时按需 LANCZOS 上采样
        # （信息量不变，只是让 PDF 里的采样密度够，避免阅读器放大时硬像素块）
        pdf_scale = max(1.0, float(int((FIG_DPI_MIN * 1.07) * rw / cw) + 1))
        _add_figure_path(page, crop_path, x=rx, y=zy, w=rw, h=zh, role="zoom",
                         px_w=cw, px_h=ch, pdf_scale=pdf_scale)
        page.note(rx, zy, rw, zh)                     # 放大面板占位（含留白）

    # ---- 左列：要点 + 全貌缩略 ------------------------------------------- #
    # 先把缩略图按「要点最小间距下剩下的高度」定尺寸，再拿剩下的空间把要点拉开：
    # 反过来的话（先按原宽高比定缩略图）缩略图偏高时整列会顶到图注上。
    band_left = PANEL_BOTTOM - BODY_TOP
    label_h = 0.24
    size = _fit_bullet_size(bullets, CAPTION_LEFT_W, sizes=(20, 19, SIZE_BULLET_FIG))
    b_min = bullets_height(len(bullets), size, 8.0)
    thumb_avail = band_left - b_min - 0.20 - label_h
    thumb_h = min(CAPTION_LEFT_W / ratio, max(1.20, thumb_avail))
    thumb_w = thumb_h * ratio
    avail_b = band_left - thumb_h - 0.20 - label_h
    space = _solve_space_after(len(bullets), size, avail_b, lo=8.0, hi=24.0)
    b_h = bullets_height(len(bullets), size, space)
    stack = b_h + 0.20 + thumb_h + label_h
    y = BODY_TOP + max(0.0, (band_left - stack) / 2.0)
    _add_bullets(page, bullets, x=TITLE_X, y=y, w=CAPTION_LEFT_W, size=size,
                 space_after=space)
    ty = y + b_h + 0.20
    _add_figure_path(page, FIGURES_DIR / name, x=TITLE_X, y=ty, w=thumb_w, h=thumb_h,
                     role="overview", px_w=px_w, px_h=px_h,
                     highlight=zoom["rect"] if zoom else None)
    box, tf = _add_textbox(page.slide, x=TITLE_X, y=ty + thumb_h + 0.04, w=CAPTION_LEFT_W,
                           h=label_h, anchor=MSO_ANCHOR.MIDDLE)
    p = tf.paragraphs[0]
    p.alignment = PP_ALIGN.CENTER
    run = p.add_run()
    run.text = "全貌（青框＝右侧放大区）" if zoom else "全貌"
    _style_run(run, size=SIZE_THUMB_LABEL, color=C_MUTED)
    page.note(TITLE_X, ty + thumb_h + 0.04, CAPTION_LEFT_W, label_h)

    _add_caption(page, spec["caption"])


def _add_figure_path(page, path, *, x, y, w, h, role, px_w, px_h, pdf_scale=1.0,
                     highlight=None):
    """同 add_figure，但源是任意路径（裁切图在 out/ 下）。"""
    ratio = px_w / px_h
    if w / h > ratio:
        fh = h
        fw = h * ratio
    else:
        fw = w
        fh = w / ratio
    fx = x + (w - fw) / 2.0
    fy = y + (h - fh) / 2.0
    pad = 0.06
    shp = _add_rect(page.slide, x=fx - pad, y=fy - pad, w=fw + 2 * pad,
                    h=fh + 2 * pad, fill=C_CARD, line=C_CARD_LINE,
                    shape=MSO_SHAPE.ROUNDED_RECTANGLE, line_w=Pt(1.0))
    shp.adjustments[0] = 0.04
    page.slide.shapes.add_picture(str(path), Inches(fx), Inches(fy),
                                  Inches(fw), Inches(fh))
    if highlight is not None:
        gx0, gy0, gx1, gy1 = highlight
        _add_rect(page.slide, x=fx + gx0 / px_w * fw, y=fy + gy0 / px_h * fh,
                  w=(gx1 - gx0) / px_w * fw, h=(gy1 - gy0) / px_h * fh,
                  fill=None, line=C_ACCENT, line_w=Pt(2.25))
    page.note_figure(role=role, src=_rel(path), x=fx, y=fy, w=fw, h=fh,
                     px_w=px_w, px_h=px_h, pdf_scale=pdf_scale)
    return fx, fy, fw, fh


def _fit_bullet_size(bullets, w, *, sizes):
    """在给定列宽下挑最大可用字号（与自检同一套字宽估算）。"""
    for size in sizes:
        if all(_text_em(b, LATIN_EM) * size / 72.0 + 0.24 <= w for b in bullets):
            return size
    return sizes[-1]


def build_slide(prs, spec, page_no, total):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    _add_rect(slide, x=0, y=0, w=SLIDE_W_IN, h=SLIDE_H_IN, fill=C_BG)
    _add_rect(slide, x=0, y=0, w=SLIDE_W_IN, h=0.10, fill=C_ACCENT)
    page = Page(slide)
    _add_title(page, spec["title"])

    kind = spec.get("kind", "bullets")
    if kind == "table":
        build_table_page(page, spec)
    elif kind == "figure":
        build_figure_page(page, spec)
    else:
        build_bullets_page(page, spec)

    _add_footer(slide, page_no, total)
    return slide, page


def build(out_path=OUT_PPTX):
    specs = slide_specs()
    total = len(specs)

    prs = Presentation()
    prs.slide_width = Inches(SLIDE_W_IN)
    prs.slide_height = Inches(SLIDE_H_IN)
    metrics = []
    manifest = []

    if ZOOM_DIR.is_dir():                  # 放大裁切图每次重建，避免留下旧参数的文件
        shutil.rmtree(ZOOM_DIR)

    for i, spec in enumerate(specs, start=1):
        if spec.get("kind") == "cover":
            slide = build_cover(prs, spec, i, total)
            page = None
        else:
            slide, page = build_slide(prs, spec, i, total)
            metrics.append((i, spec.get("kind", "bullets"), page))
            if page.zoom_notes:
                ZOOM_NOTES[i] = page.zoom_notes
            for fig in page.figures:
                manifest.append(dict(slide=i, **fig))
        _set_notes(slide, spec["notes"])

    out_path = Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    prs.save(str(out_path))
    OUT_MANIFEST.write_text(
        json.dumps(dict(slide_w=SLIDE_W_IN, slide_h=SLIDE_H_IN, figures=manifest),
                   ensure_ascii=False, indent=1), encoding="utf-8")
    return out_path, prs, metrics


# --------------------------------------------------------------------------- #
# 自检（读回生成的 pptx，输出原始统计 + 版面度量 + 门禁）
# --------------------------------------------------------------------------- #

# 禁项：学校名称与指导老师信息（submission/提交材料清单.md §6 检查表）。
# 「全国大学生嵌入式芯片与系统设计竞赛」是官方赛事名，必须原样出现 → 先整串剔除再查。
FORBIDDEN = ("大学", "学院", "指导老师", "指导教师", "导师", "校徽")
EVENT_ALLOWLIST = (EVENT_LINE_1, "全国大学生嵌入式芯片与系统设计竞赛")


def _text_em(text, latin_em=LATIN_EM):
    """估算一段文字的宽度，单位 = 一个中文字宽。"""
    return sum(1.0 if unicodedata.east_asian_width(c) in "WF" else latin_em
               for c in text)


def verify(path, metrics):
    from pptx import Presentation as _P

    global FIG_ROLES
    FIG_ROLES = {(f["slide"], round(f["rect_in"][0], 2), round(f["rect_in"][1], 2)): f
                 for f in _manifest_figures()}

    prs = _P(str(path))
    width_in = prs.slide_width / 914400
    height_in = prs.slide_height / 914400
    slides = list(prs.slides)

    print("=" * 100)
    print("文件      : %s" % path)
    print("相对路径  : %s" % path.relative_to(REPO_ROOT).as_posix())
    print("尺寸      : %.3f x %.3f in（16:9 = %.4f，期望 %.4f）"
          % (width_in, height_in, width_in / height_in, 16 / 9))
    print("页数      : %d" % len(slides))
    print("-" * 100)

    total_pics = total_notes = total_chars = 0
    problems = []
    bullet_overflow = []
    layout_rows = []

    for idx, slide in enumerate(slides, start=1):
        pics = [s for s in slide.shapes if s.shape_type == 13]  # PICTURE
        texts = []
        for shape in slide.shapes:
            if shape.has_text_frame:
                texts.append(shape.text_frame.text)
            if getattr(shape, "has_table", False) and shape.has_table:
                for row in shape.table.rows:
                    for cell in row.cells:
                        texts.append(cell.text)
        joined = "\n".join(texts)
        notes = slide.notes_slide.notes_text_frame.text.strip()
        chars = len(notes)

        # 禁项检查前先把官方赛事名整串剔除
        audit = joined
        for allowed in EVENT_ALLOWLIST:
            audit = audit.replace(allowed, "")

        total_pics += len(pics)
        total_chars += chars
        if notes:
            total_notes += 1
        else:
            problems.append("第 %d 页没有 speaker notes" % idx)

        for bad in FORBIDDEN:
            if bad in audit:
                problems.append("第 %d 页正文出现禁项「%s」" % (idx, bad))
        # 备注里的「指导老师」是禁项说明，不属于页面正文，单独提示
        if "大学" in notes or "学院" in notes:
            problems.append("第 %d 页备注出现疑似校名" % idx)
        if any(s in joined for s in ("C:\\", "D:\\", "C:/", "D:/")):
            problems.append("第 %d 页出现主机绝对路径" % idx)

        # 要点行宽估算：中文按 1 字宽、拉丁按 0.75 字宽，超过列宽即会在投影上折行
        for shape in slide.shapes:
            if not shape.has_text_frame:
                continue
            for para in shape.text_frame.paragraphs:
                txt = para.text.strip()
                if not txt.startswith("▪"):
                    continue
                body = txt.lstrip("▪ ").strip()
                sizes = [r.font.size.pt for r in para.runs if r.font.size]
                size = max(sizes) if sizes else SIZE_BULLET_FIG
                emu = _text_em(body)
                need_in = emu * size / 72.0 + 0.24
                if need_in > shape.width / 914400:
                    bullet_overflow.append(
                        "第 %02d 页要点折行：%.1f 字宽（需 %.2fin / 有 %.2fin，%gpt）：%s"
                        % (idx, emu, need_in, shape.width / 914400, size, body))

        print("第 %02d 页  形状 %2d（其中图片 %d）  备注 %3d 字  %s"
              % (idx, len(slide.shapes), len(pics), chars,
                 notes.splitlines()[0][:34] if notes else "!! 无备注"))

        # 表格 / 形状越界检查（EMU 直接比）
        for shape in slide.shapes:
            if shape.left is None:
                continue
            bottom = shape.top + shape.height
            if shape.left < 0 or shape.top < 0 \
                    or shape.left + shape.width > prs.slide_width + 1000 \
                    or bottom > prs.slide_height + 1000:
                problems.append(
                    "第 %d 页形状越界：L=%.2fin T=%.2fin W=%.2fin H=%.2fin"
                    % (idx, shape.left / 914400, shape.top / 914400,
                       shape.width / 914400, shape.height / 914400))

        # 表格：必须拉满版心宽、字号 ≥14pt；单元格折行估算（按实际字号）
        for shape in slide.shapes:
            if not (getattr(shape, "has_table", False) and shape.has_table):
                continue
            table = shape.table
            tbl_w = shape.width / 914400
            if tbl_w < CONTENT_W * TABLE_W_MIN_RATIO:
                problems.append(
                    "第 %d 页表格只占版心 %.0f%%（应 ≥%.0f%%）：%.2fin / %.2fin"
                    % (idx, tbl_w / CONTENT_W * 100, TABLE_W_MIN_RATIO * 100,
                       tbl_w, CONTENT_W))
            for r, row in enumerate(table.rows):
                for c, cell in enumerate(row.cells):
                    if not cell.text.strip():
                        continue
                    sizes = [run.font.size.pt for p in cell.text_frame.paragraphs
                             for run in p.runs if run.font.size]
                    size = max(sizes) if sizes else SIZE_TABLE_BODY
                    if size < 14:
                        problems.append("第 %02d 页表格(%d,%d)字号 %gpt < 14pt"
                                        % (idx, r, c, size))
                    avail = (table.columns[c].width - Inches(0.16)) / 914400
                    need = _text_em(cell.text) * size / 72.0
                    if need > avail:
                        problems.append(
                            "第 %02d 页表格(%d,%d)会折行：需 %.2fin / 有 %.2fin（%gpt）：%s"
                            % (idx, r, c, need, avail, size, cell.text[:26]))

        # 插图 dpi：原生插图 ≥300dpi；放大面板按 1:1 像素裁切，靠 PDF 侧上采样达标
        for shape in slide.shapes:
            if shape.shape_type != 13:
                continue
            try:
                px_w = shape.image.size[0]
            except Exception:                         # pragma: no cover
                continue
            w_in = shape.width / 914400
            dpi = px_w / w_in
            key = (idx, round(shape.left / 914400, 2), round(shape.top / 914400, 2))
            role = FIG_ROLES.get(key, {}).get("role", "full")
            if role == "zoom":
                scale = FIG_ROLES.get(key, {}).get("pdf_scale", 1.0)
                if dpi * scale < FIG_DPI_MIN:
                    problems.append(
                        "第 %02d 页放大面板 PDF 采样 %.0f dpi < %d（裁切 %dpx / %.2fin，"
                        "上采样 ×%g）" % (idx, dpi * scale, FIG_DPI_MIN, px_w, w_in, scale))
            elif dpi < FIG_DPI_MIN:
                problems.append("第 %02d 页插图原生 dpi %.0f < %d（源 %dpx / %.2fin）"
                                % (idx, dpi, FIG_DPI_MIN, px_w, w_in))

    # ---- 版面度量（生成时登记的 ink bbox） ------------------------------- #
    print("-" * 100)
    print("版面度量（ink bbox，不含页脚与页顶装饰；目标：顶档≤%.2fin 头档≤%.2fin 底档≤%.2fin 填充率 %.0f%%~%.0f%%）"
          % (GAP_TOP_MAX, GAP_HEAD_MAX, GAP_BOTTOM_MAX, FILL_MIN * 100, FILL_MAX * 100))
    print("%-4s %-8s %7s %7s %7s %7s %7s   %s"
          % ("页", "版式", "顶档in", "头档in", "底档in", "填充h", "填充A", "判定"))
    for idx, kind, page in metrics:
        m = page.metrics()
        if not m:
            continue
        bad = []
        if m["top_gap"] > GAP_TOP_MAX:
            bad.append("顶档")
        if m["head_gap"] is not None and m["head_gap"] > GAP_HEAD_MAX:
            bad.append("头档")
        if m["bottom_gap"] > GAP_BOTTOM_MAX:
            bad.append("底档")
        if not (FILL_MIN <= m["fill_area"] <= FILL_MAX):
            bad.append("填充率")
        verdict = "PASS" if not bad else ("FAIL: " + "/".join(bad))
        if bad:
            problems.append("第 %d 页版面不达标：%s（顶%.2f 头%.2f 底%.2f 填充%.0f%%）"
                            % (idx, verdict, m["top_gap"],
                               m["head_gap"] if m["head_gap"] is not None else -1,
                               m["bottom_gap"], m["fill_area"] * 100))
        layout_rows.append((idx, kind, m, verdict))
        print("%-4d %-8s %7.2f %7s %7.2f %6.0f%% %6.0f%%   %s"
              % (idx, kind, m["top_gap"],
                 ("%.2f" % m["head_gap"]) if m["head_gap"] is not None else "-",
                 m["bottom_gap"], m["fill_h"] * 100, m["fill_area"] * 100, verdict))

    # ---- 放大面板等效字号（图内 7.5pt(SVG) 文字放到面板上的实际磅值） --- #
    print("-" * 100)
    print("密集插图页放大面板：等效字号 = 图内 %.1fpt(SVG) × %.2fpx/pt × 面板宽in ÷ 裁切宽px × 72"
          % (FIG_BODY_PT, SVG_PT_TO_PX))
    for item in _manifest_figures():
        if item["role"] != "zoom":
            continue
        w_in = item["rect_in"][2]
        px = item["px"][0]
        eff = FIG_BODY_PT * SVG_PT_TO_PX * w_in / px * 72.0
        full_px = item.get("full_px")
        base = (FIG_BODY_PT * SVG_PT_TO_PX * PANEL_W / full_px[0] * 72.0
                if full_px else float("nan"))
        ok = eff >= ZOOM_TEXT_PT_MIN
        if not ok:
            problems.append("第 %d 页放大面板等效字号 %.1fpt < %.1fpt"
                            % (item["slide"], eff, ZOOM_TEXT_PT_MIN))
        print("第 %02d 页  %-52s 面板 %4.2fin / 裁 %4dpx → 等效 %4.1fpt（整图口径 %4.1fpt）  %s"
              % (item["slide"], Path(item["src"]).name, w_in, px, eff, base,
                 "PASS" if ok else "FAIL"))
        for note in ZOOM_NOTES.get(item["slide"], []):
            print("          锚点核对：%s" % note)

    print("-" * 100)
    print("合计      : 幻灯片 %d 页；图片 %d 张；有备注 %d 页；备注合计 %d 字"
          % (len(slides), total_pics, total_notes, total_chars))
    problems.extend(bullet_overflow)
    if problems:
        print("自检问题  : %d 项" % len(problems))
        for p in problems:
            print("  ! %s" % p)
    else:
        print("自检问题  : 0（尺寸 / 备注齐全 / 无禁项 / 无主机路径 / 无越界 / 要点与表格不折行）")
        print("            版面门禁全过：顶档 ≤%.2fin、头档 ≤%.2fin、底档 ≤%.2fin、填充率 %.0f%%~%.0f%%"
              % (GAP_TOP_MAX, GAP_HEAD_MAX, GAP_BOTTOM_MAX, FILL_MIN * 100, FILL_MAX * 100))
        print("            表格拉满版心 ≥%.0f%%、字号 ≥14pt；插图原生 dpi ≥%d；放大面板等效字号 ≥%.0fpt"
              % (TABLE_W_MIN_RATIO * 100, FIG_DPI_MIN, ZOOM_TEXT_PT_MIN))
    print("=" * 100)
    return len(slides), total_pics, total_notes, problems


def _manifest_figures():
    if not OUT_MANIFEST.is_file():
        return []
    data = json.loads(OUT_MANIFEST.read_text(encoding="utf-8"))
    figs = data.get("figures", [])
    full = {}
    for f in figs:
        if f["role"] == "overview":
            full[f["slide"]] = f["px"]
    for f in figs:
        f["full_px"] = full.get(f["slide"])
    return figs


def main(argv=None):
    try:                                   # 中文输出：控制台按 UTF-8 打印
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:                      # pragma: no cover
        pass

    ap = argparse.ArgumentParser(description="生成龙瞳分拣答辩 PPT（16:9）")
    ap.add_argument("--out", default=str(OUT_PPTX),
                    help="输出 pptx 路径（默认 submission/out/龙瞳分拣_答辩.pptx）")
    ap.add_argument("--no-verify", action="store_true", help="跳过读回自检")
    args = ap.parse_args(argv)

    path, _prs, metrics = build(Path(args.out))
    print("已生成：%s" % path.relative_to(REPO_ROOT).as_posix())
    print("插图清单：%s" % OUT_MANIFEST.relative_to(REPO_ROOT).as_posix())

    if not args.no_verify:
        _n, _p, _nt, problems = verify(Path(args.out), metrics)
        return 0 if not problems else 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
