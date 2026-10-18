# -*- coding: utf-8 -*-
"""docs/技术文档.md → submission/out/龙瞳分拣_作品报告.docx（竞赛作品报告排版）。

内容源只有一份：docs/技术文档.md（本脚本只读它，从不改写）。插图按 Markdown 里的
相对路径相对 docs/ 解析。脚本只认技术文档实际用到的 Markdown 写法——
`#` 封面标题、`##` 章、`###` 节、普通段落、**加粗**、行内 `代码`、`![alt](图)`
加下一行 *斜体图注*、`| … |` 表格、`- `/`1. ` 列表、`---` 分隔线——
不写通用大而全的解析器：遇到没见过的写法直接报错退出，不会静默丢内容。

排版口径（A4、中文报告）：
  封面 / 目录 / 正文三节；封面无页眉页脚，正文页码从 1 起
  封面上只有作品名、赛道、类别、落款，绝无学校名与指导老师字样（竞赛禁项，脚本自检）
  正文宋体 12pt / 首行缩进 2 字符 / 两端对齐 / 1.5 倍行距
  一至三级标题黑体（16 / 14 / 12pt，三级加粗）、不缩进、与下段同页、段前 8~12pt
  表格有边框、表头加粗加浅灰底纹、正文 10.5pt、整表不超版心、跨页重复表头
  插图居中、宽 ≤ 14.5cm、图注居中 10.5pt 去斜体、图与图注保持同页
  目录用 Word 域 TOC；页脚「第 X 页 / 共 Y 页」用 PAGE / NUMPAGES 域
  **行距一律用倍数（lineRule=auto）**：固定行距会裁字并让上下段叠在一起，见 set_spacing

用法（cwd 任意，路径都能用相对 cwd 或绝对路径）：
    python submission/build/md2docx.py [-Source <md>] [-Out <docx>] [-Stats <json>]

依赖 python-docx（装没装：`python -m pip show python-docx`）。本文件不含主机绝对
路径：源、输出、插图位置全部从脚本位置 __file__ 推导。退出码 0 成功 / 1 失败。
"""
from __future__ import annotations

import argparse
import json
import re
import struct
import sys
import unicodedata
from pathlib import Path

try:
    from docx import Document
    from docx.enum.section import WD_SECTION
    from docx.enum.style import WD_STYLE_TYPE
    from docx.enum.table import WD_TABLE_ALIGNMENT
    from docx.enum.text import WD_ALIGN_PARAGRAPH
    from docx.oxml import OxmlElement
    from docx.oxml.ns import qn
    from docx.shared import Cm, Pt, RGBColor, Twips
except ImportError as exc:  # pragma: no cover
    sys.exit("需要 python-docx：%s（python -m pip install python-docx）" % exc)

# 控制台按 UTF-8 输出（Windows 默认 GBK 会因中文 + µ / ⚠ 报 UnicodeEncodeError）
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):  # pragma: no cover
        pass

# ---------------------------------------------------------------- 仓库布局

BUILD_DIR = Path(__file__).resolve().parent          # submission/build/
SUBMISSION = BUILD_DIR.parent                        # submission/
REPO = SUBMISSION.parent                             # 仓库根（按相对位置推导）
DOCS_DIR = REPO / "docs"
DEFAULT_SOURCE = DOCS_DIR / "技术文档.md"
DEFAULT_OUT = SUBMISSION / "out" / "龙瞳分拣_作品报告.docx"

# ---------------------------------------------------------------- 排版常量

FONT_CN = "宋体"
FONT_HEI = "黑体"
FONT_EN = "Times New Roman"
FONT_CODE = "Consolas"

PAGE_W_CM, PAGE_H_CM = 21.0, 29.7            # A4
MARGIN_CM = 2.6                              # 左右 2.6cm → 版心 15.8cm
TEXT_W_CM = PAGE_W_CM - 2 * MARGIN_CM
TABLE_W_CM = 15.6                            # 整表上限，留 2mm 余量
FIG_MAX_CM = 14.5

BODY_PT = 12.0                               # 小四
BODY_LINE = 1.5                              # 正文 1.5 倍行距（倍数，不用固定值）
TIGHT_LINE = 1.0                             # 表格 / 图注 / 页眉页脚：单倍
HEAD_LINE = 1.15                             # 标题：1.15 倍，字大也不裁
H_GAP = {"h1": (12.0, 6.0), "h2": (10.0, 6.0), "h3": (8.0, 6.0)}   # 段前 / 段后 pt
H1_PT, H2_PT, H3_PT = 16.0, 14.0, 12.0
TABLE_PT = 10.5                              # 五号
CAPTION_PT = 10.5
SMALL_PT = 9.0

TWIPS_PER_CM = 566.929
CHAR_TWIPS = int(round(BODY_PT * 20 * 2))    # 2 字符 = 2em = 480 twips
CELL_MAR_TW = 80                             # 单元格左右内边距

COVER_TITLE = ("龙瞳分拣（DragonEyeSorter）—— "
               "基于安路飞龙 SoC 的塑胶传动件在线质检分拣系统")
COVER_SUBTITLE = "2026 全国大学生嵌入式芯片与系统设计竞赛 · FPGA 创新设计赛道 · 安路选题四"
COVER_KIND = "作品报告"
COVER_FOOT = "参赛队：__________　作品编号：____________　2026 年 11 月"
HEADER_TEXT = "龙瞳分拣 · 塑胶传动件在线质检分拣系统"
FOOT_LEFT, FOOT_MID, FOOT_RIGHT = "第 ", " 页 / 共 ", " 页"
TOC_INSTR = r'TOC \o "1-3" \h \z \u'
TOC_PLACEHOLDER = "（打开文档后 Word 自动更新目录；也可全选后按 F9 手动更新）"
APPENDIX_RE = re.compile(r"^附录")

