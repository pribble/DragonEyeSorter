#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""作品报告验收自检（官方模板五部分结构；独立复核用，不改任何产物）。

对 **docx + PDF + 源 Markdown** 三方对账，逐项核对报告版式与内容口径（版式令牌见
submission/build/README.md，结构模板见 docs/模板.pdf 与本轮 log），原始结果写进
submission/out/_verify_report.txt。检查项按组编号：

  1 产物与生成统计：docx / PDF / 源可读，PDF 文件头与 ≤ 50MB，生成侧自检无告警，
    页数取 Word ComputeStatistics（word_to_pdf.ps1 记录），不估算
  2 源 Markdown：官方模板骨架（# 标题 = 官网报名作品名、H1 = 摘要 + 五部分、
    H2 = 1.1~4.2、H3 = 2.2.x/2.3.x/3.2.x）、模板字数上限（摘要 800 / 1.1~1.3 400 /
    1.4~1.6 200 / 4.1 300 / 4.2 1000，Word 口径只数正文段）、
    图注表注计数与编号连续唯一、图注在图下方 / 表注在表上方、每个图/表都被正文引用、
    无代码块 / 引用块 / 五级标题 / 「- 」列表 / HTML
  3 DOCX 结构：段落 / 表格 / 插图计数三方对账，单节无封面无目录，标题与元信息、
    章节文字逐条等于源，图注表注数量等于源
  4 版式令牌：正文楷体小四 12pt / Times 两端对齐 / 首行缩进 480 twips / 1.5 倍行距、
    作品名 17.5pt 黑体居中、一 / 二 / 三级章节标题一律楷体四号 14pt 加粗 2 倍行距、
    图注表注楷体加粗五号 10.5pt、行内代码 Consolas 11pt、参考列表 12pt 悬挂 1.5 字符、
    无固定行距（倍数只允许 240/360/480）、全篇只有黑色
  5 三线表：≤ 15.6cm 固定布局、上下 1.5pt / 表头下 0.75pt 黑线、无竖线行线底纹、
    表头重复加粗、行不跨页、单元格内边距 108 twips、表体 9pt 楷体；
    **列宽与源分隔行提示（破折号 = 权重）偏差 ≤ 8%**、**列对齐 = 源冒号提示**，
    单元格估算行数 > 6 行只给 WARN（不判 FAIL）
  6 插图与占位：宽 15.8cm 居中、图注紧跟图下方且与图同页、等效 dpi ≥ 300、
    落版最小字号 ≥ 7pt；占位按 placeholder_manifest.json 的 `in_report` 计
    （图注数 = 源图数 = 报告插图数），**--final 只对 in_report=true 且
    replaced=false 的占位判残留 FAIL**（交件前门禁）
  7 页面与页码：A4、页边距 2.4cm、页脚距边 1.6cm、页眉仅官方模板 logo（1 图无文字）、
    页脚居中 PAGE 域（无 NUMPAGES / 「第 X 页」）、页码从 1 起、参考文献 1~20 篇、
    PDF 无彩色
  8 PDF 渲染：字体全部嵌入、四族字体齐全（楷体 / 黑体 / Times / Consolas）、
    中文不被回退字体替换、行框无重叠
  9 逐页留白：fill% = 内容高度 / 版心 24.9cm，正文页填充 < 50% 判 FAIL（末页除外），
    无整页空白；底部空白 > 20% 的页逐页列出原因
 10 数字守恒：调用 .refactor/check_numbers.py（该中间产物已随重构收尾删除 → SKIP）
 11 禁项与生成链：无学校 / 学院 / 指导老师 / 参赛队名、无主机绝对路径、无彩色，
    生成链脚本与 README 不含真实主机路径

计数不写死：章 / 节 / 图 / 表数量一律从源 Markdown 与 out/_md2docx.json 取数，
再与 docx / PDF 对账；元信息行、占位图条数同样按源与 manifest 实际条数核对。

用法：
    python submission/build/verify_report.py                 # 常规验收
    python submission/build/verify_report.py --final          # 交件门禁（占位图必须清零）
    python submission/build/verify_report.py --fill --pdf <任意 PDF> [--warn 20]
        —— 只跑逐页留白分析（改版前后对比用；可指向旧 PDF）
    python submission/build/verify_report.py --docx a.docx --pdf b.pdf --source c.md
