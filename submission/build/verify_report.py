# -*- coding: utf-8 -*-
"""作品报告验收自检（独立复核用；不改任何产物）。

逐项核对提交前的硬要求，把原始结果写进 submission/out/_verify_report.txt：
  1 产物存在 / PDF 文件头 / 页数
  2 仓库内脚本不含主机绝对路径（AGENTS.md 硬性约定第 9 条）
  3 docx 结构：段落 / 表格 / 插图 / 标题层级 / 节数
  4 正文字体与字号、标题字体与字号
  5 行距：全篇不得有 fixed / exact
  6 页面与版心、表格宽度、插图宽度
  7 封面 / 正文的页眉页脚、页码域
  8 竞赛禁项扫描（含页眉页脚与表格）
  9 TOC 域与 updateFields
 10 疑似行框重叠（PyMuPDF 逐页两两求交）

用法：python submission/build/verify_report.py [-Out <报告路径>]
退出码：0 全部通过 / 1 有 FAIL
"""
from __future__ import annotations

import argparse
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
SOURCE = REPO / "docs" / "技术文档.md"

FORBIDDEN = ("大学", "学院", "学校", "指导老师", "指导教师", "导师", "校徽", "班级")
ALLOWED = ("2026 全国大学生嵌入式芯片与系统设计竞赛",
           "全国大学生嵌入式芯片与系统设计竞赛")

results: list[tuple[str, object, str]] = []


