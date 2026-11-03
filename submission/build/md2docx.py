# -*- coding: utf-8 -*-
"""submission/技术文档.md → submission/out/龙瞳分拣_作品报告.docx（纯论文式单节版式）。

版式令牌见本文件顶部常量与 build/README.md「报告排版必须守住的结论」；参考值取自参照论文 PDF 实测。
本脚本只读内容源（submission/技术文档.md，只此一份），从不改写；插图路径相对源文件
所在目录解析。只认 build/README.md「源写法」表的写法——
  `#` 唯一标题、`**标签**：值` 元信息行（**仅**标题后连续行，上限 6 行；正文里行首同形的
  `**标签**：说明…` 按普通正文段渲染为粗体引导词）、`##` 一级标题、
  `###` 二级标题、`####` 三级标题、普通段落（连续行合并）、`**强调**`、`__下划线强调__`、`` `行内代码` ``、
  `![alt](图)` + 下一行 `**图 N**　说明。`、表格上一行 `**表 N**　说明`、`| … |` 表格
  （分隔行 `|:---|:---:|---:|`：破折号个数 = 该列宽度权重、冒号 = 左 / 中 / 右对齐；
  破折号个数全表相同时回退自动列宽）、`1. ` 列表（仅参考文献 / 附录）、`---` 分隔线；
  旧 `*图 N 说明*` 斜体图注兼容。
  超范围写法（代码块 / 引用块 / 五级标题 / 项目符号列表 / 图文混排）报错退出并指出行号。

版式令牌（单位 pt）：A4 21×29.7cm、页边距上下左右 2.4cm、页眉 logo 距边 1.2cm、页脚距边 1.6cm、
单节、无封面 / 无目录；页眉为官方模板「芯辰大海」logo（6.5cm 居中，下方 0.75pt 细线，无文字）；
正文楷体小四 12pt + 西文 Times New Roman、首行缩进 2 字符（480 twips）、1.5 倍行距、两端对齐；
作品名黑体 17.5pt 居中（标题块维持原版式）+ 标题后连续 10pt 元信息行（标签黑体、值楷体）；
一级章节标题（`##`）黑体 15pt 居中加粗、二 / 三级章节标题楷体四号 14pt 加粗，均 2 倍行距；
图注 / 表注楷体加粗五号 10.5pt 居中，表注在表格上方；三线表 9pt 楷体（顶 / 底 1.5pt、表头下
0.75pt，无竖线 / 行线 / 底纹，整表 15.6cm 固定布局，表头跨页重复、行不跨页、单元格顶对齐、
行高只按内容；列宽可由分隔行破折号权重控制，长 token 有硬下限）；图宽 15.8cm、居中；
行内代码 Consolas 11pt 无底纹；页脚居中 PAGE 域 11pt；全篇纯黑。

行距一律用倍数（lineRule=auto，单倍 = 240、正文 360、章节标题 480）：固定行距会裁字并让
上下段叠在一起，见 set_spacing。楷体单倍行高系数与宋体相近（≈1.29×字号），估算用。

生成后自检（读回 docx 复核本节令牌：节数 / 页面 / 页边距 / 页脚 / 页眉 logo 无文字 / 无目录域 /
字号 / 首行缩进 / 表宽与线型 / 图注加粗 / 表注加粗 / 禁项 / 固定行距 / 表列宽与分隔行
权重偏差 ≤8% / 单格 >6 行告警），任一不过退出码 1。
自检统计写 -Stats JSON（保留旧字段，新增 placeholders / captions / table_warnings）。

用法（cwd 任意，路径都能用相对 cwd 或绝对路径）：
    python submission/build/md2docx.py [-Source <md>] [-Out <docx>] [-Stats <json>]
                                       [--fig-width <cm>] [--caption-clean]

依赖 python-docx（`python -m pip show python-docx`）。本文件不含主机绝对路径：源、
输出、插图位置全部从脚本位置 __file__ 推导。退出码 0 成功 / 1 失败。
"""
from __future__ import annotations

import argparse
import json
import re
import struct
import sys
import unicodedata
import zipfile
from pathlib import Path

try:
    from docx import Document
    from docx.enum.style import WD_STYLE_TYPE
    from docx.enum.table import WD_CELL_VERTICAL_ALIGNMENT, WD_TABLE_ALIGNMENT
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
FIGURES_DIR = SUBMISSION / "figures"                 # 插图与页眉 logo（logo 入库、其余生成）
DEFAULT_SOURCE = SUBMISSION / "技术文档.md"
DEFAULT_OUT = SUBMISSION / "out" / "龙瞳分拣_作品报告.docx"

# ---------------------------------------------------------------- 设计令牌
# 全篇只有黑色：不参与信息表达的颜色一律不出现（无灰、无底纹、无彩线）。
C_INK = "000000"          # 黑：正文 / 标题 / 表格线 / 图注 / 页脚
C_LINE = C_INK            # 三线表线色（旧名保留）

FONT_CN = "楷体"            # 中文正文（规格：正文楷体小四；标题 / 表格 / 图注同族）
FONT_HEI = "黑体"           # 作品名 / 元信息标签 / 一级标题（楷体无粗体字面，一级标题靠黑体真加粗）
FONT_EN = "Times New Roman"  # 西文与数字（规格：英文全部 Times New Roman）
FONT_CODE = "Consolas"     # 行内代码（寄存器名 / 地址，保留等宽）

PAGE_W_CM, PAGE_H_CM = 21.0, 29.7            # A4
MARGIN_CM = 2.4                              # 四边 2.4cm → 版心 16.2×24.9cm
TEXT_W_CM = PAGE_W_CM - 2 * MARGIN_CM        # 16.2cm
FOOTER_DIST_CM = 1.6                         # 页脚距边 1.6cm
TABLE_W_CM = 15.6                            # 整表上限，留 6mm 余量
FIG_MAX_CM = 15.8                            # 插图默认宽度 15.8cm

BODY_PT = 12.0                               # 正文：楷体小四
TITLE_PT = 17.5                              # 作品名（标题块维持，规格只约束章节标题）
H1_PT = 15.0                                 # 一级标题（源 `##`）：黑体 15pt 居中加粗
H2_PT = 14.0                                 # 二级标题（源 `###`）：楷体四号加粗、左对齐
H3_PT = 14.0                                 # 三级标题（源 `####`）：楷体四号加粗、左对齐
META_PT = 10.0                               # 元信息行
CAPTION_PT = 10.5                            # 图注 / 表注：五号（加粗）
TABLE_PT = 11.0                              # 表体（楷体，11-04 按作者手调从 9pt 上调）
TABLE_HEAD_PT = 11.0                         # 表头（楷体加粗）
CODE_PT = 11.0                               # 行内代码（正文升小四后等宽同步 +1pt）
LIST_PT = 12.0                               # 列表（仅参考文献 / 附录，按正文小四）
FOOTER_PT = 11.0                             # 页脚页码

# 行距一律倍数（lineRule=auto）；单倍 = 1.0。
SINGLE_LINE = 1.0                            # 标题块 / 元信息 / 图注表注 / 表格 / 页脚
BODY_LINE = 1.5                              # 正文（规格：1.5 倍）
HEAD_LINE = 2.0                              # 章节标题（规格：2 倍）
LINE_FACTOR = 1.286                          # 单倍行高 / 字号（估算用，仅喂告警不算分）

TITLE_GAP = (24.0, 10.0)                     # 标题段前 / 段后
META_AFTER = 1.0                             # 元信息行段后
H_GAP = {"h1": (14.0, 8.0), "h2": (12.0, 6.0), "h3": (10.0, 5.0)}
FIG_GAP = (8.0, 3.0)                         # 插图段前 / 段后
FIGCAP_GAP = (0.0, 6.0)                      # 图注段前 / 段后
TABCAP_GAP = (6.0, 3.0)                      # 表注段前 / 段后（表注在表格上方）
LIST_AFTER = 2.0                             # 列表条目段后
BODY_AFTER = 0.0                             # 正文段后（段落靠首行缩进区分）
SPACER_LINE = 0.6                            # 表后细空行（144）

TWIPS_PER_CM = 566.929
CHAR_TWIPS = int(round(BODY_PT * 20 * 2))    # 首行缩进 2 字符 = 2em = 480 twips @12pt
LIST_LEFT_TW = int(round(LIST_PT * 20 * 1.5))   # 列表左缩进 1.5 字符（12pt → 360 twips）
LIST_HANG_TW = LIST_LEFT_TW                  # 悬挂 1.5 字符（标记落在左边距）
CELL_MAR_TW = 108                            # 单元格左右内边距
TABLE_HEAD_ROW_TW = 300                      # 表头行最小行高（15pt，给 9pt 字喘气）

# 个别图想窄一点：按源文件名覆盖宽度，单位 cm。键 = ![](...) 里的文件名。
FIG_WIDTH_OVERRIDE: dict[str, float] = {}

# ---------------------------------------------------------------- 图注净化
# 默认**关闭**：约定新源里的图注就是最终说明（替换实物图时只换 PNG、
# 不动文本），净化反而可能删掉有证据价值的括号。旧源的「生成脚本 / SVG / PPT」类
# 构建说明仍可加 --caption-clean 打开（按括号逐组判断，keep 优先于 strip）。
CAPTION_STRIP_RULES = (
    r"make_[A-Za-z0-9_]*\.py",               # 插图生成脚本名
    r"figures/",                             # 插图目录（仓库内部路径）
    r"\.svg\b",                              # SVG 源文件
    r"\bSVG\b",
    r"\bPPT\b",                              # 还能放进 PPT 之类
    r"md2docx|build\.ps1|make_ppt",          # 排版 / 生成链脚本
    r"生成",                                  # 「…生成」
)
CAPTION_KEEP_RULES = (
    r"src/",                                 # 代码 / 约束文件出处（证据）
    r"docs/",                                # 文档出处（证据）
    r"实测|测量|取自|默认配置|按比例|示意",       # 口径说明（证据）
)
CAPTION_PAREN_RE = re.compile(r"[（(]([^（）()]*)[）)]")

# 竞赛禁项自检：学校 / 学院 / 指导老师字样一律不得出现。
# 官方赛事名里天然含「大学生」「学校」，先按整串剔除再扫，避免误报。
FORBIDDEN = ("大学", "学院", "学校", "指导老师", "指导教师", "导师",
             "校徽", "学号", "班级", "队名")
ALLOWED_PHRASES = ("2026 全国大学生嵌入式芯片与系统设计竞赛",
                   "全国大学生嵌入式芯片与系统设计竞赛")


def forbidden_hits(text):
    rest = text
    for phrase in ALLOWED_PHRASES:
        rest = rest.replace(phrase, "")
    return sorted({w for w in FORBIDDEN if w in rest})


def clean_caption(text, strip=None, keep=None):
    """图注净化：逐组括号判断该留该删，返回 (净化后文本, 被删掉的括号列表)。"""
    strip_res = [re.compile(p) for p in (CAPTION_STRIP_RULES if strip is None else strip)]
    keep_res = [re.compile(p) for p in (CAPTION_KEEP_RULES if keep is None else keep)]
    removed = []

    def repl(m):
        inner = m.group(1)
        if any(k.search(inner) for k in keep_res):
            return m.group(0)
        if any(s.search(inner) for s in strip_res):
            removed.append(m.group(0))
            return ""
        return m.group(0)

    new = CAPTION_PAREN_RE.sub(repl, text)
    new = re.sub(r"[ \t]{2,}", " ", new).strip()
    if not new or not re.sub(r"^(图|表)\s*[\d.\-–]+\s*", "", new).strip():
        return text, []                      # 删空了：退回原文，不当净化
    return new, removed


ALIGN = {WD_ALIGN_PARAGRAPH.LEFT: "left", WD_ALIGN_PARAGRAPH.CENTER: "center",
         WD_ALIGN_PARAGRAPH.RIGHT: "right", WD_ALIGN_PARAGRAPH.JUSTIFY: "both"}

# ---------------------------------------------------------------- 基础工具


def _set_size(rpr, size_pt):
    """在 <w:rPr> 上写 pt 字号（w:sz 与 w:szCs 都写，半磅整数）。"""
    for tag in ("w:sz", "w:szCs"):
        el = rpr.find(qn(tag))
        if el is None:
            el = OxmlElement(tag)
            rpr.append(el)
        el.set(qn("w:val"), str(int(round(size_pt * 2))))