# 竞赛禁项自检：学校 / 学院 / 指导老师字样一律不得出现。
# 官方赛事名里天然含「大学生」「学校」，先按整串剔除再扫，避免误报。
FORBIDDEN = ("大学", "学院", "学校", "指导老师", "指导教师", "导师",
             "校徽", "学号", "班级", "队名")
ALLOWED_PHRASES = ("2026 全国大学生嵌入式芯片与系统设计竞赛", "全国大学生嵌入式芯片与系统设计竞赛")


def forbidden_hits(text):
    rest = text
    for phrase in ALLOWED_PHRASES:
        rest = rest.replace(phrase, "")
    return sorted({w for w in FORBIDDEN if w in rest})

ALIGN = {WD_ALIGN_PARAGRAPH.LEFT: "left", WD_ALIGN_PARAGRAPH.CENTER: "center",
         WD_ALIGN_PARAGRAPH.RIGHT: "right", WD_ALIGN_PARAGRAPH.JUSTIFY: "both"}

# ---------------------------------------------------------------- 基础工具


def set_run_font(run, cn=FONT_CN, en=FONT_EN, size_pt=None, bold=None):
    """显式钉死西文 / 中文 / cs 三套字体名（只设 run.font.name 管不到中文）。"""
    rpr = run._element.get_or_add_rPr()
    rfonts = rpr.get_or_add_rFonts()
    for attr in ("w:asciiTheme", "w:hAnsiTheme", "w:eastAsiaTheme", "w:cstheme"):
        if rfonts.get(qn(attr)) is not None:
            del rfonts.attrib[qn(attr)]
    rfonts.set(qn("w:ascii"), en)
    rfonts.set(qn("w:hAnsi"), en)
    rfonts.set(qn("w:eastAsia"), cn)
    rfonts.set(qn("w:cs"), en)
    if size_pt is not None:
        run.font.size = Pt(size_pt)
    if bold is not None:
        run.font.bold = bold
    run.font.color.rgb = RGBColor(0, 0, 0)


def write(par, text, cn=FONT_CN, en=FONT_EN, size_pt=BODY_PT, bold=False):
    run = par.add_run(text)
    set_run_font(run, cn, en, size_pt, bold)
    return run


def strip_theme_fonts(st_el):
    """删掉 <w:style> 上的主题字体与颜色，让显式字体 / 纯黑生效。"""
    rpr = st_el.find(qn("w:rPr"))
    if rpr is None:
        return
    for tag in ("w:rFonts", "w:color"):
        el = rpr.find(qn(tag))
        if el is not None:
            rpr.remove(el)


def fresh_pPr(st_el, par=None):
    """拿一个干净的 pPr：样式上清掉旧的，段落上则直接取。"""
    if par is not None:
        return par._p.get_or_add_pPr()
    old = st_el.find(qn("w:pPr"))
    if old is not None:
        st_el.remove(old)
    return st_el.get_or_add_pPr()


def set_spacing(pPr, before_pt=0.0, after_pt=0.0, multiple=None):
    """段距 / 行距。行距只用**倍数**（lineRule=auto），绝不用 fixed / exact：

    固定行距会把行框钉死在指定高度，中文字号略大（或字体回退到更大的字形）时上行
    会把下行盖住——实测标题 16pt 配 exact 20pt、表格 10.5pt 配 exact 12pt 都会叠字。
    倍数行距由 Word 按字号算行高，天然不会裁字。线号 240 = 单倍。
    """
    sp = pPr.get_or_add_spacing()
    sp.set(qn("w:before"), str(int(round(before_pt * 20))))
    sp.set(qn("w:after"), str(int(round(after_pt * 20))))
    sp.set(qn("w:beforeAutospacing"), "0")
    sp.set(qn("w:afterAutospacing"), "0")
    if multiple is not None:
        sp.set(qn("w:line"), str(int(round(multiple * 240))))
        sp.set(qn("w:lineRule"), "auto")
    return sp


def set_ind(pPr, first=None, left=None, hanging=None):
    ind = pPr.get_or_add_ind()
    ind.set(qn("w:firstLineChars"), "0")
    ind.set(qn("w:leftChars"), "0")
    ind.set(qn("w:hangingChars"), "0")
    ind.set(qn("w:firstLine"), str(int(first or 0)))
    ind.set(qn("w:left"), str(int(left or 0)))
    if hanging:
        ind.set(qn("w:hanging"), str(int(hanging)))


def set_keep(pPr, next_=False, lines=False, widow=False):
    pPr.get_or_add_keepNext().set(qn("w:val"), "1" if next_ else "0")
    pPr.get_or_add_keepLines().set(qn("w:val"), "1" if lines else "0")
    pPr.get_or_add_widowControl().set(qn("w:val"), "1" if widow else "0")


def fmt_par(par, *, before=0.0, after=0.0, multiple=BODY_LINE, first=0, left=0,
            hanging=0, align=WD_ALIGN_PARAGRAPH.JUSTIFY, keep_next=False,
            keep_lines=False, widow=False):
    """段落直接格式：一律显式写，不依赖样式继承。行距只给倍数。"""
    pPr = par._p.get_or_add_pPr()
    old = pPr.find(qn("w:spacing"))
    if old is not None:
        pPr.remove(old)
    set_keep(pPr, keep_next, keep_lines, widow)
    set_spacing(pPr, before, after, multiple)
    set_ind(pPr, first, left, hanging)
    set_jc(pPr, ALIGN[align])
    snap = OxmlElement("w:snapToGrid")          # 行距按倍数，别吸附到文档网格
    snap.set(qn("w:val"), "0")
    pPr.append(snap)
    return par


