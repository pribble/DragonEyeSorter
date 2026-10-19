# -*- coding: utf-8 -*-
"""docs/技术文档.md → submission/out/龙瞳分拣_作品报告.docx（竞赛作品报告排版）。

内容源只有一份：docs/技术文档.md（本脚本只读它，从不改写）。插图按 Markdown 里的
相对路径相对 docs/ 解析。脚本只认技术文档实际用到的 Markdown 写法——
`#` 封面标题、`##` 章、`###` 节、普通段落、**加粗**、行内 `代码`、`![alt](图)`
加下一行 *斜体图注*、`| … |` 表格、`- `/`1. ` 列表、`---` 分隔线——
不写通用大而全的解析器：遇到没见过的写法直接报错退出，不会静默丢内容。

设计语言（与答辩 PPT 同一套：白底、墨色文字、青绿强调、琥珀警示、灰蓝次级）：
  版式只认 python-docx 能表达的东西（段落边框 / 底纹 / 字色），不引第三方库：
    白底，墨 #0E1B2B 文字，青绿 #0D9488 强调（章标题色块 / 列表标记 / 表头分隔），
    琥珀 #B45309 留给警示与待测，次级灰 #5F7285 用于图注 / 页眉页脚，细线 #DCE3EA。
    中文统一微软雅黑，数字与行内代码可用 Consolas（代码带极浅底纹）。
  A4、页边距 2.6cm（版心 15.8cm），封面 / 目录 / 正文三节；封面无页眉页脚，正文页码从 1 起
  封面：顶部赛事信息 + 青绿短线、主标题 / 英文名 / 副标题层级、底部参赛队 / 编号 / 年月；
        只有作品信息，绝无学校名与指导老师字样（竞赛禁项，脚本每次生成后自检）
  正文：微软雅黑 10.5pt / 首行缩进 2 字符（420 twips）/ 两端对齐 / 1.3 倍行距
        （微软雅黑行高约 1.72×，视觉等效宋体 12pt 的 1.5 倍，见 BODY_LINE 注释） /
        段前 0、段后 6pt / 孤行寡行控制开；列表用青绿标记 + 悬挂缩进
  标题：一级左对齐 + 左侧青绿竖色块 + 下细线（16pt 加粗，章号青绿）；二级墨色 13pt 加粗，
        字号 / 间距成层级；标题 keepNext + keepLines，不落在页底；段前段后统一
  表格：表头浅青灰底（#E6F4F2）+ 墨色粗体 + 青绿表头分隔线；表格上粗下细、正文行细线
        （#DCE3EA）+ 可选斑马；整表 15.6cm 按版心、跨页重复表头、行不跨页
  插图：居中、宽 = 满版心 15.8cm（FIG_MAX_CM）；图注小号灰字、编号加粗，图与图注同页
  图注渲染前过一遍**净化**（CAPTION_STRIP_RULES）：去掉「（…生成，SVG 可直接放进 PPT）」
  这类给维护者看的构建说明，保留有证据价值的括号（如「管脚取自 src/constraint/io.adc」）
  目录：Word 域 TOC（word_to_pdf.ps1 导出前更新页码）；「目录」标题与章标题同一套色块语言；
        TOC 1/2/3 样式单独设（单倍行距 + plan_toc_styles() 按条目数挑段后距），
        不继承正文 1.3 倍行距
  页眉：左「龙瞳分拣 · 塑胶传动件在线质检分拣系统」+ 右标签 + 下细线；页脚「第 X 页 / 共 Y 页」
        用 PAGE / NUMPAGES 域；字体统一小号浅灰
  **行距一律用倍数（lineRule=auto）**：固定行距会裁字并让上下段叠在一起，见 set_spacing

用法（cwd 任意，路径都能用相对 cwd 或绝对路径）：
    python submission/build/md2docx.py [-Source <md>] [-Out <docx>] [-Stats <json>]
                                       [--caption-rules <json>] [--no-caption-clean]
                                       [--fig-width <cm>]

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
DOCS_DIR = REPO / "docs"
DEFAULT_SOURCE = DOCS_DIR / "技术文档.md"
DEFAULT_OUT = SUBMISSION / "out" / "龙瞳分拣_作品报告.docx"

# ---------------------------------------------------------------- 设计令牌
# 与答辩 PPT 同一套语言（白底、墨色文字、青绿强调、琥珀警示、灰蓝次级）。
# 只存 6 位十六进制，写进 OOXML 的 w:color / w:fill 属性。
C_INK = "0E1B2B"          # 墨：标题 / 正文 / 表头文字
C_TEAL = "0D9488"         # 青绿：章标题色块 / 列表标记 / 表头分隔线 / 封面短线
C_TEAL_DEEP = "0B6E69"    # 青绿深：强调级别（备用）
C_AMBER = "B45309"        # 琥珀：警示 / 待测（源文本里是 ⚠ 文字，颜色留给后续强调）
C_GRAY = "5F7285"         # 次级灰：图注 / 页眉页脚 / 表格辅助说明
C_LINE = "DCE3EA"         # 细线：标题下细线 / 表格正文行分隔 / 页眉页脚线
C_HEAD_FILL = "E6F4F2"    # 表头浅青灰底
C_ZEBRA = "F6F9FA"        # 正文行斑马纹（很浅，只做引导不抢眼）
C_CODE_FILL = "F1F6F5"    # 行内代码极浅底纹

FONT_CN = "微软雅黑"       # 中文正文 / 标题同一族
FONT_HEI = "微软雅黑"       # 标题（靠加粗拉层级，不再混第二种中文字体）
FONT_EN = "微软雅黑"       # 西文跟中文同族，避免行内字面高度跳变
FONT_CODE = "Consolas"    # 行内代码 / 数字
FONT_EN_PLAIN = "Consolas"

PAGE_W_CM, PAGE_H_CM = 21.0, 29.7            # A4
MARGIN_CM = 2.6                              # 左右 2.6cm → 版心 15.8cm
TEXT_W_CM = PAGE_W_CM - 2 * MARGIN_CM
TABLE_W_CM = 15.6                            # 整表上限，留 2mm 余量
FIG_MAX_CM = TEXT_W_CM                       # 插图默认满版心 15.8cm

# 个别图想窄一点（例如左右对比图放大后更难看清）：按源文件名覆盖宽度，单位 cm。
# 键 = `![alt](figures/xxx.png)` 里的文件名，值 = 厘米。空 = 全部按 FIG_MAX_CM。
FIG_WIDTH_OVERRIDE: dict[str, float] = {}

BODY_PT = 10.5                               # 五号：正文
# 行距：微软雅黑在 Word 里的单倍行高 ≈ 字号的 1.72 倍（实测 10.5pt → 18pt），
# 直接写 1.5 会得到 27pt 行距，整篇被撑长两成、读起来像双倍行距。1.3 倍 =
# 23.4pt，视觉密度与旧版「宋体 12pt + 1.5 倍行距」一致；行距仍只给倍数（见 set_spacing）。
MS_YAHEI_LINE = 1.72                         # 微软雅黑单倍行高 / 字号（Word 实测）
BODY_LINE = 1.3                              # 正文：≈1.5 倍视觉行距（见上）
TIGHT_LINE = 1.0                             # 表格 / 页眉页脚：单倍
CELL_LINE = 1.0                              # 表格单元格 / 图注：单倍（靠内边距喘气）
HEAD_LINE = 1.18                             # 标题：字大也不裁
BODY_AFTER = 6.0                             # 正文段后 pt
H_GAP = {"h1": (20.0, 10.0), "h2": (14.0, 6.0), "h3": (10.0, 4.0)}   # 段前 / 段后 pt
H1_PT, H2_PT, H3_PT = 16.0, 13.0, 11.5
TABLE_PT = 10.0                              # 表格正文
TABLE_HEAD_PT = 10.0                         # 表头
CAPTION_PT = 9.0                             # 图注
SMALL_PT = 8.5                               # 页眉页脚

TWIPS_PER_CM = 566.929
CHAR_TWIPS = int(round(BODY_PT * 20 * 2))    # 2 字符 = 2em = 420 twips
LIST_LEFT_TW = 840                           # 列表正文左缩进 4 字符（2 字符正文缩进 + 2 字符标记位）
LIST_HANG_TW = 420                           # 列表悬挂 2 字符（标记落在正文首行缩进处）
CELL_MAR_TW = 108                            # 单元格左右内边距（无竖线，靠留白分隔）

# 封面文案（内容无关：版式常量，不读源标题；改文案改这里）
COVER_BRAND = "龙瞳分拣"
COVER_BRAND_EN = "DragonEyeSorter"
COVER_TITLE = "基于安路飞龙 SoC 的塑胶传动件在线质检分拣系统"
COVER_SERIES = ("2026 全国大学生嵌入式芯片与系统设计竞赛",
                "FPGA 创新设计赛道 · 安路选题四")
COVER_KIND = "作品报告"
COVER_TEAM = "参赛队：____________"
COVER_ID = "作品编号：______________"
COVER_DATE = "2026 年 11 月"
HEADER_TEXT = "龙瞳分拣 · 塑胶传动件在线质检分拣系统"
HEADER_TOC = "目录"
HEADER_BODY = "作品报告"
FOOT_LEFT, FOOT_MID, FOOT_RIGHT = "第 ", " 页 / 共 ", " 页"
TOC_INSTR = r'TOC \o "1-3" \h \z \u'
TOC_PLACEHOLDER = "（打开文档后 Word 自动更新目录；也可全选后按 F9 手动更新）"
# 目录条目样式：**必须单独设**。目录条目默认用 Normal（正文 1.3 倍行距）+ 段后距，
# 36 条能撑成两页且第二页只填一半；这里给单倍行距 + 小段后距，压回一页。
# key = (样式名, 显示字号, 加粗, 左缩进 twips, 段后 pt)；Word 内置名是小写的 toc N。
# 实际档位由 plan_toc_styles() 按条目数算（微软雅黑行高偏大，条目多时要往紧里调），
# 下面的 TOC_STYLES 是缺省档位。
TOC_INDENT_TW = 420
# 目录一页可用高度：版心 694.5 − 标题块 ~57 − 目录域末尾空段一行 ~18 − 余量。
# Word 更新 TOC 域后，域末尾还会留一个空段落（行高按 Normal 算），预算要把它留出来，
# 否则最后一条贴底时会被顶到下一页，多出一页只有页眉的空白目录页。
TOC_BUDGET_PT = 612.0
TOC_STYLES = (("toc 1", 10.5, True, 0, 2.0),
              ("toc 2", 10.0, False, TOC_INDENT_TW, 1.0),
              ("toc 3", 10.0, False, 2 * TOC_INDENT_TW, 0.0))


def plan_toc_styles(n1, n2):
    """按章 / 节条目数挑一组能压进一页的 TOC 字号 + 段后距（内容无关）。"""
    tiers = (("toc 1", 10.5, True, 2.0), ("toc 2", 10.0, False, 1.0)), \
            (("toc 1", 10.5, True, 1.0), ("toc 2", 10.0, False, 0.0)), \
            (("toc 1", 10.5, True, 0.0), ("toc 2", 10.0, False, 0.0)), \
            (("toc 1", 10.5, True, 0.0), ("toc 2", 9.5, False, 0.0)), \
            (("toc 1", 10.0, True, 0.0), ("toc 2", 9.0, False, 0.0))
    for styles in tiers:
        need = sum(n * (sz * MS_YAHEI_LINE + after)
                   for n, (_, sz, _, after) in zip((n1, n2), styles))
        if need <= TOC_BUDGET_PT:
            out = []
            for name, sz, bold, after in styles:
                left = 0 if name == "toc 1" else TOC_INDENT_TW
                out.append((name, sz, bold, left, after))
            out.append(("toc 3", styles[1][1], False, 2 * TOC_INDENT_TW, 0.0))
            return tuple(out)
    return TOC_STYLES
TOC_LINE = 1.0                               # 目录条目单倍行距（不继承正文 1.5 倍）
CH_LABEL_RE = re.compile(r"^(\d+(?:\.\d+)*|附录\s*[A-Za-z]?)(\s+)(.+)$")

# ---------------------------------------------------------------- 图注净化
# 图注里的构建说明是写给维护者的（「这张图哪个脚本生成的、还能怎么用」），
# 不该出现在提交报告里。规则：按**括号**逐组判断——
#   ① 命中 KEEP（有证据价值，如来源文件、测量口径）→ 整组保留，优先于 strip；
#   ② 命中 STRIP（构建说明：生成脚本 / SVG / PPT / 排版链）→ 整组删掉；
#   ③ 都不命中 → 原样保留（保守，宁可留着也不误删证据）。
# 想改规则：改这两个元组，或用 --caption-rules <json> 传 {"strip": [...], "keep": [...]}，
# 或用 --no-caption-clean 整关闭。括号内的文字用非贪婪匹配，不支持嵌套括号。
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
ALLOWED_PHRASES = ("2026 全国大学生嵌入式芯片与系统设计竞赛", "全国大学生嵌入式芯片与系统设计竞赛")


def forbidden_hits(text):
    rest = text
    for phrase in ALLOWED_PHRASES:
        rest = rest.replace(phrase, "")
    return sorted({w for w in FORBIDDEN if w in rest})


def clean_caption(text, strip=None, keep=None):
    """图注净化：逐组括号判断该留该删，返回 (净化后文本, 被删掉的括号列表)。

    规则见 CAPTION_STRIP_RULES / CAPTION_KEEP_RULES（keep 优先）。整条图注被删空时
    退回原文——宁可留一句维护说明，也不能把图注弄没。
    """
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
    if not new or not re.sub(r"^图\s*[\d.\-–]+\s*", "", new).strip():
        return text, []                      # 删空了：退回原文，不当净化
    return new, removed

ALIGN = {WD_ALIGN_PARAGRAPH.LEFT: "left", WD_ALIGN_PARAGRAPH.CENTER: "center",
         WD_ALIGN_PARAGRAPH.RIGHT: "right", WD_ALIGN_PARAGRAPH.JUSTIFY: "both"}

# ---------------------------------------------------------------- 基础工具


def set_run_font(run, cn=FONT_CN, en=FONT_EN, size_pt=None, bold=None, color=C_INK):
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
        run.font.size = Pt(size_pt)
    if bold is not None:
        run.font.bold = bold
    if color:
        run.font.color.rgb = RGBColor.from_string(color)
    return run


def write(par, text, cn=FONT_CN, en=FONT_EN, size_pt=BODY_PT, bold=False, color=C_INK):
    run = par.add_run(text)
    set_run_font(run, cn, en, size_pt, bold, color)
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
            keep_lines=False, widow=True):
    """段落直接格式：一律显式写，不依赖样式继承。行距只给倍数。

    widow=True 是默认值：孤行 / 寡行控制开着，段首或段尾的单行不会被甩到相邻页
    （正文段落靠它避免「一页只剩一两行」）。整段不许跨页用 keep_lines=True，
    必须与下段同页（标题、图）用 keep_next=True。
    """
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


def add_field(par, instr, placeholder, cn=FONT_CN, size_pt=SMALL_PT, bold=False,
              color=C_GRAY):
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


def set_pPr_border(pPr, *, top=None, left=None, bottom=None, right=None):
    """给 <w:pPr> 加段落边框（段落与样式共用；设计令牌里的细线 / 色块都靠它）。

    edge 值是 dict：{"sz": 磅的 1/8, "color": 六位色, "space": 与文字间距 pt}。
    """
    old = pPr.find(qn("w:pBdr"))
    if old is not None:
        pPr.remove(old)
    specs = {"top": top, "left": left, "bottom": bottom, "right": right}
    if not any(specs.values()):
        return
    bdr = _ordered_child(pPr, "w:pBdr", _PPR_ORDER)
    for edge in ("top", "left", "bottom", "right"):
        cfg = specs[edge]
        if not cfg:
            continue
        el = OxmlElement("w:" + edge)
        el.set(qn("w:val"), cfg.get("val", "single"))
        el.set(qn("w:sz"), str(cfg.get("sz", 4)))
        el.set(qn("w:space"), str(cfg.get("space", 1)))
        el.set(qn("w:color"), cfg.get("color", C_LINE))
        bdr.append(el)


def para_border(par, *, top=None, left=None, bottom=None, right=None):
    """段落直接格式版的 set_pPr_border。"""
    set_pPr_border(par._p.get_or_add_pPr(), top=top, left=left,
                   bottom=bottom, right=right)


def add_tab_stop(par, pos_tw, align="left", leader=None, clear=()):
    """给段落加制表位（clear 里的位置先清掉，避免继承 Header / 目录样式的旧制表位）。"""
    pPr = par._p.get_or_add_pPr()
    tabs = pPr.get_or_add_tabs()
    for pos in clear:
        el = OxmlElement("w:tab")
        el.set(qn("w:val"), "clear")
        el.set(qn("w:pos"), str(int(pos)))
        tabs.append(el)
    el = OxmlElement("w:tab")
    el.set(qn("w:val"), align)
    el.set(qn("w:pos"), str(int(pos_tw)))
    if leader:
        el.set(qn("w:leader"), leader)
    tabs.append(el)


def set_char_spacing(run, twips):
    """字符间距（twips，1/20 pt）。小标签类的字距就靠它。"""
    rpr = run._element.get_or_add_rPr()
    el = _ordered_child(rpr, "w:spacing", _RPR_ORDER)
    el.set(qn("w:val"), str(int(twips)))


def shade_run(run, fill=C_CODE_FILL):
    """行内代码的极浅底纹。"""
    rpr = run._element.get_or_add_rPr()
    shd = _ordered_child(rpr, "w:shd", _RPR_ORDER)
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), fill)


def set_para_mark_font(par, size_pt=1.0, color=C_INK):
    """段落标记的字号 / 颜色：空段落（封面上画短线用）靠它把行高压到最小。"""
    pPr = par._p.get_or_add_pPr()
    rpr = _ordered_child(pPr, "w:rPr", _PPR_ORDER)
    set_rfonts_element(rpr)
    for tag in ("w:sz", "w:szCs"):
        el = _ordered_child(rpr, tag, _RPR_ORDER)
        el.set(qn("w:val"), str(max(2, int(round(size_pt * 2)))))
    el = _ordered_child(rpr, "w:color", _RPR_ORDER)
    el.set(qn("w:val"), color)


def set_cell_border(cell, **edges):
    """单元格边框（表头行的青绿分隔线用）。"""
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
        el.set(qn("w:color"), cfg.get("color", C_LINE))
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


def add_runs(par, text, size_pt=BODY_PT, base_bold=False, base_cn=FONT_CN,
             color=C_INK, code_size=None, code_color=C_TEAL_DEEP,
             code_shade=True):
    """写一行带 **加粗** 与 `行内代码` 的文本。

    base_bold=True 用于标题段：整段连西文、连行内代码一起走标题字体，标题里不出现
    第二种字面（正文里行内代码走 Consolas + 极浅底纹，见 shade_run）。
    """
    code_pt = code_size if code_size is not None else max(size_pt - 1.0, 8.0)
    for piece in INLINE_RE.split(text):
        if not piece:
            continue
        if piece.startswith("**") and piece.endswith("**") and len(piece) > 4:
            write(par, piece[2:-2], FONT_HEI, size_pt=size_pt, bold=True, color=color)
        elif piece.startswith("`") and piece.endswith("`") and len(piece) > 2:
            if base_bold:                   # 标题里的行内代码跟标题同字体，不引 Consolas
                write(par, piece[1:-1], FONT_HEI, size_pt=size_pt, bold=True,
                      color=color)
            else:
                run = write(par, piece[1:-1], FONT_CN, FONT_CODE, code_pt,
                            base_bold, code_color)
                if code_shade:
                    shade_run(run, C_CODE_FILL)
        else:
            write(par, piece, base_cn, size_pt=size_pt, bold=base_bold, color=color)


def add_chapter_runs(par, text, size_pt=H1_PT):
    """章标题：开头编号 / 「附录 A」青绿，其余墨色加粗（目录里仍是完整文本）。"""
    m = CH_LABEL_RE.match(text)
    if m:
        write(par, m.group(1), size_pt=size_pt, bold=True, color=C_TEAL)
        write(par, m.group(2), size_pt=size_pt, bold=True, color=C_INK)
        add_runs(par, m.group(3), size_pt=size_pt, base_bold=True)
    else:
        add_runs(par, text, size_pt=size_pt, base_bold=True)


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


def set_table_borders(table, spec):
    """表格边框（设计令牌）：spec = {edge: {"sz","color","val"}}。

    不用 Word 的「网格表 浅色」观感：上下各一条墨色横线（上粗下细），正文行之间
    只留 #DCE3EA 细分隔线，不画竖线（靠单元格留白分隔）——这是正式报告里常见的
    三线表语言，和 PPT 的「细线 + 白底」一致。
    """
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


def set_cell_margins(table):
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


def repeat_header(row):
    el = _ordered_child(row._tr.get_or_add_trPr(), "w:tblHeader", _TRPR_ORDER)
    el.set(qn("w:val"), "true")


def no_row_split(row):
    """行内不跨页：整行挪到下一页，表格不会在行中间被拦腰截断（跨页更整齐）。"""
    el = _ordered_child(row._tr.get_or_add_trPr(), "w:cantSplit", _TRPR_ORDER)
    el.set(qn("w:val"), "true")


def set_row_height(row, twips):
    """表头行最小高度（atLeast）：表头浅底 + 粗体加一行内边距才好喘气。"""
    el = _ordered_child(row._tr.get_or_add_trPr(), "w:trHeight", _TRPR_ORDER)
    el.set(qn("w:val"), str(int(twips)))
    el.set(qn("w:hRule"), "atLeast")


def shade(cell, fill):
    shd = _ordered_child(cell._tc.get_or_add_tcPr(), "w:shd", _TCPR_ORDER)
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), fill)


ONLY_NUM_RE = re.compile(r"^[\d.\s%±~≤≥<>/()\-–—、]+$")
PCT_RE = re.compile(r"\d\s*[%％]")
UNIT_RE = re.compile(r"(fps|ms|µs|us|ns|mm|cm|pt|Hz|MHz|kbit|步|次|项|件|轴|页)")


def _set_keep_next(par, value=True):
    par._p.get_or_add_pPr().get_or_add_keepNext().set(
        qn("w:val"), "1" if value else "0")


def _table_est_height(rows, widths, ncol):
    """估表格高度（pt）：行数最多的那列行数 × 单倍行高 + 单元格内边距。

    微软雅黑的单倍行高 ≈ 1.72 × 字号（见 MS_YAHEI_LINE），按 1.32 估会把表估矮
    三成，整表同页的判断就会失准、把大表硬推到下一页留下半页空白。
    """
    total = 0.0
    for cells in rows:
        lines = 1
        for c in range(ncol):
            plain = re.sub(r"[*`]", "", cells[c])
            avail = (widths[c] / TWIPS_PER_CM * 28.3465
                     - 2 * CELL_MAR_TW / TWIPS_PER_CM * 28.3465)
            lines = max(lines, wrapped_lines(plain, max(avail, 1.0)))
        total += lines * (TABLE_PT * MS_YAHEI_LINE) + 4.0
    return total


def keep_table_rows(table, rows, widths, threshold_pt=260.0):
    """表格分页控制：整表放得下就整表同页；放不下时至少表头 + 前两行同页。

    Word 没有「表格整体不跨页」开关，靠给行内段落 keepNext 串起来实现；大表仍允许
    跨页（跨页有重复表头），但不会出现「表头孤零零留在页底、正文行从下一页开始」。
    """
    ncol = len(rows[0])
    est = _table_est_height(rows, widths, ncol)
    last_keep = len(rows) - 2 if est <= threshold_pt else min(2, len(rows) - 1)
    for r in range(0, last_keep + 1):
        for c in range(ncol):
            for par in table.rows[r].cells[c].paragraphs:
                _set_keep_next(par, True)


def cell_align(text):
    """表头 / 短串 / 数值 / 带单位测量值居中，长文字左对齐。

    长中文串里天然会出现「件 / 次 / 步」这类单位字，不能一命中就居中——只有
    短串（≤16 字）才按单位判断，长段落一律左对齐，整张表才有阅读方向。
    """
    t = text.strip()
    if not t:
        return WD_ALIGN_PARAGRAPH.CENTER
    if len(t) <= 10 or ONLY_NUM_RE.match(t) or PCT_RE.search(t):
        return WD_ALIGN_PARAGRAPH.CENTER
    if len(t) <= 16 and UNIT_RE.search(t):
        return WD_ALIGN_PARAGRAPH.CENTER
    return WD_ALIGN_PARAGRAPH.LEFT


def render_table(doc, rows):
    """表格：表头浅青灰底 + 墨色粗体 + 青绿表头分隔线；正文行细分隔线 + 浅斑马。"""
    ncol = len(rows[0])
    widths = column_widths(rows, size_pt=TABLE_PT)
    table = doc.add_table(rows=len(rows), cols=ncol)
    table.style = "Table Grid"
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    table.autofit = False
    for i, w in enumerate(widths):
        table.columns[i].width = Twips(w)       # 网格列宽与 tcW 都写，Word 排版才稳
    tblW = table._tbl.tblPr.find(qn("w:tblW"))
    tblW.set(qn("w:type"), "dxa")
    tblW.set(qn("w:w"), str(sum(widths)))
    set_table_borders(table, {
        "top": {"val": "single", "sz": 12, "color": C_INK},       # 上粗线 1.5pt
        "bottom": {"val": "single", "sz": 6, "color": C_INK},     # 下细线 0.75pt
        "insideH": {"val": "single", "sz": 4, "color": C_LINE},   # 正文行细线
        "left": {"val": "none"}, "right": {"val": "none"}, "insideV": {"val": "none"},
    })
    set_cell_margins(table)
    set_table_indent(table)
    for r, cells in enumerate(rows):
        for c in range(ncol):
            cell = table.cell(r, c)
            cell.width = Twips(widths[c])
            cell.vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.CENTER
            par = cell.paragraphs[0]
            fmt_par(par, before=0.0, after=0.0, multiple=CELL_LINE,
                    align=WD_ALIGN_PARAGRAPH.CENTER if r == 0 else cell_align(cells[c]))
            add_runs(par, cells[c], size_pt=TABLE_HEAD_PT if r == 0 else TABLE_PT,
                     base_bold=(r == 0))
            if r == 0:
                shade(cell, C_HEAD_FILL)
                set_cell_border(cell, bottom={"val": "single", "sz": 8,
                                              "color": C_TEAL})
            elif r % 2 == 0:
                shade(cell, C_ZEBRA)            # 斑马纹：很浅，只做横向引导
    for row in table.rows:                       # 行不跨页，再让表头行跨页重复
        no_row_split(row)
    set_row_height(table.rows[0], 460)
    repeat_header(table.rows[0])
    keep_table_rows(table, rows, widths)         # 小表整表同页 / 大表表头不落单
    return table


def render_image(doc, src: Path, width_cm=FIG_MAX_CM):
    """插图：默认满版心 15.8cm（图内小字才能看清），细高图按高度上限再压。

    Word 导出 PDF 时会把插图重采样到 96 dpi，图框越大越糊——所以图框用满版心是
    清晰度的前提；像素换回原图靠 build.ps1 的第 3 步 pdf_images_hires.py。
    单张想窄一点：改 FIG_WIDTH_OVERRIDE；整篇想窄一点：--fig-width <cm>。
    """
    w_px, h_px = image_size_px(src)
    width_cm = FIG_WIDTH_OVERRIDE.get(src.name, width_cm)
    if h_px and w_px / h_px < 0.35:            # 细高图再按高度压一压（≤ 22cm）
        width_cm = min(width_cm, 22.0 * w_px / h_px)
    par = doc.add_paragraph()
    fmt_par(par, before=9.0, after=3.0, multiple=TIGHT_LINE,
            align=WD_ALIGN_PARAGRAPH.CENTER, keep_next=True)
    par.add_run().add_picture(str(src), width=Cm(width_cm))
    return par


CAPTION_NUM_RE = re.compile(r"^(图\s*[\dA-Za-z.\-–]+)\s*(.*)$")


def add_caption_runs(par, text):
    """图注：小号灰字，编号加粗墨色（「图 2-1」与说明文字拉开层级）。

    说明文字走 add_runs：图注里偶尔有 `行内代码`（如「管脚取自 `src/…`」），
    不能把反引号原样印到 PDF 上；图注里的代码用灰字、不上底纹，免得比图还抢眼。
    """
    m = CAPTION_NUM_RE.match(text)
    head, rest = (m.group(1), m.group(2)) if m else ("", text)
    if head:
        write(par, head, size_pt=CAPTION_PT, bold=True, color=C_INK)
        if rest:
            write(par, " ", size_pt=CAPTION_PT, color=C_GRAY)
    add_runs(par, rest, size_pt=CAPTION_PT, color=C_GRAY,
             code_size=CAPTION_PT - 0.5, code_color=C_GRAY, code_shade=False)


# ---------------------------------------------------------------- 文档搭建


def style_el(styles, name):
    """按名字取样式的 <w:style> 元素（style.element 对内置样式拿不到）。"""
    for st in styles:
        if st.name == name:
            return st.element
    raise KeyError(name)


def build_styles(doc, toc_styles=None):
    styles = doc.styles
    normal = styles["Normal"]
    strip_theme_fonts(style_el(styles, "Normal"))
    pPr = fresh_pPr(style_el(styles, "Normal"))
    set_keep(pPr, widow=True)
    set_spacing(pPr, 0.0, 0.0, BODY_LINE)
    set_ind(pPr, 0, 0)
    set_jc(pPr, "both")
    _ordered_child(pPr, "w:snapToGrid", _PPR_ORDER).set(qn("w:val"), "0")
    normal.font.size = Pt(BODY_PT)
    normal.font.name = FONT_EN
    normal.font.color.rgb = RGBColor.from_string(C_INK)
    set_rfonts_element(style_el(styles, "Normal").get_or_add_rPr())

    # 标题靠内置 Heading 样式提供大纲级别（目录域据此取条目）。
    # 一级标题 = 左侧青绿竖色块 + 下细线（放在样式上，目录条目不会继承）；
    # 段前 / 段后取正值，行距 1.18 倍——固定行距会裁字，见 set_spacing 注释
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
        if level == 0:                     # 章标题：青绿竖色块 + 下细线
            set_pPr_border(pPr,
                           left={"sz": 32, "space": 9, "color": C_TEAL},
                           bottom={"sz": 4, "space": 6, "color": C_LINE})
        out = OxmlElement("w:outlineLvl")
        out.set(qn("w:val"), str(level))
        pPr.append(out)
        st.font.name = FONT_EN
        st.font.size = Pt(size_pt)
        st.font.bold = True
        st.font.color.rgb = RGBColor.from_string(C_INK)
        set_rfonts_element(st_el.get_or_add_rPr())

    # 图注：居中、微软雅黑 9pt、灰字、无斜体、无缩进；编号在段落里单独加粗
    cap = styles.add_style("CaptionCN", WD_STYLE_TYPE.PARAGRAPH)
    cap.base_style = styles["Normal"]
    strip_theme_fonts(style_el(styles, "CaptionCN"))
    pPr = fresh_pPr(style_el(styles, "CaptionCN"))
    set_keep(pPr, next_=False, lines=True, widow=True)
    set_spacing(pPr, 3.0, 10.0, CELL_LINE)
    set_ind(pPr, 0, 0)
    set_jc(pPr, "center")
    cap.font.name = FONT_EN
    cap.font.size = Pt(CAPTION_PT)
    cap.font.bold = False
    cap.font.italic = False
    cap.font.color.rgb = RGBColor.from_string(C_GRAY)
    set_rfonts_element(style_el(styles, "CaptionCN").get_or_add_rPr())

    # 目录条目：TOC 1/2/3 单倍行距 + 小段后距。不这么设，条目会继承 Normal 的
    # 1.3 倍行距，36 条目录能占两页、第二页只填一半（改版前的实测现象）。
    # Word 内置样式名是小写 "toc 1"、styleId 是 "TOC1"——名字改回内置写法，
    # Word 更新 TOC 域时才会认这几个样式，而不是另建一套。
    for toc_name, sz, bold, left_tw, after_pt in (toc_styles or TOC_STYLES):
        disp_name = toc_name.upper()                       # python-docx 里叫 "TOC 1"
        if disp_name in [s.name for s in styles]:
            st = styles[disp_name]
        else:
            st = styles.add_style(disp_name, WD_STYLE_TYPE.PARAGRAPH)
            st.base_style = styles["Normal"]
        st_el = style_el(styles, disp_name)
        st_el.find(qn("w:name")).set(qn("w:val"), toc_name)   # 内置名 "toc 1"
        # python-docx 新建样式会打 w:customStyle="1"；去掉它，Word 才会把 "toc N"
        # 当成内置目录样式来用（否则目录条目仍会退回 Normal 的 1.3 倍行距）
        if st_el.get(qn("w:customStyle")) is not None:
            del st_el.attrib[qn("w:customStyle")]
        strip_theme_fonts(st_el)
        pPr = fresh_pPr(st_el)
        set_keep(pPr, next_=False, lines=True, widow=True)
        set_spacing(pPr, 0.0, after_pt, TOC_LINE)
        set_ind(pPr, 0, left_tw)
        set_jc(pPr, "left")
        # 页码右对齐靠样式里的右对齐制表位（带点线）：版心右边界 = TEXT_W_CM
        tabs = pPr.get_or_add_tabs()            # 按 schema 顺序插在 spacing 之前
        tab = OxmlElement("w:tab")
        tab.set(qn("w:val"), "right")
        tab.set(qn("w:leader"), "dot")
        tab.set(qn("w:pos"), str(int(round(TEXT_W_CM * TWIPS_PER_CM))))
        tabs.append(tab)
        st.font.name = FONT_EN
        st.font.size = Pt(sz)
        st.font.bold = bold
        st.font.color.rgb = RGBColor.from_string(C_INK)
        set_rfonts_element(st_el.get_or_add_rPr())

    # 正文 / 列表：单独给样式名，验收脚本按样式取样，不再靠对齐方式猜
    for name, first, left, hanging, after, align, tabs_at in (
            ("BodyCN", CHAR_TWIPS, 0, 0, BODY_AFTER, "both", None),
            ("ListCN", 0, LIST_LEFT_TW, LIST_HANG_TW, 2.5, "left", LIST_LEFT_TW)):
        st = styles.add_style(name, WD_STYLE_TYPE.PARAGRAPH)
        st.base_style = styles["Normal"]
        strip_theme_fonts(style_el(styles, name))
        pPr = fresh_pPr(style_el(styles, name))
        set_keep(pPr, widow=True)
        set_spacing(pPr, 0.0, after, BODY_LINE)
        set_ind(pPr, first, left, hanging)
        set_jc(pPr, align)
        if tabs_at:
            tabs = pPr.get_or_add_tabs()
            tab = OxmlElement("w:tab")
            tab.set(qn("w:val"), "left")
            tab.set(qn("w:pos"), str(int(tabs_at)))
            tabs.append(tab)
        st.font.name = FONT_EN
        st.font.size = Pt(BODY_PT)
        st.font.color.rgb = RGBColor.from_string(C_INK)
        set_rfonts_element(style_el(styles, name).get_or_add_rPr())


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
            fmt_par(par, before=0.0, after=0.0, multiple=TIGHT_LINE, widow=False)


def decorate(section, right_label, page_numbers=False):
    """目录 / 正文节：页眉「左品牌 + 右标签 + 下细线」；正文页脚加页码域。

    Header / Footer 内置样式自带 Letter 纸的居中 / 右对齐制表位（4680 / 9360
    twips），这里先 clear 掉再在版心右边界重设，右侧标签才不会被推到页中间。
    """
    section.header.is_linked_to_previous = False
    section.footer.is_linked_to_previous = False
    hp = section.header.paragraphs[0]
    clear_par(hp)
    fmt_par(hp, before=0.0, after=0.0, multiple=TIGHT_LINE,
            align=WD_ALIGN_PARAGRAPH.LEFT, widow=False)
    add_tab_stop(hp, int(round(TEXT_W_CM * TWIPS_PER_CM)), align="right",
                 clear=(4680, 9360, 12240))
    write(hp, HEADER_TEXT, size_pt=SMALL_PT, color=C_GRAY)
    hp.add_run("\t")
    write(hp, right_label, size_pt=SMALL_PT, bold=True, color=C_TEAL)
    para_border(hp, bottom={"sz": 4, "space": 4, "color": C_LINE})

    fp = section.footer.paragraphs[0]
    clear_par(fp)
    fmt_par(fp, before=0.0, after=0.0, multiple=TIGHT_LINE,
            align=WD_ALIGN_PARAGRAPH.CENTER, widow=False)
    if page_numbers:
        para_border(fp, top={"sz": 4, "space": 4, "color": C_LINE})
        write(fp, FOOT_LEFT, size_pt=SMALL_PT, color=C_GRAY)
        add_field(fp, " PAGE ", "1", size_pt=SMALL_PT, bold=True, color=C_INK)
        write(fp, FOOT_MID, size_pt=SMALL_PT, color=C_GRAY)
        add_field(fp, " NUMPAGES ", "1", size_pt=SMALL_PT, color=C_GRAY)
        write(fp, FOOT_RIGHT, size_pt=SMALL_PT, color=C_GRAY)


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


def _cover_par(doc, *, before=0.0, after=0.0, line=1.2):
    """封面段落：统一的段距 / 行距，段落标记压到 1pt（空段落不占行高）。"""
    p = fmt_par(doc.add_paragraph(), before=before, after=after, multiple=line,
                align=WD_ALIGN_PARAGRAPH.LEFT, widow=False)
    set_para_mark_font(p, 1.0)
    return p


def add_cover(doc):
    """封面：白底 + 青绿短横线 / 主副标题层级 / 底部参赛队 · 编号 · 年月。

    全部直排（不用表格、不用文本框，python-docx 里最稳）：竖向节奏靠段前 / 段后，
    层级靠字号 / 字重 / 字距。只有作品信息，绝无学校名与指导老师字样。
    """
    # 顶部赛事信息：两行小号灰字 + 字距，第二行下面画一条 6cm 青绿短线
    for i, line in enumerate(COVER_SERIES):
        p = _cover_par(doc, before=0.0 if i == 0 else 2.0, after=0.0)
        run = write(p, line, size_pt=9.5, color=C_GRAY)
        set_char_spacing(run, 24)
        if i == len(COVER_SERIES) - 1:
            ind = p._p.get_or_add_pPr().get_or_add_ind()
            ind.set(qn("w:right"),
                    str(int(round((TEXT_W_CM - 6.0) * TWIPS_PER_CM))))
            para_border(p, bottom={"sz": 18, "space": 10, "color": C_TEAL})

    # 「作品报告」小标签（青绿 / 大字距）
    p = _cover_par(doc, before=200.0, after=6.0)
    run = write(p, COVER_KIND, size_pt=11.0, bold=True, color=C_TEAL)
    set_char_spacing(run, 100)

    # 主标题三层：中文品牌 → 英文名 → 技术副标题
    p = _cover_par(doc, before=0.0, after=2.0, line=1.15)
    run = write(p, COVER_BRAND, size_pt=34.0, bold=True, color=C_INK)
    set_char_spacing(run, 60)
    p = _cover_par(doc, before=0.0, after=0.0)
    run = write(p, COVER_BRAND_EN, size_pt=13.0, bold=True, color=C_GRAY)
    set_char_spacing(run, 40)
    p = _cover_par(doc, before=26.0, after=0.0, line=1.35)
    write(p, COVER_TITLE, size_pt=15.0, color=C_INK)

    # 底部信息条：上细线 + 左「参赛队」/ 中「作品编号」/ 右「年月」
    p = _cover_par(doc, before=225.0, after=0.0)
    add_tab_stop(p, int(round(TEXT_W_CM * TWIPS_PER_CM / 2.0)), align="center")
    add_tab_stop(p, int(round(TEXT_W_CM * TWIPS_PER_CM)), align="right")
    write(p, COVER_TEAM, size_pt=10.5, color=C_GRAY)
    p.add_run("\t")
    write(p, COVER_ID, size_pt=10.5, color=C_GRAY)
    p.add_run("\t")
    write(p, COVER_DATE, size_pt=10.5, bold=True, color=C_INK)
    para_border(p, top={"sz": 4, "space": 8, "color": C_LINE})


def add_toc(doc):
    """目录页：小标签 + 「目录」标题（与章标题同一套青绿色块语言）+ TOC 域。"""
    p = fmt_par(doc.add_paragraph(), before=0.0, after=2.0, multiple=TIGHT_LINE,
                align=WD_ALIGN_PARAGRAPH.LEFT, widow=False)
    set_para_mark_font(p, 1.0)
    run = write(p, "CONTENTS", size_pt=8.5, bold=True, color=C_TEAL)
    set_char_spacing(run, 70)
    p = fmt_par(doc.add_paragraph(), before=0.0, after=4.0, multiple=TIGHT_LINE,
                align=WD_ALIGN_PARAGRAPH.LEFT, widow=False)
    run = write(p, "目录", size_pt=H1_PT, bold=True, color=C_INK)
    set_char_spacing(run, 40)
    set_pPr_border(p._p.get_or_add_pPr(),
                   left={"sz": 32, "space": 9, "color": C_TEAL},
                   bottom={"sz": 4, "space": 6, "color": C_LINE})
    p = doc.add_paragraph()
    fmt_par(p, before=6.0, after=0.0, multiple=TIGHT_LINE,
            align=WD_ALIGN_PARAGRAPH.LEFT, widow=False)
    set_para_mark_font(p, 1.0, C_GRAY)
    add_field(p, TOC_INSTR, TOC_PLACEHOLDER, size_pt=BODY_PT, color=C_GRAY)


def compact_section_breaks(doc):
    """把装 sectPr 的空段压到 1pt 行高。

    python-docx 的 add_section() 会在上一节末尾留一个「装着上一节 sectPr 的空段落」。
    这个段落默认继承 Normal（10.5pt / 1.3 倍行距），会独占约 24pt：目录最后一条正好
    贴到版心底时，它就被顶到下一页，于是目录后面多出一整页空白。压到 1pt 后只是页面
    底部看不见的一条线，不会白白多一页。
    """
    for par in doc.paragraphs:
        pPr = par._p.pPr
        if pPr is None:
            continue
        sect = pPr.find(qn("w:sectPr"))
        if sect is None:
            continue
        clear_par(par)
        for child in list(pPr):
            if child is not sect:
                pPr.remove(child)
        sp = _ordered_child(pPr, "w:spacing", _PPR_ORDER)
        sp.set(qn("w:before"), "0")
        sp.set(qn("w:after"), "0")
        sp.set(qn("w:line"), "240")
        sp.set(qn("w:lineRule"), "auto")
        rpr = _ordered_child(pPr, "w:rPr", _PPR_ORDER)
        set_rfonts_element(rpr)
        for tag in ("w:sz", "w:szCs"):
            _ordered_child(rpr, tag, _RPR_ORDER).set(qn("w:val"), "2")
        _ordered_child(rpr, "w:color", _RPR_ORDER).set(qn("w:val"), C_INK)


def set_update_fields(doc):
    """让 Word 打开时自动更新域（目录 / 页码因此不会停在占位文本）。"""
    el = OxmlElement("w:updateFields")
    el.set(qn("w:val"), "true")
    doc.settings.element.append(el)


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
    if not 3.0 <= fig_w <= TEXT_W_CM:
        raise SystemExit("--fig-width 只允许 3.0~%.1fcm（版心宽）" % TEXT_W_CM)

    doc = Document()
    n_ch = sum(1 for k, _ in blocks if k == "h2")
    n_sec = sum(1 for k, _ in blocks if k == "h3")
    build_styles(doc, plan_toc_styles(n_ch, n_sec))
    setup_section(doc.sections[0])
    add_cover(doc)

    sec_toc = doc.add_section(WD_SECTION.NEW_PAGE)
    setup_section(sec_toc)
    add_toc(doc)

    sec_body = doc.add_section(WD_SECTION.NEW_PAGE)
    setup_section(sec_body)
    restart_page_numbering(sec_body)

    counts = {"h2": 0, "h3": 0, "images": 0, "tables": 0}
    figures, caption_diffs = [], []
    for kind, payload in blocks:
        if kind in ("pagebreak",):
            continue
        if kind == "h2":
            counts["h2"] += 1
            before, after = H_GAP["h1"]
            # keepNext：章标题不单独留在页尾（孤行标题）
            par = fmt_par(doc.add_paragraph(style="Heading 1"), before=before,
                          after=after, multiple=HEAD_LINE, keep_next=True,
                          keep_lines=True, align=WD_ALIGN_PARAGRAPH.LEFT)
            add_chapter_runs(par, payload, size_pt=H1_PT)
        elif kind == "h3":
            counts["h3"] += 1
            before, after = H_GAP["h2"]
            add_runs(fmt_par(doc.add_paragraph(style="Heading 2"), before=before,
                             after=after, multiple=HEAD_LINE, keep_next=True,
                             keep_lines=True, align=WD_ALIGN_PARAGRAPH.LEFT),
                     payload, size_pt=H2_PT, base_bold=True)
        elif kind == "p":
            add_runs(fmt_par(doc.add_paragraph(style="BodyCN"), before=0.0,
                             after=BODY_AFTER, multiple=BODY_LINE,
                             first=CHAR_TWIPS), payload)
        elif kind == "ul":
            for item in payload:
                par = fmt_par(doc.add_paragraph(style="ListCN"), before=0.0,
                              after=2.5, multiple=BODY_LINE, left=LIST_LEFT_TW,
                              hanging=LIST_HANG_TW)
                write(par, "•", size_pt=BODY_PT, bold=True, color=C_TEAL)
                par.add_run("\t")
                add_runs(par, item, size_pt=BODY_PT)
        elif kind == "ol":
            for num, item in payload:
                par = fmt_par(doc.add_paragraph(style="ListCN"), before=0.0,
                              after=2.5, multiple=BODY_LINE, left=LIST_LEFT_TW,
                              hanging=LIST_HANG_TW)
                write(par, "%d." % num, size_pt=BODY_PT, bold=True, color=C_TEAL)
                par.add_run("\t")
                add_runs(par, item, size_pt=BODY_PT)
        elif kind == "image":
            src = (DOCS_DIR / payload).resolve()
            if not src.is_file():
                raise SystemExit("插图不存在：%s（按相对 docs/ 解析）" % payload)
            render_image(doc, src, fig_w)
            counts["images"] += 1
            figures.append(src)
        elif kind == "caption":
            text = payload
            if caption_clean:
                text, removed = clean_caption(payload, strip_rules, keep_rules)
                for r in removed:
                    caption_diffs.append({"caption": payload[:40], "removed": r})
            par = fmt_par(doc.add_paragraph(style="CaptionCN"), before=3.0,
                          after=10.0, multiple=CELL_LINE, keep_lines=True,
                          align=WD_ALIGN_PARAGRAPH.CENTER)
            add_caption_runs(par, text)
        elif kind == "table":
            render_table(doc, payload)
            counts["tables"] += 1
            fmt_par(doc.add_paragraph(), before=0.0, after=0.0,
                    multiple=0.6)                 # 表后间隔（细空行，144 twips）
        else:  # pragma: no cover
            raise ValueError("未知块类型 %r" % kind)

    compact_section_breaks(doc)
    blank(doc.sections[0])
    decorate(doc.sections[1], HEADER_TOC)
    decorate(doc.sections[2], HEADER_BODY, page_numbers=True)
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
        # 插图顺序 = 正文里 ![](...) 的出现顺序：build.ps1 第 3 步
        # （pdf_images_hires.py --order …）照这个列表替换，保证与 PDF 内插图一一对应
        "figures": [str(f.relative_to(REPO)).replace("\\", "/") if REPO in f.parents
                    else f.name for f in figures],
        "figure_width_cm": fig_w,
        "caption_clean": bool(caption_clean),
        "caption_cleanups": caption_diffs,
    }
    return stats


def main(argv=None):
    ap = argparse.ArgumentParser(description="技术文档 Markdown → 作品报告 DOCX")
    ap.add_argument("-Source", "--source", default=str(DEFAULT_SOURCE))
    ap.add_argument("-Out", "--out", default=str(DEFAULT_OUT))
    ap.add_argument("-Stats", "--stats", default=None, help="自检统计写成 JSON")
    ap.add_argument("--no-caption-clean", action="store_true",
                    help="关闭图注净化（默认开：去掉图注里的构建说明）")
    ap.add_argument("--caption-rules", default=None,
                    help='图注净化规则 JSON：{"strip": ["正则", …], "keep": ["正则", …]}')
    ap.add_argument("--fig-width", type=float, default=None,
                    help="插图宽度 cm（默认满版心 15.8；想整体窄一点才用，最大 15.8）")
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
    try:
        stats = build(Path(args.source).resolve(), Path(args.out).resolve(),
                      caption_clean=not args.no_caption_clean, caption_rules=rules,
                      fig_width_cm=args.fig_width)
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
    print("[md2docx] tables    : %d   images: %d（宽 %.1fcm）"
          % (stats["tables"], stats["images"], stats["figure_width_cm"]))
    if stats["caption_cleanups"]:
        for c in stats["caption_cleanups"]:
            print("[md2docx] 图注净化  : 删掉 %s ← %s…" % (c["removed"], c["caption"]))
    else:
        print("[md2docx] 图注净化  : 没命中构建说明（规则见 CAPTION_STRIP_RULES）")
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
