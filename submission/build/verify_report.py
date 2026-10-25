# -*- coding: utf-8 -*-
"""作品报告验收自检（独立复核用；不改任何产物）。

逐项核对提交前的硬要求，把原始结果写进 submission/out/_verify_report.txt：
  1 产物存在 / PDF 文件头 / 页数 / **PDF ≤ 50MB（打印现值）**
  2 仓库内脚本不含主机绝对路径（AGENTS.md 硬性约定第 9 条）
  3 docx 结构：段落 / 表格 / 插图 / 标题层级 / 节数（与 _md2docx.json 对账 + 源文件独立数）
  4 版式设计口径：宋体 10.5pt 正文、黑体 16/13pt 标题、1.5 倍行距、
    首行缩进 2 字符（420 twips）、行内代码 Consolas、全篇黑色正文
  5 设计令牌：章标题无装饰线（学术版）、三线表无竖线无行线无底纹、图注全黑 9pt、
    页眉页脚小号深灰
  6 页面与版心、表格宽度、表格固定布局、表头重复、插图宽度（满版心 15.8cm）
  7 封面 / 目录 / 正文的页眉页脚、页码域（PAGE / NUMPAGES）
  8 竞赛禁项扫描（含页眉页脚与表格）
  9 TOC 域与 updateFields
 10 疑似行框重叠（PyMuPDF 逐页两两求交）
 11 **插图清晰度：每张插图等效 dpi ≥ 300**（Word 导出只有 96，靠 pdf_images_hires.py 提上来）
 12 **插图可读性：每张插图落版最小字号 ≥ 7pt**（figkit 按 15.8cm 落版 1:1 作图，读 paper/fig_meta.json）
 13 **逐页底部空白分析**（信息项：每页数值 + >20% 的页及原因）+ 正文页不被撑空（填充 ≥ 50%）
 14 代码包与 README 登记

计数类检查不再写死数字（submission/技术文档.md 的内容在持续优化，章 / 表数量可能变）：
一律从 out/_md2docx.json 读生成侧实际值，对 docx 结构对账，再给「下限」兜底；
H1 / 表 / 图再与源 Markdown 独立数出来的数对比。

用法：
    python submission/build/verify_report.py [-Out <报告路径>]
    python submission/build/verify_report.py --fill [--pdf <任意 PDF>] [--warn 20]
        —— 只跑留白分析，用来做改版前后对比（可指向旧 PDF，比如 out/_baseline_96dpi.pdf）
退出码：0 全部通过 / 1 有 FAIL
"""
from __future__ import annotations

import argparse
import json
import re
import sys
import zipfile
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

BUILD = Path(__file__).resolve().parent
REPO = BUILD.parent.parent
OUT = REPO / "submission" / "out"
DOCX = OUT / "龙瞳分拣_作品报告.docx"
PDF = OUT / "龙瞳分拣_作品报告.pdf"
SOURCE = REPO / "submission" / "技术文档.md"
STATS = OUT / "_md2docx.json"
PDF_STATS = OUT / "_word_to_pdf.json"

# ---- 版式设计令牌（与 md2docx.py 同口径；独立复核用，不 import 生成脚本）
FONT_CN = "宋体"
FONT_HEI = "黑体"
FONT_EN = "Times New Roman"
FONT_CODE = "Consolas"
C_INK = "000000"          # 黑：标题 / 正文
C_GRAY = "3F3F3F"         # 深灰：图注 / 页眉页脚
C_LINE = "808080"         # 灰线：页眉细线
BODY_PT = 10.5
BODY_LINE = 1.5           # 宋体单倍行高 ≈ 字号 × 1.286，1.5 倍 ≈ 20.25pt
H1_PT, H2_PT = 16.0, 13.0
CAPTION_PT = 9.0
TABLE_PT = 9.0
SMALL_PT = 9.0
CHAR_TWIPS = 420          # 2 字符 = 2 × 10.5pt × 20
LIST_LEFT_TW = 840        # 列表正文左缩进 4 字符
LIST_HANG_TW = 420        # 列表悬挂 2 字符
HEADER_TEXT = "龙瞳分拣 · 塑胶传动件在线质检分拣系统"
HEADER_TOC = "目录"
HEADER_BODY = "作品报告"

# 版心上下边界（pt，A4 高 841.89pt / 上下页边距 2.6cm = 73.7pt）。
# 页眉在页边距里（header_distance 1.5cm ≈ 42.5pt）、页脚同理，都在这个区间之外，
# 所以按 y 过滤就能把页眉页脚排除，只留正文内容块（文字 + 图片）。
TEXT_TOP_PT = 73.7
TEXT_BOT_PT = 841.89 - 73.7
TEXT_H_PT = TEXT_BOT_PT - TEXT_TOP_PT
FILL_WARN = 20.0          # 底部空白 > 20% 的页在报告里逐页列出原因（信息项）
FILL_FAIL = 50.0          # 正文页填充 < 50% 判 FAIL（一页只剩一两行）
MIN_IMG_DPI = 300.0       # 插图等效 dpi 硬门槛
MIN_FIG_PT = 7.0          # 插图落版最小字号硬门槛（pt）：低于这个数在纸上/屏幕上都读不动
PLACED_CM = 15.8          # 报告里插图的显示宽度（满版心）；figkit 按同一宽度 1:1 作图
FIG_NAMES = ("fig_architecture", "fig_timebase", "fig_pl_regs", "fig_pick_sequence", "fig_layout")