def set_jc(pPr, val):
    pPr.get_or_add_jc().set(qn("w:val"), val)


def add_field(par, instr, placeholder, cn=FONT_CN, size_pt=SMALL_PT, bold=False):
    """插入 Word 域：fldChar begin → instrText → separate → 占位 → end。"""
    def marker(kind):
        r = par.add_run()
        set_run_font(r, cn, FONT_EN, size_pt, bold)
        el = OxmlElement("w:fldChar")
        el.set(qn("w:fldCharType"), kind)
        r._element.append(el)

    marker("begin")
    r = par.add_run()
    set_run_font(r, cn, FONT_EN, size_pt, bold)
    instr_el = OxmlElement("w:instrText")
    instr_el.set(qn("xml:space"), "preserve")
    instr_el.text = instr
    r._element.append(instr_el)
    marker("separate")
    if placeholder:
        write(par, placeholder, cn, size_pt=size_pt, bold=bold)
    marker("end")


def set_border(pPr, tag, **attrs):
    el = pPr.find(qn(tag))
    if el is None:
        el = OxmlElement(tag)
        pPr.append(el)
    for k, v in attrs.items():
        el.set(qn("w:" + k), str(v))


def clear_par(par):
    for r in list(par.runs):
        r._element.getparent().remove(r._element)
    for child in list(par._p):
        if child.tag in (qn("w:hyperlink"), qn("w:bookmarkStart"),
                         qn("w:bookmarkEnd")):
            par._p.remove(child)


# ---------------------------------------------------------------- Markdown 解析

IMG_RE = re.compile(r"!\[[^\]]*\]\(([^)]+)\)")
CAPTION_RE = re.compile(r"^\*([^*].*?)\*$")
TABLE_SEP_RE = re.compile(r"^\|?[\s:|-]+\|[\s:|-]*$")
CELL_SPLIT_RE = re.compile(r"(?<!\\)\|")
OL_RE = re.compile(r"^(\d+)\.\s+(.*)$")


def split_cells(line):
    s = line.strip()
    if s.startswith("|"):
        s = s[1:]
    if s.endswith("|") and not s.endswith("\\|"):
        s = s[:-1]
    return [c.strip().replace("\\|", "|") for c in CELL_SPLIT_RE.split(s)]


def parse_source(md_path: Path):
    """拆块；遇到不认识的写法直接抛错。返回 (封面标题, blocks)。"""
    lines = md_path.read_text(encoding="utf-8").replace(
        "\r\n", "\n").replace("\r", "\n").split("\n")
    title, blocks, buf = None, [], []
    n = len(lines)

    def flush():
        if buf:
            blocks.append(("p", " ".join(x.strip() for x in buf)))
            buf.clear()

    i = 0
    while i < n:
        s = lines[i].strip()
        if not s:
            flush()
            i += 1
            continue
        if s.startswith("```"):
            raise ValueError("第 %d 行：不支持代码块（源里没用过）" % (i + 1))
        if re.match(r"^-{3,}$", s):
            flush()
            i += 1
            continue
        if s.startswith("#### "):
            raise ValueError("第 %d 行：只支持到 ### 三级标题，源里出现 4 级" % (i + 1))
        if s.startswith("### "):
            flush()
            blocks.append(("h3", s[4:].strip()))
            i += 1
            continue
        if s.startswith("## "):
            flush()
            head = s[3:].strip()
            blocks.append(("h2", head))
            if APPENDIX_RE.match(head):
                blocks.append(("pagebreak", head))    # 附录另起一页
            i += 1
            continue
        if s.startswith("# "):
            flush()
            if title is not None:
                raise ValueError("源里出现多个一级标题，只能有一个作封面标题")
            title = s[2:].strip()
            i += 1
            continue
        if s.startswith("|"):
            flush()
            tlines = []
            while i < n and lines[i].strip().startswith("|"):
                tlines.append(lines[i].strip())
                i += 1
            if len(tlines) < 2 or not TABLE_SEP_RE.match(tlines[1]):
                raise ValueError("表格缺少第 2 行 |---|---| 分隔行：%r" % tlines[:2])
            rows = [split_cells(tlines[0])]
            for tl in tlines[2:]:
                cells = split_cells(tl)
                if len(cells) != len(rows[0]):
                    raise ValueError("表格列数不一致：表头 %d 列 / 本行 %d 列（%r）"
                                     % (len(rows[0]), len(cells), tl[:60]))
                rows.append(cells)
            blocks.append(("table", rows))
            continue
        m = IMG_RE.match(s)
        if m and m.group(0) == s:
            flush()
            blocks.append(("image", m.group(1)))
            i += 1
            continue
        if IMG_RE.search(s):
            raise ValueError("第 %d 行：插图请独占一行，不支持图文混排" % (i + 1))
        cm = CAPTION_RE.match(s)
        if cm and not s.startswith("**"):
            flush()
            blocks.append(("caption", cm.group(1).strip()))
            i += 1
            continue
        if s.startswith("- "):
            flush()
            items = []
            while i < n and lines[i].strip().startswith("- "):
                items.append(lines[i].strip()[2:].strip())
                i += 1
            blocks.append(("ul", items))
            continue
        if OL_RE.match(s):
            flush()
            items = []
            while i < n:
                mm = OL_RE.match(lines[i].strip())
                if not mm:
                    break
                items.append((int(mm.group(1)), mm.group(2)))
                i += 1
            blocks.append(("ol", items))
            continue
        if s.startswith(">"):
            raise ValueError("第 %d 行：不支持引用块（源里没用过）" % (i + 1))
        buf.append(s)
        i += 1
    flush()
    if title is None:
        raise ValueError("源里没有一级标题，无法生成封面")
    return title, blocks