def set_run_font(run, cn=FONT_CN, en=FONT_EN, size_pt=None, bold=None, color=C_INK,
                 underline=None):
    """显式钉死西文 / 中文 / cs 三套字体名（只设 run.font.name 管不到中文）+ 字色。"""
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
        run.font.size = Pt(size_pt)          # 半磅粒度：只传 .0 / .5 结尾的值
        _set_size(rpr, size_pt)
    if bold is not None:
        run.font.bold = bold
        run.font.italic = False
    if underline is not None:
        run.font.underline = underline
    if color:
        run.font.color.rgb = RGBColor.from_string(color)
    return run


def write(par, text, cn=FONT_CN, en=FONT_EN, size_pt=BODY_PT, bold=False, color=C_INK,
          underline=None):
    run = par.add_run(text)
    set_run_font(run, cn, en, size_pt, bold, color, underline)
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


def fresh_pPr(st_el):
    """拿一个干净的 pPr：把样式上可能继承的旧段落格式整块清掉。"""
    old = st_el.find(qn("w:pPr"))
    if old is not None:
        st_el.remove(old)
    return st_el.get_or_add_pPr()


def set_spacing(pPr, before_pt=0.0, after_pt=0.0, multiple=None):
    """段距 / 行距。行距只用**倍数**（lineRule=auto），绝不用 fixed / exact：

    固定行距会把行框钉死在指定高度，中文字号略大（或字体回退到更大的字形）时上行
    会把下行盖住——实测标题配 exact 会叠字。倍数行距由 Word 按字号算行高，天然不裁字。
    线号 240 = 单倍。
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


def set_jc(pPr, val):
    pPr.get_or_add_jc().set(qn("w:val"), val)


def _no_snap(pPr):
    """行距按倍数算，别吸附到文档网格（网格行高会覆盖单倍行距）。"""
    el = pPr.find(qn("w:snapToGrid"))
    if el is None:
        el = OxmlElement("w:snapToGrid")
        pPr.append(el)
    el.set(qn("w:val"), "0")


def _no_overflow_punct(pPr):
    """关掉「允许标点溢出边界」：行末标点不再挂到版心外，页边距 2.4cm 实测才站得住。"""
    el = _ordered_child(pPr, "w:overflowPunct", _PPR_ORDER)
    el.set(qn("w:val"), "0")


def fmt_par(par, *, before=0.0, after=0.0, multiple=SINGLE_LINE, first=0, left=0,
            hanging=0, align=WD_ALIGN_PARAGRAPH.JUSTIFY, keep_next=False,
            keep_lines=False, widow=True, page_break_before=False):
    """段落直接格式：一律显式写，不依赖样式继承。行距只给倍数。

    widow=True 是默认值：孤行 / 寡行控制开着，段首或段尾的单行不会被甩到相邻页。
    整段不许跨页用 keep_lines=True，必须与下段同页（标题、图、表注）用 keep_next=True。
    """
    pPr = par._p.get_or_add_pPr()
    old = pPr.find(qn("w:spacing"))
    if old is not None:
        pPr.remove(old)
    set_keep(pPr, keep_next, keep_lines, widow)
    set_spacing(pPr, before, after, multiple)
    set_ind(pPr, first, left, hanging)
    set_jc(pPr, ALIGN[align])
    _no_snap(pPr)
    _no_overflow_punct(pPr)
    pb = pPr.find(qn("w:pageBreakBefore"))
    if pb is None:
        pb = OxmlElement("w:pageBreakBefore")
        pPr.append(pb)
    pb.set(qn("w:val"), "1" if page_break_before else "0")
    return par


def set_para_mark_font(par, size_pt=1.0, color=C_INK):
    """段落标记的字号 / 颜色（空段落靠它控制行高）。"""
    pPr = par._p.get_or_add_pPr()
    rpr = pPr.find(qn("w:rPr"))
    if rpr is None:
        rpr = OxmlElement("w:rPr")
        pPr.append(rpr)
    set_rfonts_element(rpr)
    _set_size(rpr, size_pt)
    el = rpr.find(qn("w:color"))
    if el is None:
        el = OxmlElement("w:color")
        rpr.append(el)
    el.set(qn("w:val"), color)


def add_field(par, instr, placeholder, cn=FONT_CN, size_pt=FOOTER_PT, bold=False,
              color=C_INK):
    """插入 Word 域：fldChar begin → instrText → separate → 占位 → end。"""
    def marker(kind):
        r = par.add_run()
        set_run_font(r, cn, FONT_EN, size_pt, bold, color)
        el = OxmlElement("w:fldChar")
        el.set(qn("w:fldCharType"), kind)
        r._element.append(el)

    marker("begin")
    r = par.add_run()
    set_run_font(r, cn, FONT_EN, size_pt, bold, color)
    instr_el = OxmlElement("w:instrText")
    instr_el.set(qn("xml:space"), "preserve")
    instr_el.text = instr
    r._element.append(instr_el)
    marker("separate")
    if placeholder:
        write(par, placeholder, cn, size_pt=size_pt, bold=bold, color=color)
    marker("end")


# ---------------------------------------------------------------- OOXML 小工具
# Word 对 pPr / rPr / tblPr / tcPr 的子元素顺序有 schema 要求（顺序错了 Word 会
# 忽略整段格式，python-docx 的 get_or_add_ 只覆盖它自己认识的元素）。这里按
# ECMA-376 的顺序表把新增元素插到正确位置，保证设计令牌真能落到 PDF 里。
_PPR_ORDER = ("w:pStyle", "w:keepNext", "w:keepLines", "w:pageBreakBefore",
              "w:framePr", "w:widowControl", "w:numPr", "w:suppressLineNumbers",
              "w:pBdr", "w:shd", "w:tabs", "w:suppressAutoHyphens", "w:kinsoku",
              "w:wordWrap", "w:overflowPunct", "w:topLinePunct", "w:autoSpaceDE",
              "w:autoSpaceDN", "w:bidi", "w:adjustRightInd", "w:snapToGrid",
              "w:spacing", "w:ind", "w:contextualSpacing", "w:mirrorIndents",
              "w:suppressOverlap", "w:jc", "w:textDirection", "w:textAlignment",
              "w:textboxTightWrap", "w:outlineLvl", "w:divId", "w:cnfStyle",
              "w:rPr", "w:sectPr", "w:pPrChange")
_RPR_ORDER = ("w:rStyle", "w:rFonts", "w:b", "w:bCs", "w:i", "w:iCs", "w:caps",
              "w:smallCaps", "w:strike", "w:dstrike", "w:outline", "w:shadow",
              "w:emboss", "w:imprint", "w:noProof", "w:snapToGrid", "w:vanish",
              "w:webHidden", "w:color", "w:spacing", "w:w", "w:kern", "w:position",
              "w:sz", "w:szCs", "w:highlight", "w:u", "w:effect", "w:bdr",
              "w:shd", "w:fitText", "w:vertAlign", "w:rtl", "w:cs", "w:em",
              "w:lang", "w:eastAsianLayout", "w:specVanish", "w:oMath")
_TBLPR_ORDER = ("w:tblStyle", "w:tblpPr", "w:tblOverlap", "w:bidiVisual",
                "w:tblStyleRowBandSize", "w:tblStyleColBandSize", "w:tblW",
                "w:jc", "w:tblCellSpacing", "w:tblInd", "w:tblBorders", "w:shd",
                "w:tblLayout", "w:tblCellMar", "w:tblLook", "w:tblCaption",
                "w:tblDescription", "w:tblPrChange")
_TCPR_ORDER = ("w:cnfStyle", "w:tcW", "w:gridSpan", "w:hMerge", "w:vMerge",
               "w:tcBorders", "w:shd", "w:noWrap", "w:tcMar", "w:textDirection",
               "w:tcFitText", "w:vAlign", "w:hideMark", "w:headers", "w:cellIns",
               "w:cellDel", "w:cellMerge", "w:tcPrChange")
_TRPR_ORDER = ("w:cnfStyle", "w:divId", "w:gridBefore", "w:gridAfter",
               "w:wBefore", "w:wAfter", "w:cantSplit", "w:trHeight", "w:tblHeader",
               "w:tblCellSpacing", "w:jc", "w:hidden", "w:ins", "w:del",
               "w:trPrChange")


def _ordered_child(parent, tag, order):
    """取 parent 下的 tag 子元素；没有就按 order 插到正确位置再返回。"""
    el = parent.find(qn(tag))
    if el is not None:
        return el
    pos = order.index(tag)
    for child in list(parent):
        for i, name in enumerate(order):
            if child.tag == qn(name):
                if i > pos:
                    el = OxmlElement(tag)
                    child.addprevious(el)
                    return el
                break
    el = OxmlElement(tag)
    parent.append(el)
    return el


def set_rfonts_element(rpr, cn=FONT_CN, en=FONT_EN):
    """在 <w:rPr>（run 或样式的）上钉死三套字体名，清掉主题字体引用。"""
    rfonts = _ordered_child(rpr, "w:rFonts", _RPR_ORDER)
    for attr in ("w:asciiTheme", "w:hAnsiTheme", "w:eastAsiaTheme", "w:cstheme"):
        if rfonts.get(qn(attr)) is not None:
            del rfonts.attrib[qn(attr)]
    rfonts.set(qn("w:ascii"), en)
    rfonts.set(qn("w:hAnsi"), en)
    rfonts.set(qn("w:eastAsia"), cn)
    rfonts.set(qn("w:cs"), en)


def set_char_spacing(run, twips):
    """字符间距（twips，1/20 pt，可为负）：元信息行一行放不下时轻微收紧用。"""
    rpr = run._element.get_or_add_rPr()
    el = _ordered_child(rpr, "w:spacing", _RPR_ORDER)
    el.set(qn("w:val"), str(int(twips)))


def set_cell_border(cell, **edges):
    """单元格边框（三线表的表头下细线用）。"""
    tcPr = cell._tc.get_or_add_tcPr()
    old = tcPr.find(qn("w:tcBorders"))
    if old is not None:
        tcPr.remove(old)
    if not any(edges.values()):
        return
    bdr = _ordered_child(tcPr, "w:tcBorders", _TCPR_ORDER)
    for edge in ("top", "left", "bottom", "right"):
        cfg = edges.get(edge)
        if not cfg:
            continue
        el = OxmlElement("w:" + edge)
        el.set(qn("w:val"), cfg.get("val", "single"))
        el.set(qn("w:sz"), str(cfg.get("sz", 4)))
        el.set(qn("w:space"), str(cfg.get("space", 0)))
        el.set(qn("w:color"), cfg.get("color", C_INK))
        bdr.append(el)


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
# 图注 / 表注：`**图 N**　说明。` / `**表 N**　说明`（编号与说明间为全角空格）
CAP_CAPTION_RE = re.compile(r"^\*\*(图|表)\s*([0-9]+(?:[-–.][0-9]+)*)\*\*\s*(.*)$")
# 元信息行：`**标签**：值`（标题后连续行，遇空行结束；上限 6 行）
META_RE = re.compile(r"^\*\*([^*]+?)\*\*\s*[：:]\s*(.*)$")
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


def parse_sep_cells(line):
    """解析表格分隔行（SPEC 源写法扩展）：

    - **破折号个数 = 该列相对宽度权重**（归一化到整表 15.6cm；全表相同时回退自动算法）；
    - **冒号 = 对齐**：` :--- ` 左 / ` :---: ` 中 / ` ---: ` 右；都不写 = 沿用默认对齐。
    """
    specs = []
    for cell in split_cells(line):
        c = cell.strip()
        left = c.startswith(":")
        right = len(c) > 1 and c.endswith(":")
        if left and right:
            align = "center"
        elif left:
            align = "left"
        elif right:
            align = "right"
        else:
            align = None
        specs.append({"weight": max(c.count("-"), 1), "align": align})
    return specs


def _caption_text(kind, num, desc):
    """统一成约定的 `图 N　说明` 写法（全角空格分隔编号与说明）。"""
    desc = desc.strip()
    if desc:
        return "%s %s　%s" % (kind, num, desc)
    return "%s %s" % (kind, num)


def parse_source(md_path: Path):
    """拆块；遇到不认识的写法直接抛错。返回 (标题, blocks)。

    blocks 为 (kind, payload, 行号) 三元组，kind ∈
    meta / h2 / h3 / p / image / figcap / tabcap / table / ol。
    """
    lines = md_path.read_text(encoding="utf-8").replace(
        "\r\n", "\n").replace("\r", "\n").split("\n")
    title, blocks, buf = None, [], []
    meta_count = 0
    meta_open = False                 # 标题后到第一条正文 / 空行之间：元信息窗口
    body_started = False
    n = len(lines)

    def flush():
        nonlocal meta_open
        if buf:
            blocks.append(("p", " ".join(x.strip() for x in buf), 0))
            buf.clear()
            meta_open = False

    def push(kind, payload, lineno, is_meta=False):
        nonlocal body_started, meta_open
        flush()
        blocks.append((kind, payload, lineno))
        if not is_meta:
            body_started = True
            meta_open = False

    i = 0
    while i < n:
        s = lines[i].strip()
        if not s:
            flush()
            if meta_count:            # 元信息块以第一个空行结束
                meta_open = False
            i += 1
            continue
        if s.startswith("```"):
            raise ValueError("第 %d 行：不支持代码块（论文体不用）" % (i + 1))
        if re.match(r"^-{3,}$", s):
            flush()
            i += 1
            continue
        if s.startswith("#### "):
            push("h4", s[5:].strip(), i + 1)
            i += 1
            continue
        if s.startswith("### "):
            push("h3", s[4:].strip(), i + 1)
            i += 1
            continue
        if s.startswith("## "):
            push("h2", s[3:].strip(), i + 1)
            i += 1
            continue
        if s.startswith("# "):
            flush()
            if title is not None:
                raise ValueError("源里出现多个一级标题，只能有一个作论文标题")
            title = s[2:].strip()
            meta_open = True          # 标题后开始元信息窗口（允许标题与元信息间有空行）
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
            seps = parse_sep_cells(tlines[1])
            if len(seps) != len(rows[0]):
                raise ValueError("表格分隔行列数 %d 与表头列数 %d 不一致：%r"
                                 % (len(seps), len(rows[0]), tlines[1][:60]))
            weights = [s["weight"] for s in seps]
            hinted = len(set(weights)) > 1          # 破折号个数全表相同时回退自动列宽
            push("table", {"rows": rows, "seps": seps, "hinted": hinted}, i + 1)
            continue
        m = IMG_RE.match(s)
        if m and m.group(0) == s:
            push("image", m.group(1), i + 1)
            i += 1
            continue
        if IMG_RE.search(s):
            raise ValueError("第 %d 行：插图请独占一行，不支持图文混排" % (i + 1))
        cm = CAP_CAPTION_RE.match(s)
        if cm:
            kind = "figcap" if cm.group(1) == "图" else "tabcap"
            push(kind, _caption_text(cm.group(1), cm.group(2), cm.group(3)), i + 1)
            i += 1
            continue
        cm = CAPTION_RE.match(s)
        if cm and not s.startswith("**"):
            # 旧写法 `*图 2-1 说明*`：只用于图注，兼容为同一套加粗 10pt 图注
            old = cm.group(1).strip()
            om = re.match(r"^(图|表)\s*([0-9]+(?:[-–.][0-9]+)*)\s*(.*)$", old)
            if not om:
                raise ValueError("第 %d 行：不支持整行斜体（旧源里只用于图注）"
                                 % (i + 1))
            kind = "figcap" if om.group(1) == "图" else "tabcap"
            push(kind, _caption_text(om.group(1), om.group(2), om.group(3)), i + 1)
            i += 1
            continue
        if s.startswith("- "):
            raise ValueError("第 %d 行：论文体不用项目符号列表——改成「（1）…（2）…」"
                             "或表格" % (i + 1))
        mm = OL_RE.match(s)
        if mm:
            flush()
            items = []
            while i < n:
                mo = OL_RE.match(lines[i].strip())
                if not mo:
                    break
                items.append((int(mo.group(1)), mo.group(2), i + 1))
                i += 1
            push("ol", items, items[0][2])
            continue
        if s.startswith(">"):
            raise ValueError("第 %d 行：不支持引用块（论文体不用）" % (i + 1))
        mm = META_RE.match(s)
        if mm:
            if title is None:
                raise ValueError("第 %d 行：元信息行不能出现在标题之前" % (i + 1))
            if not meta_open:
                # 正文里的行首粗体引导词（`**标签**：说明…`）与元信息行同形，靠位置区分：
                # 只有「标题后、第一条正文前」的连续行才是元信息，其余按普通正文段渲染。
                buf.append(s)
                i += 1
                continue
            if meta_count >= 6:
                raise ValueError("第 %d 行：元信息行最多 6 行" % (i + 1))
            meta_count += 1
            push("meta", (mm.group(1).strip(), mm.group(2).strip()), i + 1,
                 is_meta=True)
            i += 1
            continue
        buf.append(s)
        i += 1
    flush()
    if title is None:
        raise ValueError("源里没有一级标题（`# 论文标题`），无法生成标题块")

    # ---- 图注 / 表注与图表的配对检查（图注在图下一行、表注在表格上一行）
    for idx, (kind, _payload, lineno) in enumerate(blocks):
        prev = blocks[idx - 1][0] if idx > 0 else None
        nxt = blocks[idx + 1][0] if idx + 1 < len(blocks) else None
        if kind == "figcap" and prev != "image":
            raise ValueError("第 %d 行：图注必须紧跟插图行（图下一行）" % lineno)
        if kind == "image" and nxt != "figcap":
            raise ValueError("第 %d 行：插图下一行必须是「**图 N**　说明」图注"
                             % lineno)
        if kind == "tabcap" and nxt != "table":
            raise ValueError("第 %d 行：表注必须紧贴表格上方（表格上一行）" % lineno)
        if kind == "table" and prev != "tabcap":
            raise ValueError("第 %d 行：表格上一行必须是「**表 N**　说明」表注"
                             % lineno)
    return title, blocks


# ---------------------------------------------------------------- 行内文本

INLINE_RE = re.compile(r"(\*\*.+?\*\*|__.+?__|`[^`]+`)")
# CJK / 全角字符（强调的西文部分走 Times Bold）
CJK_RE = re.compile(r"[\u2e80-\u303f\u3200-\u4dbf\u4e00-\u9fff"
                    r"\uf900-\ufaff\ufe30-\ufe4f\uff00-\uffef]+")


def add_emphasis_runs(par, text, size_pt=BODY_PT, color=C_INK):
    """`**强调**`：中文楷体加粗、西文 Times Bold，同字号。"""
    pos = 0
    for m in CJK_RE.finditer(text):
        if m.start() > pos:
            write(par, text[pos:m.start()], FONT_EN, FONT_EN, size_pt, True, color)
        write(par, m.group(0), FONT_CN, FONT_EN, size_pt, True, color)
        pos = m.end()
    if pos < len(text):
        write(par, text[pos:], FONT_EN, FONT_EN, size_pt, True, color)


def add_underline_runs(par, text, size_pt=BODY_PT, color=C_INK):
    """`__强调__`：中文楷体下划线、西文 Times 下划线，同字号（不加粗）。"""
    pos = 0
    for m in CJK_RE.finditer(text):
        if m.start() > pos:
            write(par, text[pos:m.start()], FONT_CN, FONT_EN, size_pt, False, color,
                  underline=True)
        write(par, m.group(0), FONT_CN, FONT_EN, size_pt, False, color, underline=True)
        pos = m.end()
    if pos < len(text):
        write(par, text[pos:], FONT_CN, FONT_EN, size_pt, False, color, underline=True)


def add_runs(par, text, size_pt=BODY_PT, heading=False, bold=False, color=C_INK,
             cn=None):
    """写一行带 `**强调**` 与 `` `行内代码` `` 的文本。

    heading=True 用于标题段：整段（含西文、行内代码）都走标题中文字体 / Times，标题段里
    不出现第三、第四种字面；中文字体默认黑体（作品名 / 一级标题），二级、三级章节标题经
    cn=FONT_CN 显式改楷体。
    行内代码默认 Consolas、不加底纹、黑色；在 9pt 表格里降到 9pt（不撑破表格行高），
    正文里是 11pt。
    """
    if cn is None:
        cn = FONT_HEI if heading else FONT_CN
    for piece in INLINE_RE.split(text):
        if not piece:
            continue
        if piece.startswith("**") and piece.endswith("**") and len(piece) > 4:
            inner = piece[2:-2]
            if heading:
                write(par, inner, cn, FONT_EN, size_pt, bold, color)
            else:
                add_emphasis_runs(par, inner, size_pt, color)
        elif piece.startswith("__") and piece.endswith("__") and len(piece) > 4:
            inner = piece[2:-2]
            if heading:
                write(par, inner, cn, FONT_EN, size_pt, bold, color)
            else:
                add_underline_runs(par, inner, size_pt, color)
        elif piece.startswith("`") and piece.endswith("`") and len(piece) > 2:
            if heading:
                write(par, piece[1:-1], cn, FONT_EN, size_pt, bold, color)
            else:
                code_pt = min(CODE_PT, size_pt)
                write(par, piece[1:-1], FONT_CN, FONT_CODE, code_pt, bold, color)
        else:
            write(par, piece, cn, FONT_EN, size_pt, bold, color)


def add_caption_runs(par, text):
    """图注 / 表注：楷体加粗五号 10.5pt，编号与说明同一字面，居中由段落给。"""
    for piece in INLINE_RE.split(text):
        if not piece:
            continue
        if piece.startswith("**") and piece.endswith("**") and len(piece) > 4:
            write(par, piece[2:-2], FONT_CN, FONT_EN, CAPTION_PT, True, C_INK)
        elif piece.startswith("__") and piece.endswith("__") and len(piece) > 4:
            write(par, piece[2:-2], FONT_CN, FONT_EN, CAPTION_PT, True, C_INK)
        elif piece.startswith("`") and piece.endswith("`") and len(piece) > 2:
            write(par, piece[1:-1], FONT_CN, FONT_CODE, CAPTION_PT, True, C_INK)
        else:
            write(par, piece, FONT_CN, FONT_EN, CAPTION_PT, True, C_INK)


def add_meta_runs(par, label, value, char_tw=0):
    """元信息行：`**标签**` 渲染黑体，后接全角冒号 + 10pt 楷体值。

    一行放不下时按字符间距轻微收紧（char_tw < 0，最多 -0.4pt/字），10pt 字号不变，
    保证元信息每条各占一行（版式观感；换行会让标题块散架）。
    """
    first = len(par.runs)
    write(par, label, FONT_HEI, FONT_EN, META_PT, False, C_INK)
    add_runs(par, "：" + value, size_pt=META_PT)
    if char_tw:
        for r in par.runs[first:]:
            set_char_spacing(r, char_tw)


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


def plain_markup(s):
    """去掉行内强调 / 下划线 / 代码标记：排版测量按纸上实际字形算，标记本身不占宽度。"""
    return re.sub(r"\*\*|__|`", "", s or "")


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
    """列宽（twips）：每列不小于 3 个字宽，且让整表估高最小；合计 = 整表宽。

    先剥掉 `**…**` / `__…__` / 行内代码标记再量：标记不是纸上字符，不参与列宽分配。
    """
    rows = [[plain_markup(c) for c in r] for r in rows]
    ncol = len(rows[0])
    usable_pt = total_cm * 28.3465          # 整表宽（含单元格内边距）→ 固定布局 15.6cm
    min_pt = []
    for c in range(ncol):
        longest = 0.0
        for r in rows:
            cell = plain_markup(r[c] if c < len(r) else "")
            token = max(re.split(r"[\s、，,；;：:/|（）()]+", cell) or [""], key=len)
            longest = max(longest, text_w(cell) / max(1, wrapped_lines(cell, 1e9)),
                          text_w(token, size_pt))
        # 列宽下限：至少容下最长 token（长路径 / 文件名不能被拆开），但不超过 12 个字宽
        min_pt.append(min(max(longest, 3 * size_pt * 1.7), 12 * size_pt))
    if sum(min_pt) > usable_pt:
        min_pt = [usable_pt / ncol] * ncol

    def evaluate(weights):
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
        est += sum(text_w(r[c] if c < len(r) else "", size_pt)
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


def _unbreakable_token(cell, size_pt=TABLE_PT):
    """单元格里最长的**不可断**片段（型号 / 路径 / 代码 / 数字串）。

    CJK 可以逐字断行、全角标点与空格是断点；只有西文数字串这类不能拆的才算下限。
    （旧算法把整段中文当 token，会把「指标」列硬撑宽，反而抢走数值列的宽度。）
    """
    cell = plain_markup(cell)
    runs = re.split(r"[\u2e80-\u9fff\u3000-\u303f\uff00-\uffef]+"
                    r"|[\s、，,；;：:/|（）()\[\]]+", cell)
    return max(runs or [""], key=len)


def table_min_widths(rows, size_pt=TABLE_PT):
    """每列宽度下限（pt）= 最长不可断 token + 左右单元格内边距（SPEC 表格令牌）。"""
    ncol = len(rows[0])
    margin_pt = 2 * CELL_MAR_TW / 20.0
    mins = []
    for c in range(ncol):
        longest = 0.0
        for r in rows:
            token = _unbreakable_token(r[c] if c < len(r) else "", size_pt)
            longest = max(longest, text_w(token, size_pt))
        mins.append(longest + margin_pt)
    return mins


def hinted_widths(rows, seps, total_cm=TABLE_W_CM, size_pt=TABLE_PT):
    """按分隔行破折号权重分配列宽（归一化到整表 15.6cm，固定布局）。

    权重 = 目标份额；每列有「最长不可断 token + 内边距」的下限。先满足下限，再按
    权重把余量分给各列；若某列被下限顶开（长 token 不能拆），其余列按权重让位。
    返回 twips 列表，合计 = 整表宽。
    """
    ncol = len(rows[0])
    total_pt = total_cm * 28.3465
    weights = [max(int(s.get("weight") or 0), 1) for s in seps]
    sw = float(sum(weights))
    ideal = [total_pt * w / sw for w in weights]
    mins = table_min_widths(rows, size_pt)
    if sum(mins) > total_pt:                 # 极端：下限和超整表宽 → 等比缩放
        scale = total_pt / sum(mins)
        mins = [m * scale for m in mins]
    widths = list(mins)
    extra = total_pt - sum(widths)
    if extra > 0:
        caps = [max(ideal[i] - widths[i], 0.0) for i in range(ncol)]
        cap_sum = sum(caps)
        if cap_sum > 1e-6:
            take = min(extra, cap_sum)
            widths = [widths[i] + take * caps[i] / cap_sum for i in range(ncol)]
            extra -= take
        if extra > 1e-6:                     # 各列都到目标份额后仍有剩 → 按权重均摊
            widths = [widths[i] + extra * weights[i] / sw for i in range(ncol)]
    scale = total_pt / sum(widths)
    return [int(round(x * scale / 28.3465 * TWIPS_PER_CM)) for x in widths]


JC_BY_NAME = {"left": WD_ALIGN_PARAGRAPH.LEFT,
              "center": WD_ALIGN_PARAGRAPH.CENTER,
              "right": WD_ALIGN_PARAGRAPH.RIGHT}


def body_cell_align(col, text, seps=None):
    """表体单元格对齐：分隔行写了冒号按冒号；否则第一列左对齐、其余沿用 cell_align()。"""
    if seps and col < len(seps) and seps[col].get("align"):
        return JC_BY_NAME[seps[col]["align"]]
    if col == 0:
        return WD_ALIGN_PARAGRAPH.LEFT
    return cell_align(text)


def set_table_borders(table, spec):
    """表格边框（设计令牌）：三线表——顶 / 底粗、表头下细，无竖线 / 行线 / 底纹。"""
    tblPr = table._tbl.tblPr
    old = tblPr.find(qn("w:tblBorders"))
    if old is not None:
        tblPr.remove(old)
    borders = _ordered_child(tblPr, "w:tblBorders", _TBLPR_ORDER)
    for edge in ("top", "left", "bottom", "right", "insideH", "insideV"):
        cfg = spec.get(edge) or {"val": "none", "sz": 0, "color": "auto"}
        el = OxmlElement("w:" + edge)
        el.set(qn("w:val"), cfg.get("val", "none"))
        el.set(qn("w:sz"), str(cfg.get("sz", 0)))
        el.set(qn("w:space"), "0")
        el.set(qn("w:color"), cfg.get("color", "auto"))
        borders.append(el)


def set_table_cell_margins(table):
    tblPr = table._tbl.tblPr
    old = tblPr.find(qn("w:tblCellMar"))
    if old is not None:
        tblPr.remove(old)
    mar = _ordered_child(tblPr, "w:tblCellMar", _TBLPR_ORDER)
    for edge in ("top", "left", "bottom", "right"):
        el = OxmlElement("w:" + edge)
        el.set(qn("w:w"), "40" if edge in ("top", "bottom") else str(CELL_MAR_TW))
        el.set(qn("w:type"), "dxa")
        mar.append(el)


def set_table_indent(table):
    ind = _ordered_child(table._tbl.tblPr, "w:tblInd", _TBLPR_ORDER)
    ind.set(qn("w:w"), "0")
    ind.set(qn("w:type"), "dxa")


def set_table_layout_fixed(table):
    lay = _ordered_child(table._tbl.tblPr, "w:tblLayout", _TBLPR_ORDER)
    lay.set(qn("w:type"), "fixed")


def repeat_header(row):
    el = _ordered_child(row._tr.get_or_add_trPr(), "w:tblHeader", _TRPR_ORDER)
    el.set(qn("w:val"), "true")


def no_row_split(row):
    """行内不跨页：整行挪到下一页，表格不会在行中间被拦腰截断。"""
    el = _ordered_child(row._tr.get_or_add_trPr(), "w:cantSplit", _TRPR_ORDER)
    el.set(qn("w:val"), "true")


ONLY_NUM_RE = re.compile(r"^[\d.\s%±~≤≥<>/()\-–—、]+$")
PCT_RE = re.compile(r"\d\s*[%％]")
UNIT_RE = re.compile(r"(fps|ms|µs|us|ns|mm|cm|pt|Hz|MHz|kbit|步|次|项|件|轴|页)")


def _set_keep_next(par, value=True):
    par._p.get_or_add_pPr().get_or_add_keepNext().set(
        qn("w:val"), "1" if value else "0")


def _table_est_height(rows, widths, ncol):
    """估表格高度（pt）：单行行数 × 单倍行高 + 单元格内边距。"""
    total = 0.0
    for cells in rows:
        lines = 1
        for c in range(ncol):
            plain = re.sub(r"[*`]", "", cells[c])
            avail = (widths[c] / TWIPS_PER_CM * 28.3465
                     - 2 * CELL_MAR_TW / TWIPS_PER_CM * 28.3465)
            lines = max(lines, wrapped_lines(plain, max(avail, 1.0)))
        total += lines * (TABLE_PT * LINE_FACTOR) + 4.0
    return total


def keep_table_rows(table, rows, widths, threshold_pt=280.0):
    """表格分页控制：整表放得下就整表同页；放不下时至少表头 + 前两行同页。"""
    ncol = len(rows[0])
    est = _table_est_height(rows, widths, ncol)
    last_keep = len(rows) - 2 if est <= threshold_pt else min(2, len(rows) - 1)
    for r in range(0, last_keep + 1):
        for c in range(ncol):
            for par in table.rows[r].cells[c].paragraphs:
                _set_keep_next(par, True)


def cell_align(text):
    """表头 / 短串 / 数值 / 带单位测量值居中，长文字左对齐。"""
    t = text.strip()
    if not t:
        return WD_ALIGN_PARAGRAPH.CENTER
    if len(t) <= 10 or ONLY_NUM_RE.match(t) or PCT_RE.search(t):
        return WD_ALIGN_PARAGRAPH.CENTER
    if len(t) <= 16 and UNIT_RE.search(t):
        return WD_ALIGN_PARAGRAPH.CENTER
    return WD_ALIGN_PARAGRAPH.LEFT


def render_table(doc, payload):
    """三线表：顶 / 底 1.5pt 黑、表头下 0.75pt；无竖线 / 行线 / 底纹；
    整表 15.6cm 固定布局；表头楷体加粗 9pt、表体楷体 9pt；行不跨页、表头跨页重复。

    列宽：分隔行有破折号权重提示时按权重归一化（长 token 下限兜底），破折号个数
    全表相同时回退自动算法；对齐：冒号提示优先，否则首列左、其余短串居中 / 长文左；
    单元格垂直顶对齐，行高只按内容（不再给表头设最小行高）。
    """
    rows = payload["rows"]
    seps = payload.get("seps")
    hinted = bool(payload.get("hinted")) and seps and len(seps) == len(rows[0])
    ncol = len(rows[0])
    widths = (hinted_widths(rows, seps) if hinted
              else column_widths(rows, size_pt=TABLE_PT))
    table = doc.add_table(rows=len(rows), cols=ncol)
    table.style = "Table Grid"
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    table.autofit = False
    set_table_layout_fixed(table)
    for i, w in enumerate(widths):
        table.columns[i].width = Twips(w)       # 网格列宽与 tcW 都写，Word 排版才稳
    tblW = table._tbl.tblPr.find(qn("w:tblW"))
    if tblW is None:
        tblW = _ordered_child(table._tbl.tblPr, "w:tblW", _TBLPR_ORDER)
    tblW.set(qn("w:type"), "dxa")
    tblW.set(qn("w:w"), str(sum(widths)))
    set_table_borders(table, {
        "top": {"val": "single", "sz": 12, "color": C_INK},       # 顶线 1.5pt
        "bottom": {"val": "single", "sz": 12, "color": C_INK},    # 底线 1.5pt
        "left": {"val": "none"}, "right": {"val": "none"},
        "insideV": {"val": "none"}, "insideH": {"val": "none"},   # 三线表：不画行线
    })
    set_table_cell_margins(table)
    set_table_indent(table)
    for r, cells in enumerate(rows):
        for c in range(ncol):
            cell = table.cell(r, c)
            cell.width = Twips(widths[c])
            cell.vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.TOP
            par = cell.paragraphs[0]
            align = (WD_ALIGN_PARAGRAPH.CENTER if r == 0
                     else body_cell_align(c, cells[c], seps if hinted else None))
            fmt_par(par, before=0.0, after=0.0, multiple=SINGLE_LINE, align=align)
            add_runs(par, cells[c], size_pt=TABLE_HEAD_PT if r == 0 else TABLE_PT,
                     bold=(r == 0))
            if r == 0:                              # 栏目线：表头下 0.75pt 细线
                set_cell_border(cell, bottom={"val": "single", "sz": 6,
                                              "color": C_INK})
    for row in table.rows:                       # 行不跨页，再让表头行跨页重复
        no_row_split(row)
    repeat_header(table.rows[0])
    keep_table_rows(table, rows, widths)         # 小表整表同页 / 大表表头不落单
    return table


def render_image(doc, src: Path, width_cm=FIG_MAX_CM):
    """插图：居中、宽 15.8cm（现有图按 1:1 落版），与图注同页。"""
    w_px, h_px = image_size_px(src)
    width_cm = FIG_WIDTH_OVERRIDE.get(src.name, width_cm)
    if h_px and w_px / h_px < 0.35:            # 细高图按高度上限再压一压（≤ 22cm）
        width_cm = min(width_cm, 22.0 * w_px / h_px)
    par = fmt_par(doc.add_paragraph(), before=FIG_GAP[0], after=FIG_GAP[1],
                  multiple=SINGLE_LINE, align=WD_ALIGN_PARAGRAPH.CENTER,
                  keep_next=True)
    par.add_run().add_picture(str(src), width=Cm(width_cm))
    return par


# ---------------------------------------------------------------- 文档搭建


def style_el(styles, name):
    """按名字取样式的 <w:style> 元素（style.element 对内置样式拿不到）。"""
    for st in styles:
        if st.name == name:
            return st.element
    raise KeyError(name)


def _new_par_style(styles, name):
    st = styles.add_style(name, WD_STYLE_TYPE.PARAGRAPH)
    st.base_style = styles["Normal"]
    return st


def _style_rpr(st_el):
    rpr = st_el.find(qn("w:rPr"))
    if rpr is None:
        rpr = OxmlElement("w:rPr")
        st_el.append(rpr)
    return rpr


def _setup_para_style(st_el, *, before, after, multiple, first=0, left=0,
                      hanging=0, align="both", keep_next=False, keep_lines=False,
                      widow=True, outline=None, size_pt=BODY_PT,
                      cn=FONT_CN, en=FONT_EN, bold=False, italic=False):
    """样式级段落格式：与 fmt_par 同一套令牌，保证「样式 + 直接格式」两边一致。"""
    pPr = fresh_pPr(st_el)
    set_keep(pPr, keep_next, keep_lines, widow)
    set_spacing(pPr, before, after, multiple)
    set_ind(pPr, first, left, hanging)
    set_jc(pPr, align)
    _no_snap(pPr)
    _no_overflow_punct(pPr)
    if outline is not None:
        el = pPr.find(qn("w:outlineLvl"))
        if el is None:
            el = OxmlElement("w:outlineLvl")
            pPr.append(el)
        el.set(qn("w:val"), str(outline))
    rpr = _style_rpr(st_el)
    set_rfonts_element(rpr, cn, en)
    _set_size(rpr, size_pt)
    for tag, val in (("w:b", "1" if bold else "0"),
                     ("w:i", "1" if italic else "0"),
                     ("w:color", C_INK)):
        el = rpr.find(qn(tag))
        if el is None:
            el = OxmlElement(tag)
            rpr.append(el)
        el.set(qn("w:val"), val)


def setup_doc_defaults(doc):
    """docDefaults 也钉死字体 / 字号 / 单倍行距：空段落与未显式设字体的文字不走主题字体。"""
    styles_el = doc.styles.element
    dd = styles_el.find(qn("w:docDefaults"))
    if dd is None:
        dd = OxmlElement("w:docDefaults")
        styles_el.insert(0, dd)
    rpr_default = dd.find(qn("w:rPrDefault"))
    if rpr_default is None:
        rpr_default = OxmlElement("w:rPrDefault")
        dd.append(rpr_default)
    rpr = rpr_default.find(qn("w:rPr"))
    if rpr is None:
        rpr = OxmlElement("w:rPr")
        rpr_default.append(rpr)
    for tag in ("w:rFonts", "w:color"):
        el = rpr.find(qn(tag))
        if el is not None:
            rpr.remove(el)
    set_rfonts_element(rpr)
    _set_size(rpr, BODY_PT)
    ppr_default = dd.find(qn("w:pPrDefault"))
    if ppr_default is None:
        ppr_default = OxmlElement("w:pPrDefault")
        dd.append(ppr_default)
    pPr = ppr_default.find(qn("w:pPr"))
    if pPr is None:
        pPr = OxmlElement("w:pPr")
        ppr_default.append(pPr)
    old = pPr.find(qn("w:spacing"))
    if old is not None:
        pPr.remove(old)
    set_spacing(pPr, 0.0, 0.0, SINGLE_LINE)


def build_styles(doc):
    """样式：令牌全部在这里再设一遍（验收脚本按样式名取样，不靠对齐方式猜）。"""
    styles = doc.styles
    setup_doc_defaults(doc)

    # Normal：楷体 12pt / Times、单倍行距、无缩进（具体段落由 BodyCN 等覆盖）
    normal = styles["Normal"]
    st_el = style_el(styles, "Normal")
    _setup_para_style(st_el, before=0.0, after=0.0, multiple=SINGLE_LINE,
                      align="both", widow=True, size_pt=BODY_PT)
    normal.font.name = FONT_EN
    normal.font.size = Pt(BODY_PT)
    normal.font.bold = False
    normal.font.italic = False
    normal.font.color.rgb = RGBColor.from_string(C_INK)

    # 标题块：黑体 17.5pt 居中，段前 24 / 段后 10，与元信息同页（不随规格变）
    _new_par_style(styles, "TitleCN")
    _setup_para_style(style_el(styles, "TitleCN"),
                      before=TITLE_GAP[0], after=TITLE_GAP[1],
                      multiple=SINGLE_LINE, align="center", keep_next=True,
                      keep_lines=True, size_pt=TITLE_PT, cn=FONT_HEI)
    # 元信息行：10pt、单倍、段后 1pt、左对齐（标签黑体由 run 直接格式给，值楷体）
    _new_par_style(styles, "MetaCN")
    _setup_para_style(style_el(styles, "MetaCN"), before=0.0, after=META_AFTER,
                      multiple=SINGLE_LINE, align="left", keep_lines=True,
                      size_pt=META_PT)

    # 一级章节标题（源 `##`）：黑体 15pt 加粗、2 倍行距、居中——Windows 楷体没有粗体字面，
    # Word 导 PDF 时 <w:b/> 对楷体 run 不生效，一级标题换黑体才真「变粗」（作者 11-03 要求）；
    # 段前段后沿用令牌表 H_GAP["h1"]，与下段同页；靠内置样式给大纲级别
    h1 = styles["Heading 1"]
    _setup_para_style(style_el(styles, "Heading 1"),
                      before=H_GAP["h1"][0], after=H_GAP["h1"][1],
                      multiple=HEAD_LINE, align="center", keep_next=True,
                      keep_lines=True, widow=True, outline=0,
                      size_pt=H1_PT, cn=FONT_HEI, bold=True)
    h1.font.name = FONT_EN
    h1.font.size = Pt(H1_PT)
    h1.font.bold = True
    h1.font.italic = False
    h1.font.color.rgb = RGBColor.from_string(C_INK)

    # 二级 / 三级章节标题（源 `###` / `####`）：一律楷体四号 14pt 加粗、2 倍行距、左对齐，
    # 段前段后见令牌表，与下段同页（这两级维持楷体系，不随一级标题变）
    for name, size_pt, level in (("Heading 2", H2_PT, 1), ("Heading 3", H3_PT, 2)):
        before, after = H_GAP["h%d" % (level + 1)]
        st = styles[name]
        _setup_para_style(style_el(styles, name), before=before, after=after,
                          multiple=HEAD_LINE, align="left", keep_next=True,
                          keep_lines=True, widow=True, outline=level,
                          size_pt=size_pt, cn=FONT_CN, bold=True)
        st.font.name = FONT_EN
        st.font.size = Pt(size_pt)
        st.font.bold = True
        st.font.italic = False
        st.font.color.rgb = RGBColor.from_string(C_INK)

    # 正文：楷体小四 12pt / Times、两端对齐、首行缩进 480 twips、1.5 倍行距
    _new_par_style(styles, "BodyCN")
    _setup_para_style(style_el(styles, "BodyCN"), before=0.0, after=BODY_AFTER,
                      multiple=BODY_LINE, first=CHAR_TWIPS, align="both",
                      widow=True, size_pt=BODY_PT)

    # 图注：楷体加粗五号 10.5pt、居中、段前 0 / 段后 6
    _new_par_style(styles, "CaptionCN")
    _setup_para_style(style_el(styles, "CaptionCN"),
                      before=FIGCAP_GAP[0], after=FIGCAP_GAP[1],
                      multiple=SINGLE_LINE, align="center", keep_lines=True,
                      size_pt=CAPTION_PT, bold=True)
    # 表注：楷体加粗五号 10.5pt、居中、段前 6 / 段后 3，在表格上方且与表同页
    _new_par_style(styles, "TableCaptionCN")
    _setup_para_style(style_el(styles, "TableCaptionCN"),
                      before=TABCAP_GAP[0], after=TABCAP_GAP[1],
                      multiple=SINGLE_LINE, align="center", keep_next=True,
                      keep_lines=True, size_pt=CAPTION_PT, bold=True)

    # 列表（仅参考文献 / 附录）：按正文小四 12pt、悬挂缩进 1.5 字符、1.5 倍、段后 2pt
    _new_par_style(styles, "ListCN")
    _setup_para_style(style_el(styles, "ListCN"), before=0.0, after=LIST_AFTER,
                      multiple=BODY_LINE, left=LIST_LEFT_TW,
                      hanging=LIST_HANG_TW, align="left", size_pt=LIST_PT)


def setup_section(section):
    section.page_width = Cm(PAGE_W_CM)
    section.page_height = Cm(PAGE_H_CM)
    section.top_margin = Cm(MARGIN_CM)
    section.bottom_margin = Cm(MARGIN_CM)
    section.left_margin = Cm(MARGIN_CM)
    section.right_margin = Cm(MARGIN_CM)
    section.header_distance = Cm(1.2)          # 页眉（官方模板 logo）距边 1.2cm
    section.footer_distance = Cm(FOOTER_DIST_CM)


def restart_page_numbering(section):
    """单节文档页码从 1 起：<w:pgNumType w:start="1"/>（显式声明，不靠默认值）。"""
    sectPr = section._sectPr
    el = sectPr.find(qn("w:pgNumType"))
    if el is None:
        el = OxmlElement("w:pgNumType")
        anchor = sectPr.find(qn("w:cols"))
        if anchor is None:
            anchor = sectPr.find(qn("w:docGrid"))
        if anchor is not None:
            anchor.addprevious(el)
        else:
            sectPr.append(el)
    el.set(qn("w:start"), "1")


def setup_header(section):
    """页眉：官方模板每页的「芯辰大海 FPGA 点亮未来」logo，居中，下方一条细横线
    （复刻官方模板 PDF 的版式：logo 宽 184.7pt ≈ 6.5cm，线即页眉段落底框线）。
    页眉里不能有文字（验收两处把关：md2docx 自检 + verify_report）。"""
    header = section.header
    header.is_linked_to_previous = False
    hp = header.paragraphs[0]
    clear_par(hp)
    fmt_par(hp, before=0.0, after=0.0, multiple=SINGLE_LINE,
            align=WD_ALIGN_PARAGRAPH.CENTER, widow=False)
    logo = FIGURES_DIR / "logo_header.png"
    if not logo.is_file():
        raise SystemExit("页眉 logo 不存在：%s（提交库内应带此文件）" % logo)
    run = hp.add_run()
    run.add_picture(str(logo), width=Cm(6.5))
    # 段落底框线 = 模板页眉下方那条横线（0.75pt 黑）
    pPr = hp._p.get_or_add_pPr()
    pbdr = OxmlElement("w:pBdr")
    bottom = OxmlElement("w:bottom")
    bottom.set(qn("w:val"), "single")
    bottom.set(qn("w:sz"), "6")                # 八分之一磅：6/8 = 0.75pt
    bottom.set(qn("w:space"), "1")
    bottom.set(qn("w:color"), C_INK)
    pbdr.append(bottom)
    pPr.append(pbdr)


def setup_footer(section):
    """页脚：居中 PAGE 域 11pt 黑字（不写「第 X 页 / 共 Y 页」）。"""
    footer = section.footer
    footer.is_linked_to_previous = False
    fp = footer.paragraphs[0]
    clear_par(fp)
    fmt_par(fp, before=0.0, after=0.0, multiple=SINGLE_LINE,
            align=WD_ALIGN_PARAGRAPH.CENTER, widow=False)
    set_para_mark_font(fp, FOOTER_PT)
    add_field(fp, " PAGE ", "1", cn=FONT_CN, size_pt=FOOTER_PT, color=C_INK)


def set_update_fields(doc):
    """让 Word 打开时自动更新域（页脚 PAGE 不会停在占位文本）。"""
    el = OxmlElement("w:updateFields")
    el.set(qn("w:val"), "true")
    doc.settings.element.append(el)


# ---------------------------------------------------------------- 生成后自检

def _xml_of(zf, name):
    try:
        return zf.read(name).decode("utf-8", "replace")
    except KeyError:
        return ""


def self_check(out_path: Path, *, title, n_h1, n_h2, n_h3=None, n_img, n_tbl, n_figcap,
               n_tabcap, n_meta, fig_w, placeholder_names, forbidden,
               table_specs=None):
    """读回 docx 复核令牌表。返回 (issues, stats)。"""
    issues = []
    warnings = []

    def need(ok, msg, detail=""):
        if not ok:
            issues.append(msg if not detail else "%s（%s）" % (msg, detail))

    def near(a, b, tol=0.02):
        return a is not None and abs(float(a) - float(b)) <= tol

    back = Document(str(out_path))
    zf = zipfile.ZipFile(str(out_path))
    docxml = _xml_of(zf, "word/document.xml")
    stylesxml = _xml_of(zf, "word/styles.xml")
    footxml = "".join(_xml_of(zf, n) for n in zf.namelist()
                      if re.match(r"word/footer\d*\.xml$", n))
    headxml = "".join(_xml_of(zf, n) for n in zf.namelist()
                      if re.match(r"word/header\d*\.xml$", n))
    usedxml = docxml + footxml

    # ---- 单节 / A4 / 页边距 / 页脚距边
    need(len(back.sections) == 1, "不是单节文档", "%d 节" % len(back.sections))
    sec = back.sections[0]
    need(near(sec.page_width.cm, PAGE_W_CM, 0.01)
         and near(sec.page_height.cm, PAGE_H_CM, 0.01), "页面不是 A4",
         "%.2f×%.2f cm" % (sec.page_width.cm, sec.page_height.cm))
    bad_m = [m for m in ("left_margin", "right_margin", "top_margin", "bottom_margin")
             if not near(getattr(sec, m).cm, MARGIN_CM, 0.01)]
    need(not bad_m, "页边距不是 2.4cm", ",".join(bad_m))
    need(near(sec.footer_distance.cm, FOOTER_DIST_CM, 0.02), "页脚距边不是 1.6cm",
         "%.2fcm" % sec.footer_distance.cm)

    # ---- 页眉：官方模板 logo（只有一张图、无文字）/ 无目录域
    need("w:headerReference" in docxml, "文档里没有页眉引用")
    need("<a:blip" in headxml and headxml.count("<a:blip") == 1,
         "页眉应恰好含 1 张 logo 图", "blip=%d" % headxml.count("<a:blip"))
    head_text = "".join(re.findall(r"<w:t[^>]*>([^<]*)</w:t>", headxml))
    need(not head_text.strip(), "页眉有文字", head_text[:30])
    instr_all = "".join(re.findall(r"<w:instrText[^>]*>([^<]*)</w:instrText>", docxml))
    need("TOC" not in instr_all, "文档里仍有 TOC 目录域", instr_all[:40])
    need(docxml.count("<w:sectPr") == 1, "sectPr 不是唯一一个",
         "count=%d" % docxml.count("<w:sectPr"))

    # ---- 页脚：PAGE 域、11pt、居中、黑、无「共 Y 页」
    need("w:footerReference" in docxml, "没有页脚引用")
    need("PAGE" in footxml, "页脚没有 PAGE 域")
    need("NUMPAGES" not in footxml, "页脚仍有「共 Y 页」（NUMPAGES 域）")
    need('w:sz w:val="%d"' % int(round(FOOTER_PT * 2)) in footxml
         or 'w:sz w:val="%d"' % int(round(FOOTER_PT * 2)) in footxml,
         "页脚字号不是 11pt")
    need('w:jc w:val="center"' in footxml, "页脚不是居中")
    foot_text = "".join(re.findall(r"<w:t[^>]*>([^<]*)</w:t>", footxml))
    need("第 " not in foot_text and "共" not in foot_text,
         "页脚仍有「第 X 页 / 共 Y 页」文字", foot_text[:30])

    # ---- 页码从 1 起
    need(re.search(r'<w:pgNumType[^>]*w:start="1"', docxml) is not None,
         "没有 pgNumType start=1（页码不是从 1 起）")

    # ---- 结构计数（与源块一一对应）
    h1 = sum(1 for p in back.paragraphs if p.style.name == "Heading 1")
    h2 = sum(1 for p in back.paragraphs if p.style.name == "Heading 2")
    h3 = sum(1 for p in back.paragraphs if p.style.name == "Heading 3")
    need(h1 == n_h1, "一级标题数与源块不一致", "docx %d / 源 %d" % (h1, n_h1))
    need(h2 == n_h2, "二级标题数与源块不一致", "docx %d / 源 %d" % (h2, n_h2))
    if n_h3 is not None:
        need(h3 == n_h3, "三级标题数与源块不一致", "docx %d / 源 %d" % (h3, n_h3))
    need(len(back.tables) == n_tbl, "表格数与源块不一致",
         "docx %d / 源 %d" % (len(back.tables), n_tbl))
    need(len(back.inline_shapes) == n_img, "插图数与源块不一致",
         "docx %d / 源 %d" % (len(back.inline_shapes), n_img))

    # ---- 标题块 / 元信息
    title_pars = [p for p in back.paragraphs if p.style.name == "TitleCN"]
    need(len(title_pars) == 1, "标题块不是唯一一个", "%d 个" % len(title_pars))
    if title_pars:
        tp = title_pars[0]
        need(tp.text == title, "标题文字与源不一致", tp.text[:20])
        need(tp.alignment == WD_ALIGN_PARAGRAPH.CENTER, "标题不居中")
        need(near(tp.paragraph_format.space_before.pt, TITLE_GAP[0], 0.05),
             "标题段前不是 24pt")
        need(near(tp.paragraph_format.space_after.pt, TITLE_GAP[1], 0.05),
             "标题段后不是 10pt")
        sizes = {r.font.size.pt for r in tp.runs if r.font.size is not None}
        need(sizes == {TITLE_PT}, "标题字号不是 17.5pt", str(sorted(sizes)))
        fonts = {_run_fonts(r)[0] for r in tp.runs if r.text.strip()}
        need(fonts == {FONT_HEI}, "标题中文不是黑体", str(sorted(fonts)))
    meta_pars = [p for p in back.paragraphs if p.style.name == "MetaCN"]
    need(len(meta_pars) == n_meta, "元信息行数不符",
         "docx %d / 源 %d" % (len(meta_pars), n_meta))
    need(len(meta_pars) <= 6, "元信息行超过 6 行", str(len(meta_pars)))

    # ---- 样式字号 / 字体
    expect_styles = {"Normal": BODY_PT, "BodyCN": BODY_PT, "Heading 1": H1_PT,
                     "Heading 2": H2_PT, "Heading 3": H3_PT, "TitleCN": TITLE_PT,
                     "MetaCN": META_PT, "CaptionCN": CAPTION_PT,
                     "TableCaptionCN": CAPTION_PT, "ListCN": LIST_PT}
    for name, pt in expect_styles.items():
        st = back.styles[name]
        need(st.font.size is not None and near(st.font.size.pt, pt, 0.01),
             "样式 %s 字号不是 %.1fpt" % (name, pt),
             "%.2f" % (st.font.size.pt if st.font.size else float("nan")))
    for name, cn in (("BodyCN", FONT_CN), ("Heading 1", FONT_HEI),
                     ("Heading 2", FONT_CN), ("Heading 3", FONT_CN),
                     ("TitleCN", FONT_HEI), ("CaptionCN", FONT_CN)):
        fonts = _style_fonts(back.styles, name)
        need(fonts[0] == cn, "样式 %s 中文字体不是 %s" % (name, cn), str(fonts))
        need(fonts[1] == FONT_EN, "样式 %s 西文字体不是 Times" % name, str(fonts))
    # 标题层级对齐（样式级）：一级居中，二 / 三级左对齐
    for name, align in (("Heading 1", "center"), ("Heading 2", "left"),
                        ("Heading 3", "left")):
        ppr = style_el(back.styles, name).find(qn("w:pPr"))
        j = ppr.find(qn("w:jc")) if ppr is not None else None
        need(j is not None and j.get(qn("w:val")) == align,
             "样式 %s 对齐不是 %s" % (name, align))
    need(back.styles["CaptionCN"].font.bold, "图注样式不加粗")
    need(back.styles["TableCaptionCN"].font.bold, "表注样式不加粗")
    for name in ("Heading 1", "Heading 2", "Heading 3"):
        need(back.styles[name].font.bold, "样式 %s 不加粗（章节标题须加粗）" % name)
    need(not back.styles["BodyCN"].font.bold, "正文样式被加粗了")

    # ---- 正文：首行缩进 2 字符 / 1.5 倍 / 两端对齐 / 小四
    body_pars = [p for p in back.paragraphs if p.style.name == "BodyCN"]
    need(bool(body_pars), "没有正文段落")
    if body_pars:
        bp = body_pars[0]
        fli = bp.paragraph_format.first_line_indent
        need(fli is not None and near(fli.pt, CHAR_TWIPS / 20.0, 0.05),
             "正文首行缩进不是 %d twips" % CHAR_TWIPS,
             "%s twips" % (fli.twips if fli else None))
        need(near(bp.paragraph_format.line_spacing, BODY_LINE, 0.01),
             "正文行距不是 %.1f 倍" % BODY_LINE,
             str(bp.paragraph_format.line_spacing))
        need(bp.alignment == WD_ALIGN_PARAGRAPH.JUSTIFY, "正文不是两端对齐")
        sizes = {r.font.size.pt for r in bp.runs if r.text.strip()
                 and _run_fonts(r)[0] != FONT_CODE}
        need(sizes == {BODY_PT}, "正文字号不是 %gpt" % BODY_PT, str(sorted(sizes)))

    # ---- 章节标题：H1 黑体 15pt 居中 / H2、H3 楷体 14pt 左对齐，均整段加粗、2 倍行距、段距
    for name, size_pt, gap, cn, align in (
            ("Heading 1", H1_PT, H_GAP["h1"], FONT_HEI, WD_ALIGN_PARAGRAPH.CENTER),
            ("Heading 2", H2_PT, H_GAP["h2"], FONT_CN, WD_ALIGN_PARAGRAPH.LEFT),
            ("Heading 3", H3_PT, H_GAP["h3"], FONT_CN, WD_ALIGN_PARAGRAPH.LEFT)):
        pars = [p for p in back.paragraphs if p.style.name == name]
        if not pars:
            continue
        runs = [r for r in pars[0].runs if r.text.strip()]
        sizes = {r.font.size.pt for r in runs}
        need(sizes == {size_pt}, "%s 字号不是 %gpt" % (name, size_pt),
             str(sorted(sizes)))
        fonts_cn = {_run_fonts(r)[0] for r in runs}
        need(fonts_cn == {cn}, "%s 中文字体不是 %s" % (name, cn), str(fonts_cn))
        need(pars[0].alignment == align,
             "%s 不是%s" % (name, "居中" if align == WD_ALIGN_PARAGRAPH.CENTER
                            else "左对齐"))
        need(all(r.font.bold for r in runs), "%s 没有整段加粗" % name)
        ls = pars[0].paragraph_format.line_spacing
        need(near(ls, HEAD_LINE, 0.01), "%s 行距不是 %.1f 倍" % (name, HEAD_LINE),
             str(ls))
        need(near(pars[0].paragraph_format.space_before.pt, gap[0], 0.05),
             "%s 段前不是 %.0fpt" % (name, gap[0]))
        need(near(pars[0].paragraph_format.space_after.pt, gap[1], 0.05),
             "%s 段后不是 %.0fpt" % (name, gap[1]))

    # ---- 图注 / 表注：加粗 10pt 居中
    for style_name, expected in (("CaptionCN", n_figcap),
                                 ("TableCaptionCN", n_tabcap)):
        pars = [p for p in back.paragraphs if p.style.name == style_name]
        need(len(pars) == expected, "%s 段落数与源块不一致" % style_name,
             "docx %d / 源 %d" % (len(pars), expected))
        for p in pars:
            need(p.alignment == WD_ALIGN_PARAGRAPH.CENTER,
                 "%s 不居中" % style_name, p.text[:20])
            for r in p.runs:
                if r.text.strip():
                    need(r.font.bold is True, "%s 有未加粗的片段" % style_name,
                         r.text[:20])
                    need(r.font.size is not None
                         and near(r.font.size.pt, CAPTION_PT, 0.01),
                         "%s 字号不是 %gpt" % (style_name, CAPTION_PT), r.text[:20])
        # 表注在表格上方：表注段落下标 < 表对象在 body 里的位置
    if n_tabcap:
        body_children = list(back.element.body.iterchildren())
        tbl_idx = [i for i, el in enumerate(body_children) if el.tag == qn("w:tbl")]
        cap_idx = [i for i, el in enumerate(body_children)
                   if el.tag == qn("w:p")
                   and el.find(qn("w:pPr")) is not None
                   and el.find(qn("w:pPr")).find(qn("w:pStyle")) is not None
                   and el.find(qn("w:pPr")).find(qn("w:pStyle")).get(qn("w:val"))
                   in ("TableCaptionCN",)]
        need(len(cap_idx) == n_tabcap and len(tbl_idx) == n_tbl
             and all(cap < tbl for cap, tbl in zip(cap_idx, tbl_idx)),
             "表注没有全部位于表格上方")

    # ---- 表格：三线表 / 9pt / 15.6cm / 固定布局 / 表头重复 / 行不跨页
    for ti, table in enumerate(back.tables):
        tblPr = table._tbl.tblPr
        tblW = tblPr.find(qn("w:tblW"))
        width_cm = (int(tblW.get(qn("w:w"))) / TWIPS_PER_CM) if tblW is not None else 0
        need(near(width_cm, TABLE_W_CM, 0.05), "第 %d 张表宽不是 15.6cm" % (ti + 1),
             "%.2fcm" % width_cm)
        lay = tblPr.find(qn("w:tblLayout"))
        need(lay is not None and lay.get(qn("w:type")) == "fixed",
             "第 %d 张表不是固定布局" % (ti + 1))
        borders = tblPr.find(qn("w:tblBorders"))
        need(borders is not None, "第 %d 张表没有表格边框定义" % (ti + 1))
        if borders is not None:
            for edge, want_sz, want_val in (("top", 12, "single"), ("bottom", 12, "single"),
                                            ("left", 0, "none"), ("right", 0, "none"),
                                            ("insideH", 0, "none"), ("insideV", 0, "none")):
                el = borders.find(qn("w:" + edge))
                val = el.get(qn("w:val")) if el is not None else None
                sz = int(el.get(qn("w:sz"))) if el is not None and el.get(qn("w:sz")) else 0
                need(val == want_val and sz == want_sz,
                     "第 %d 张表 %s 线不对" % (ti + 1, edge),
                     "%s/%s" % (val, sz))
        tr = table.rows[0]._tr.find(qn("w:trPr"))
        need(tr is not None and tr.find(qn("w:tblHeader")) is not None,
             "第 %d 张表表头不跨页重复" % (ti + 1))
        hdr_cell_borders = table.cell(0, 0)._tc.find(qn("w:tcPr")).find(qn("w:tcBorders"))
        need(hdr_cell_borders is not None, "第 %d 张表表头下无 0.75pt 细线" % (ti + 1))
        if hdr_cell_borders is not None:
            bottom = hdr_cell_borders.find(qn("w:bottom"))
            need(bottom is not None and int(bottom.get(qn("w:sz"))) == 6,
                 "第 %d 张表表头下线条不是 0.75pt" % (ti + 1))
        for row in table.rows:
            trPr = row._tr.find(qn("w:trPr"))
            need(trPr is not None and trPr.find(qn("w:cantSplit")) is not None,
                 "第 %d 张表有行允许跨页" % (ti + 1))
        for r in table.rows:
            for cell in r.cells:
                for p in cell.paragraphs:
                    for run in p.runs:
                        if not run.text.strip():
                            continue
                        need(run.font.size is not None
                             and near(run.font.size.pt, TABLE_PT, 0.01),
                             "第 %d 张表有单元格字号不是 %gpt" % (ti + 1, TABLE_PT),
                             run.text[:16])
                        if r is table.rows[0]:
                            need(run.font.bold is True,
                                 "第 %d 张表表头未加粗" % (ti + 1))

    # ---- 单元格垂直对齐 = top（多行行不再上下居中撑高，行高只按内容）
    for ti, table in enumerate(back.tables):
        for row in table.rows:
            for cell in row.cells:
                tcPr = cell._tc.tcPr
                v = tcPr.find(qn("w:vAlign")) if tcPr is not None else None
                need(v is not None and v.get(qn("w:val")) == "top",
                     "第 %d 张表有单元格不是顶对齐" % (ti + 1))

    # ---- 列宽提示：实测列宽与破折号权重的偏差 ≤8%
    # 长 token 下限会顶开列宽（否则型号 / 代码会被拆开），这属于规则内的让位：
    # 和「权重 + 下限」算出来的期望一致就只告警，否则才算实现错误。
    for ti, table in enumerate(back.tables):
        spec = ((table_specs or [])[ti]
                if table_specs and ti < len(table_specs) else None)
        if not spec or not spec.get("hinted"):
            continue
        seps = spec.get("seps") or []
        rows_src = spec.get("rows") or []
        if len(seps) != len(table.columns) or not rows_src:
            continue
        weights = [max(int(s.get("weight") or 0), 1) for s in seps]
        sw = float(sum(weights))
        grid = [int(c.get(qn("w:w")) or 0)
                for c in table._tbl.tblGrid.findall(qn("w:gridCol"))]
        total = sum(grid) or 1
        bounded = hinted_widths(rows_src, seps)
        for ci, w in enumerate(grid):
            if ci >= len(weights):
                break
            expect = weights[ci] / sw
            dev = abs(w / total - expect) / expect if expect else 0.0
            if dev <= 0.08:
                continue
            bounded_ok = (ci < len(bounded)
                          and abs(w - bounded[ci]) <= max(2.0, bounded[ci] * 0.02))
            if bounded_ok:
                warnings.append(
                    "表 %d 第 %d 列宽比破折号权重宽 %.0f%%（最长 token 下限顶开，"
                    "属规则内让位）" % (ti + 1, ci + 1, dev * 100))
            else:
                need(False, "表 %d 第 %d 列宽与权重偏差 %.0f%% > 8%%"
                     % (ti + 1, ci + 1, dev * 100),
                     "%.2fcm / 权重份额 %.0f%%"
                     % (w / TWIPS_PER_CM, expect * 100))

    # ---- 单元格行数 > 6 行告警（源里写得太长，靠排版硬撑会让表高失控）
    for ti, table in enumerate(back.tables):
        for ri, row in enumerate(table.rows):
            for ci, cell in enumerate(row.cells):
                text = re.sub(r"[*`]", "", cell.text or "")
                if not text.strip():
                    continue
                w_tw = cell.width.twips if cell.width is not None else 0
                avail = w_tw / 20.0 - 2 * CELL_MAR_TW / 20.0
                lines = wrapped_lines(text, max(avail, 1.0), TABLE_PT)
                if lines > 6:
                    warnings.append(
                        "表 %d 行 %d 列 %d 约 %d 行（建议精简源单元格）"
                        % (ti + 1, ri + 1, ci + 1, lines))

    # 图注与插图数量一致（每张图有图注、宽 15.8cm）
    need(n_figcap == n_img, "图注数与插图数不一致",
         "图注 %d / 插图 %d" % (n_figcap, n_img))
    for i, shape in enumerate(back.inline_shapes):
        need(near(shape.width.cm, fig_w, 0.05), "第 %d 张插图宽不是 %.1fcm"
             % (i + 1, fig_w), "%.2fcm" % shape.width.cm)

    # ---- 全篇纯黑；无固定行距（144 的表后空行除外；240/360/480 均为合法倍数）
    colors = {c.upper() for c in re.findall(r'w:color w:val="([0-9A-Fa-f]{6})"', usedxml)}
    weird = sorted(c for c in colors if c != C_INK)
    need(not weird, "出现非黑色字色", ",".join(weird[:8]))
    bad_rule = re.findall(r'w:lineRule="(exact|atLeast|fixed)"',
                          docxml + stylesxml + footxml)
    need(not bad_rule, "出现固定行距", str(sorted(set(bad_rule))))
    tiny_line = sorted({int(v) for v in re.findall(r'w:line="(\d+)"',
                                                   docxml + stylesxml + footxml)
                        if int(v) < 240 and int(v) != int(SPACER_LINE * 240)})
    need(not tiny_line, "有倍数行距 < 1.0 的 w:line 值", str(tiny_line))
    odd_line = sorted({int(v) for v in re.findall(r'w:line="(\d+)"',
                                                  docxml + stylesxml + footxml)
                       if int(v) >= 240
                       and int(v) not in (240, int(BODY_LINE * 240),
                                          int(HEAD_LINE * 240))})
    need(not odd_line, "出现规格外的倍数行距（只允许 240/360/480）", str(odd_line))

    # ---- 禁项
    need(not forbidden, "禁项命中", ",".join(forbidden))

    stats = {
        "font_cn": FONT_CN, "title_pt": TITLE_PT, "h1_pt": H1_PT, "h2_pt": H2_PT,
        "h3_pt": H3_PT, "h1_font": FONT_HEI, "h1_align": "center",
        "body_pt": BODY_PT, "list_pt": LIST_PT, "code_pt": CODE_PT,
        "caption_pt": CAPTION_PT, "table_pt": TABLE_PT, "footer_pt": FOOTER_PT,
        "body_line": BODY_LINE, "head_line": HEAD_LINE,
        "first_line_twips": CHAR_TWIPS, "margin_cm": MARGIN_CM,
        "figure_width_cm": fig_w, "table_width_cm": TABLE_W_CM,
        "placeholders": placeholder_names,
        "table_warnings": warnings,
        "self_check_issues": issues,
    }
    return issues, stats


def _run_fonts(run):
    rpr = run._element.find(qn("w:rPr"))
    fonts = rpr.find(qn("w:rFonts")) if rpr is not None else None
    return (fonts.get(qn("w:eastAsia")) if fonts is not None else None,
            fonts.get(qn("w:ascii")) if fonts is not None else None)


def _style_fonts(styles, name):
    el = style_el(styles, name)
    rpr = el.find(qn("w:rPr"))
    fonts = rpr.find(qn("w:rFonts")) if rpr is not None else None
    return (fonts.get(qn("w:eastAsia")) if fonts is not None else None,
            fonts.get(qn("w:ascii")) if fonts is not None else None)


# ---------------------------------------------------------------- 主流程

def sync_effects_part(out_path: Path):
    """stylesWithEffects.xml（模板遗留的旧式样式副本，仅 Word 2010 读取）的 docDefaults
    与 styles.xml 同步成单倍 / 段后 0。python-docx 默认模板在这个部件里带
    after=200 / line=276（Word 2010 默认值），setup_doc_defaults 够不到它，保存后
    原地重写；不处理的话验收会把 276 当规格外行距扫出来。"""
    if not out_path.is_file():
        return
    with zipfile.ZipFile(str(out_path)) as zin:
        if "word/stylesWithEffects.xml" not in zin.namelist():
            return
        items = [(zi, zin.read(zi.filename)) for zi in zin.infolist()]
    tmp = out_path.with_name(out_path.name + ".tmp")
    with zipfile.ZipFile(str(tmp), "w", zipfile.ZIP_DEFLATED) as zout:
        for zi, data in items:
            if zi.filename == "word/stylesWithEffects.xml":
                data = re.sub(rb'<w:spacing[^>]*w:line="276"[^>]*/>',
                              b'<w:spacing w:after="0" w:line="240" '
                              b'w:lineRule="auto"/>', data, count=1)
            zout.writestr(zi, data)
    tmp.replace(out_path)


def build(source: Path, out_path: Path, caption_clean=True, caption_rules=None,
          fig_width_cm=None):
    if not source.is_file():
        raise SystemExit("找不到内容源：%s" % source)
    title, blocks = parse_source(source)
    strip_rules, keep_rules = (None, None)
    if caption_rules:
        strip_rules = caption_rules.get("strip")
        keep_rules = caption_rules.get("keep")
    fig_w = float(fig_width_cm) if fig_width_cm else FIG_MAX_CM
    if not 3.0 <= fig_w <= FIG_MAX_CM:
        raise SystemExit("--fig-width 只允许 3.0~%.1fcm（落版宽）" % FIG_MAX_CM)

    doc = Document()
    build_styles(doc)
    sec = doc.sections[0]
    setup_section(sec)
    setup_header(sec)
    setup_footer(sec)
    restart_page_numbering(sec)

    # 标题块 + 元信息行（唯一标题 + 标题后连续若干行元信息）
    fmt_par(doc.add_paragraph(style="TitleCN"), before=TITLE_GAP[0],
            after=TITLE_GAP[1], multiple=SINGLE_LINE,
            align=WD_ALIGN_PARAGRAPH.CENTER, keep_next=True, keep_lines=True)
    tpar = doc.paragraphs[-1]
    add_runs(tpar, title, size_pt=TITLE_PT, heading=True)

    counts = {"h2": 0, "h3": 0, "h4": 0, "images": 0, "tables": 0, "figcap": 0,
              "tabcap": 0, "meta": 0}
    figures, placeholders, captions, caption_diffs = [], [], [], []
    table_specs = []                          # 表格源块（含分隔行权重 / 对齐提示）

    for kind, payload, _lineno in blocks:
        if kind == "meta":
            counts["meta"] += 1
            par = fmt_par(doc.add_paragraph(style="MetaCN"), before=0.0,
                          after=META_AFTER, multiple=SINGLE_LINE,
                          align=WD_ALIGN_PARAGRAPH.LEFT, keep_lines=True)
            line = "%s：%s" % (payload[0], payload[1])
            est = text_w(line, META_PT)
            avail = TEXT_W_CM * 28.3465
            char_tw = 0
            if est > avail and line:
                # 只收紧到刚好放得下，上限 0.4pt/字（再长就该在内容源里改写）
                squeeze_pt = min((est - avail) / len(line) + 0.02, 0.4)
                char_tw = -int(round(squeeze_pt * 20))
            add_meta_runs(par, payload[0], payload[1], char_tw)
        elif kind == "h2":
            counts["h2"] += 1
            before, after = H_GAP["h1"]
            is_appendix = payload.lstrip().startswith("附录")
            par = fmt_par(doc.add_paragraph(style="Heading 1"), before=before,
                          after=after, multiple=HEAD_LINE, keep_next=True,
                          keep_lines=True, align=WD_ALIGN_PARAGRAPH.CENTER,
                          page_break_before=is_appendix)
            add_runs(par, payload, size_pt=H1_PT, heading=True, bold=True,
                     cn=FONT_HEI)
        elif kind == "h3":
            counts["h3"] += 1
            before, after = H_GAP["h2"]
            par = fmt_par(doc.add_paragraph(style="Heading 2"), before=before,
                          after=after, multiple=HEAD_LINE, keep_next=True,
                          keep_lines=True, align=WD_ALIGN_PARAGRAPH.LEFT)
            add_runs(par, payload, size_pt=H2_PT, heading=True, bold=True,
                     cn=FONT_CN)
        elif kind == "h4":
            counts["h4"] += 1
            before, after = H_GAP["h3"]
            par = fmt_par(doc.add_paragraph(style="Heading 3"), before=before,
                          after=after, multiple=HEAD_LINE, keep_next=True,
                          keep_lines=True, align=WD_ALIGN_PARAGRAPH.LEFT)
            add_runs(par, payload, size_pt=H3_PT, heading=True, bold=True,
                     cn=FONT_CN)
        elif kind == "p":
            par = fmt_par(doc.add_paragraph(style="BodyCN"), before=0.0,
                          after=BODY_AFTER, multiple=BODY_LINE,
                          first=CHAR_TWIPS)
            add_runs(par, payload, size_pt=BODY_PT)
        elif kind == "ol":
            for num, item, _ln in payload:
                par = fmt_par(doc.add_paragraph(style="ListCN"), before=0.0,
                              after=LIST_AFTER, multiple=BODY_LINE,
                              left=LIST_LEFT_TW, hanging=LIST_HANG_TW,
                              align=WD_ALIGN_PARAGRAPH.LEFT)
                write(par, "%d." % num, size_pt=LIST_PT, color=C_INK)
                par.add_run("\t")
                add_runs(par, item, size_pt=LIST_PT)
        elif kind == "image":
            src = (source.parent / payload).resolve()
            if not src.is_file():
                raise SystemExit("插图不存在：%s（相对源文件所在目录解析）" % payload)
            render_image(doc, src, fig_w)
            counts["images"] += 1
            figures.append(src)
            if src.name.startswith("ph_"):
                placeholders.append(src.name)
        elif kind in ("figcap", "tabcap"):
            text = payload
            if caption_clean:
                text, removed = clean_caption(payload, strip_rules, keep_rules)
                for r in removed:
                    caption_diffs.append({"caption": payload[:40], "removed": r})
            captions.append({"kind": "figure" if kind == "figcap" else "table",
                             "text": text})
            if kind == "figcap":
                counts["figcap"] += 1
                par = fmt_par(doc.add_paragraph(style="CaptionCN"),
                              before=FIGCAP_GAP[0], after=FIGCAP_GAP[1],
                              multiple=SINGLE_LINE, keep_lines=True,
                              align=WD_ALIGN_PARAGRAPH.CENTER)
            else:
                counts["tabcap"] += 1
                par = fmt_par(doc.add_paragraph(style="TableCaptionCN"),
                              before=TABCAP_GAP[0], after=TABCAP_GAP[1],
                              multiple=SINGLE_LINE, keep_next=True,
                              keep_lines=True, align=WD_ALIGN_PARAGRAPH.CENTER)
            add_caption_runs(par, text)
        elif kind == "table":
            render_table(doc, payload)
            table_specs.append(payload)
            counts["tables"] += 1
            sp = fmt_par(doc.add_paragraph(), before=0.0, after=0.0,
                         multiple=SPACER_LINE, widow=False)
            set_para_mark_font(sp, 9.0)            # 表后细空行（144 twips）
        else:  # pragma: no cover
            raise ValueError("未知块类型 %r" % kind)

    set_update_fields(doc)
    doc.core_properties.title = title
    out_path.parent.mkdir(parents=True, exist_ok=True)
    doc.save(str(out_path))
    sync_effects_part(out_path)

    # ---- 自检：读回统计 + 令牌复核 + 禁项扫描 ----
    back = Document(str(out_path))
    text_all = "\n".join(p.text for p in back.paragraphs)
    for t in back.tables:
        for row in t.rows:
            for cell in row.cells:
                text_all += "\n" + cell.text
    for p in back.sections[0].footer.paragraphs:
        text_all += "\n" + p.text
    hits = forbidden_hits(text_all)

    # 页眉只有 logo 图无文字：按 header 部件的 <w:t> 判（访问 section.header 会凭空创建部件）
    header_texts = []
    footers = [p.text for p in back.sections[0].footer.paragraphs]
    issues, token_stats = self_check(
        out_path, title=title, n_h1=counts["h2"], n_h2=counts["h3"], n_h3=counts["h4"],
        n_img=counts["images"], n_tbl=counts["tables"],
        n_figcap=counts["figcap"], n_tabcap=counts["tabcap"],
        n_meta=counts["meta"], fig_w=fig_w, placeholder_names=placeholders,
        forbidden=hits, table_specs=table_specs)

    stats = {
        "source": str(source),
        "source_title": title,
        "docx": str(out_path),
        "paragraphs": len(back.paragraphs),
        "headings": {"h1": counts["h2"], "h2": counts["h3"], "h3": counts["h4"],
                     "total": counts["h2"] + counts["h3"] + counts["h4"]},
        "tables": counts["tables"],
        "images": counts["images"],
        "sections": len(back.sections),
        "headers": header_texts,
        "footers": footers,
        "bytes": out_path.stat().st_size,
        "forbidden_hits": hits,
        "spacing_issues": [] if not token_stats["self_check_issues"] else
                          [m for m in token_stats["self_check_issues"]
                           if "行距" in m or "w:line" in m],
        "source_blocks": {"chapters": counts["h2"], "sections": counts["h3"],
                          "images": counts["images"], "tables": counts["tables"],
                          "figure_captions": counts["figcap"],
                          "table_captions": counts["tabcap"],
                          "meta_lines": counts["meta"]},
        # 插图顺序 = 正文里 ![](...) 的出现顺序：build.ps1 第 ③ 步
        # （pdf_images_hires.py --order …）照这个列表替换，保证与 PDF 内插图一一对应
        "figures": [str(f.relative_to(REPO)).replace("\\", "/") if REPO in f.parents
                    else f.name for f in figures],
        # 占位图文件名（交件 --final 时须清零）与图注 / 表注文本
        "placeholders": placeholders,
        "captions": captions,
        "figure_width_cm": fig_w,
        "table_warnings": token_stats.get("table_warnings") or [],
        "caption_clean": bool(caption_clean),
        "caption_cleanups": caption_diffs,
        "tokens": token_stats,
        "self_check_issues": issues,
    }
    return stats


def main(argv=None):
    ap = argparse.ArgumentParser(description="技术文档 Markdown → 论文版式作品报告 DOCX")
    ap.add_argument("-Source", "--source", default=str(DEFAULT_SOURCE))
    ap.add_argument("-Out", "--out", default=str(DEFAULT_OUT))
    ap.add_argument("-Stats", "--stats", default=None, help="自检统计写成 JSON")
    ap.add_argument("--caption-clean", action="store_true",
                    help="打开图注净化（默认开；旧源里的构建说明才会命中）")
    ap.add_argument("--no-caption-clean", action="store_true",
                    help="关闭图注净化（新源图注即最终说明，可不净化）")
    ap.add_argument("--caption-rules", default=None,
                    help='图注净化规则 JSON：{"strip": ["正则", …], "keep": ["正则", …]}')
    ap.add_argument("--fig-width", type=float, default=None,
                    help="插图宽度 cm（默认落版宽 15.8）")
    args = ap.parse_args(argv)
    rules = None
    if args.caption_rules:
        try:
            rules = json.loads(Path(args.caption_rules).read_text(encoding="utf-8"))
        except (OSError, ValueError) as exc:
            print("[error] --caption-rules 读不了：%s" % exc, file=sys.stderr)
            return 1
        for key in ("strip", "keep"):
            if key in rules:
                for pat in rules[key]:
                    try:
                        re.compile(pat)
                    except re.error as exc:
                        print("[error] --caption-rules 的 %s 里正则不合法 %r：%s"
                              % (key, pat, exc), file=sys.stderr)
                        return 1
    clean = not args.no_caption_clean
    try:
        stats = build(Path(args.source).resolve(), Path(args.out).resolve(),
                      caption_clean=clean, caption_rules=rules,
                      fig_width_cm=args.fig_width)
    except SystemExit as exc:
        print("[error] %s" % exc.code, file=sys.stderr)
        return 1
    except ValueError as exc:
        print("[error] %s" % exc, file=sys.stderr)
        return 1

    print("[md2docx] source    : %s" % stats["source"])
    print("[md2docx] docx      : %s" % stats["docx"])
    print("[md2docx] paragraphs: %d   sections: %d（单节）"
          % (stats["paragraphs"], stats["sections"]))
    print("[md2docx] headings  : H1(一级)=%d H2(二级)=%d"
          % (stats["headings"]["h1"], stats["headings"]["h2"]))
    print("[md2docx] head style: H1 %s %.1fpt 居中加粗 / H2·H3 %s %.1fpt 左对齐加粗"
          "（均 %.1f 倍行距）" % (FONT_HEI, H1_PT, FONT_CN, H2_PT, HEAD_LINE))
    print("[md2docx] tables    : %d（三线表 9pt）   images: %d（宽 %.1fcm）"
          % (stats["tables"], stats["images"], stats["figure_width_cm"]))
    print("[md2docx] captions  : 图注 %d / 表注 %d（加粗 10pt 居中）"
          % (stats["source_blocks"]["figure_captions"],
             stats["source_blocks"]["table_captions"]))
    print("[md2docx] meta      : %d 行   占位图: %d %s"
          % (stats["source_blocks"]["meta_lines"], len(stats["placeholders"]),
             "、".join(stats["placeholders"]) or "（无）"))
    if stats["caption_cleanups"]:
        for c in stats["caption_cleanups"]:
            print("[md2docx] 图注净化  : 删掉 %s ← %s…" % (c["removed"], c["caption"]))
    warns = stats.get("table_warnings") or []
    if warns:
        print("[md2docx] 表格提示  : %d 条（列宽下限顶开 / 单元格过长；不阻断生成）"
              % len(warns))
        for w in warns[:10]:
            print("[md2docx]   - %s" % w)
    print("[md2docx] footer    : %s（PAGE 域 11pt 居中）"
          % " | ".join(t for t in stats["footers"] if t))
    print("[md2docx] bytes     : %d" % stats["bytes"])
    if stats["forbidden_hits"]:
        print("[md2docx] 禁项扫描  : 命中 %s —— 不得提交"
              % ",".join(stats["forbidden_hits"]), file=sys.stderr)
        return 1
    print("[md2docx] 禁项扫描  : 通过（无学校名 / 指导老师字样）")
    if stats["self_check_issues"]:
        for msg in stats["self_check_issues"]:
            print("[md2docx] 自检失败  : %s" % msg, file=sys.stderr)
        return 1
    print("[md2docx] 版式自检  : 通过（A4 / 2.4cm / 单节 / 页眉 logo / 字号 / 三线表 / "
          "图注表注加粗 / 页脚 PAGE 域）")
    if args.stats:
        Path(args.stats).write_text(json.dumps(stats, ensure_ascii=False, indent=2),
                                    encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