FORBIDDEN = ("大学", "学院", "学校", "指导老师", "指导教师", "导师", "校徽", "班级")
ALLOWED = ("2026 全国大学生嵌入式芯片与系统设计竞赛",
           "全国大学生嵌入式芯片与系统设计竞赛")

results: list[tuple[str, object, str]] = []


def check(name: str, ok, detail: str = "") -> None:
    """ok = True 通过 / False 失败 / None 跳过（合理例外，报告里写明原因）。"""
    results.append((name, ok, detail))


# ---------------------------------------------------------------- 留白分析
# 复跑方式（改版前后对比用同一个函数，口径才可比）：
#   python submission/build/verify_report.py --fill --pdf <PDF>
# 口径：fill% = 正文内容高度 / 版心高度（24.5cm）；底部空白 = 版心底 − 内容最低点。

def content_blocks(page):
    """一页的正文内容块（文字 + 图片），已排除页眉页脚，按纵向位置排序。"""
    out = []
    for b in page.get_text("dict")["blocks"]:
        if b["type"] not in (0, 1):        # 0 = 文字块，1 = 图片块
            continue
        y0, y1 = b["bbox"][1], b["bbox"][3]
        if y1 < TEXT_TOP_PT or y0 > TEXT_BOT_PT:
            continue                       # 整块在页边距里 → 页眉 / 页脚
        out.append(b)
    return sorted(out, key=lambda b: b["bbox"][1])


def block_label(b):
    """一句话说明这个块是什么：插图（带高度）或文字（首行截断）。"""
    if b["type"] == 1:
        h_cm = (b["bbox"][3] - b["bbox"][1]) / 28.3465
        return "插图(高%.1fcm)" % h_cm
    for line in b.get("lines", []):
        txt = "".join(s["text"] for s in line["spans"]).strip()
        if txt:
            return "文字:%s" % txt[:26]
    return "文字"


def page_fill_rows(pdf, warn=FILL_WARN):
    """逐页量底部空白，返回 [{page, printed, role, fill, gap, gap_pt, first, image}]。

    printed = 页脚「第 X 页」里的页码（None = 封面 / 目录，不编号）
    role    = 封面 / 目录 / 正文 —— 正文以「有页脚页码」判定，不靠页序猜
    image   = 该页第一张插图的说明（没有插图则 None）；用来说明**下一页**为什么
              会把上一页顶出空白：满版心插图是整块不可分割，放不下就整页留白
    """
    rows = []
    for pno, page in enumerate(pdf, 1):
        blocks = content_blocks(page)
        text = page.get_text()
        m = re.search(r"第\s*(\d+)\s*页", text)
        printed = int(m.group(1)) if m else None
        # 目录页现在也有页眉（「龙瞳分拣 · …」在最上面），不能再靠「第一行是不是目录」
        # 判断角色——没有页码的页里，带「目录」页眉的是目录页，其余是封面。
        role = "正文" if m else ("目录" if "目录" in text.replace(" ", "") else "封面")
        imgs = [b for b in blocks if b["type"] == 1]
        row = {"page": pno, "printed": printed, "role": role,
               "image": block_label(imgs[0]) if imgs else None,
               "first": block_label(blocks[0]) if blocks else "(无内容)"}
        if not blocks:
            row.update(fill=0.0, gap=100.0, gap_pt=TEXT_H_PT)
            rows.append(row)
            continue
        ymin = min(b["bbox"][1] for b in blocks)
        ymax = max(b["bbox"][3] for b in blocks)
        gap_pt = TEXT_BOT_PT - ymax
        row.update(
            fill=(ymax - max(ymin, TEXT_TOP_PT)) / TEXT_H_PT * 100.0,
            gap=gap_pt / TEXT_H_PT * 100.0, gap_pt=gap_pt)
        rows.append(row)
    return rows



def emit_fill_table(pdf_path, emit, warn=FILL_WARN) -> list[dict]:
    """打印逐页留白表（并返回数据行）。--fill 模式与正式自检共用同一份口径。"""
    import pymupdf
    with pymupdf.open(str(pdf_path)) as pdf:
        rows = page_fill_rows(pdf, warn)
    emit("逐页留白分析 —— %s（%d 页）" % (pdf_path.name, len(rows)))
    emit("口径：内容 = 正文文字块 + 图片块（排除页眉页脚）；"
         "fill% = 内容高度 / 版心高度(24.5cm)；底部空白 = 版心底 − 内容最低点")
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
            nxt["first"], ("；该页有%s，整块不可分割" % nxt["image"]) if nxt["image"] else "")
        emit("  p%02d %s 页码 %s：底部空白 %.1f%%（%.0fpt 中空 %.0fpt）← %s"
             % (r["page"], r["role"], r["printed"] or "-", r["gap"], TEXT_H_PT,
                r["gap_pt"], why))
    return rows