# ---------------------------------------------------------------- 行内文本

INLINE_RE = re.compile(r"(\*\*.+?\*\*|`[^`]+`)")


def add_runs(par, text, size_pt=BODY_PT, base_bold=False, base_cn=FONT_CN):
    """写一行带 **加粗** 与 `行内代码` 的文本。

    base_bold=True 用于标题段：整段连西文、连行内代码一起走黑体，标题里不出现第二种
    字体（标题混排 Consolas 会多出一种字面高度，也让「标题全黑体」这条校验失真）。
    """
    for piece in INLINE_RE.split(text):
        if not piece:
            continue
        if piece.startswith("**") and piece.endswith("**") and len(piece) > 4:
            write(par, piece[2:-2], FONT_HEI, size_pt=size_pt, bold=True)
        elif piece.startswith("`") and piece.endswith("`") and len(piece) > 2:
            if base_bold:                   # 标题里的行内代码也走黑体，不引 Consolas
                write(par, piece[1:-1], FONT_HEI, size_pt=size_pt, bold=True)
            else:
                write(par, piece[1:-1], FONT_CN, FONT_CODE, size_pt, base_bold)
        else:
            write(par, piece, base_cn, size_pt=size_pt, bold=base_bold)


# ---------------------------------------------------------------- 图片 / 表格

PNG_SIG = b"\x89PNG\r\n\x1a\n"
JPEG_SOF = (b"\xc0", b"\xc1", b"\xc2", b"\xc3", b"\xc5", b"\xc6", b"\xc7",
            b"\xc9", b"\xca", b"\xcb", b"\xcd", b"\xce", b"\xcf")


def image_size_px(path: Path):
    """只读文件头取像素尺寸（PNG / JPEG / GIF / BMP），不引第三方库。"""
    with open(path, "rb") as fh:
        head = fh.read(32)
        if head[:8] == PNG_SIG:
            w, h = struct.unpack(">II", head[16:24])
            return int(w), int(h)
        if head[:2] == b"\xff\xd8":
            fh.seek(2)
            while True:
                b = fh.read(1)
                if not b:
                    break
                if b != b"\xff":
                    continue
                marker = fh.read(1)
                while marker == b"\xff":
                    marker = fh.read(1)
                if marker in JPEG_SOF:
                    fh.read(3)
                    h, w = struct.unpack(">HH", fh.read(4))
                    return int(w), int(h)
                ln = struct.unpack(">H", fh.read(2))[0]
                fh.seek(ln - 2, 1)
        if head[:6] in (b"GIF87a", b"GIF89a"):
            w, h = struct.unpack("<HH", head[6:10])
            return int(w), int(h)
        if head[:2] == b"BM":
            w, h = struct.unpack("<ii", head[18:26])
            return abs(int(w)), abs(int(h))
    raise ValueError("不认识的图片格式：%s" % path.name)


def char_w(ch, size_pt):
    if ch == "\t":
        return 2.0 * size_pt
    return size_pt if unicodedata.east_asian_width(ch) in ("W", "F") else 0.55 * size_pt


def text_w(s, size_pt=TABLE_PT):
    return sum(char_w(c, size_pt) for c in s)


def wrapped_lines(s, avail_pt, size_pt=TABLE_PT):
    """估行数：CJK 可任意断行，贪心累加。只用于挑列宽，不需要精确。"""
    if avail_pt <= size_pt:
        return max(1, int(round(text_w(s, size_pt) / max(avail_pt, 1e-6))))
    lines, cur = 1, 0.0
    for ch in s:
        cw = char_w(ch, size_pt)
        if cur + cw > avail_pt and cur > 0:
            lines += 1
            cur = 0.0
        cur += cw
    return lines


def column_widths(rows, total_cm=TABLE_W_CM, size_pt=TABLE_PT):
    """列宽（twips）：每列不小于 3 个字宽，且让整表估高最小；合计 = 整表宽。"""
    ncol = len(rows[0])
    total_tw = int(round(total_cm * TWIPS_PER_CM))
    usable_pt = (total_cm - 2 * CELL_MAR_TW / TWIPS_PER_CM) * 28.3465
    min_pt = []
    for c in range(ncol):
        longest = 0.0
        for r in rows:
            cell = re.sub(r"[*`]", "", r[c] if c < len(r) else "")
            token = max(re.split(r"[\s、，,；;：:/|（）()]+", cell) or [""], key=len)
            longest = max(longest, text_w(cell) / max(1, wrapped_lines(cell, 1e9)), 
                          text_w(token, size_pt))
        min_pt.append(min(max(longest, 3 * size_pt * 1.7), 9 * size_pt))
    if sum(min_pt) / 28.3465 > usable_pt / 28.3465:
        min_pt = [usable_pt / ncol] * ncol
    min_cm = [p / 28.3465 for p in min_pt]

    def evaluate(weights, width_penalty=1.0):
        raw = [max(w, 1e-6) for w in weights]
        s = sum(raw)
        widths = [usable_pt * w / s for w in raw]
        for k in range(ncol):
            widths[k] = max(widths[k], min_pt[k])
        over = sum(widths) - usable_pt
        if over > 0:
            slack = [widths[k] - min_pt[k] for k in range(ncol)]
            if sum(slack) > 0:
                widths = [widths[k] - over * slack[k] / sum(slack) for k in range(ncol)]
        est = 0.0
        for r in rows:
            est += max(wrapped_lines(r[c] if c < len(r) else "",
                                     widths[c] - 2 * CELL_MAR_TW / TWIPS_PER_CM * 28.3465)
                       for c in range(ncol))
        # 轻微偏好「按内容分配」，避免极端等分把窄列拉宽
        est += width_penalty * sum(text_w(r[c] if c < len(r) else "", size_pt)
                                   for r in rows for c in range(ncol)) / 600.0
        return est, widths

    natural = [max(text_w(r[c] if c < len(r) else "", size_pt) for r in rows)
               for c in range(ncol)]
    eq = sum(natural) / ncol
    best, best_w = None, None
    for t in range(21):
        k = t / 20.0
        est, widths = evaluate([(1 - k) * natural[c] + k * eq for c in range(ncol)])
        if best is None or est < best - 1e-9:
            best, best_w = est, widths
    if sum(best_w) > 0:
        best_w = [w * usable_pt / sum(best_w) for w in best_w]
    return [int(round(w / 28.3465 * TWIPS_PER_CM)) for w in best_w]