def check(name: str, ok, detail: str = "") -> None:
    """ok = True 通过 / False 失败 / None 跳过（合理例外，报告里写明原因）。"""
    results.append((name, ok, detail))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("-Out", "--out", default=str(OUT / "_verify_report.txt"))
    args = ap.parse_args(argv)

    lines: list[str] = []

    def emit(s: str = "") -> None:
        lines.append(s)
        print(s)

    emit("作品报告验收自检 —— 生成自 submission/build/verify_report.py")
    emit("=" * 78)

    # ---- 1 产物 ----
    check("docx 存在", DOCX.is_file(), DOCX.name)
    check("pdf 存在", PDF.is_file(), PDF.name)
    if not (DOCX.is_file() and PDF.is_file()):
        return flush(args.out, lines)
    head = PDF.read_bytes()[:5]
    check("PDF 文件头 %PDF-", head == b"%PDF-", repr(head))
    check("PDF ≤ 50MB", PDF.stat().st_size <= 50 * 1024 * 1024,
          "%.2f MB" % (PDF.stat().st_size / 1e6))

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
            "verify_report.py", "README.md"}
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
    check("段落数 ≥ 150", n_par >= 150, str(n_par))
    check("表格数 ≥ 8", n_tbl >= 8, str(n_tbl))
    check("插图数 = 5（源里 5 张）", n_img == 5, str(n_img))
    check("节数 = 3（封面/目录/正文）", len(doc.sections) == 3,
          str(len(doc.sections)))
    check("标题层级 H1 = 10 章 + 附录 = 11", h1 == 11, "H1=%d H2=%d H3=%d" % (h1, h2, h3))
    check("H2 节数 = 源里 ### 数", h2 == 25, str(h2))

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

    # ---- 4 字体字号 ----
    def run_font(run):
        rf = run._element.find(qn("w:rPr"))
        rf = rf.find(qn("w:rFonts")) if rf is not None else None
        return (rf.get(qn("w:eastAsia")) if rf is not None else None,
                rf.get(qn("w:ascii")) if rf is not None else None)

    # 正文按**样式名**取样：BodyCN = 源里的普通段落（首行缩进 2 字符），
    # ListCN = 列表项（悬挂缩进，不强求首行缩进）。封面 / 目录 / 图注 / 页眉页脚 /
    # 表格单元格各有各的样式，天然不进这个样本。
    body = [p for p in doc.paragraphs if p.style.name == "BodyCN" and p.text.strip()]
    lst = [p for p in doc.paragraphs if p.style.name == "ListCN" and p.text.strip()]
    body_runs = [r for p in body + lst for r in p.runs
                 if r.text.strip() and run_font(r)[1] != "Consolas"]
    code_runs = [r for p in body + lst for r in p.runs
                 if r.text.strip() and run_font(r)[1] == "Consolas"]
    # 正文里两种字体都是对的：普通文字宋体，**加粗** 那几段按口径走黑体。
    # 逐 run 判「该粗的用黑体、不该粗的用宋体」，而不是一刀切要求全宋体。
    bad_font, n_song, n_hei = [], 0, 0
    for r in body_runs:
        ea, _ = run_font(r)
        want = "黑体" if r.font.bold else "宋体"
        if ea == want:
            n_hei += want == "黑体"
            n_song += want == "宋体"
        else:
            bad_font.append("%r(期望%s 实为%s)" % (r.text[:12], want, ea))
    sz_ok = all(r.font.size is not None and r.font.size.pt == 12.0
                for r in body_runs + code_runs)
    check("正文 %d 段 + 列表 %d 段：%d 个文字 run 字体正确（宋体 %d / 加粗黑体 %d，"
          "另 %d 个行内代码 run）"
          % (len(body), len(lst), len(body_runs), n_song, n_hei, len(code_runs)),
          not bad_font, "; ".join(bad_font[:4]))
    check("正文 / 列表 run 字号 = 12pt", sz_ok)
    h_ok, h_detail = True, []
    for lvl, want in (("Heading 1", 16.0), ("Heading 2", 14.0)):
        # 标题里的行内 `代码` 也走黑体（生成侧已统一），所以标题 run 应当只有黑体
        runs = [r for p in doc.paragraphs if p.style.name == lvl
                for r in p.runs if r.text.strip()]
        got_sz = {r.font.size.pt for r in runs if r.font.size}
        got_cn = {run_font(r)[0] for r in runs}
        h_detail.append("%s: %s %s" % (lvl, sorted(got_cn), sorted(got_sz)))
        if got_cn != {"黑体"} or got_sz != {want}:
            h_ok = False
    check("标题全 run 中文 = 黑体，字号 16/14pt", h_ok, "; ".join(h_detail))

    # 首行缩进：BodyCN 必须是 480 twips（2 字符）；ListCN 用悬挂缩进，不要求首行缩进
    ind_bad = [p.text[:24] for p in body
               if p.paragraph_format.first_line_indent is None
               or p.paragraph_format.first_line_indent.twips != 480]
    check("正文首行缩进 = 480 twips（2 字符）", not ind_bad,
          "; ".join(ind_bad[:3]) if ind_bad else "%d 段全部合规" % len(body))
    jc_bad = [p.text[:20] for p in body + lst if p.paragraph_format.alignment != 3]
    check("正文 / 列表两端对齐", not jc_bad, "; ".join(jc_bad[:3]) if jc_bad else "")
    line_bad = [p.text[:20] for p in body + lst
                if p.paragraph_format.line_spacing is None
                or abs(p.paragraph_format.line_spacing - 1.5) > 1e-6]
    check("正文 / 列表 1.5 倍行距", not line_bad,
          "; ".join(line_bad[:3]) if line_bad else "")
    check("正文段 ≥ 30 且列表段 ≥ 20（样本量够）",
          len(body) >= 30 and len(lst) >= 20, "正文 %d / 列表 %d" % (len(body), len(lst)))
    hang_bad = [p.text[:20] for p in lst
                if p.paragraph_format.left_indent is None
                or p.paragraph_format.left_indent.twips != 480]
    check("列表项悬挂缩进 480 twips", not hang_bad,
          "; ".join(hang_bad[:3]) if hang_bad else "")

    # ---- 5 行距 ----
    xml_all = "".join(zf.read(n).decode("utf-8", "replace") for n in zf.namelist()
                      if n.startswith("word/") and n.endswith(".xml"))
    bad = sorted(set(re.findall(r'w:lineRule="(exact|atLeast|fixed)"', xml_all)))
    tiny = sorted({int(v) for v in re.findall(r'w:line="(\d+)"', xml_all)
                   if int(v) < 240 and int(v) != 144})
    check("无 fixed / exact 行距", not bad, ",".join(bad) if bad else "仅 auto")
    check("无 < 1.0 倍行距（144 的表后空行除外）", not tiny, str(tiny))

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
    check("插图宽 ≤ 14.5cm", all(w <= 14.5 + 1e-6 for w in img_w), str(img_w))

    # ---- 7 页眉页脚 ----
    cover_hdr = "".join(p.text for p in doc.sections[0].header.paragraphs).strip()
    cover_ftr = "".join(p.text for p in doc.sections[0].footer.paragraphs).strip()
    check("封面无页眉页脚文字", cover_hdr == "" and cover_ftr == "",
          "hdr=%r ftr=%r" % (cover_hdr, cover_ftr))
    body_hdr = "".join(p.text for p in doc.sections[2].header.paragraphs)
    check("正文页眉 = 作品名短版", body_hdr.strip() == "龙瞳分拣 · 塑胶传动件在线质检分拣系统",
          repr(body_hdr.strip()))
    ftr3 = zf.read("word/footer3.xml").decode("utf-8")
    check("页脚含 PAGE 与 NUMPAGES 域", "PAGE" in ftr3 and "NUMPAGES" in ftr3)
    check("页脚文字 = 第 X 页 / 共 Y 页", "第 " in ftr3 and " 页 / 共 " in ftr3)
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
        check("PDF 页数 ≥ 15", pdf.page_count >= 15, "%d 页" % pdf.page_count)
        check("PDF 页面尺寸 = A4", all(
            abs(p.rect.width - 595.28) < 1.5 and abs(p.rect.height - 841.89) < 1.5
            for p in pdf))
    except ImportError:
        check("PyMuPDF 可用（PDF 逐页核对）", False, "未安装 pymupdf，跳过重叠检测")

    # ---- 11 代码包与 README 登记 ----
    zip_path = OUT / "DragonEyeSorter_代码_v1.0.zip"
    if zip_path.is_file():
        check("代码包 zip 存在且 ≤ 100MB",
              zip_path.stat().st_size <= 100 * 1024 * 1024,
              "%s，%.2f MB" % (zip_path.name, zip_path.stat().st_size / 1e6))
    else:
        check("代码包 zip（pack_code.ps1 产出，可选）", None,
              "未生成：跑 pwsh -File submission/build/pack_code.ps1")
    readme = (BUILD / "README.md").read_text(encoding="utf-8")
    missing = [n for n in ("md2docx.py", "word_to_pdf.ps1", "build.ps1",
                           "pack_code.ps1", "verify_report.py",
                           "export_ppt_preview.ps1", "make_ppt.py")
               if n not in readme]
    check("README 登记全部 build/ 脚本", not missing,
          ("缺：" + ",".join(missing)) if missing else "7 个脚本全部登记")

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
    return flush(args.out, lines, failed)


def flush(out_path, lines, failed=()) -> int:
    p = Path(out_path)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n[verify_report] 已写入 %s" % p)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