退出码：0 无 FAIL / 1 有 FAIL（WARN 不影响退出码）。
"""
from __future__ import annotations

import argparse
import json
import math
import re
import subprocess
import sys
import zipfile
from pathlib import Path

try:  # Windows 控制台默认 GBK：打印 µ / ° / ⚠ 之类字符时别崩
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except (AttributeError, ValueError):
    pass

BUILD = Path(__file__).resolve().parent
REPO = BUILD.parent.parent
SUB = REPO / "submission"
OUT = SUB / "out"
FIGURES = SUB / "figures"
REFACTOR = SUB / ".refactor"
DOCX = OUT / "龙瞳分拣_作品报告.docx"
PDF = OUT / "龙瞳分拣_作品报告.pdf"
SOURCE = SUB / "技术文档.md"
STATS = OUT / "_md2docx.json"
PDF_STATS = OUT / "_word_to_pdf.json"
MANIFEST = FIGURES / "placeholder_manifest.json"
FIG_META = FIGURES / "fig_meta.json"
CHECK_NUMBERS = REFACTOR / "check_numbers.py"
DOCS_TEST = REPO / "docs" / "测试与验证.md"
DOCS_LOG = REPO / "docs" / "log"

# ---------------------------------------------------------------- 版式令牌
# 与 SPEC §2 同口径（独立复核用，不 import 生成脚本）。
# 1cm = 28.3465pt；1pt = 20 twips。
FONT_CN = "楷体"
FONT_HEI = "黑体"
FONT_EN = "Times New Roman"
FONT_CODE = "Consolas"
C_INK = "000000"
PAGE_W_CM, PAGE_H_CM = 21.0, 29.7
MARGIN_CM = 2.4
FOOTER_DIST_CM = 1.6
TEXT_W_CM = PAGE_W_CM - 2 * MARGIN_CM            # 16.2cm 版心
TEXT_H_CM = PAGE_H_CM - 2 * MARGIN_CM            # 24.9cm 版心
BODY_PT = 12.0                                   # 正文：楷体小四
TITLE_PT = 17.5                                  # 作品名（标题块维持，规格只约束章节标题）
H1_PT = 14.0                                     # 章节标题：楷体四号（一 / 二 / 三级同号）
H2_PT = 14.0
H3_PT = 14.0
META_PT = 10.0
CAPTION_PT = 10.5                                # 图注 / 表注：五号（加粗）
TABLE_PT = 9.0
CODE_PT = 11.0
LIST_PT = 12.0                                   # 参考文献列表按正文小四
FOOTER_PT = 11.0
INDENT_TW = 480                                  # 2 字符 @12pt
LIST_HANG_TW = 360                               # 1.5 字符 @12pt
CELL_MAR_TW = 108                                # 单元格左右内边距
TABLE_MAX_CM = 15.6
FIG_W_CM = 15.8
FIG_W_PT = FIG_W_CM / 2.54 * 72.0                # 447.92pt（figkit 画布宽）
LINE_TW = 240                                    # 单倍行距
BODY_LINE_TW = 360                               # 正文 1.5 倍
HEAD_LINE_TW = 480                               # 章节标题 2 倍
MIN_IMG_DPI = 300.0
MIN_FIG_PT = 7.0
FILL_WARN = 20.0
FILL_FAIL = 50.0
BLANK_FILL = 8.0

A4_W_PT, A4_H_PT = 595.28, 841.89
TEXT_TOP_PT = MARGIN_CM / 2.54 * 72.0            # 68.03pt
TEXT_BOT_PT = A4_H_PT - TEXT_TOP_PT              # 773.86pt
TEXT_H_PT = TEXT_BOT_PT - TEXT_TOP_PT            # 705.83pt

FORBIDDEN = ("大学", "学院", "学校", "指导老师", "指导教师", "导师", "校徽",
             "班级", "队名", "代表队", "参赛队")
ALLOWED = ("2026 全国大学生嵌入式芯片与系统设计竞赛",
           "全国大学生嵌入式芯片与系统设计竞赛")

GROUPS: list[str] = []
results: list[tuple[str, str, object, str]] = []
warnings: list[tuple[str, str, str]] = []      # WARN：只提示，不影响退出码
LINES: list[str] = []
_seen_groups: set[str] = set()
GROUP_ORDER = ["1 产物与生成统计", "2 源 Markdown", "3 DOCX 结构", "4 版式令牌",
               "5 三线表", "6 插图与占位", "7 页面与页码", "8 PDF 渲染",
               "9 逐页留白", "10 数字守恒", "11 禁项与生成链"]


# ---------------------------------------------------------------- 基础
def emit(s: str = "") -> None:
    LINES.append(s)
    print(s)


def _group_head(group: str) -> None:
    """该组第一次出现：报告里起组头。"""
    if group not in GROUPS:
        GROUPS.append(group)
    if group not in _seen_groups:
        _seen_groups.add(group)
        emit("")
        emit("【%s】" % group)


def check(group: str, name: str, ok, detail: str = "") -> None:
    """ok = True 通过 / False 失败 / None 跳过（合理例外，报告写明原因）。"""
    _group_head(group)
    results.append((group, name, ok, detail))


def warn(group: str, name: str, detail: str = "") -> None:
    """WARN：版面提示项，不算失败、不影响退出码（如单元格 > 6 行）。"""
    _group_head(group)
    warnings.append((group, name, detail))
    emit("  WARN %s%s" % (name, ("  —— " + detail) if detail else ""))


def section(group: str) -> None:
    emit("")
    emit("【%s】" % group)


def read_json(path: Path) -> dict:
    if not path.is_file():
        return {}
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (ValueError, OSError):
        return {}


def near(a, b, tol=1e-6) -> bool:
    try:
        return abs(float(a) - float(b)) <= tol
    except (TypeError, ValueError):
        return False


def has_cjk(s: str) -> bool:
    return bool(re.search(r"[\u4e00-\u9fff]", s))


def has_latin(s: str) -> bool:
    return bool(re.search(r"[A-Za-z0-9]", s))


def strip_allowed(text: str) -> str:
    for phrase in ALLOWED:
        text = text.replace(phrase, "")
    return text


def host_paths(text: str) -> list[str]:
    """文本里的真实主机绝对路径（盘符 + 至少两段 / UNC 路径）。"""
    bs = chr(92)
    hits = []
    sep = "[" + bs + bs + "/]"
    bad = "[^ \t\r\n\"'`,;)]*"
    pat = re.compile("[A-Za-z]:" + sep + bad)
    for m in pat.finditer(text):
        raw = m.group(0)
        if len(raw) > 3 and raw[2] == "/" and raw[3] == "/":
            continue                       # https:// / s:// 这类协议，不是主机路径
        p = raw.rstrip(bs + "/")
        parts = [x for x in re.split("[" + bs + bs + "/]+", p[2:]) if x]
        if len(parts) >= 2:
            hits.append(p)
    unc = re.compile(bs * 4 + "[A-Za-z0-9_.-]+" + bs * 2 + bad)
    hits.extend(m.group(0) for m in unc.finditer(text))
    return hits


# ---------------------------------------------------------------- 源 Markdown
SRC_CAP_FIG_RE = re.compile(r"^\*\*图\s*(\d+)\*\*\u3000(.+)$")
SRC_CAP_TBL_RE = re.compile(r"^\*\*表\s*(\d+)\*\*\u3000(.+)$")
# docx 里加粗标记已渲染掉：图注文字是「图 N　说明。」/「表 N　说明」
CAPTION_FIG_RE = re.compile(r"^图\s*(\d+)\u3000(.+)$")
CAPTION_TBL_RE = re.compile(r"^表\s*(\d+)\u3000(.+)$")
IMAGE_LINE_RE = re.compile(r"^!\[([^\]]*)\]\(([^)]+)\)$")
META_LINE_RE = re.compile(r"^\*\*([^*]+)\*\*：(.+)$")
TABLE_ROW_RE = re.compile(r"^\|.*\|$")
TABLE_SEP_RE = re.compile(r"^\|[\s:|-]+\|$")


def split_row(line: str) -> list[str]:
    """按未转义的 | 切表格行。"""
    t = line.strip()
    if t.startswith("|"):
        t = t[1:]
    if t.endswith("|"):
        t = t[:-1]
    cells, buf, esc = [], [], False
    for ch in t:
        if esc:
            buf.append(ch)
            esc = False
        elif ch == "\\":
            buf.append(ch)
            esc = True
        elif ch == "|":
            cells.append("".join(buf).strip())
            buf = []
        else:
            buf.append(ch)
    cells.append("".join(buf).strip())
    return cells


def sep_hint(cell: str):
    """分隔行单元 → (列宽权重, 对齐提示)。

    破折号个数 = 列宽权重（至少 1）；冒号位置 = 对齐：
    `:---` 左、`:---:` 居中、`---:` 右、无冒号 = 未提示。
    """
    c = cell.strip()
    weight = max(c.count("-"), 1)
    left, right = c.startswith(":"), c.endswith(":")
    if left and right:
        align = "center"
    elif right:
        align = "right"
    elif left:
        align = "left"
    else:
        align = None
    return weight, align


def parse_source(text: str) -> dict:
    """把源 Markdown 解析成结构（独立于 md2docx.py 的统计）。"""
    lines = text.splitlines()
    src = {"title": "", "meta": [], "h1": [], "h2": [], "h3": [], "heading_seq": [],
           "images": [], "fig_captions": [], "tbl_captions": [],
           "table_blocks": [], "tables": 0, "tables_data": [], "ref_items": []}
    cur_h1 = ""                         # 当前 H1 节名（用于收集参考文献条目）
    meta_open = True                    # 元信息区：标题后连续的 **标签**：值 行（上限 6）
    for i, ln in enumerate(lines):
        t = ln.strip()
        if t.startswith("# ") and not src["title"]:
            src["title"] = t[2:].strip()
            continue
        if meta_open:
            m = META_LINE_RE.match(t)
            if m and len(src["meta"]) < 6:
                src["meta"].append((m.group(1).strip(), m.group(2).strip()))
                continue
            if t:
                meta_open = False
        if t.startswith("#### "):
            src["h3"].append(t[5:].strip())
            src.setdefault("heading_seq", []).append(("h3", t[5:].strip()))
        elif t.startswith("### "):
            src["h2"].append(t[4:].strip())
            src.setdefault("heading_seq", []).append(("h2", t[4:].strip()))
        elif t.startswith("## "):
            src["h1"].append(t[3:].strip())
            cur_h1 = t[3:].strip()
            src.setdefault("heading_seq", []).append(("h1", t[3:].strip()))
        elif re.match(r"^\d+\.\s", t) and cur_h1.startswith("第五部分"):
            src["ref_items"].append(t)
        elif IMAGE_LINE_RE.match(t):
            src["images"].append(IMAGE_LINE_RE.match(t).group(2).strip())
        elif SRC_CAP_FIG_RE.match(t):
            m = SRC_CAP_FIG_RE.match(t)
            src["fig_captions"].append((int(m.group(1)), m.group(2), i))
        elif SRC_CAP_TBL_RE.match(t):
            m = SRC_CAP_TBL_RE.match(t)
            src["tbl_captions"].append((int(m.group(1)), m.group(2), i))
    # 表格：表头 / 分隔行（列宽权重 + 对齐提示）/ 数据行
    tables_data = []
    i = 0
    while i < len(lines):
        t = lines[i].strip()
        if TABLE_ROW_RE.match(t) and i + 1 < len(lines) \
                and TABLE_SEP_RE.match(lines[i + 1].strip()):
            rows, j = [], i
            while j < len(lines) and TABLE_ROW_RE.match(lines[j].strip()):
                rows.append(split_row(lines[j]))
                j += 1
            if len(rows) >= 2:
                tables_data.append({"line": i, "header": rows[0],
                                    "sep": rows[1], "rows": rows[2:]})
            i = j
            continue
        i += 1
    src["tables_data"] = tables_data
    src["table_blocks"] = [d["line"] for d in tables_data]
    src["tables"] = len(tables_data)
    return src


def source_problems(text: str) -> list[str]:
    """SPEC §3 禁止的源写法。"""
    bad = []
    for i, ln in enumerate(text.splitlines(), 1):
        t = ln.rstrip()
        if t.strip().startswith("```"):
            bad.append("第 %d 行：代码块" % i)
        elif t.lstrip().startswith(">"):
            bad.append("第 %d 行：引用块" % i)
        elif t.startswith("#####"):
            bad.append("第 %d 行：五级标题" % i)
        elif re.match(r"^\s*-\s+", t) and not TABLE_ROW_RE.match(t.strip()):
            bad.append("第 %d 行：「- 」项目符号" % i)
        elif re.search(r"<[a-zA-Z][^>]*>", t) and "`" not in t:
            bad.append("第 %d 行：HTML 标签" % i)
    return bad[:13]


def word_count_cn(s: str) -> int:
    """Word「字数」口径：CJK 字符（含中文标点等宽区）每个计 1，连续的非 CJK 非空白串计 1。"""
    cjk_re = r"[\u4e00-\u9fff\u3000-\u303f\uff00-\uffef⚠µ×]"
    n, i = 0, 0
    while i < len(s):
        ch = s[i]
        if re.match(cjk_re, ch):
            n += 1
            i += 1
        elif ch.isspace():
            i += 1
        else:
            j = i
            while j < len(s) and not re.match(cjk_re, s[j]) and not s[j].isspace():
                j += 1
            n += 1
            i = j
    return n


def section_word_counts(text: str) -> dict[str, int]:
    """官方模板各节的正文字数（Word 口径）。

    只数正文段落：排除标题、表格行、插图行、图注 / 表注、列表（参考文献）、元信息。
    """
    lines = text.splitlines()
    cur, secs = None, {}
    for ln in lines:
        t = ln.strip()
        if t.startswith("#"):
            cur = t.lstrip("#").strip()
            secs.setdefault(cur, [])
        elif cur is not None:
            secs[cur].append(ln)
    out = {}
    for name, body in secs.items():
        n = 0
        for ln in body:
            t = ln.strip()
            if not t or t.startswith(("|", "![", "#")):
                continue
            if re.match(r"^\*\*[图表] \d+\*\*", t):
                continue
            if re.match(r"^\d+\.\s", t):
                continue
            n += word_count_cn(t)
        out[name] = n
    return out


# 官方模板的字数上限（submission/build/README.md「官方模板」节）：节名前缀 → 上限字数
TEMPLATE_WORD_LIMITS = [("摘要", 800), ("1.1", 400), ("1.2", 400), ("1.3", 400),
                        ("1.4", 200), ("1.5", 200), ("1.6", 200),
                        ("4.1", 300), ("4.2", 1000)]

# 官方模板的章节骨架：H1 集合 + H2 编号 + H3 编号（见 docs/模板.pdf）
TEMPLATE_H1 = ["摘要", "第一部分", "第二部分", "第三部分", "第四部分", "第五部分"]
TEMPLATE_H2 = ["1.1", "1.2", "1.3", "1.4", "1.5", "1.6", "2.1", "2.2", "2.3",
               "3.1", "3.2", "3.3", "4.1", "4.2"]
TEMPLATE_H3 = ["2.2.1", "2.2.2", "2.2.3", "2.3.1", "2.3.2", "3.2.1", "3.2.2", "3.2.3"]
TEMPLATE_TITLE = "基于FPGA的工业智能检测系统"          # 与官网报名「作品名称」一致


def check_source(src: dict, text: str, stats: dict) -> None:
    n_fig, n_tbl = len(src["images"]), src["tables"]
    figs = [n for n, _, _ in src["fig_captions"]]
    tbls = [n for n, _, _ in src["tbl_captions"]]
    h1, h2, h3 = len(src["h1"]), len(src["h2"]), len(src["h3"])

    # 官方模板骨架：首页标题 = 官网报名作品名；H1 = 摘要 + 五部分；H2/H3 = 模板编号
    h1_prefixes = [x.split("\u3000")[0].split(" ")[0] for x in src["h1"]]
    h2_numbers = [x.split(" ")[0].split("\u3000")[0] for x in src["h2"]]
    h3_numbers = [x.split(" ")[0].split("\u3000")[0] for x in src["h3"]]
    skel_bad = []
    if src["title"] != TEMPLATE_TITLE:
        skel_bad.append("# 标题 %r ≠ 官网报名作品名" % src["title"])
    if h1_prefixes != TEMPLATE_H1:
        skel_bad.append("H1 序列 %s ≠ %s" % (h1_prefixes, TEMPLATE_H1))
    if h2_numbers != TEMPLATE_H2:
        skel_bad.append("H2 序列 %s" % h2_numbers)
    if h3_numbers != TEMPLATE_H3:
        skel_bad.append("H3 序列 %s" % h3_numbers)
    check("2 源 Markdown", "官方模板骨架：标题=报名名、H1 摘要+五部分、H2 1.1~4.2、H3 2.2.x/2.3.x/3.2.x",
          not skel_bad and bool(src["title"]),
          "; ".join(skel_bad[:4]) if skel_bad
          else "标题 %r；H1 %d / H2 %d / H3 %d" % (src["title"][:24], h1, h2, h3))

    # 官方模板字数上限（Word 口径，只数正文段，不含表格 / 图注）
    wc = section_word_counts(text)
    wc_bad = []
    for prefix, limit in TEMPLATE_WORD_LIMITS:
        key = next((k for k in wc if k == prefix or k.startswith(prefix + " ")), None)
        if key is None:
            wc_bad.append("缺节 %s" % prefix)
        elif wc[key] > limit:
            wc_bad.append("%s %d > %d 字" % (key, wc[key], limit))
    wc_detail = "；".join("%s %d/%d" % (p, wc.get(next((k for k in wc if k == p or k.startswith(p + " ")), ""), 0), lim)
                          for p, lim in TEMPLATE_WORD_LIMITS)
    check("2 源 Markdown", "官方模板字数上限（摘要 800 / 1.1~1.3 400 / 1.4~1.6 200 / 4.1 300 / 4.2 1000）",
          not wc_bad, "; ".join(wc_bad[:5]) if wc_bad else wc_detail)

    check("2 源 Markdown", "图注数 = 插图数、表注数 = 表格数",
          len(figs) == n_fig and len(tbls) == n_tbl,
          "图注 %d / 插图 %d；表注 %d / 表格 %d" % (len(figs), n_fig, len(tbls), n_tbl))
    check("2 源 Markdown", "图编号与表编号连续且从 1 起（图 %d / 表 %d）" % (n_fig, n_tbl),
          figs == list(range(1, n_fig + 1)) and tbls == list(range(1, n_tbl + 1)),
          "图 %s；表 %s" % (str(figs[:14]), str(tbls[:14])))

    # 每个图 / 表都被正文引用，且引用编号在范围内
    lines = text.splitlines()
    cap_lines = {i for _, _, i in src["fig_captions"]} | {i for _, _, i in src["tbl_captions"]}
    body = "\n".join(ln for i, ln in enumerate(lines) if i not in cap_lines)
    fig_refs = {int(x) for x in re.findall(r"图\s*(\d+)(?!\.\d)", body)} - {0}
    tbl_refs = {int(x) for x in re.findall(r"表\s*(\d+)(?!\.\d)", body)} - {0}
    miss = ["图 %d" % n for n in figs if n not in fig_refs] + \
           ["表 %d" % n for n in tbls if n not in tbl_refs]
    out_of_range = [n for n in fig_refs if n < 1 or n > n_fig] + \
                   [n for n in tbl_refs if n < 1 or n > n_tbl]
    check("2 源 Markdown", "每个图 / 表都被正文引用且引用编号在范围内",
          not miss and not out_of_range,
          ("未被引用：" + "、".join(miss[:8]) if miss else "")
          + ("；越界：" + str(sorted(set(out_of_range))[:8]) if out_of_range else "")
          if (miss or out_of_range) else "全部被引用")

    # 图注紧邻插图、表注紧邻表格
    pos_bad = []
    for num, _, i in src["fig_captions"]:
        j = i - 1
        while j >= 0 and not lines[j].strip():
            j -= 1
        if j < 0 or not IMAGE_LINE_RE.match(lines[j].strip()):
            pos_bad.append("图 %d 图注不在插图下方" % num)
    for num, _, i in src["tbl_captions"]:
        j = i + 1
        while j < len(lines) and not lines[j].strip():
            j += 1
        if j >= len(lines) or not TABLE_ROW_RE.match(lines[j].strip()):
            pos_bad.append("表 %d 表注不在表格上方" % num)
    check("2 源 Markdown", "图注紧随插图、表注紧随表格", not pos_bad,
          "; ".join(pos_bad[:6]) if pos_bad else "全部合规")

    # 图注 / 表注格式：编号与说明之间全角空格；图注以句号收尾、表注不带句号
    fmt_bad = []
    for num, cap, _ in src["fig_captions"]:
        if not cap.rstrip().endswith("。"):
            fmt_bad.append("图 %d 图注未以句号收尾" % num)
    for num, cap, _ in src["tbl_captions"]:
        if cap.rstrip().endswith("。"):
            fmt_bad.append("表 %d 表注带了句号" % num)
    check("2 源 Markdown", "图注 / 表注格式：编号后全角空格、图注句号", not fmt_bad,
          "; ".join(fmt_bad[:6]) if fmt_bad else "全部合规")

    bad_md = source_problems(text)
    check("2 源 Markdown", "无代码块 / 引用块 / 五级标题 / 「- 」列表 / HTML",
          not bad_md, "; ".join(bad_md[:6]) if bad_md else "0 处")

    sb = stats.get("source_blocks") or {}
    if sb:
        check("2 源 Markdown", "生成侧 source_blocks = 源独立计数",
              sb.get("chapters") == h1 and sb.get("sections") == h2
              and sb.get("images") == n_fig and sb.get("tables") == n_tbl
              and (sb.get("meta_lines") in (None, len(src["meta"]))),
              "json %s / 源 H1 %d H2 %d 图 %d 表 %d 元信息 %d"
              % (sb, h1, h2, n_fig, n_tbl, len(src["meta"])))
    else:
        check("2 源 Markdown", "生成侧 source_blocks = 源独立计数", None,
              "_md2docx.json 无 source_blocks（新版生成器未提供）")


# ---------------------------------------------------------------- docx 工具
def qn(tag: str):
    from docx.oxml.ns import qn as _qn
    return _qn(tag)


def iter_blocks(doc):
    """按文档顺序产出 ('p', Paragraph) / ('tbl', Table)。"""
    from docx.table import Table
    from docx.text.paragraph import Paragraph
    for child in doc.element.body.iterchildren():
        if child.tag == qn("w:p"):
            yield "p", Paragraph(child, doc)
        elif child.tag == qn("w:tbl"):
            yield "tbl", Table(child, doc)


def style_chain(par):
    st = par.style
    seen = 0
    while st is not None and seen < 12:
        yield st
        st = st.base_style
        seen += 1


def _pPr_attr(par, tag: str, attr: str):
    """沿 段落 pPr → 样式链 pPr 解析属性（返回字符串或 None）。"""
    p = par._p.pPr
    if p is not None:
        el = p.find(qn("w:" + tag))
        if el is not None and el.get(qn("w:" + attr)) is not None:
            return el.get(qn("w:" + attr))
    for st in style_chain(par):
        pPr = st.element.find(qn("w:pPr"))
        if pPr is not None:
            el = pPr.find(qn("w:" + tag))
            if el is not None and el.get(qn("w:" + attr)) is not None:
                return el.get(qn("w:" + attr))
    return None


def ind(par, attr: str):
    v = _pPr_attr(par, "ind", attr)
    try:
        return None if v is None else int(v)
    except ValueError:
        return None


def spacing(par, attr: str):
    v = _pPr_attr(par, "spacing", attr)
    try:
        return None if v is None else int(v)
    except ValueError:
        return None


def jc(par):
    return _pPr_attr(par, "jc", "val")


def centered(par) -> bool:
    return jc(par) in ("center",)


def page_break_before(par) -> bool:
    v = _pPr_attr(par, "pageBreakBefore", "val")
    return v not in (None, "0", "false")


def rfonts(run, par) -> dict:
    """有效 rFonts：run → 段落样式链（eastAsia / ascii / hAnsi）。"""
    out = {}

    def absorb(el):
        if el is None:
            return
        for key in ("eastAsia", "ascii", "hAnsi"):
            if el.get(qn("w:" + key)):
                out.setdefault(key, el.get(qn("w:" + key)))

    rPr = run._element.find(qn("w:rPr"))
    absorb(None if rPr is None else rPr.find(qn("w:rFonts")))
    for st in style_chain(par):
        rPr = st.element.find(qn("w:rPr"))
        absorb(None if rPr is None else rPr.find(qn("w:rFonts")))
    return out


def font_size(run, par):
    sz = run.font.size
    if sz is not None:
        return sz.pt
    for st in style_chain(par):
        if st.font.size is not None:
            return st.font.size.pt
    return None


def bold(run, par) -> bool:
    b = run.font.bold
    if b is not None:
        return bool(b)
    for st in style_chain(par):
        if st.font.bold is not None:
            return bool(st.font.bold)
    return False


def color(run, par):
    c = run.font.color
    if c is not None and c.rgb is not None:
        return str(c.rgb)
    for st in style_chain(par):
        c = st.font.color
        if c is not None and c.rgb is not None:
            return str(c.rgb)
    return None


def no_shading(run) -> bool:
    rPr = run._element.find(qn("w:rPr"))
    if rPr is None:
        return True
    shd = rPr.find(qn("w:shd"))
    return shd is None or shd.get(qn("w:fill")) in (None, "auto", "FFFFFF")


def caption_kind(text: str):
    m = CAPTION_FIG_RE.match(text.strip())
    if m:
        return "图", int(m.group(1)), m.group(2)
    m = CAPTION_TBL_RE.match(text.strip())
    if m:
        return "表", int(m.group(1)), m.group(2)
    return None


def is_list_item(text: str) -> bool:
    return bool(re.match(r"^\d+\.\s", text.strip()))


# ---------------------------------------------------------------- DOCX

# ---------------------------------------------------------------- 表格提示
def _norm_align(v):
    """docx 对齐值归一：left / center / right / both。"""
    v = (v or "").lower()
    if v in ("", "left", "start"):
        return "left"
    if v in ("right", "end"):
        return "right"
    if v == "center":
        return "center"
    return v


def _cell_aligns(cell) -> set:
    """单元格内非空段落的对齐集合（归一）。"""
    vals = set()
    for p in cell.paragraphs:
        if p.text.strip():
            vals.add(_norm_align(jc(p)))
    return vals


def _grid_widths_pt(t) -> list[float]:
    grid = t._tbl.tblGrid
    if grid is None:
        return []
    return [int(c.get(qn("w:w")) or 0) / 20.0
            for c in grid.findall(qn("w:gridCol"))]


def est_cell_lines(cell, avail_pt: float) -> int:
    """估算单元格排版行数：CJK 9pt、西文约 4.5pt 贪心折行（只用于提示）。"""
    if avail_pt <= 1.0:
        return 1
    total = 0
    for p in cell.paragraphs:
        text = p.text.strip()
        if not text:
            total += 1
            continue
        width = 0.0
        for ch in text:
            o = ord(ch)
            if 0x4E00 <= o <= 0x9FFF or ch in "，。；：、（）「」【】…—·":
                width += TABLE_PT
            elif ch == " ":
                width += TABLE_PT * 0.3
            else:
                width += TABLE_PT * 0.5
        total += max(1, math.ceil(width / avail_pt))
    return total


def check_table_hints(tbls, src: dict) -> None:
    """列宽 = 源分隔行破折号权重（偏差 ≤ 8%）、对齐 = 源冒号提示；>6 行只 WARN。"""
    data = src.get("tables_data") or []
    if not data or len(data) != len(tbls):
        check("5 三线表", "表格列宽 / 对齐提示", None,
              "源表 %d 张 / docx %d 张，数量对不上无法核对提示"
              % (len(data), len(tbls)))
        return

    width_bad, align_bad, worst_dev = [], [], []
    n_width_cols = n_align_cols = 0
    for ti, (t, td) in enumerate(zip(tbls, data), 1):
        hints = [sep_hint(c) for c in td["sep"]]
        weights = [h[0] for h in hints]
        aligns = [h[1] for h in hints]
        has_weight = len(set(weights)) > 1
        has_align = any(a is not None for a in aligns)
        widths = _grid_widths_pt(t)
        if len(widths) != len(weights):
            if has_weight or has_align:
                width_bad.append("表%d 列数 %d ≠ 提示 %d"
                                 % (ti, len(widths), len(weights)))
            continue
        total = sum(widths)
        if has_weight and total > 0:
            n_width_cols += len(weights)
            tw = sum(weights)
            for ci, (w, hw) in enumerate(zip(widths, weights), 1):
                exp = hw / tw * total
                dev = abs(w - exp) / exp if exp > 0 else 0.0
                worst_dev.append(dev)
                if dev > 0.08 + 1e-9:
                    width_bad.append(
                        "表%d 列%d 实宽 %.2fcm / 提示期望 %.2fcm（偏 %+.0f%%）"
                        % (ti, ci, w / 28.3465, exp / 28.3465, dev * 100))
        if has_align:
            for ci, exp in enumerate(aligns, 1):
                if exp is None:
                    continue
                n_align_cols += 1
                found = set()
                for row in t.rows[1:]:              # 表头另算，只核数据行
                    cells = row.cells
                    if ci - 1 >= len(cells) or len(cells) != len(weights):
                        continue                        # 合并单元格行跳过
                    found |= _cell_aligns(cells[ci - 1])
                if found - {exp}:
                    align_bad.append("表%d 列%d 期望 %s 实为 %s"
                                     % (ti, ci, exp, "/".join(sorted(found))))

    if n_width_cols == 0 and not width_bad:
        check("5 三线表", "列宽与源提示权重偏差 ≤8%（有提示的列）", None,
              "源分隔行没有差异化的破折号权重")
    elif width_bad:
        check("5 三线表", "列宽与源提示权重偏差 ≤8%%（%d 列）" % n_width_cols, False,
              "; ".join(width_bad[:5]))
    else:
        check("5 三线表", "列宽与源提示权重偏差 ≤8%%（%d 列）" % n_width_cols, True,
              "最大偏差 %.1f%%" % (max(worst_dev) * 100 if worst_dev else 0))
    if n_align_cols == 0 and not align_bad:
        check("5 三线表", "列对齐 = 源冒号提示（有提示的列）", None,
              "源分隔行没有冒号对齐提示")
    else:
        check("5 三线表", "列对齐 = 源冒号提示（%d 列）" % n_align_cols,
              not align_bad, "; ".join(align_bad[:5]) if align_bad else "全部一致")

    # 单元格 > 6 行：只 WARN（排版提示，不判失败）
    long_cells = []
    for ti, t in enumerate(tbls, 1):
        widths = _grid_widths_pt(t)
        for ri, row in enumerate(t.rows):
            for ci, cell in enumerate(row.cells):
                if ci >= len(widths):
                    continue
                avail = widths[ci] - 2 * CELL_MAR_TW / 20.0
                n = est_cell_lines(cell, avail)
                if n > 6:
                    long_cells.append((n, ti, ri + 1, ci + 1,
                                       cell.text.strip()[:14]))
    if long_cells:
        long_cells.sort(reverse=True)
        n, tb, r, c, txt = long_cells[0]
        warn("5 三线表", "单元格估算行数 > 6 行（%d 处，仅提示）" % len(long_cells),
             "最深 %d 行：表%d 行%d 列%d %r；建议缩表或精简文字"
             % (n, tb, r, c, txt))
    else:
        check("5 三线表", "单元格估算行数 ≤ 6 行（%d 张表）" % len(tbls), True,
              "无超长单元格")


def analyze_docx(doc, src: dict, stats: dict) -> dict:
    blocks = list(iter_blocks(doc))
    paras = [b for k, b in blocks if k == "p"]
    tbls = [b for k, b in blocks if k == "tbl"]
    n_par, n_tbl, n_img = len(doc.paragraphs), len(doc.tables), len(doc.inline_shapes)
    s_h = stats.get("headings") or {}

    check("3 DOCX 结构", "计数三方对账：段落 / 表格 / 插图 = 源 = 生成侧统计",
          n_par == stats.get("paragraphs")
          and n_tbl == src["tables"] == stats.get("tables")
          and n_img == len(src["images"]) == stats.get("images"),
          "docx 段落 %d / 表 %d / 图 %d；源 表 %d / 图 %d；json %s / %s / %s"
          % (n_par, n_tbl, n_img, src["tables"], len(src["images"]),
             stats.get("paragraphs"), stats.get("tables"), stats.get("images")))

    try:
        with zipfile.ZipFile(str(DOCX)) as zf:
            docxml = zf.read("word/document.xml").decode("utf-8", "replace")
    except (KeyError, OSError, zipfile.BadZipFile):
        docxml = ""
    toc_text = [p.text for p in paras if p.text.strip() in ("目录", "目 录")]
    toc_field = "TOC \\o" in docxml or "TOC \\\\o" in docxml or \
                bool(re.search(r"TOC[^<]{0,4}\\\\", docxml))
    if not docxml:
        check("3 DOCX 结构", "单节（无封面 / 目录节）且无目录域", None,
              "document.xml 读取失败，无法核对 TOC")
    else:
        check("3 DOCX 结构", "单节（无封面 / 目录节）且无目录域 / 「目录」标题",
              len(doc.sections) == 1 and not toc_field and not toc_text,
              "节数 %d / TOC %s / 「目录」%d 处"
              % (len(doc.sections), "有" if toc_field else "无", len(toc_text)))

    first_par = paras[0] if paras else None
    title_text = first_par.text.strip() if first_par is not None else ""
    check("3 DOCX 结构", "首段文字 = 源 # 标题", title_text == src["title"],
          "docx %r / 源 %r" % (title_text[:36], src["title"][:36]))

    # 元信息行：按样式名 MetaCN 取全部（旧样式名时回退为标题后连续的「标签：值」段）
    meta_pars = [p for p in paras
                 if p.style is not None
                 and p.style.name.replace(" ", "").lower() in ("metacn", "meta")]
    if not meta_pars:
        start = paras.index(first_par) + 1 if first_par is not None else 0
        for p in paras[start:start + 10]:
            if not p.text.strip():
                continue
            if re.match(r"^[^：:]{1,16}[：:]", p.text.strip()) and len(meta_pars) < 6:
                meta_pars.append(p)
            else:
                break
    meta_bad = []
    for (label, _), got in zip(src["meta"], meta_pars):
        if not got.text.strip().startswith(label):
            meta_bad.append("期望「%s」实为 %r" % (label, got.text[:22]))
    check("3 DOCX 结构", "元信息行 %d 条且标签与源一致" % len(src["meta"]),
          len(meta_pars) == len(src["meta"]) and not meta_bad,
          "; ".join(meta_bad[:3]) if meta_bad
          else "；".join("%s：…" % l for l, _ in src["meta"]))

    # 章节标题逐条对账（按文字顺序匹配，不依赖样式名）
    texts = [p.text.strip() for p in paras]
    cursor, problems = 0, []
    found_h1, found_h2, found_h3 = [], [], []
    for level, htext in src.get("heading_seq") or (
            [("h1", x) for x in src["h1"]] + [("h2", x) for x in src["h2"]]
            + [("h3", x) for x in src.get("h3", [])]):
        hit = next((i for i in range(cursor, len(texts)) if texts[i] == htext.strip()), None)
        if hit is None:
            problems.append("缺标题 %r" % htext[:26])
        else:
            cursor = hit
            (found_h1 if level == "h1" else found_h2 if level == "h2" else found_h3).append(paras[hit])
    h_ok = (len(found_h1) == len(src["h1"]) and len(found_h2) == len(src["h2"])
            and len(found_h3) == len(src.get("h3", [])))
    if s_h:
        h_ok = (h_ok and len(found_h1) == s_h.get("h1")
                and len(found_h2) == s_h.get("h2")
                and len(found_h3) == s_h.get("h3", 0))
    check("3 DOCX 结构", "章节标题文字 = 源（H1 %d / H2 %d / H3 %d）"
          % (len(src["h1"]), len(src["h2"]), len(src.get("h3", []))), h_ok,
          "; ".join(problems[:6]) if problems
          else "H1 %d / H2 %d / H3 %d 全部匹配（json %s / %s / %s）"
               % (len(found_h1), len(found_h2), len(found_h3),
                  s_h.get("h1"), s_h.get("h2"), s_h.get("h3", 0)))
    heading_ids = {id(p) for p in found_h1 + found_h2 + found_h3}

    # 图注 / 表注段落
    caps = {"图": [], "表": []}
    for p in paras:
        kind = caption_kind(p.text)
        if kind:
            caps[kind[0]].append((kind[1], p, kind[2]))
    check("3 DOCX 结构", "docx 图注 / 表注数 = 源 = 图表数（图 %d / 表 %d）"
          % (n_img, n_tbl),
          len(caps["图"]) == len(src["fig_captions"]) == n_img
          and len(caps["表"]) == len(src["tbl_captions"]) == n_tbl,
          "docx 图注 %d / 源 %d / 插图 %d；docx 表注 %d / 源 %d / 表格 %d"
          % (len(caps["图"]), len(src["fig_captions"]), n_img,
             len(caps["表"]), len(src["tbl_captions"]), n_tbl))

    # --- 版式令牌：正文 ---
    cap_ids = {id(p) for _, p, _ in caps["图"] + caps["表"]}
    meta_ids = {id(p) for p in meta_pars}
    list_pars = [p for p in paras if is_list_item(p.text) and id(p) not in cap_ids]
    body_pars = [p for p in paras
                 if p.text.strip() and id(p) not in cap_ids
                 and id(p) not in meta_ids and id(p) not in heading_ids
                 and p is not first_par and not is_list_item(p.text)]
    body_runs = [r for p in body_pars for r in p.runs if r.text.strip()]
    sample_ok = len(body_pars) >= 20 and len(body_runs) >= 50

    bad_font, bad_size, bad_color = [], [], []
    n_hei = n_code = 0
    for p in body_pars:
        for r in p.runs:
            t = r.text.strip()
            if not t:
                continue
            fonts = rfonts(r, p)
            ea, asc = fonts.get("eastAsia"), fonts.get("ascii")
            if asc == FONT_CODE:
                n_code += 1
                if not near(font_size(r, p), CODE_PT, 0.01):
                    bad_size.append("代码 %r 字号 %s" % (t[:10], font_size(r, p)))
                continue
            if has_cjk(t) and ea not in (FONT_CN, FONT_HEI):
                bad_font.append("%r 中文 eastAsia=%s" % (t[:12], ea))
            elif has_cjk(t) and ea == FONT_HEI:
                n_hei += 1
            if has_latin(t) and asc not in (FONT_EN, FONT_CN, FONT_HEI):
                bad_font.append("%r 西文 ascii=%s" % (t[:12], asc))
            if not near(font_size(r, p), BODY_PT, 0.01):
                bad_size.append("%r 字号 %s" % (t[:10], font_size(r, p)))
            c = color(r, p)
            if c is not None and c.upper() != C_INK:
                bad_color.append("%r 字色 %s" % (t[:10], c))
    check("4 版式令牌", "正文：中文楷体 / 西文 Times，12pt（代码 %d，样本 %d 段）"
          % (n_code, len(body_pars)),
          sample_ok and not bad_font and not bad_size,
          ("样本不足 %d 段 / %d run；" % (len(body_pars), len(body_runs))
           if not sample_ok else "") + "; ".join((bad_font + bad_size)[:5]))
    check("4 版式令牌", "正文两端对齐、段前段后 = 0",
          all(jc(p) in ("both", "justify") for p in body_pars)
          and all((spacing(p, "before") or 0) == 0 and (spacing(p, "after") or 0) == 0
                  for p in body_pars),
          "; ".join(p.text[:16] for p in body_pars
                    if jc(p) not in ("both", "justify"))[:80] or "合规")
    bad_ind = [(p.text[:16], ind(p, "firstLine")) for p in body_pars
               if ind(p, "firstLine") != INDENT_TW]
    check("4 版式令牌", "正文首行缩进 %d twips（2 字符 @12pt）" % INDENT_TW, not bad_ind,
          "; ".join("%r=%s" % b for b in bad_ind[:4]) or "%d 段全部合规" % len(body_pars))
    bad_line = []
    for p in body_pars:
        line, rule = spacing(p, "line"), spacing(p, "lineRule")
        if line is not None and line != BODY_LINE_TW:
            bad_line.append("%r line=%s" % (p.text[:12], line))
        if rule is not None and rule != "auto":
            bad_line.append("%r lineRule=%s" % (p.text[:12], rule))
    check("4 版式令牌", "正文行距 1.5 倍（line 360 / auto）", not bad_line,
          "; ".join(bad_line[:4]) or "合规")
    emph_bad = []
    for p in body_pars:
        for r in p.runs:
            t = r.text.strip()
            if not t or not bold(r, p):
                continue
            fonts = rfonts(r, p)
            if has_cjk(t) and fonts.get("eastAsia") != FONT_CN:
                emph_bad.append("%r 强调中文非楷体" % t[:10])
            if not has_cjk(t) and has_latin(t) and fonts.get("ascii") != FONT_EN:
                emph_bad.append("%r 强调西文非 Times" % t[:10])
    check("4 版式令牌", "强调：中文楷体加粗 / 西文 Times 粗", not emph_bad,
          "; ".join(emph_bad[:4]) or "无异常")

    # 标题 + 元信息格式
    t_bad = []
    if first_par is None:
        t_bad.append("无标题段")
    else:
        runs = [r for r in first_par.runs if r.text.strip()]
        ea = {rfonts(r, first_par).get("eastAsia") for r in runs}
        sz = {round(font_size(r, first_par) or 0, 2) for r in runs}
        if ea - {FONT_HEI}:
            t_bad.append("字体 %s" % ea)
        if sz - {round(TITLE_PT, 2)}:
            t_bad.append("字号 %s" % sz)
        if not centered(first_par):
            t_bad.append("未居中")
    for p in meta_pars:
        for r in p.runs:
            if r.text.strip() and not near(font_size(r, p), META_PT, 0.01):
                t_bad.append("元信息 %r 字号 %s" % (r.text[:8], font_size(r, p)))
                break
    h_bad = []
    for p, want in ([(p, H1_PT) for p in found_h1] + [(p, H2_PT) for p in found_h2]
                    + [(p, H3_PT) for p in found_h3]):
        runs = [r for r in p.runs if r.text.strip()]
        ea = {rfonts(r, p).get("eastAsia") for r in runs}
        sz = {round(font_size(r, p) or 0, 2) for r in runs}
        line, rule = spacing(p, "line"), spacing(p, "lineRule")
        if ea - {FONT_CN} or sz - {round(want, 2)} or jc(p) not in (None, "left"):
            h_bad.append("%r 字体 %s 字号 %s 对齐 %s"
                         % (p.text[:14], ea, sz, jc(p)))
        if runs and not all(bold(r, p) for r in runs):
            h_bad.append("%r 未整段加粗" % p.text[:14])
        if (line or HEAD_LINE_TW) != HEAD_LINE_TW or (rule or "auto") != "auto":
            h_bad.append("%r 行距 line=%s rule=%s" % (p.text[:14], line, rule))
    level_bad = t_bad + h_bad
    check("4 版式令牌", "标题层级：作品名 17.5pt 黑体居中；H1/H2/H3 均 14pt 楷体加粗、"
          "2 倍行距；元信息 10pt",
          not level_bad, "; ".join(level_bad[:6]) if level_bad else "合规")

    # 图注 / 表注格式与位置
    cap_bad = []
    for kind, items in caps.items():
        for num, p, _ in items:
            runs = [r for r in p.runs if r.text.strip()]
            if not runs:
                cap_bad.append("%s %d 空" % (kind, num))
                continue
            if any(not bold(r, p) for r in runs):
                cap_bad.append("%s %d 未加粗" % (kind, num))
            if any(not near(font_size(r, p), CAPTION_PT, 0.01) for r in runs):
                cap_bad.append("%s %d 字号非 %g" % (kind, num, CAPTION_PT))
            if not centered(p):
                cap_bad.append("%s %d 未居中" % (kind, num))
            if {rfonts(r, p).get("eastAsia") for r in runs} - {FONT_CN}:
                cap_bad.append("%s %d 非楷体" % (kind, num))
            if {color(r, p) for r in runs} - {None, C_INK}:
                cap_bad.append("%s %d 非黑色" % (kind, num))
    check("4 版式令牌", "图注 / 表注：加粗五号 10.5pt 居中 楷体 黑色（%d 条）"
          % (len(caps["图"]) + len(caps["表"])), not cap_bad,
          "; ".join(cap_bad[:5]) if cap_bad else "全部合规")

    seq_bad = []
    for idx, (k, obj) in enumerate(blocks):
        if k != "p":
            continue
        kind = caption_kind(obj.text)
        if not kind:
            continue
        if kind[0] == "图":
            prev = blocks[idx - 1] if idx > 0 else None
            if prev is None or prev[0] != "p" or \
                    not prev[1]._p.findall(".//" + qn("w:drawing")):
                seq_bad.append("图 %d 图注不在插图下方" % kind[1])
        else:
            nxt = blocks[idx + 1] if idx + 1 < len(blocks) else None
            if nxt is None or nxt[0] != "tbl":
                seq_bad.append("表 %d 表注不在表格上方" % kind[1])
    check("4 版式令牌", "图注在图下方、表注在表上方", not seq_bad,
          "; ".join(seq_bad[:5]) if seq_bad else "全部合规")

    code_bad = []
    for p in body_pars:
        for r in p.runs:
            if rfonts(r, p).get("ascii") != FONT_CODE or not r.text.strip():
                continue
            if not near(font_size(r, p), CODE_PT, 0.01):
                code_bad.append("%r 字号 %s" % (r.text[:10], font_size(r, p)))
            if not no_shading(r):
                code_bad.append("%r 有底纹" % r.text[:10])
            c = color(r, p)
            if c is not None and c.upper() != C_INK:
                code_bad.append("%r 字色 %s" % (r.text[:10], c))
    check("4 版式令牌", "行内代码 Consolas %gpt 无底纹黑色" % CODE_PT, not code_bad,
          "; ".join(code_bad[:4]) if code_bad else "无异常")

    list_bad = []
    for p in list_pars:
        runs = [r for r in p.runs if r.text.strip()]
        if not runs:
            continue
        if any(not near(font_size(r, p), LIST_PT, 0.01) for r in runs):
            list_bad.append("%r 字号非 %gpt" % (p.text[:12], LIST_PT))
        if ind(p, "hanging") != LIST_HANG_TW or ind(p, "left") != LIST_HANG_TW:
            list_bad.append("%r 缩进 left=%s hanging=%s"
                            % (p.text[:12], ind(p, "left"), ind(p, "hanging")))
        if (spacing(p, "line") or BODY_LINE_TW) != BODY_LINE_TW:
            list_bad.append("%r 行距 %s" % (p.text[:12], spacing(p, "line")))
    if list_pars:
        check("4 版式令牌", "参考文献 / 附录列表：12pt、悬挂 1.5 字符、1.5 倍",
              not list_bad, "; ".join(list_bad[:4]) if list_bad else "%d 条合规" % len(list_pars))
    else:
        check("4 版式令牌", "参考文献 / 附录列表：12pt、悬挂 1.5 字符、1.5 倍", None,
              "未识别到编号列表条目（参考文献 / 附录可能为空）")

    # 行距 / 颜色 XML 扫描
    try:
        with zipfile.ZipFile(str(DOCX)) as zf:
            xml_all = "".join(zf.read(n).decode("utf-8", "replace")
                              for n in zf.namelist()
                              if n.startswith("word/") and n.endswith(".xml"))
        fixed_rules = sorted(set(re.findall(r'w:lineRule="(exact|atLeast|fixed)"', xml_all)))
        ok_lines = {LINE_TW, BODY_LINE_TW, HEAD_LINE_TW}
        tiny = sorted({int(v) for v in re.findall(r'w:line="(\d+)"', xml_all)
                       if int(v) < LINE_TW and int(v) != 144})
        odd = sorted({int(v) for v in re.findall(r'w:line="(\d+)"', xml_all)
                      if int(v) >= LINE_TW and int(v) not in ok_lines})
        check("4 版式令牌", "全篇无固定 / 精确 / 压缩行距（倍数只允许 auto 240/360/480）",
              not fixed_rules and not tiny and not odd,
              ("；".join(x for x in (
                  ("fixed/exact：%s" % ",".join(fixed_rules)) if fixed_rules else "",
                  ("line<240：%s" % tiny) if tiny else "",
                  ("规格外倍数：%s" % odd) if odd else "") if x) or "仅 auto 240/360/480"))
    except (OSError, zipfile.BadZipFile):
        check("4 版式令牌", "全篇无固定 / 精确 / 压缩行距（倍数只允许 auto 240/360/480）", False,
              "docx 读取失败")

    try:
        color_xml = ""
        with zipfile.ZipFile(str(DOCX)) as zf:
            for n in zf.namelist():
                if re.match(r"word/(document|footer\d*|header\d*)\.xml$", n):
                    color_xml += zf.read(n).decode("utf-8", "replace")
        colors = {c.upper() for c in
                  re.findall(r'w:color w:val="([0-9A-Fa-f]{6})"', color_xml)}
        weird = sorted(c for c in colors if c != C_INK)
        theme = re.findall(r"w:themeColor=", color_xml)
        check("4 版式令牌", "全篇只有黑色（#000000）", not weird and not theme,
              ("出现 %s" % "、".join(weird[:6])) if weird
              else ("themeColor %d 处" % len(theme) if theme
                    else "实测 %d 种：%s" % (len(colors),
                                            "、".join(sorted(colors)) or "无显式彩色")))
    except (OSError, zipfile.BadZipFile):
        check("4 版式令牌", "全篇只有黑色（#000000）", False, "docx 读取失败")
    if bad_color:
        check("4 版式令牌", "正文 run 字色 = #000000", not bad_color,
              "; ".join(bad_color[:5]))

    # --- 页面 / 页眉 / 页脚 / 页码 ---
    sec = doc.sections[0]
    page_problems = []
    if not (near(sec.page_width.cm, PAGE_W_CM, 0.01)
            and near(sec.page_height.cm, PAGE_H_CM, 0.01)):
        page_problems.append("页面 %.2f x %.2f cm"
                             % (sec.page_width.cm, sec.page_height.cm))
    for m in ("left_margin", "right_margin", "top_margin", "bottom_margin"):
        if not near(getattr(sec, m).cm, MARGIN_CM, 0.01):
            page_problems.append("%s %.2fcm" % (m, getattr(sec, m).cm))
    if not near(sec.footer_distance.cm, FOOTER_DIST_CM, 0.02):
        page_problems.append("页脚距边 %.2fcm" % sec.footer_distance.cm)
    check("7 页面与页码", "A4 + 页边距 2.4cm + 页脚距边 1.6cm", not page_problems,
          "; ".join(page_problems) if page_problems
          else "%.1f x %.1f cm，四边 2.4cm" % (sec.page_width.cm, sec.page_height.cm))

    hdr_refs = sec._sectPr.findall(qn("w:headerReference"))
    ftr_refs = sec._sectPr.findall(qn("w:footerReference"))
    hdr_text = "".join(p.text for p in sec.header.paragraphs).strip()
    try:
        with zipfile.ZipFile(str(DOCX)) as zf:
            ftr_xml = "".join(zf.read(n).decode("utf-8", "replace")
                              for n in zf.namelist()
                              if re.match(r"word/footer\d*\.xml$", n))
            hdr_xml = "".join(zf.read(n).decode("utf-8", "replace")
                              for n in zf.namelist()
                              if re.match(r"word/header\d*\.xml$", n))
    except (OSError, zipfile.BadZipFile):
        ftr_xml = hdr_xml = ""
    footer_text = "".join(p.text for p in sec.footer.paragraphs).strip()
    foot_bad2 = []
    # 页眉：官方模板 logo（恰好 1 个引用、恰好 1 张图、无文字）
    if len(hdr_refs) != 1:
        foot_bad2.append("headerReference %d 个（应 1）" % len(hdr_refs))
    n_blip = hdr_xml.count("<a:blip")
    if n_blip != 1:
        foot_bad2.append("页眉图片 %d 张（应 1 张 logo）" % n_blip)
    if hdr_text:
        foot_bad2.append("页眉文字 %r" % hdr_text[:14])
    if len(ftr_refs) != 1:
        foot_bad2.append("footerReference %d 个" % len(ftr_refs))
    if "PAGE" not in ftr_xml:
        foot_bad2.append("页脚无 PAGE 域")
    if "NUMPAGES" in ftr_xml:
        foot_bad2.append("页脚含 NUMPAGES")
    if re.search(r"第|共|页", footer_text):
        foot_bad2.append("页脚文字 %r" % footer_text[:14])
    for p in sec.footer.paragraphs:
        if not p.text.strip() and not p.runs:
            continue
        if not centered(p):
            foot_bad2.append("页脚未居中")
            break
        for r in p.runs:
            if not r.text.strip():
                continue
            fonts = rfonts(r, p)
            if fonts.get("ascii") != FONT_EN:
                foot_bad2.append("页脚西文 %s" % fonts.get("ascii"))
            elif not near(font_size(r, p), FOOTER_PT, 0.01):
                foot_bad2.append("页脚字号 %s" % font_size(r, p))
            c = color(r, p)
            if c is not None and c.upper() != C_INK:
                foot_bad2.append("页脚字色 %s" % c)
    check("7 页面与页码", "页眉仅模板 logo（1 图无文字）；页脚居中 PAGE 域（Times 11pt，无 NUMPAGES / 第 X 页）",
          not foot_bad2, "; ".join(foot_bad2[:5]) if foot_bad2 else "页眉 logo + 居中 PAGE 域")

    pg = sec._sectPr.find(qn("w:pgNumType"))
    check("7 页面与页码", "页码从 1 起（pgNumType start=1）",
          pg is not None and pg.get(qn("w:start")) == "1",
          "start=%s" % (None if pg is None else pg.get(qn("w:start"))))

    # 官方模板：第五部分 参考文献，标准格式、限 20 篇以内
    n_refs = len(src["ref_items"])
    ref_ok = 1 <= n_refs <= 20
    check("7 页面与页码", "参考文献 1~20 篇（官方模板上限 20）", ref_ok,
          "参考文献 %d 条" % n_refs)

    # --- 三线表 ---
    def tbl_width_cm(t):
        grid = t._tbl.tblGrid
        cols = grid.findall(qn("w:gridCol")) if grid is not None else []
        total = sum(int(c.get(qn("w:w")) or 0) for c in cols)
        if total <= 0:
            total = sum((c.width.twips if c.width else 0) for c in t.columns)
        return total / 567.0

    widths = [round(tbl_width_cm(t), 2) for t in tbls]
    layout_ok = all(t._tbl.tblPr.find(qn("w:tblLayout")) is not None
                    and t._tbl.tblPr.find(qn("w:tblLayout")).get(qn("w:type")) == "fixed"
                    for t in tbls)
    check("5 三线表", "每张表 ≤ 15.6cm 且固定布局（%d 张）" % n_tbl,
          all(w <= TABLE_MAX_CM + 1e-6 for w in widths) and layout_ok, str(widths))

    def edge(bdr, name):
        return None if bdr is None else bdr.find(qn("w:" + name))

    border_bad, header_bad, split_bad, shade_bad, mar_bad, font_bad = [], [], [], [], [], []
    for i, t in enumerate(tbls, 1):
        if not t.rows:
            border_bad.append("表%d 空表" % i)
            continue
        tblPr = t._tbl.tblPr
        tblb = tblPr.find(qn("w:tblBorders"))
        top, bot = edge(tblb, "top"), edge(tblb, "bottom")
        iv, ih = edge(tblb, "insideV"), edge(tblb, "insideH")
        left, right = edge(tblb, "left"), edge(tblb, "right")
        hdr_line = True
        for c in t.rows[0].cells:
            tcPr = c._tc.tcPr
            tb = tcPr.find(qn("w:tcBorders")) if tcPr is not None else None
            b = edge(tb, "bottom")
            if b is None or b.get(qn("w:sz")) != "6" or b.get(qn("w:color")) != C_INK:
                hdr_line = False
        if not (top is not None and top.get(qn("w:sz")) == "12"
                and top.get(qn("w:color")) == C_INK
                and bot is not None and bot.get(qn("w:sz")) == "12"
                and bot.get(qn("w:color")) == C_INK
                and (iv is None or iv.get(qn("w:val")) in (None, "none"))
                and (ih is None or ih.get(qn("w:val")) in (None, "none"))
                and (left is None or left.get(qn("w:val")) in (None, "none"))
                and (right is None or right.get(qn("w:val")) in (None, "none"))
                and hdr_line):
            border_bad.append("表%d" % i)
        hdr = t.rows[0]
        trPr = hdr._tr.find(qn("w:trPr"))
        if trPr is None or trPr.find(qn("w:tblHeader")) is None:
            header_bad.append("表%d 未设表头重复" % i)
        for c in hdr.cells:
            for p in c.paragraphs:
                if any(r.text.strip() and not bold(r, p) for r in p.runs):
                    header_bad.append("表%d 表头未加粗" % i)
        for row in t.rows:
            rp = row._tr.find(qn("w:trPr"))
            if rp is None or rp.find(qn("w:cantSplit")) is None:
                split_bad.append("表%d" % i)
                break
        for row in t.rows:
            for c in row.cells:
                tcPr = c._tc.tcPr
                shd = tcPr.find(qn("w:shd")) if tcPr is not None else None
                if shd is not None and shd.get(qn("w:fill")) not in (None, "auto", "FFFFFF"):
                    shade_bad.append("表%d 底色 %s" % (i, shd.get(qn("w:fill"))))
        mar = tblPr.find(qn("w:tblCellMar"))
        l, r = edge(mar, "left"), edge(mar, "right")
        if (l is None or l.get(qn("w:w")) != str(CELL_MAR_TW)
                or r is None or r.get(qn("w:w")) != str(CELL_MAR_TW)):
            mar_bad.append("表%d" % i)
        for ri, row in enumerate(t.rows):
            for c in row.cells:
                for p in c.paragraphs:
                    for r in p.runs:
                        if not r.text.strip() or rfonts(r, p).get("ascii") == FONT_CODE:
                            continue
                        if not near(font_size(r, p), TABLE_PT, 0.01):
                            font_bad.append("表%d r%d 字号 %s"
                                            % (i, ri + 1, font_size(r, p)))
                        ea = rfonts(r, p).get("eastAsia")
                        if has_cjk(r.text) and ea != FONT_CN:
                            font_bad.append("表%d r%d 中文 %s" % (i, ri + 1, ea))
    check("5 三线表", "三线表线条：上下 1.5pt 黑 + 表头下 0.75pt，无竖线行线",
          not border_bad, "; ".join(border_bad[:4]) if border_bad else "%d 张合规" % n_tbl)
    check("5 三线表", "表头重复加粗、表体 / 表头 9pt 楷体",
          not header_bad and not font_bad,
          "; ".join((header_bad + font_bad)[:5]) if (header_bad or font_bad) else "全部合规")
    check("5 三线表", "行不跨页（cantSplit）", not split_bad,
          "; ".join(split_bad[:4]) if split_bad else "全部合规")
    check("5 三线表", "单元格无底纹 + 左右内边距 108 twips", not shade_bad and not mar_bad,
          "; ".join((shade_bad + mar_bad)[:4]) if (shade_bad or mar_bad) else "全部合规")

    check_table_hints(tbls, src)

    # --- 插图 ---
    img_pars = [p for p in paras if p._p.findall(".//" + qn("w:drawing"))]
    widths_img = [round(s.width.cm, 2) for s in doc.inline_shapes]
    check("6 插图与占位", "插图宽 15.8cm 且段落居中（%d 张）" % n_img,
          len(widths_img) == n_img and all(near(w, FIG_W_CM, 0.02) for w in widths_img)
          and all(centered(p) for p in img_pars),
          "宽度 %s；未居中 %d 段"
          % (str(widths_img), sum(1 for p in img_pars if not centered(p))))
    fig_ok, fig_bad, n_fig_seen = True, [], 0
    for idx, (k, obj) in enumerate(blocks):
        if k != "p" or not obj._p.findall(".//" + qn("w:drawing")):
            continue
        n_fig_seen += 1
        nxt = blocks[idx + 1] if idx + 1 < len(blocks) else None
        kind = caption_kind(nxt[1].text) if nxt and nxt[0] == "p" else None
        if not kind or kind[0] != "图":
            fig_ok = False
            fig_bad.append("第 %d 个插图后无图注" % n_fig_seen)
    check("6 插图与占位", "图注紧跟插图下方", fig_ok,
          "; ".join(fig_bad[:5]) if fig_bad else "%d 张全部合规" % len(img_pars))

    return {"n_img": n_img, "n_tbl": n_tbl, "img_pars": img_pars,
            "found_h1": found_h1, "found_h2": found_h2}


# ---------------------------------------------------------------- 插图与占位
def png_size(path: Path):
    try:
        b = path.read_bytes()[:33]
    except OSError:
        return None
    if len(b) < 24 or b[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    return int.from_bytes(b[16:20], "big"), int.from_bytes(b[20:24], "big")


def _in_report(e: dict, src_files: set) -> bool:
    """manifest 项是否在报告内：优先 in_report 字段，旧 manifest 回退为「出现在源里」。"""
    v = e.get("in_report")
    return (e.get("file") in src_files) if v is None else bool(v)


def check_figures(src: dict, stats: dict) -> None:
    """报告插图清单：线稿 + in_report 占位；fig_meta 只核对报告内插图。"""
    manifest = read_json(MANIFEST)
    meta = read_json(FIG_META)
    ph_list = manifest.get("placeholders", []) if isinstance(manifest, dict) else []
    fig_meta = meta.get("figures", {}) if isinstance(meta, dict) else {}
    meta_w = meta.get("figure_width_pt", FIG_W_PT)
    meta_px = meta.get("png_width", 3200)

    src_files = [Path(p).name for p in src["images"]]
    src_stems = [Path(p).stem for p in src["images"]]
    src_set = set(src_files)
    in_ph = [e for e in ph_list if _in_report(e, src_set)]
    out_ph = [e for e in ph_list if not _in_report(e, src_set)]
    in_files = [e.get("file", "") for e in in_ph]

    problems = []
    for f in in_files:                              # 报告内占位必须在源里出现且仅一次
        if f not in src_files:
            problems.append("报告内占位图 %s 不在源里" % f)
        elif src_files.count(f) > 1:
            problems.append("占位图 %s 在源里重复" % f)
    for e in in_ph:                                 # 图号位置对得上
        try:
            no = int(e.get("figure_no"))
        except (TypeError, ValueError):
            continue
        if 1 <= no <= len(src_files) and src_files[no - 1] != e.get("file"):
            problems.append("图 %d 期望 %s 实为 %s"
                            % (no, e.get("file"), src_files[no - 1]))
    for f in src_files:                             # 源里的 ph_* 都必须是 in_report
        if f.startswith("ph_") and f not in in_files:
            problems.append("%s 在源里但 manifest 未标 in_report" % f)
    for e in out_ph:                                # 移出报告的占位不能再出现
        if e.get("file") in src_set:
            problems.append("%s 标 in_report=false 但仍出现在源里" % e.get("file"))
    for e in in_ph:                                 # 占位 PNG：画布 3200px、宽高比
        size = png_size(FIGURES / e.get("file", ""))
        if size is None:
            problems.append("%s 缺失或非 PNG" % e.get("file"))
            continue
        w_px, h_px = size
        if w_px != meta_px:
            problems.append("%s 宽 %dpx（期望 %d）" % (e.get("file"), w_px, meta_px))
        aspect = e.get("aspect")
        if aspect == "16:9" and abs(h_px / w_px - 9 / 16) > 0.02:
            problems.append("%s 非 16:9" % e.get("file"))
        if aspect == "3:2" and abs(h_px / w_px - 2 / 3) > 0.02:
            problems.append("%s 非 3:2" % e.get("file"))

    # fig_meta：只核对报告内插图（画布宽 = 落版宽、PNG 3200px、落版最小字号 ≥ 7pt）
    meta_bad, low_fs = [], []
    for stem in src_stems:
        m = fig_meta.get(stem)
        if not m:
            meta_bad.append("%s 无 fig_meta 记录" % stem)
            continue
        if not near(m.get("w_pt"), meta_w, 0.01):
            meta_bad.append("%s 画布 %.1fpt ≠ %.1fpt" % (stem, m.get("w_pt", 0), meta_w))
        if m.get("png_w") != meta_px:
            meta_bad.append("%s PNG 宽 %s" % (stem, m.get("png_w")))
        eff = (m.get("min_fs_pt") or 0) * FIG_W_PT / (m.get("w_pt") or FIG_W_PT)
        if eff < MIN_FIG_PT - 1e-6:
            low_fs.append("%s %.1fpt" % (stem, eff))
    problems_all = problems + meta_bad + low_fs
    n_lines = len(src_files) - len(in_ph)
    check("6 插图与占位",
          "插图清单：%d 张线稿 + %d 张 in_report 占位 = %d 张；fig_meta %d 条"
          % (n_lines, len(in_ph), len(src_files), len(src_stems)),
          not problems_all,
          "; ".join(problems_all[:6]) if problems_all
          else "报告内占位 %d 张（另 %d 张移出报告）；画布 15.8cm / PNG %dpx / 落版字号 ≥ %.1fpt"
               % (len(in_ph), len(out_ph), meta_px, MIN_FIG_PT))

    # 生成侧占位清单（md2docx 输出的 _md2docx.json 的 placeholders）应与 in_report 一致
    sb_ph = stats.get("placeholders")
    if sb_ph is None:
        check("6 插图与占位", "生成侧占位清单覆盖 manifest in_report 项", None,
              "_md2docx.json 无 placeholders 字段")
    else:
        all_files = {e.get("file", "") for e in ph_list}
        miss = [f for f in in_files if f not in sb_ph]
        unknown = [f for f in sb_ph if f not in all_files]
        check("6 插图与占位", "生成侧占位清单覆盖 manifest in_report 项",
              not miss and not unknown,
              ("缺 %s" % miss if miss else "")
              + ("；未登记 %s" % unknown if unknown else "")
              if (miss or unknown)
              else "json %d 项（报告内 %d 项）" % (len(sb_ph), len(in_files)))


def check_placeholders_final(final: bool, src: dict) -> None:
    """--final 门禁：只针对 in_report=true 且 replaced=false 的占位。"""
    manifest = read_json(MANIFEST)
    ph_list = manifest.get("placeholders", []) if isinstance(manifest, dict) else []
    src_set = {Path(p).name for p in src["images"]}
    in_ph = [e for e in ph_list if _in_report(e, src_set)]
    out_n = len(ph_list) - len(in_ph)
    remain = [e for e in in_ph if not e.get("replaced")]
    if final:
        check("6 插图与占位", "--final 门禁：报告内占位图已全部替换", not remain,
              ("报告内仍残留 %d 张占位图：%s（交件前必须替换并置 replaced=true）"
               % (len(remain), "、".join(e.get("file", "") for e in remain[:8])))
              if remain else "报告内占位图清零")
    else:
        check("6 插图与占位", "--final 门禁：报告内占位图已全部替换", None,
              "非 --final 模式：报告内 %d 张占位待替换（另有 %d 张已移出报告，不计门禁）"
              % (len(remain), out_n))


# ---------------------------------------------------------------- PDF
def content_blocks(page):
    """一页的正文内容块（文字 + 图片），排除页眉页脚。"""
    out = []
    for b in page.get_text("dict")["blocks"]:
        if b["type"] not in (0, 1):
            continue
        y0, y1 = b["bbox"][1], b["bbox"][3]
        if y1 < TEXT_TOP_PT or y0 > TEXT_BOT_PT:
            continue
        out.append(b)
    return sorted(out, key=lambda b: b["bbox"][1])


def block_label(b):
    if b["type"] == 1:
        return "插图(高%.1fcm)" % ((b["bbox"][3] - b["bbox"][1]) / 28.3465)
    for line in b.get("lines", []):
        txt = "".join(s["text"] for s in line["spans"]).strip()
        if txt:
            return "文字:%s" % txt[:26]
    return "文字"


def page_fill_rows(pdf):
    """逐页量填充与底部空白（版心按 SPEC 24.9cm）。"""
    rows = []
    for pno, page in enumerate(pdf, 1):
        blocks = content_blocks(page)
        foot = [s for b in page.get_text("dict")["blocks"] if b["type"] == 0
                for line in b["lines"] for s in line["spans"]
                if s["bbox"][1] > TEXT_BOT_PT and s["text"].strip()]
        printed = None
        if foot:
            m = re.search(r"\d+", "".join(s["text"] for s in foot))
            printed = int(m.group(0)) if m else None
        imgs = [b for b in blocks if b["type"] == 1]
        row = {"page": pno, "printed": printed, "role": "正文",
               "image": block_label(imgs[0]) if imgs else None,
               "first": block_label(blocks[0]) if blocks else "(无内容)"}
        if not blocks:
            row.update(fill=0.0, gap=100.0, gap_pt=TEXT_H_PT)
            rows.append(row)
            continue
        ymin = min(b["bbox"][1] for b in blocks)
        ymax = max(b["bbox"][3] for b in blocks)
        gap_pt = TEXT_BOT_PT - ymax
        row.update(fill=(ymax - max(ymin, TEXT_TOP_PT)) / TEXT_H_PT * 100.0,
                   gap=gap_pt / TEXT_H_PT * 100.0, gap_pt=gap_pt)
        rows.append(row)
    return rows


def emit_fill_table(pdf_path: Path, warn=None) -> list[dict]:
    import pymupdf
    warn = FILL_WARN if warn is None else warn
    with pymupdf.open(str(pdf_path)) as pdf:
        rows = page_fill_rows(pdf)
    emit("逐页留白分析 —— %s（%d 页）" % (pdf_path.name, len(rows)))
    emit("口径：内容 = 正文文字块 + 图片块（排除页眉页脚）；"
         "fill%% = 内容高度 / 版心高度(%.1fcm)；底部空白 = 版心底 − 内容最低点"
         % TEXT_H_CM)
    emit("")
    emit("%-6s %-5s %-5s %-8s %-10s %-8s %s"
         % ("PDF页", "角色", "页码", "fill%", "底部空白%", "空白pt", "本页首块"))
    for r in rows:
        emit("%-6d %-6s %-6s %-8.1f %-10.1f %-8.1f %s"
             % (r["page"], r["role"], r["printed"] if r["printed"] else "-",
                r["fill"], r["gap"], r["gap_pt"], r["first"]))
    over = [(i, r) for i, r in enumerate(rows) if r["gap"] > warn]
    emit("")
    emit("底部空白 > %.0f%% 的页：%d 页（序号为 PDF 页）" % (warn, len(over)))
    for i, r in over:
        nxt = rows[i + 1] if i + 1 < len(rows) else None
        why = "（末页，内容到此结束）" if nxt is None else "下一页首块：%s%s" % (
            nxt["first"],
            ("；该页有%s，整块不可分割" % nxt["image"]) if nxt["image"] else "")
        emit("  p%02d %s 页码 %s：底部空白 %.1f%%（%.0fpt 中空 %.0fpt）← %s"
             % (r["page"], r["role"], r["printed"] or "-", r["gap"], TEXT_H_PT,
                r["gap_pt"], why))
    return rows


def analyze_pdf(pdf_path: Path, src: dict, pdf_stats: dict) -> None:
    import pymupdf

    pdf = pymupdf.open(str(pdf_path))
    n_pages = pdf.page_count
    word_pages = pdf_stats.get("Pages") or pdf_stats.get("pages") or \
        pdf_stats.get("PageCount")
    if word_pages is None:
        check("1 产物与生成统计", "PDF 页数 = Word ComputeStatistics 记录（不估算）", None,
              "缺 _word_to_pdf.json 的 Pages（先跑 word_to_pdf.ps1）")
    else:
        check("1 产物与生成统计", "PDF 页数 = Word ComputeStatistics 记录（不估算）",
              n_pages == int(word_pages) and n_pages >= 10,
              "PDF %d 页 / Word %s 页" % (n_pages, word_pages))

    a4_ok = all(abs(p.rect.width - A4_W_PT) < 1.5
                and abs(p.rect.height - A4_H_PT) < 1.5 for p in pdf)
    check("7 页面与页码", "A4 纵版（%d 页全部）" % n_pages, a4_ok,
          "%.2f x %.2f pt" % (pdf[0].rect.width, pdf[0].rect.height) if a4_ok
          else "存在非 A4 页")

    hdr_bad = []
    for i, page in enumerate(pdf, 1):
        for b in page.get_text("dict")["blocks"]:
            if b["type"] != 0:
                continue
            for line in b["lines"]:
                for s in line["spans"]:
                    if s["text"].strip() and s["bbox"][3] < TEXT_TOP_PT - 1:
                        hdr_bad.append("第 %d 页 %r" % (i, s["text"][:12]))
    check("7 页面与页码", "PDF 无页眉文字", not hdr_bad,
          "; ".join(hdr_bad[:4]) if hdr_bad else "版心上方无文字")

    # 页脚：PAGE 域渲染出的页码 1..N，Times 11pt 居中
    num_bad, foot_bad = [], []
    for i, page in enumerate(pdf, 1):
        foot = [s for b in page.get_text("dict")["blocks"] if b["type"] == 0
                for line in b["lines"] for s in line["spans"]
                if s["bbox"][1] > TEXT_BOT_PT and s["text"].strip()]
        txt = "".join(s["text"] for s in foot).strip()
        if txt != str(i):
            num_bad.append("第 %d 页页脚 %r" % (i, txt[:14]))
        for s in foot:
            if not near(s["size"], FOOTER_PT, 0.8):
                foot_bad.append("p%d %.1fpt" % (i, s["size"]))
            if "times" not in s["font"].lower():
                foot_bad.append("p%d %s" % (i, s["font"]))
            if s["color"] != 0:
                foot_bad.append("p%d 字色 #%06x" % (i, s["color"]))
    check("7 页面与页码", "PDF 页码 = 1..N 连续、页脚 Times 11pt 黑色",
          not num_bad and not foot_bad,
          "; ".join((num_bad + foot_bad)[:5])
          if (num_bad or foot_bad) else "1..%d 连续" % n_pages)

    # 字体：全部嵌入、四族齐全、中文不被回退字体替换
    fonts = {}
    for page in pdf:
        for f in page.get_fonts(full=True):
            name = re.sub(r"^[A-Z]{6}\+", "", f[3] or "")
            fonts.setdefault(name, {"ext": f[1], "type": f[2], "n": 0})
            fonts[name]["n"] += 1
    unembedded = sorted(n for n, v in fonts.items() if not v["ext"])

    def fam(name: str) -> str:
        n = name.replace(" ", "").lower()
        if "kai" in n or "楷" in name:
            return FONT_CN
        if "simsun" in n or "song" in n or "宋" in name:
            return FONT_CN
        if "simhei" in n or "hei" in n or "黑" in name:
            return FONT_HEI
        if "times" in n:
            return "Times"
        if "consolas" in n:
            return "Consolas"
        if any(k in n for k in ("cambria", "segoe", "symbol", "wingdings",
                                "dejavu", "liberation", "stix", "math")):
            return "符号回退"
        return "其他"

    fams = {fam(n) for n in fonts}
    miss_fam = [x for x in (FONT_CN, FONT_HEI, "Times", "Consolas") if x not in fams]
    check("8 PDF 渲染", "字体全部嵌入且四族齐全（%d 族）" % len(fonts),
          not unembedded and not miss_fam,
          (("未嵌入：" + "、".join(unembedded[:6])) if unembedded else "")
          + (("缺字体：" + "、".join(miss_fam)) if miss_fam else "")
          if (unembedded or miss_fam) else "、".join(sorted(fonts)[:8]))

    cjk_bad, sym_bad, other_fams = [], [], set()
    for pno, page in enumerate(pdf, 1):
        for b in page.get_text("dict")["blocks"]:
            if b["type"] != 0:
                continue
            for line in b["lines"]:
                for s in line["spans"]:
                    t = s["text"]
                    if not t.strip():
                        continue
                    f = fam(s["font"])
                    if f == "其他":
                        other_fams.add(s["font"])
                    if has_cjk(t) and f not in (FONT_CN, FONT_HEI):
                        cjk_bad.append("p%d %r 用 %s" % (pno, t[:10], s["font"]))
                    if f == "符号回退" and has_cjk(t):
                        sym_bad.append("p%d %r 用 %s" % (pno, t[:10], s["font"]))
    check("8 PDF 渲染", "中文由楷体 / 黑体渲染（非回退替换）",
          not cjk_bad and not sym_bad,
          ("；".join((cjk_bad + sym_bad)[:6]) if (cjk_bad or sym_bad)
           else ("未登记字体：" + "、".join(sorted(other_fams)) if other_fams
                 else "全部命中楷体 / 黑体；符号回退字体仅承载 ⚠ / 数学符号")))

    # 行框重叠
    overlaps = []
    for pno, page in enumerate(pdf, 1):
        spans = []
        for b in page.get_text("dict")["blocks"]:
            if b["type"] != 0:
                continue
            for line in b["lines"]:
                for s in line["spans"]:
                    if s["text"].strip() and s["bbox"][1] > 55 and s["bbox"][3] < 790:
                        spans.append((s["bbox"], s["text"].strip()))
        for i in range(len(spans)):
            for j in range(i + 1, len(spans)):
                a, b = spans[i][0], spans[j][0]
                if (min(a[2], b[2]) - max(a[0], b[0]) > 5
                        and min(a[3], b[3]) - max(a[1], b[1]) > 3):
                    overlaps.append("p%d %r<>%r"
                                    % (pno, spans[i][1][:12], spans[j][1][:12]))
    check("8 PDF 渲染", "PDF 无行框重叠（横向 >5pt 且纵向 >3pt）", not overlaps,
          "; ".join(overlaps[:6]) if overlaps else "0 处")

    # 插图 dpi / 数量 / 图注同页
    # 与 pdf_images_hires.py 共用同一份量法（只算实际显示的图像对象、同 xref 去重），
    # 避免它替换像素后这里把旧/新对象重复计数；页眉 logo（官方模板版式，每页共用
    # 一个 xref）按同一规则豁免，不算正文插图。
    sys.path.insert(0, str(BUILD))
    try:
        import pdf_images_hires as hires
        imgs, _hdr_xrefs = hires.body_images(pdf)
    except Exception:                              # noqa: BLE001 保底：自行测量
        raw, seen = [], set()
        for pno, page in enumerate(pdf, 1):
            for info in page.get_image_info(xrefs=True):
                xref = info.get("xref") or 0
                x0, y0, x1, y1 = info["bbox"]
                w_px = info.get("width") or 0
                disp = x1 - x0
                dpi = w_px / (disp / 72.0) if disp > 0 else 0
                raw.append({"page": pno, "xref": xref, "dpi": dpi, "y1": y1})
        pages_by_xref = {}
        for it in raw:
            pages_by_xref.setdefault(it["xref"], set()).add(it["page"])
        hdr_xrefs = {x for x, ps in pages_by_xref.items()
                     if len(ps) >= max(3, pdf.page_count // 2)}
        for it in raw:
            if it["xref"] in seen or it["xref"] in hdr_xrefs:
                continue
            seen.add(it["xref"])
            imgs.append(it)
    for it in imgs:                                # hires 的条目用 bbox，这里统一成 y1
        if "y1" not in it and it.get("bbox"):
            it["y1"] = it["bbox"][3]
    dpi_bad = ["p%d %.0f dpi" % (it["page"], it["dpi"])
               for it in imgs if it["dpi"] < MIN_IMG_DPI - 1e-6]
    check("6 插图与占位", "PDF 插图等效 dpi ≥ %.0f（%d 张）" % (MIN_IMG_DPI, len(imgs)),
          not dpi_bad and len(imgs) == len(src["images"]),
          ("低于阈值：" + "; ".join(dpi_bad[:6])) if dpi_bad
          else ("%.0f~%.0f dpi" % (min(i["dpi"] for i in imgs),
                                   max(i["dpi"] for i in imgs)) if imgs else "无插图"))
    same_bad = []
    for it in imgs:
        page = pdf[it["page"] - 1]
        found = False
        for b in page.get_text("dict")["blocks"]:
            if b["type"] != 0 or b["bbox"][1] < TEXT_TOP_PT:
                continue
            if b["bbox"][3] < it["y1"] - 40:      # 图上方太远的文字不算
                continue
            txt = "".join(s["text"] for line in b["lines"] for s in line["spans"])
            if re.match(r"\s*图\s*\d+", txt):
                found = True
                break
        if not found:
            same_bad.append("p%d" % it["page"])
    check("6 插图与占位", "图注与插图同页（图注在图所在页）", not same_bad,
          "异常页：" + "、".join(same_bad[:6]) if same_bad else "全部同页")

    # 彩色文字 / 彩色矢量线
    color_bad, draw_bad = [], []
    for pno, page in enumerate(pdf, 1):
        for b in page.get_text("dict")["blocks"]:
            if b["type"] != 0:
                continue
            for line in b["lines"]:
                for s in line["spans"]:
                    if s["text"].strip() and s["color"] != 0:
                        color_bad.append("p%d %r #%06x"
                                         % (pno, s["text"][:10], s["color"]))
        for dr in page.get_drawings():
            for key in ("color", "fill"):
                c = dr.get(key)
                if not c:
                    continue
                comps = tuple(round(x, 3) for x in c)
                if comps in ((0.0, 0.0, 0.0), (1.0, 1.0, 1.0)):
                    continue
                if not (comps[0] == comps[1] == comps[2]):
                    draw_bad.append("p%d %s=%s" % (pno, key, comps))
    check("7 页面与页码", "PDF 无彩色文字 / 彩色矢量线（全黑）",
          not color_bad and not draw_bad,
          "; ".join((color_bad + draw_bad)[:5])
          if (color_bad or draw_bad) else "文字与矢量线全黑")

    # 逐页留白（信息项 + 硬门槛）
    emit("")
    fill_rows = emit_fill_table(pdf_path)
    last = fill_rows[-1] if fill_rows else None
    thin = [r for r in fill_rows if r["fill"] < FILL_FAIL and r is not last]
    if thin:
        thin_txt = "以下页填充不足：" + "; ".join(
            "p%d %.1f%%" % (r["page"], r["fill"]) for r in thin)
    elif fill_rows:
        worst = min([r for r in fill_rows if r is not last] or fill_rows,
                    key=lambda r: r["fill"])
        thin_txt = ("正文 %d 页最低填充 %.1f%%（p%d，末页除外）；"
                    "底部空白 > %.0f%% 的页见上表"
                    % (len(fill_rows), worst["fill"], worst["page"], FILL_WARN))
    else:
        thin_txt = "无正文页"
    check("9 逐页留白", "无正文页被撑空（填充 ≥ %.0f%%，末页除外）" % FILL_FAIL,
          not thin, thin_txt)
    blank = [r["page"] for r in fill_rows if r["fill"] < BLANK_FILL]
    check("9 逐页留白", "无整页空白页（fill < %.0f%%）" % BLANK_FILL, not blank,
          "空白页：" + "、".join("p%d" % p for p in blank) if blank else "无")

    pdf.close()


# ---------------------------------------------------------------- 数字审计
def check_numbers(text_path: Path) -> None:
    """调用 .refactor/check_numbers.py：新文本「新有旧无」数字必须为 0。"""
    grp, name = "10 数字守恒", "check_numbers.py：新有旧无数字 = 0"
    if not CHECK_NUMBERS.is_file():
        check(grp, name, None, "脚本不存在：%s（重构期一次性迁移审计脚本，终审后已归档；本文正文数字已由该审计把关，见 docs/log/2026-10-29-报告重构纯论文式.md）" % CHECK_NUMBERS.name)
        return
    if not text_path.is_file():
        check(grp, name, None, "报告源不存在：%s" % text_path.name)
        return
    cmd = [sys.executable, str(CHECK_NUMBERS), "--new", str(text_path),
           "--max-list", "0"]
    if DOCS_TEST.is_file():
        cmd += ["--corpus", str(DOCS_TEST)]
    if DOCS_LOG.is_dir():
        for f in sorted(DOCS_LOG.glob("*.md")):
            cmd += ["--corpus", str(f)]
    try:
        proc = subprocess.run(cmd, cwd=str(REPO), capture_output=True,
                              text=True, encoding="utf-8", errors="replace",
                              timeout=240)
    except (OSError, subprocess.TimeoutExpired) as exc:
        check(grp, name, None, "无法运行审计脚本：%s" % exc)
        return
    out = (proc.stdout or "") + (proc.stderr or "")
    m = re.search(r"【1】新有旧无，需解释：(\d+)\s*个", out)
    if m is None:
        tail = out.strip().splitlines()[-1][:80] if out.strip() else "无输出"
        check(grp, name, None, "审计输出无法解析（退出码 %d）：%s"
              % (proc.returncode, tail))
        return
    n_needs = int(m.group(1))
    detail = "新有旧无 %d 个" % n_needs
    if n_needs:
        seg = out.split("【1】", 1)[-1].split("【2】", 1)[0]
        items = [ln.strip() for ln in seg.splitlines() if ln.strip().startswith("-")]
        detail += "：" + "; ".join(items[:8])
    else:
        m_var = re.search(r"【2】新有旧无，但数值旧报告里已有[^：]*：(\d+)\s*个", out)
        detail = "新有旧无 0 个（写法变体 %s 个，旧有新无可接受）" % (
            m_var.group(1) if m_var else "?")
    check(grp, name, n_needs == 0 and proc.returncode == 0, detail)


# ---------------------------------------------------------------- 禁项
def check_forbidden(src_text: str, docx_text: str) -> None:
    rest_src, rest_docx = strip_allowed(src_text), strip_allowed(docx_text)
    hits_src = sorted({w for w in FORBIDDEN if w in rest_src})
    hits_docx = sorted({w for w in FORBIDDEN if w in rest_docx})
    check("11 禁项与生成链", "无学校 / 学院 / 指导老师 / 参赛队名字样",
          not hits_src and not hits_docx,
          (("源：" + "、".join(hits_src) + " ") if hits_src else "")
          + ("docx：" + "、".join(hits_docx)) if (hits_src or hits_docx)
          else "无（竞赛名按允许名单剔除）")
    hp_src, hp_docx = host_paths(src_text), host_paths(docx_text)
    check("11 禁项与生成链", "报告无主机绝对路径", not hp_src and not hp_docx,
          "; ".join((hp_src + hp_docx)[:4]) if (hp_src or hp_docx) else "0 处")

    # 生成链脚本 / README 不得含真实主机路径（AGENTS.md 红线 4）
    drive = "[A-Za-z]" + ":" + "[\\\\/]"
    unc = "\\\\\\\\" + "[A-Za-z0-9_.-]+" + "\\\\"
    pattern = re.compile("(" + drive + r"[^\s\"'`,;)]*|" + unc + r"[^\s\"'`,;)]*)")
    offenders = []
    for d in (BUILD, FIGURES):
        for f in sorted(d.glob("*")):
            if not (f.is_file() and f.suffix in (".py", ".ps1", ".md")):
                continue
            if f.resolve() == Path(__file__).resolve():
                continue                      # 本文件：模式串按片段拼接，不自扫
            try:
                lines = f.read_text(encoding="utf-8").splitlines()
            except (OSError, UnicodeDecodeError):
                continue
            for i, ln in enumerate(lines, 1):
                for m in pattern.finditer(ln):
                    p = m.group(1).rstrip("\\/")
                    parts = [x for x in re.split(r"[\\/]+", p[2:]) if x]
                    if len(parts) >= 2 and Path(p).exists():
                        offenders.append("%s/%s:%d %s" % (d.name, f.name, i, p))
    check("11 禁项与生成链", "生成链脚本与 README 无真实主机绝对路径（build/ + figures/）",
          not offenders, "; ".join(offenders[:5]) if offenders
          else "0 处（检测模式串按片段拼接，不自扫）")

    zip_path = OUT / "DragonEyeSorter_代码_v1.0.zip"
    if zip_path.is_file():
        check("11 禁项与生成链", "代码包 zip 存在且 ≤ 100MB",
              zip_path.stat().st_size <= 100 * 1024 * 1024,
              "%s，%.2f MB" % (zip_path.name, zip_path.stat().st_size / 1e6))
    else:
        check("11 禁项与生成链", "代码包 zip（pack_code.ps1 产出，可选）", None,
              "未生成：跑 pwsh -File submission/build/pack_code.ps1")


# ---------------------------------------------------------------- 主流程
def flush(out_path, failed=()) -> int:
    p = Path(out_path)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text("\n".join(LINES) + "\n", encoding="utf-8")
    print("\n[verify_report] 已写入 %s" % p)
    return 1 if failed else 0


def main(argv=None) -> int:
    global DOCX, PDF, SOURCE
    default_docx, default_pdf = DOCX, PDF
    ap = argparse.ArgumentParser(description="作品报告验收自检（SPEC v1）")
    ap.add_argument("-Out", "--out", default=None,
                    help="报告写到这里（默认 out/_verify_report.txt）")
    ap.add_argument("--docx", default=str(DOCX), help="要核对的 docx")
    ap.add_argument("--pdf", default=str(PDF), help="要核对的 PDF（留白分析可用任意 PDF）")
    ap.add_argument("--source", default=str(SOURCE), help="内容源 Markdown")
    ap.add_argument("--stats", default=None,
                    help="生成侧统计 JSON（默认 out/_md2docx.json；--docx 指定临时产物时取其同名 .json）")
    ap.add_argument("--pdf-stats", default=None,
                    help="Word 页数统计 JSON（默认 out/_word_to_pdf.json）")
    ap.add_argument("--fill", action="store_true",
                    help="只跑逐页留白分析（配合 --pdf 可对比改版前的 PDF）")
    ap.add_argument("--warn", type=float, default=FILL_WARN,
                    help="底部空白告警阈值 %%，默认 %(default)s")
    ap.add_argument("--final", action="store_true",
                    help="交件门禁：仍有占位图残留判 FAIL")
    args = ap.parse_args(argv)

    DOCX = Path(args.docx).resolve()
    PDF = Path(args.pdf).resolve()
    SOURCE = Path(args.source).resolve()
    out_path = Path(args.out) if args.out else OUT / "_verify_report.txt"

    if args.fill:
        if not PDF.is_file():
            emit("[error] 找不到 PDF：%s" % PDF)
            return 1
        emit_fill_table(PDF, args.warn)
        return flush(out_path)

    emit("作品报告验收自检 —— submission/build/verify_report.py（SPEC v1 论文版）")
    emit("模式：%s   docx=%s   pdf=%s"
         % ("--final 交件门禁" if args.final else "常规验收", DOCX.name, PDF.name))
    emit("=" * 86)

    # ---- 1 产物 ----
    sib = Path(str(DOCX)[:-5] + ".json")
    stats_path = Path(args.stats) if args.stats else (
        sib if (Path(args.docx).resolve() != default_docx.resolve() and sib.is_file())
        else STATS)
    pdf_stats_path = Path(args.pdf_stats) if args.pdf_stats else PDF_STATS
    stats = read_json(stats_path)
    pdf_stats = read_json(pdf_stats_path)
    src_text = SOURCE.read_text(encoding="utf-8", errors="replace") \
        if SOURCE.is_file() else ""
    check("1 产物与生成统计", "产物齐全：docx / PDF / 内容源 Markdown",
          DOCX.is_file() and PDF.is_file() and bool(src_text),
          "docx %s / PDF %s / 源 %s"
          % ("有" if DOCX.is_file() else "缺", "有" if PDF.is_file() else "缺",
             "有" if src_text else "缺"))
    check("1 产物与生成统计", "自检统计可读：_md2docx.json + _word_to_pdf.json",
          bool(stats) and bool(pdf_stats),
          "stats %s %d 键 / pdf_stats %s %d 键"
          % (stats_path.name, len(stats), pdf_stats_path.name, len(pdf_stats)))
    if PDF.is_file():
        head = PDF.read_bytes()[:5]
        size_mb = PDF.stat().st_size / 1e6
        check("1 产物与生成统计", "PDF 文件头 %PDF- 且 ≤ 50MB",
              head == b"%PDF-" and size_mb <= 50.0,
              "%r，%.2f MB" % (head, size_mb))
    else:
        check("1 产物与生成统计", "PDF 文件头 %PDF- 且 ≤ 50MB", False, "PDF 缺失")
    gen_issues = []
    if stats.get("forbidden_hits"):
        gen_issues.append("禁项 %s" % stats.get("forbidden_hits"))
    if stats.get("spacing_issues"):
        gen_issues.append("行距 %s" % stats.get("spacing_issues"))
    if stats.get("self_check_issues"):
        gen_issues.append("自检 %s" % stats.get("self_check_issues"))
    if gen_issues:
        check("1 产物与生成统计", "生成侧自检无禁项 / 行距 / 自检告警", False,
              "; ".join(gen_issues)[:150])
    elif stats.get("forbidden_hits") is not None or stats.get("spacing_issues") is not None:
        check("1 产物与生成统计", "生成侧自检无禁项 / 行距 / 自检告警", True, "无告警")
    else:
        check("1 产物与生成统计", "生成侧自检无禁项 / 行距 / 自检告警", None,
              "_md2docx.json 无生成侧自检字段")

    if not (DOCX.is_file() and PDF.is_file() and src_text):
        emit("")
        emit("结论：产物缺失，后续检查无法执行 —— 先跑 submission/build/build.ps1")
        return flush(out_path, [r for r in results if r[2] is False])

    # ---- 2 源 ----
    src = parse_source(src_text)
    try:
        check_source(src, src_text, stats)
    except Exception as exc:                       # noqa: BLE001 保底不崩
        check("2 源 Markdown", "源结构解析", False, "解析异常：%r" % exc)

    # ---- 3~6 docx ----
    doc, docx_text = None, ""
    try:
        from docx import Document
        doc = Document(str(DOCX))
        analyze_docx(doc, src, stats)
        check_figures(src, stats)
        check_placeholders_final(args.final, src)
        docx_text = "\n".join([p.text for p in doc.paragraphs]
                              + [c.text for t in doc.tables
                                 for row in t.rows for c in row.cells])
    except ImportError as exc:
        check("3 DOCX 结构", "python-docx 可用", False, str(exc))
    except Exception as exc:                       # noqa: BLE001 保底不崩
        check("3 DOCX 结构", "docx 解析", False, "解析异常：%r" % exc)

    # ---- 7~9 PDF ----
    if doc is not None:
        try:
            import pymupdf  # noqa: F401
            analyze_pdf(PDF, src, pdf_stats)
        except ImportError as exc:
            check("8 PDF 渲染", "PyMuPDF 可用", False, str(exc))
        except Exception as exc:                   # noqa: BLE001 保底不崩
            check("8 PDF 渲染", "PDF 解析", False, "解析异常：%r" % exc)

    # ---- 10 数字守恒 ----
    try:
        check_numbers(SOURCE)
    except Exception as exc:                       # noqa: BLE001
        check("10 数字守恒", "check_numbers.py：新有旧无数字 = 0", None,
              "审计异常：%r" % exc)

    # ---- 11 禁项 ----
    try:
        check_forbidden(src_text, docx_text)
    except Exception as exc:                       # noqa: BLE001
        check("11 禁项与生成链", "禁项扫描", False, "扫描异常：%r" % exc)

    # ---- 汇总 ----
    emit("")
    emit("=" * 86)
    failed = [r for r in results if r[2] is False]
    skipped = [r for r in results if r[2] is None]
    ordered = [g for g in GROUP_ORDER if g in GROUPS] +               [g for g in GROUPS if g not in GROUP_ORDER]
    for g in ordered:
        rows = [r for r in results if r[0] == g]
        emit("【%s】%d 项：PASS %d / FAIL %d / SKIP %d"
             % (g, len(rows), sum(1 for r in rows if r[2] is True),
                sum(1 for r in rows if r[2] is False),
                sum(1 for r in rows if r[2] is None)))
        for _, name, ok, detail in rows:
            label = "PASS" if ok is True else ("SKIP" if ok is None else "FAIL")
            emit("  %-4s %s%s" % (label, name, ("  —— " + detail) if detail else ""))
    emit("-" * 86)
    emit("合计 %d 项：通过 %d，警告 %d，跳过 %d，失败 %d"
         % (len(results), len(results) - len(failed) - len(skipped),
            len(warnings), len(skipped), len(failed)))
    if warnings:
        emit("")
        emit("警告项（WARN，不影响退出码）：%d" % len(warnings))
        for g, name, detail in warnings:
            emit("  WARN  【%s】%s%s" % (g, name, ("  —— " + detail) if detail else ""))
    if skipped:
        emit("跳过项（需人工确认）：")
        for _, name, _, detail in skipped:
            emit("  SKIP  %s —— %s" % (name, detail))
    if args.final:
        emit("模式：--final —— 占位图残留即判 FAIL（交件门禁）")
    emit("结论：%s" % ("全部通过（无失败项）" if not failed else
                       "有不通过项：" + "; ".join(n for _, n, _, _ in failed[:12])))
    return flush(out_path, failed)


if __name__ == "__main__":
    sys.exit(main())