def set_table_borders(table):
    borders = OxmlElement("w:tblBorders")
    for edge in ("top", "left", "bottom", "right", "insideH", "insideV"):
        el = OxmlElement("w:" + edge)
        el.set(qn("w:val"), "single")
        el.set(qn("w:sz"), "6")            # 0.75pt
        el.set(qn("w:space"), "0")
        el.set(qn("w:color"), "000000")
        borders.append(el)
    table._tbl.tblPr.append(borders)


def set_cell_margins(table):
    mar = OxmlElement("w:tblCellMar")
    for edge in ("top", "left", "bottom", "right"):
        el = OxmlElement("w:" + edge)
        el.set(qn("w:w"), "20" if edge in ("top", "bottom") else str(CELL_MAR_TW))
        el.set(qn("w:type"), "dxa")
        mar.append(el)
    table._tbl.tblPr.append(mar)


def set_table_indent(table):
    ind = OxmlElement("w:tblInd")
    ind.set(qn("w:w"), "0")
    ind.set(qn("w:type"), "dxa")
    table._tbl.tblPr.append(ind)


def repeat_header(row):
    el = OxmlElement("w:tblHeader")
    el.set(qn("w:val"), "true")
    row._tr.get_or_add_trPr().append(el)


def shade(cell, fill):
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), fill)
    cell._tc.get_or_add_tcPr().append(shd)


ONLY_NUM_RE = re.compile(r"^[\d.\s%±~≤≥<>/()\-–—、]+$")
PCT_RE = re.compile(r"\d\s*[%％]")
UNIT_RE = re.compile(r"(fps|ms|µs|us|ns|mm|cm|pt|Hz|MHz|kbit|步|次|项|件|轴|页)")


def cell_align(text):
    """表头 / 短串 / 数值 / 带单位测量值居中，长文字左对齐。"""
    t = text.strip()
    if not t or len(t) <= 10 or ONLY_NUM_RE.match(t) or PCT_RE.search(t) \
            or UNIT_RE.search(t):
        return WD_ALIGN_PARAGRAPH.CENTER
    return WD_ALIGN_PARAGRAPH.LEFT


def render_table(doc, rows):
    ncol = len(rows[0])
    widths = column_widths(rows)
    table = doc.add_table(rows=len(rows), cols=ncol)
    table.style = "Table Grid"
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    table.autofit = False
    tblW = table._tbl.tblPr.find(qn("w:tblW"))
    tblW.set(qn("w:type"), "dxa")
    tblW.set(qn("w:w"), str(sum(widths)))
    set_table_borders(table)
    set_cell_margins(table)
    set_table_indent(table)
    repeat_header(table.rows[0])
    for r, cells in enumerate(rows):
        for c in range(ncol):
            cell = table.cell(r, c)
            cell.width = Twips(widths[c])
            par = cell.paragraphs[0]
            fmt_par(par, after=0.0, multiple=TIGHT_LINE,
                    align=WD_ALIGN_PARAGRAPH.CENTER if r == 0 else cell_align(cells[c]))
            add_runs(par, cells[c], size_pt=TABLE_PT, base_bold=(r == 0))
            if r == 0:
                shade(cell, "D9D9D9")
    return table


def render_image(doc, src: Path):
    w_px, h_px = image_size_px(src)
    width_cm = FIG_MAX_CM
    if h_px and w_px / h_px < 0.35:            # 细高图再按高度压一压（≤ 22cm）
        width_cm = min(width_cm, 22.0 * w_px / h_px)
    par = doc.add_paragraph()
    fmt_par(par, before=6.0, after=4.0, multiple=TIGHT_LINE,
            align=WD_ALIGN_PARAGRAPH.CENTER, keep_next=True)
    par.add_run().add_picture(str(src), width=Cm(width_cm))
    return par


# ---------------------------------------------------------------- 文档搭建


def style_el(styles, name):
    """按名字取样式的 <w:style> 元素（style.element 对内置样式拿不到）。"""
    for st in styles:
        if st.name == name:
            return st.element
    raise KeyError(name)