def run_fill(pdf_path: Path, warn: float, out_path, emit, lines) -> int:
    """--fill：只做留白分析（可指向任意 PDF，用于改版前后对比）。"""
    if not pdf_path.is_file():
        emit("[error] 找不到 PDF：%s" % pdf_path)
        return 1
    emit_fill_table(pdf_path, emit, warn)
    if out_path:
        flush(out_path, lines)
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("-Out", "--out", default=None,
                    help="报告写到这里（默认 out/_verify_report.txt；--fill 时不写文件）")
    ap.add_argument("--pdf", default=str(PDF), help="要核对的 PDF（留白分析可用任意 PDF）")
    ap.add_argument("--fill", action="store_true",
                    help="只跑逐页留白分析（配合 --pdf 可对比改版前的 PDF）")
    ap.add_argument("--warn", type=float, default=FILL_WARN,
                    help="底部空白告警阈值 %%，默认 %(default)s")
    args = ap.parse_args(argv)
    out_path = args.out or str(OUT / "_verify_report.txt")

    lines: list[str] = []

    def emit(s: str = "") -> None:
        lines.append(s)
        print(s)

    if args.fill:
        return run_fill(Path(args.pdf).resolve(), args.warn, args.out, emit, lines)

    emit("作品报告验收自检 —— 生成自 submission/build/verify_report.py")
    emit("=" * 78)

    # ---- 1 产物 ----
    check("docx 存在", DOCX.is_file(), DOCX.name)
    check("pdf 存在", PDF.is_file(), PDF.name)
    if not (DOCX.is_file() and PDF.is_file()):
        # 产物缺失：后面的检查都做不了，但必须按失败退出（退出码 1），否则门禁会把「没生成」当通过
        missing = [r for r in results if r[1] is False]
        for n, _, d in missing:
            emit("FAIL  %s —— %s" % (n, d))
        emit("结论：有不通过项：" + "; ".join(n for n, _, _ in missing))
        return flush(out_path, lines, missing)
    head = PDF.read_bytes()[:5]
    check("PDF 文件头 %PDF-", head == b"%PDF-", repr(head))
    check("PDF ≤ 50MB", PDF.stat().st_size <= 50 * 1024 * 1024,
          "现值 %.2f MB（%.0f 字节）" % (PDF.stat().st_size / 1e6, PDF.stat().st_size))

    # ---- 2 脚本不含主机绝对路径 ----
    # 只认「盘符 + 至少两段 + 该路径真实存在」：脚本里写盘符模式串（自查用）、注释里
    # 写 <盘符>:\Users\<占位> 这类说明，都不算硬编码主机路径
    # （AGENTS.md 硬性约定第 9 条禁的是真路径）。
    # 检测模式按片段拼出来，免得本文件自己被自己的规则扫到。
    drive = "[A-Za-z]" + ":" + "[\\\\/]"
    unc = "\\\\\\\\" + "[A-Za-z0-9_.-]+" + "\\\\"
    candidates = re.compile("(" + drive + "[^\\s\"'`,;)]*|" + unc + "[^\\s\"'`,;)]*)")
    # 本任务写入范围内的文件；其余（同事的 PPT 链）扫到只记 SKIP，不算我这条线的失败
    mine = {"md2docx.py", "word_to_pdf.ps1", "build.ps1", "pack_code.ps1",
            "verify_report.py", "pdf_images_hires.py", "README.md"}
    offenders, others = [], []
    for f in sorted(BUILD.glob("*")):
        if not (f.is_file() and f.suffix in (".py", ".ps1", ".md")):
            continue
        for i, ln in enumerate(f.read_text(encoding="utf-8").splitlines(), 1):
            for m in candidates.finditer(ln):
                p = m.group(1).rstrip("\\/")
                parts = [x for x in re.split("[\\\\/]+", p[2:]) if x]
                if len(parts) >= 2 and Path(p).exists():
                    (offenders if f.name in mine else others).append(
                        "%s:%d %s" % (f.name, i, p))
    check("build/ 报告链脚本与 README 无真实主机绝对路径", not offenders,
          "; ".join(offenders) if offenders else "0 处（检测用模式串按片段拼接，不自扫）")
    check("build/ 同事 PPT 链文件无真实主机绝对路径（不在本任务写入范围）",
          None if others else True,
          ("非本任务文件，不改：" + "; ".join(others)) if others else "0 处")

    # ---- 统计对账：生成侧 out/_md2docx.json（内容源在持续优化，计数不写死） ----
    stats = {}
    if STATS.is_file():
        try:
            stats = json.loads(STATS.read_text(encoding="utf-8"))
        except ValueError:
            stats = {}
    check("out/_md2docx.json 存在且可读", bool(stats), STATS.name)

    # ---- docx 读回 ----
    from docx import Document
    from docx.oxml.ns import qn
    doc = Document(str(DOCX))
    zf = zipfile.ZipFile(str(DOCX))

    n_par = len(doc.paragraphs)
    n_tbl = len(doc.tables)
    n_img = len(doc.inline_shapes)
    h1 = sum(1 for p in doc.paragraphs if p.style.name == "Heading 1")
    h2 = sum(1 for p in doc.paragraphs if p.style.name == "Heading 2")
    h3 = sum(1 for p in doc.paragraphs if p.style.name == "Heading 3")

    # ---- 3 结构：与生成侧统计对账 + 下限兜底 ----
    s_h = stats.get("headings") or {}
    check("段落数 = 生成侧统计（且 ≥ 150）",
          n_par == stats.get("paragraphs") and n_par >= 150,
          "docx %d / json %s" % (n_par, stats.get("paragraphs")))
    check("表格数 = 生成侧统计（且 ≥ 8）",
          n_tbl == stats.get("tables") and n_tbl >= 8,
          "docx %d / json %s" % (n_tbl, stats.get("tables")))
    check("插图数 = 生成侧统计 = 5（源里 5 张）",
          n_img == stats.get("images") and n_img == 5,
          "docx %d / json %s" % (n_img, stats.get("images")))
    check("节数 = 3（封面/目录/正文）", len(doc.sections) == 3, str(len(doc.sections)))
    check("标题数 = 生成侧统计（H1 ≥ 10 章、H2 ≥ 20 节）",
          h1 == s_h.get("h1") and h1 >= 10 and h2 == s_h.get("h2") and h2 >= 20,
          "H1=%d H2=%d H3=%d / json H1=%s H2=%s"
          % (h1, h2, h3, s_h.get("h1"), s_h.get("h2")))

    # 源里的章 / 节数（独立数一遍源文件，别信脚本自己的统计）
    src = SOURCE.read_text(encoding="utf-8").splitlines()
    src_h2 = sum(1 for ln in src if ln.startswith("## "))
    src_h3 = sum(1 for ln in src if ln.startswith("### "))
    src_img = sum(1 for ln in src if re.match(r"^!\[[^\]]*\]\([^)]+\)$", ln.strip()))
    src_tbl = sum(1 for ln in src if re.match(r"^\|\s*-", ln.strip()))
    check("源里 ## 章数 = docx H1", src_h2 == h1, "源 %d / docx %d" % (src_h2, h1))
    check("源里 ### 节数 = docx H2", src_h3 == h2, "源 %d / docx %d" % (src_h3, h2))
    check("源里插图数 = docx 插图数", src_img == n_img, "源 %d / docx %d" % (src_img, n_img))
    check("源里表格数 = docx 表格数", src_tbl == n_tbl, "源 %d / docx %d" % (src_tbl, n_tbl))

    # ---- 4 字体字号 / 行距 / 缩进（学术报告口径：宋体 10.5pt） ----
    def run_font(run):
        rpr = run._element.find(qn("w:rPr"))
        fonts = rpr.find(qn("w:rFonts")) if rpr is not None else None
        color = rpr.find(qn("w:color")) if rpr is not None else None
        return (fonts.get(qn("w:eastAsia")) if fonts is not None else None,
                fonts.get(qn("w:ascii")) if fonts is not None else None,
                color.get(qn("w:val")) if color is not None else None)

    def par_ind(par, attr):
        pPr = par._p.pPr
        ind = pPr.find(qn("w:ind")) if pPr is not None else None
        return None if ind is None else ind.get(qn("w:" + attr))

    # 正文按**样式名**取样：BodyCN = 源里的普通段落（首行缩进 2 字符），
    # ListCN = 列表项（悬挂缩进，不强求首行缩进）。封面 / 目录 / 图注 / 页眉页脚 /
    # 表格单元格各有各的样式，天然不进这个样本。
    body = [p for p in doc.paragraphs if p.style.name == "BodyCN" and p.text.strip()]
    lst = [p for p in doc.paragraphs if p.style.name == "ListCN" and p.text.strip()]
    code_runs = [r for p in body + lst for r in p.runs
                 if r.text.strip() and run_font(r)[1] == FONT_CODE]
    body_runs = [r for p in body + lst for r in p.runs
                 if r.text.strip() and run_font(r)[1] != FONT_CODE]
    # 正文宋体（中文强调走黑体，不叠字重）、西文 Times、黑色；行内代码 Consolas。
    bad_font, n_plain, n_hei = [], 0, 0
    for r in body_runs:
        ea, _, color = run_font(r)
        if ea not in (FONT_CN, FONT_HEI):
            bad_font.append("%r(期望宋体/黑体 实为%s)" % (r.text[:10], ea))
        elif color != C_INK:
            bad_font.append("%r(字色 %s 期望 %s)" % (r.text[:10], color, C_INK))
        elif ea == FONT_HEI:
            n_hei += 1
        else:
            n_plain += 1
    sz_ok = all(r.font.size is not None and abs(r.font.size.pt - BODY_PT) < 1e-6
                for r in body_runs)
    code_ok = all(r.font.size is not None and 8.0 <= r.font.size.pt <= BODY_PT
                  for r in code_runs)
    check("正文 %d 段 + 列表 %d 段：%d 个文字 run 字体/字色正确"
          "（宋体 %d / 黑体强调 %d，另 %d 个行内代码 run）"
          % (len(body), len(lst), len(body_runs), n_plain, n_hei, len(code_runs)),
          not bad_font, "; ".join(bad_font[:4]))
    check("正文 / 列表字号 = 10.5pt（行内代码 8~10.5pt）", sz_ok and code_ok)
    h_ok, h_detail = True, []
    for lvl, want in (("Heading 1", H1_PT), ("Heading 2", H2_PT)):
        runs = [r for p in doc.paragraphs if p.style.name == lvl
                for r in p.runs if r.text.strip()]
        got_sz = {round(r.font.size.pt, 2) for r in runs if r.font.size}
        got_cn = {run_font(r)[0] for r in runs}
        h_detail.append("%s: %s %s" % (lvl, sorted(got_cn), sorted(got_sz)))
        if got_cn != {FONT_HEI} or got_sz != {want}:
            h_ok = False
    check("标题全 run 中文 = 黑体，字号 16 / 13pt", h_ok, "; ".join(h_detail))

    # 首行缩进：BodyCN 必须是 420 twips（2 字符 @10.5pt）；ListCN 用悬挂缩进
    ind_bad = [p.text[:24] for p in body if par_ind(p, "firstLine") != str(CHAR_TWIPS)]
    check("正文首行缩进 = 420 twips（2 字符）", not ind_bad,
          "; ".join(ind_bad[:3]) if ind_bad else "%d 段全部合规" % len(body))
    jc_bad = [p.text[:20] for p in body + lst if p.paragraph_format.alignment != 3]
    check("正文 / 列表两端对齐", not jc_bad, "; ".join(jc_bad[:3]) if jc_bad else "")
    line_bad = [p.text[:20] for p in body + lst
                if p.paragraph_format.line_spacing is None
                or abs(p.paragraph_format.line_spacing - BODY_LINE) > 1e-6]
    check("正文 / 列表行距 = 1.5 倍（宋体 10.5pt → 20.25pt）", not line_bad,
          "; ".join(line_bad[:3]) if line_bad else "")
    check("正文段 ≥ 30 且列表段 ≥ 20（样本量够）",
          len(body) >= 30 and len(lst) >= 20, "正文 %d / 列表 %d" % (len(body), len(lst)))
    hang_bad = [p.text[:20] for p in lst
                if par_ind(p, "left") != str(LIST_LEFT_TW)
                or par_ind(p, "hanging") != str(LIST_HANG_TW)]
    check("列表项左缩进 840 + 悬挂 420 twips", not hang_bad,
          "; ".join(hang_bad[:3]) if hang_bad else "")
    marker_bad = []
    for p in lst:
        runs = [r for r in p.runs if r.text.strip()]
        if not runs or run_font(runs[0])[2] != C_INK:
            marker_bad.append(p.text[:16])
    check("列表标记 = 黑色（• / 1.）", not marker_bad, "; ".join(marker_bad[:3]))

    # ---- 5 设计令牌（学术版：黑白灰、无装饰色） ----
    def style_pPr(name):
        for st in doc.styles:
            if st.name.lower() == name.lower():
                return st.element.find(qn("w:pPr"))
        return None

    def edge(bdr, name):
        return None if bdr is None else bdr.find(qn("w:" + name))

    # 学术版：章标题不加装饰色块 / 色线（Heading 1 样式不应带 pBdr）
    ppr1 = style_pPr("Heading 1")
    bdr1 = ppr1.find(qn("w:pBdr")) if ppr1 is not None else None
    decorated = bdr1 is not None and any(
        edge(bdr1, e) is not None and edge(bdr1, e).get(qn("w:val")) not in (None, "none")
        for e in ("top", "left", "bottom", "right"))
    check("一级标题无装饰色块 / 色线（学术版）", not decorated,
          "pBdr=%s" % ("有" if decorated else "无"))

    head_bad, border_bad = [], []
    for i, t in enumerate(doc.tables, 1):
        cells = list(t.rows[0].cells)

        def unshaded(c):
            tcPr = c._tc.tcPr
            if tcPr is None:
                return True
            shd = tcPr.find(qn("w:shd"))
            if shd is None:
                return True
            return shd.get(qn("w:fill")) in (None, "auto", "FFFFFF")

        if not cells or not all(unshaded(c) for c in cells):
            head_bad.append("表%d" % i)
        tblb = t._tbl.tblPr.find(qn("w:tblBorders"))
        top, bot = edge(tblb, "top"), edge(tblb, "bottom")
        iv, ih = edge(tblb, "insideV"), edge(tblb, "insideH")
        hdr_line = True
        for c in cells:                      # 栏目线：表头单元格下边框 0.75pt 黑
            tcPr = c._tc.tcPr
            tb = tcPr.find(qn("w:tcBorders")) if tcPr is not None else None
            b = edge(tb, "bottom")
            if b is None or b.get(qn("w:sz")) != "6" or b.get(qn("w:color")) != C_INK:
                hdr_line = False
        if not (top is not None and top.get(qn("w:sz")) == "12"
                and top.get(qn("w:color")) == C_INK
                and bot is not None and bot.get(qn("w:sz")) == "12"
                and bot.get(qn("w:color")) == C_INK
                and iv is not None and iv.get(qn("w:val")) == "none"
                and ih is not None and ih.get(qn("w:val")) == "none"
                and hdr_line):
            border_bad.append("表%d" % i)
    check("表头无底色（三线表，%d 张表）" % n_tbl, not head_bad, "; ".join(head_bad))
    check("三线表：上下 1.5pt 黑线 + 表头下 0.75pt 细线，无竖线无行线",
          not border_bad, "; ".join(border_bad[:3]))

    caps = [p for p in doc.paragraphs if p.style.name == "CaptionCN" and p.text.strip()]
    cap_bad = []
    for p in caps:
        runs = [r for r in p.runs if r.text.strip()]
        if not runs:
            continue
        bad = [r for r in runs if run_font(r)[2] != C_INK
               or r.font.size is None or abs(r.font.size.pt - CAPTION_PT) > 1e-6]
        if bad:
            cap_bad.append("%s（字色 / 字号不合口径）" % p.text[:14])
        elif p.paragraph_format.alignment != 1:      # 1 = 居中
            cap_bad.append("%s（未居中）" % p.text[:14])
    check("图注：全黑 %gpt 居中（%d 条）" % (CAPTION_PT, len(caps)), not cap_bad,
          "; ".join(cap_bad[:3]))

    toc_bad = []
    for name in ("TOC 1", "TOC 2"):
        pPr = style_pPr(name)
        sp_el = pPr.find(qn("w:spacing")) if pPr is not None else None
        if (sp_el is None or sp_el.get(qn("w:line")) != "240"
                or sp_el.get(qn("w:lineRule")) != "auto"):
            toc_bad.append(name)
    check("目录条目 TOC 1/2 单倍行距（压回一页的关键）", not toc_bad, ",".join(toc_bad))

    # ---- 5b 行距：全篇不得有 fixed / exact（标题压字的成因） ----
    xml_all = "".join(zf.read(n).decode("utf-8", "replace") for n in zf.namelist()
                      if n.startswith("word/") and n.endswith(".xml"))
    bad = sorted(set(re.findall(r'w:lineRule="(exact|atLeast|fixed)"', xml_all)))
    tiny = sorted({int(v) for v in re.findall(r'w:line="(\d+)"', xml_all)
                   if int(v) < 240 and int(v) != 144})
    check("无 fixed / exact 行距", not bad, ",".join(bad) if bad else "仅 auto")
    check("无 < 1.0 倍行距（144 的表后空行除外）", not tiny, str(tiny))

    # 学术版不再用彩色：正文 / 页眉页脚里只允许黑、深灰、灰三种字色，表格线同理
    # （000000 / 808080）。styles.xml 里 Word 内置样式的默认主题色不算（未被任何文字使用）。
    color_xml = "".join(
        zf.read(n).decode("utf-8", "replace") for n in zf.namelist()
        if re.match(r"word/(document|header\d*|footer\d*)\.xml$", n))
    colors = {c.upper() for c in re.findall(r'w:color w:val="([0-9A-Fa-f]{6})"', color_xml)}
    weird = sorted(c for c in colors if c not in {C_INK, C_GRAY, C_LINE})
    check("全篇无彩色（字色 / 线条只有黑 / 深灰 / 灰）", not weird,
          ("出现 %s" % "、".join(weird[:8])) if weird
          else "实测 %d 种：%s" % (len(colors), "、".join(sorted(colors))))

    # ---- 6 页面 / 版心 / 表格 / 插图宽度 ----
    sec = doc.sections[2]
    a4 = (abs(sec.page_width.cm - 21.0) < 0.01 and abs(sec.page_height.cm - 29.7) < 0.01)
    marg = all(abs(getattr(sec, m).cm - 2.6) < 0.01
               for m in ("left_margin", "right_margin", "top_margin", "bottom_margin"))
    check("A4 纵版", a4, "%.2f x %.2f cm" % (sec.page_width.cm, sec.page_height.cm))
    check("页边距 2.6cm（版心 15.8cm）", marg)
    tbl_w = []
    for t in doc.tables:
        w = sum(c.width.twips for c in t.columns) / 567.0
        tbl_w.append(round(w, 2))
    check("每张表 ≤ 15.8cm 版心", all(w <= 15.8 + 1e-6 for w in tbl_w), str(tbl_w))
    check("表格均为固定布局（列宽可控）", all(
        t._tbl.tblPr.find(qn("w:tblLayout")) is not None and
        t._tbl.tblPr.find(qn("w:tblLayout")).get(qn("w:type")) == "fixed"
        for t in doc.tables))
    check("表头行重复（跨页）", all(
        t.rows[0]._tr.find(qn("w:trPr")) is not None and
        t.rows[0]._tr.find(qn("w:trPr")).find(qn("w:tblHeader")) is not None
        for t in doc.tables))
    img_w = [round(s.width.cm, 2) for s in doc.inline_shapes]
    check("插图宽 ≤ 15.8cm 版心（满版心 15.8）",
          all(w <= 15.8 + 1e-6 for w in img_w), str(img_w))

    # ---- 7 页眉页脚 ----
    cover_hdr = "".join(p.text for p in doc.sections[0].header.paragraphs).strip()
    cover_ftr = "".join(p.text for p in doc.sections[0].footer.paragraphs).strip()
    check("封面无页眉页脚文字", cover_hdr == "" and cover_ftr == "",
          "hdr=%r ftr=%r" % (cover_hdr, cover_ftr))
    hdr_par = doc.sections[2].header.paragraphs[0]
    body_hdr = hdr_par.text.strip()
    check("正文页眉 = 作品名短版 + 右标签「作品报告」",
          body_hdr.startswith(HEADER_TEXT) and body_hdr.endswith(HEADER_BODY),
          repr(body_hdr))
    toc_hdr = "".join(p.text for p in doc.sections[1].header.paragraphs).strip()
    check("目录页眉 = 作品名短版 + 右标签「目录」",
          toc_hdr.startswith(HEADER_TEXT) and toc_hdr.endswith(HEADER_TOC),
          repr(toc_hdr))
    hdr_runs = [r for r in hdr_par.runs if r.text.strip()]
    check("页眉字号 = 9pt 深灰", all(
        r.font.size is not None and abs(r.font.size.pt - SMALL_PT) < 1e-6
        and run_font(r)[2] == C_GRAY for r in hdr_runs),
        "字号 %s" % sorted({r.font.size.pt for r in hdr_runs if r.font.size}))
    footer_xml = "".join(zf.read(n).decode("utf-8") for n in zf.namelist()
                         if re.match(r"word/footer\d*\.xml$", n))
    check("页脚含 PAGE 与 NUMPAGES 域", "PAGE" in footer_xml and "NUMPAGES" in footer_xml)
    check("页脚文字 = 第 X 页 / 共 Y 页", "第 " in footer_xml and " 页 / 共 " in footer_xml)
    pnum = doc.sections[2]._sectPr.find(qn("w:pgNumType"))
    check("正文页码从 1 起（pgNumType start=1）",
          pnum is not None and pnum.get(qn("w:start")) == "1")

    # ---- 8 禁项 ----
    text = "\n".join(p.text for p in doc.paragraphs)
    for t in doc.tables:
        for row in t.rows:
            for cell in row.cells:
                text += "\n" + cell.text
    for s in doc.sections:
        text += "\n" + "".join(p.text for p in s.header.paragraphs)
        text += "\n" + "".join(p.text for p in s.footer.paragraphs)
    rest = text
    for phrase in ALLOWED:
        rest = rest.replace(phrase, "")
    hits = sorted({w for w in FORBIDDEN if w in rest})
    check("无学校 / 指导老师 / 班级字样", not hits, ",".join(hits) if hits else "无")

    # ---- 9 目录域 ----
    docxml = zf.read("word/document.xml").decode("utf-8")
    check("TOC 域存在", "TOC \\o" in docxml, re.findall(r"TOC[^<]{0,30}", docxml)[:1])
    settings = zf.read("word/settings.xml").decode("utf-8")
    check("settings 置 updateFields=true", 'w:updateFields w:val="true"' in settings)

    # ---- 10 重叠 ----
    try:
        import pymupdf
        pdf = pymupdf.open(str(PDF))
        overlaps = []
        # 判据取保守值：横向 > 5pt 且纵向 > 3pt 才算压字。
        # 中文标点与相邻西文（如「40°，」）的字框天然相邻，阈值太小会误报。
        for pno, page in enumerate(pdf, 1):
            spans = []
            for b in page.get_text("dict")["blocks"]:
                if b["type"] != 0:
                    continue
                for l in b["lines"]:
                    for s in l["spans"]:
                        if s["text"].strip() and s["bbox"][1] > 60 and s["bbox"][3] < 780:
                            spans.append((s["bbox"], s["text"].strip()))
            for i in range(len(spans)):
                for j in range(i + 1, len(spans)):
                    a, b = spans[i][0], spans[j][0]
                    if (min(a[2], b[2]) - max(a[0], b[0]) > 5
                            and min(a[3], b[3]) - max(a[1], b[1]) > 3):
                        overlaps.append("p%d %r<>%r" % (pno, spans[i][1][:14],
                                                        spans[j][1][:14]))
        check("PDF 无行框重叠（横向>5pt 且纵向>3pt）", not overlaps,
              "; ".join(overlaps[:6]) if overlaps else "0 处")
        pdf_stats = {}
        if PDF_STATS.is_file():
            try:
                pdf_stats = json.loads(PDF_STATS.read_text(encoding="utf-8"))
            except ValueError:
                pdf_stats = {}
        check("PDF 页数 = _word_to_pdf.json 记录（≥ 15）",
              pdf.page_count >= 15 and pdf.page_count == pdf_stats.get("Pages"),
              "pdf %d 页 / json %s 页" % (pdf.page_count, pdf_stats.get("Pages")))
        check("PDF 页面尺寸 = A4", all(
            abs(p.rect.width - 595.28) < 1.5 and abs(p.rect.height - 841.89) < 1.5
            for p in pdf))

        # ---- 11 插图清晰度：等效 dpi ≥ 300 ----
        # Word 导出把插图压到 96 dpi（3200px 原图只剩 547~597px）。build.ps1 第 3 步
        # pdf_images_hires.py 把像素换回源图，这里独立量一遍：dpi = 像素宽 /（显示宽 inch）。
        # 量法与那个脚本共用同一份代码（import 进来用），避免两套口径。
        sys.path.insert(0, str(BUILD))
        import pdf_images_hires as hires
        imgs = hires.distinct(hires.page_images(pdf))
        dpi_bad = ["#%d p%02d %.0f dpi" % (i, it["page"], it["dpi"])
                   for i, it in enumerate(imgs, 1) if it["dpi"] < MIN_IMG_DPI]
        dpi_txt = " ".join("%d" % round(it["dpi"]) for it in imgs)
        check("PDF 插图等效 dpi ≥ %.0f（%d 张）" % (MIN_IMG_DPI, len(imgs)),
              not dpi_bad and len(imgs) == n_img,
              ("低于阈值：" + "; ".join(dpi_bad)) if dpi_bad
              else "%s dpi（源图 3200px / 显示宽 %.2fcm）"
                   % (dpi_txt, imgs[0]["disp_cm"] if imgs else 0))
        check("PDF 插图数量 = 源里 ![](...) 数（无图标混入）",
              len(imgs) == src_img == n_img, "%d 张" % len(imgs))

        # ---- 12 插图字号：落版 1:1 作图，图内字号就是纸面字号 ----
        # 教训（本版重画的起因）：旧插图按 16in（1152pt）画布作图、再缩到 15.8cm 落版，
        # 缩放 0.389 → 图内 8pt 落到纸面只剩 3.1pt，报告里整片文字读不动。
        # 现在 docs/figures/figkit.py 强制「画布宽 = 落版宽、字号 = 纸面字号」，
        # 这里拿报告用的 paper 主题 fig_meta.json 的原始记录独立复核一遍等效字号。
        meta_p = REPO / "docs" / "figures" / "paper" / "fig_meta.json"
        figs_meta = {}
        if meta_p.is_file():
            figs_meta = json.loads(meta_p.read_text(encoding="utf-8")).get("figures", {})
        placed_pt = PLACED_CM / 2.54 * 72.0
        missing = [k for k in FIG_NAMES if k not in figs_meta]
        eff = [(k, figs_meta[k]["min_fs_pt"] * placed_pt / figs_meta[k]["w_pt"])
               for k in FIG_NAMES if k in figs_meta]
        low = ["%s %.1fpt" % (k, e) for k, e in eff if e < MIN_FIG_PT - 1e-6]
        if missing:
            fs_txt = "fig_meta.json 缺条目：" + ", ".join(missing) + "（先跑 docs/figures/make_figures.py --paper）"
        elif low:
            fs_txt = "低于阈值：" + "; ".join(low)
        else:
            k_min = min(eff, key=lambda t: t[1])
            fs_txt = ("最低 %.1fpt（%s）；画布 %.1fpt = 落版 %.1fcm，缩放 %.3f"
                      % (k_min[1], k_min[0], figs_meta[k_min[0]]["w_pt"], PLACED_CM,
                         placed_pt / figs_meta[k_min[0]]["w_pt"]))
        check("插图落版字号 ≥ %.1fpt（%d 张）" % (MIN_FIG_PT, len(FIG_NAMES)),
              not missing and not low, fs_txt)

        # ---- 13 逐页留白（信息项 + 一条硬门槛）----
        emit("")
        fill_rows = emit_fill_table(PDF, emit)
        body = [r for r in fill_rows if r["role"] == "正文"]
        # 末页不设填充门槛：正文在末页自然收尾，尾部留白是文档结束，不是「撑空」。
        last = body[-1] if body else None
        thin = [r for r in body if r["fill"] < FILL_FAIL and r is not last]
        if thin:
            thin_txt = "以下页填充不足：" + "; ".join(
                "p%d %.1f%%" % (r["page"], r["fill"]) for r in thin)
        elif body:
            worst = min([r for r in body if r is not last] or body,
                        key=lambda r: r["fill"])
            thin_txt = ("正文 %d 页最低填充 %.1f%%（p%d，末页除外）；"
                        "底部空白 > %.0f%% 的正文页见上表"
                        % (len(body), worst["fill"], worst["page"], FILL_WARN))
        else:
            thin_txt = "无正文页"
        check("无正文页被撑空（填充 ≥ %.0f%%，末页除外）" % FILL_FAIL, not thin, thin_txt)

        # 分页卫生：封面 / 目录各 1 页，全文没有整页空白（改版前出现过目录后空白页）
        cover_rows = [r for r in fill_rows if r["role"] == "封面"]
        toc_rows = [r for r in fill_rows if r["role"] == "目录"]
        blank_rows = [r["page"] for r in fill_rows if r["fill"] < 8.0]
        check("封面 1 页 / 目录 1 页 / 无整页空白页",
              len(cover_rows) == 1 and len(toc_rows) == 1 and not blank_rows,
              "封面 %d 页 / 目录 %d 页 / 空白页 %s"
              % (len(cover_rows), len(toc_rows), blank_rows or "无"))
    except ImportError as exc:
        check("PyMuPDF 可用（PDF 逐页核对 / dpi / 留白）", False,
              "未安装 pymupdf 或 pdf_images_hires.py 导入失败：%s" % exc)

    # ---- 13 代码包与 README 登记 ----
    zip_path = OUT / "DragonEyeSorter_代码_v1.0.zip"
    if zip_path.is_file():
        check("代码包 zip 存在且 ≤ 100MB",
              zip_path.stat().st_size <= 100 * 1024 * 1024,
              "%s，%.2f MB" % (zip_path.name, zip_path.stat().st_size / 1e6))
    else:
        check("代码包 zip（pack_code.ps1 产出，可选）", None,
              "未生成：跑 pwsh -File submission/build/pack_code.ps1")
    readme = (BUILD / "README.md").read_text(encoding="utf-8")
    scripts = ("md2docx.py", "word_to_pdf.ps1", "build.ps1", "pack_code.ps1",
               "verify_report.py", "pdf_images_hires.py",
               "export_ppt_preview.ps1", "make_ppt.py", "ppt_pdf_hires.py")
    missing = [n for n in scripts if n not in readme]
    check("README 登记全部 build/ 脚本", not missing,
          ("缺：" + ",".join(missing)) if missing else "%d 个脚本全部登记" % len(scripts))


    # ---- 汇总 ----
    emit("")
    emit("%-46s %s" % ("检查项", "结果"))
    emit("-" * 78)
    for name, ok, detail in results:
        label = "PASS" if ok is True else ("SKIP" if ok is None else "FAIL")
        emit("%-46s %-4s %s" % (name, label, detail))
    failed = [r for r in results if r[1] is False]
    skipped = [r for r in results if r[1] is None]
    emit("-" * 78)
    emit("合计 %d 项：通过 %d，跳过 %d，失败 %d"
         % (len(results), len(results) - len(failed) - len(skipped),
            len(skipped), len(failed)))
    for n, _, d in skipped:
        emit("  SKIP  %s —— %s" % (n, d))
    emit("结论：%s" % ("全部通过（无失败项）" if not failed else
                       "有不通过项：" + "; ".join(n for n, _, _ in failed)))
    return flush(out_path, lines, failed)


def flush(out_path, lines, failed=()) -> int:
    p = Path(out_path)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n[verify_report] 已写入 %s" % p)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