def build_styles(doc):
    styles = doc.styles
    normal = styles["Normal"]
    strip_theme_fonts(style_el(styles, "Normal"))
    pPr = fresh_pPr(style_el(styles, "Normal"))
    set_keep(pPr, widow=True)
    set_spacing(pPr, 0.0, 0.0, BODY_LINE)
    set_jc(pPr, "both")
    normal.font.size = Pt(BODY_PT)
    normal.font.name = FONT_EN

    # 标题靠内置 Heading 样式提供大纲级别（目录域据此取条目）；
    # 段前 / 段后取正值，行距 1.15 倍——固定行距会裁字，见 set_spacing 注释
    for name, size_pt, level in (("Heading 1", H1_PT, 0),
                                 ("Heading 2", H2_PT, 1),
                                 ("Heading 3", H3_PT, 2)):
        before, after = H_GAP["h%d" % (level + 1)]
        st = styles[name]
        st_el = style_el(styles, name)
        strip_theme_fonts(st_el)
        pPr = fresh_pPr(st_el)
        set_keep(pPr, next_=True, lines=True)
        set_spacing(pPr, before, after, HEAD_LINE)
        set_ind(pPr, 0, 0)
        set_jc(pPr, "left")
        out = OxmlElement("w:outlineLvl")
        out.set(qn("w:val"), str(level))
        pPr.append(out)
        st.font.name = FONT_HEI
        st.font.size = Pt(size_pt)
        st.font.color.rgb = RGBColor(0, 0, 0)
        rfonts = st_el.get_or_add_rPr().get_or_add_rFonts()
        for attr in ("w:asciiTheme", "w:hAnsiTheme", "w:eastAsiaTheme", "w:cstheme"):
            if rfonts.get(qn(attr)) is not None:
                del rfonts.attrib[qn(attr)]
        for attr in ("w:ascii", "w:hAnsi", "w:eastAsia", "w:cs"):
            rfonts.set(qn(attr), FONT_HEI)

    # 图注：居中、宋体 10.5pt、无斜体、无缩进
    cap = styles.add_style("CaptionCN", WD_STYLE_TYPE.PARAGRAPH)
    cap.base_style = styles["Normal"]
    strip_theme_fonts(style_el(styles, "CaptionCN"))
    pPr = fresh_pPr(style_el(styles, "CaptionCN"))
    set_keep(pPr, next_=False, lines=True)
    set_spacing(pPr, 2.0, 8.0, TIGHT_LINE)
    set_ind(pPr, 0, 0)
    set_jc(pPr, "center")
    cap.font.name = FONT_EN
    cap.font.size = Pt(CAPTION_PT)
    cap.font.bold = False
    cap.font.italic = False
    cap.font.color.rgb = RGBColor(0, 0, 0)

    # 正文 / 列表：单独给样式名，验收脚本按样式取样，不再靠对齐方式猜
    for name, first, left, hanging in (("BodyCN", CHAR_TWIPS, 0, 0),
                                       ("ListCN", 0, 480, 240)):
        st = styles.add_style(name, WD_STYLE_TYPE.PARAGRAPH)
        st.base_style = styles["Normal"]
        strip_theme_fonts(style_el(styles, name))
        pPr = fresh_pPr(style_el(styles, name))
        set_keep(pPr, widow=True)
        set_spacing(pPr, 0.0, 0.0, BODY_LINE)
        set_ind(pPr, first, left, hanging)
        set_jc(pPr, "both")
        st.font.name = FONT_EN
        st.font.size = Pt(BODY_PT)
        st.font.color.rgb = RGBColor(0, 0, 0)

    # 封面段落样式
    for name, size_pt, after, bold in (("CoverTitle", 22.0, 20.0, True),
                                       ("CoverSub", 14.0, 10.0, False),
                                       ("CoverKind", 20.0, 0.0, True),
                                       ("CoverFoot", 12.0, 0.0, False)):
        st = styles.add_style(name, WD_STYLE_TYPE.PARAGRAPH)
        st.base_style = styles["Normal"]
        strip_theme_fonts(style_el(styles, name))
        pPr = fresh_pPr(style_el(styles, name))
        set_keep(pPr)
        set_spacing(pPr, 0.0, after, 1.1)
        set_ind(pPr, 0, 0)
        set_jc(pPr, "center")
        st.font.size = Pt(size_pt)
        st.font.bold = bold
        st.font.color.rgb = RGBColor(0, 0, 0)


def setup_section(section):
    section.page_width = Cm(PAGE_W_CM)
    section.page_height = Cm(PAGE_H_CM)
    section.top_margin = Cm(MARGIN_CM)
    section.bottom_margin = Cm(MARGIN_CM)
    section.left_margin = Cm(MARGIN_CM)
    section.right_margin = Cm(MARGIN_CM)
    section.header_distance = Cm(1.5)
    section.footer_distance = Cm(1.5)


def blank(section):
    """封面节：页眉页脚都清空（阻断继承，保证封面上什么也没有）。"""
    section.header.is_linked_to_previous = False
    section.footer.is_linked_to_previous = False
    for part in (section.header, section.footer):
        for par in part.paragraphs:
            clear_par(par)
            fmt_par(par, multiple=TIGHT_LINE)


def decorate(section, header_text=None, page_numbers=False):
    """目录 / 正文节：断开与上节的链接；正文再加页眉与「第 X 页 / 共 Y 页」。"""
    section.header.is_linked_to_previous = False
    section.footer.is_linked_to_previous = False
    hp = section.header.paragraphs[0]
    clear_par(hp)
    fmt_par(hp, multiple=TIGHT_LINE, align=WD_ALIGN_PARAGRAPH.CENTER)
    if header_text:
        write(hp, header_text, size_pt=SMALL_PT)
        set_border(hp._p.get_or_add_pPr(), "w:pBdr",
                   bottom={"val": "single", "sz": "6", "space": "1", "color": "000000"})
    fp = section.footer.paragraphs[0]
    clear_par(fp)
    fmt_par(fp, multiple=TIGHT_LINE, align=WD_ALIGN_PARAGRAPH.CENTER)
    if page_numbers:
        write(fp, FOOT_LEFT, size_pt=SMALL_PT)
        add_field(fp, " PAGE ", "1")
        write(fp, FOOT_MID, size_pt=SMALL_PT)
        add_field(fp, " NUMPAGES ", "1")
        write(fp, FOOT_RIGHT, size_pt=SMALL_PT)


def restart_page_numbering(section):
    """本节的 <w:pgNumType w:start="1"/>：正文页码从 1 起（封面 / 目录不计数重排）。"""
    sectPr = section._sectPr
    el = sectPr.find(qn("w:pgNumType"))
    if el is None:
        el = OxmlElement("w:pgNumType")
        # schema 顺序：pgNumType 在 pgMar 之后、cols 之前
        anchor = sectPr.find(qn("w:cols"))
        if anchor is None:
            anchor = sectPr.find(qn("w:docGrid"))
        if anchor is not None:
            anchor.addprevious(el)
        else:
            sectPr.append(el)
    el.set(qn("w:start"), "1")


def add_cover(doc):
    for _ in range(4):
        fmt_par(doc.add_paragraph(), multiple=BODY_LINE)
    p = doc.add_paragraph(style="CoverTitle")
    fmt_par(p, after=20.0, multiple=1.2, align=WD_ALIGN_PARAGRAPH.CENTER)
    write(p, COVER_TITLE, FONT_HEI, size_pt=22, bold=True)
    p = doc.add_paragraph(style="CoverSub")
    fmt_par(p, after=54.0, multiple=1.2, align=WD_ALIGN_PARAGRAPH.CENTER)
    write(p, COVER_SUBTITLE, size_pt=14)
    p = doc.add_paragraph(style="CoverKind")
    fmt_par(p, after=0.0, multiple=1.2, align=WD_ALIGN_PARAGRAPH.CENTER)
    write(p, COVER_KIND, FONT_HEI, size_pt=20, bold=True)
    for _ in range(7):
        fmt_par(doc.add_paragraph(), multiple=BODY_LINE)
    p = doc.add_paragraph(style="CoverFoot")
    fmt_par(p, after=0.0, multiple=1.2, align=WD_ALIGN_PARAGRAPH.CENTER)
    write(p, COVER_FOOT, size_pt=12)


def add_toc(doc):
    """目录页：「目录」标题（普通段，不占大纲标题）+ TOC 域。"""
    p = doc.add_paragraph()
    fmt_par(p, after=18.0, multiple=1.2, align=WD_ALIGN_PARAGRAPH.CENTER)
    write(p, "目录", FONT_HEI, size_pt=H1_PT, bold=True)
    p = doc.add_paragraph()
    fmt_par(p, after=0.0, multiple=TIGHT_LINE, align=WD_ALIGN_PARAGRAPH.LEFT)
    add_field(p, TOC_INSTR, TOC_PLACEHOLDER, size_pt=BODY_PT)


def set_update_fields(doc):
    """让 Word 打开时自动更新域（目录 / 页码因此不会停在占位文本）。"""
    el = OxmlElement("w:updateFields")
    el.set(qn("w:val"), "true")
    doc.settings.element.append(el)


def build(source: Path, out_path: Path):
    if not source.is_file():
        raise SystemExit("找不到内容源：%s" % source)
    title, blocks = parse_source(source)

    doc = Document()
    build_styles(doc)
    setup_section(doc.sections[0])
    add_cover(doc)

    sec_toc = doc.add_section(WD_SECTION.NEW_PAGE)
    setup_section(sec_toc)
    add_toc(doc)

    sec_body = doc.add_section(WD_SECTION.NEW_PAGE)
    setup_section(sec_body)
    restart_page_numbering(sec_body)

    counts = {"h2": 0, "h3": 0, "images": 0, "tables": 0}
    for kind, payload in blocks:
        if kind in ("pagebreak",):
            continue
        if kind == "h2":
            counts["h2"] += 1
            before, after = H_GAP["h1"]
            add_runs(fmt_par(doc.add_paragraph(style="Heading 1"), before=before,
                             after=after, multiple=HEAD_LINE,
                             align=WD_ALIGN_PARAGRAPH.LEFT),
                     payload, size_pt=H1_PT, base_bold=True, base_cn=FONT_HEI)
        elif kind == "h3":
            counts["h3"] += 1
            before, after = H_GAP["h2"]
            add_runs(fmt_par(doc.add_paragraph(style="Heading 2"), before=before,
                             after=after, multiple=HEAD_LINE,
                             align=WD_ALIGN_PARAGRAPH.LEFT),
                     payload, size_pt=H2_PT, base_bold=True, base_cn=FONT_HEI)
        elif kind == "p":
            add_runs(fmt_par(doc.add_paragraph(style="BodyCN"), multiple=BODY_LINE,
                             first=CHAR_TWIPS), payload)
        elif kind == "ul":
            for item in payload:
                add_runs(fmt_par(doc.add_paragraph(style="ListCN"),
                                 multiple=BODY_LINE, left=480, hanging=240), item)
        elif kind == "ol":
            for num, item in payload:
                add_runs(fmt_par(doc.add_paragraph(style="ListCN"),
                                 multiple=BODY_LINE, left=480, hanging=240),
                         "%d. %s" % (num, item))
        elif kind == "image":
            src = (DOCS_DIR / payload).resolve()
            if not src.is_file():
                raise SystemExit("插图不存在：%s（按相对 docs/ 解析）" % payload)
            render_image(doc, src)
            counts["images"] += 1
        elif kind == "caption":
            add_runs(fmt_par(doc.add_paragraph(style="CaptionCN"),
                             multiple=TIGHT_LINE,
                             align=WD_ALIGN_PARAGRAPH.CENTER),
                     payload, size_pt=CAPTION_PT)
        elif kind == "table":
            render_table(doc, payload)
            counts["tables"] += 1
            fmt_par(doc.add_paragraph(), multiple=0.6)      # 表后间隔（细空行）
        else:  # pragma: no cover
            raise ValueError("未知块类型 %r" % kind)

    blank(doc.sections[0])
    decorate(doc.sections[1])
    decorate(doc.sections[2], HEADER_TEXT, page_numbers=True)
    set_update_fields(doc)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    doc.save(str(out_path))

    # ---- 自检：读回统计 + 禁项扫描 ----
    back = Document(str(out_path))
    h1 = sum(1 for p in back.paragraphs if p.style.name == "Heading 1")
    h2 = sum(1 for p in back.paragraphs if p.style.name == "Heading 2")
    h3 = sum(1 for p in back.paragraphs if p.style.name == "Heading 3")
    text_all = "\n".join(p.text for p in back.paragraphs)
    for t in back.tables:
        for row in t.rows:
            for cell in row.cells:
                text_all += "\n" + cell.text
    header_texts, footer_texts = [], []
    for s in back.sections:
        header_texts += [p.text for p in s.header.paragraphs]
        footer_texts += [p.text for p in s.footer.paragraphs]
    text_all += "\n" + "\n".join(header_texts + footer_texts)
    hits = forbidden_hits(text_all)
    # 行距回归检查：全文（含样式与页眉页脚）不得出现 fixed / exact 行距，
    # 也不得有 line 值小于单倍（240）的倍数行距——那正是标题压字的成因
    import zipfile
    with zipfile.ZipFile(str(out_path)) as zf:
        xml_all = "".join(zf.read(n).decode("utf-8", "replace")
                          for n in zf.namelist()
                          if n.endswith(".xml") and n.startswith("word/"))
    bad_rule = re.findall(r'w:lineRule="(exact|atLeast|fixed)"', xml_all)
    # 144 = 表后细空行的 0.6 倍行距（本脚本有意为之），不算问题
    tiny_line = sorted({int(v) for v in re.findall(r'w:line="(\d+)"', xml_all)
                        if int(v) < 240 and int(v) != 144})
    spacing_issues = []
    if bad_rule:
        spacing_issues.append("固定行距 %s" % sorted(set(bad_rule)))
    if tiny_line:
        spacing_issues.append("行距倍数 < 1.0 的 w:line 值 %s" % tiny_line)
    stats = {
        "source": str(source),
        "source_title": title,
        "docx": str(out_path),
        "paragraphs": len(back.paragraphs),
        "headings": {"h1": h1, "h2": h2, "h3": h3, "total": h1 + h2 + h3},
        "tables": len(back.tables),
        "images": len(back.inline_shapes),
        "sections": len(back.sections),
        "headers": header_texts,
        "footers": footer_texts,
        "bytes": out_path.stat().st_size,
        "forbidden_hits": hits,
        "spacing_issues": spacing_issues,
        "source_blocks": {"chapters": counts["h2"], "sections": counts["h3"],
                          "images": counts["images"], "tables": counts["tables"]},
    }
    return stats


def main(argv=None):
    ap = argparse.ArgumentParser(description="技术文档 Markdown → 作品报告 DOCX")
    ap.add_argument("-Source", "--source", default=str(DEFAULT_SOURCE))
    ap.add_argument("-Out", "--out", default=str(DEFAULT_OUT))
    ap.add_argument("-Stats", "--stats", default=None, help="自检统计写成 JSON")
    args = ap.parse_args(argv)
    try:
        stats = build(Path(args.source).resolve(), Path(args.out).resolve())
    except SystemExit as exc:
        print("[error] %s" % exc.code, file=sys.stderr)
        return 1
    except ValueError as exc:
        print("[error] %s" % exc, file=sys.stderr)
        return 1
    print("[md2docx] source    : %s" % stats["source"])
    print("[md2docx] docx      : %s" % stats["docx"])
    print("[md2docx] paragraphs: %d   sections: %d" % (stats["paragraphs"],
                                                       stats["sections"]))
    print("[md2docx] headings  : H1(章)=%d H2(节)=%d H3=%d"
          % (stats["headings"]["h1"], stats["headings"]["h2"],
             stats["headings"]["h3"]))
    print("[md2docx] tables    : %d   images: %d" % (stats["tables"], stats["images"]))
    print("[md2docx] footer    : %s" % " | ".join(t for t in stats["footers"] if t))
    print("[md2docx] bytes     : %d" % stats["bytes"])
    if stats["forbidden_hits"]:
        print("[md2docx] 禁项扫描  : 命中 %s —— 不得提交"
              % ",".join(stats["forbidden_hits"]), file=sys.stderr)
        return 1
    print("[md2docx] 禁项扫描  : 通过（无学校名 / 指导老师字样）")
    if stats["spacing_issues"]:
        print("[md2docx] 行距检查  : 异常 %s —— 会出现标题 / 正文叠字"
              % "；".join(stats["spacing_issues"]), file=sys.stderr)
        return 1
    print("[md2docx] 行距检查  : 通过（全篇倍数行距，无 fixed/exact）")
    if args.stats:
        Path(args.stats).write_text(json.dumps(stats, ensure_ascii=False, indent=2),
                                    encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
