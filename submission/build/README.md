# submission/build/ —— 提交件生成链

把内容源 [docs/技术文档.md](../../docs/技术文档.md) 转成可提交的 Word 与 PDF，
并从仓库打代码包。**技术文档是唯一内容源**：改内容改那里，本目录只负责排版与导出，
不改源文件。

```powershell
pwsh -File submission/build/build.ps1        # 报告：md → docx → pdf
```

## 本目录脚本清单

| 脚本 | 归属 | 用途 | 入口 |
|---|---|---|---|
| [build.ps1](build.ps1) | 报告链 | **一键入口**：先 `md2docx.py` 再 `word_to_pdf.ps1`，打印产物路径 / 页数 / 大小 | `pwsh -File submission/build/build.ps1` |
| [md2docx.py](md2docx.py) | 报告链 | Markdown → DOCX：封面、目录域、正文排版、表格、插图；生成后自检（禁项 / 行距 / 计数） | 由 `build.ps1` 调用；也可单独 `python submission/build/md2docx.py` |
| [word_to_pdf.ps1](word_to_pdf.ps1) | 报告链 | DOCX → PDF（Word COM），导出前更新页脚与目录域，回报页数 | 由 `build.ps1` 调用；也可单独跑 |
| [pack_code.ps1](pack_code.ps1) | 报告链 | `git archive` 打「作品重要代码」zip，超 100MB 报警并非零退出 | `pwsh -File submission/build/pack_code.ps1` |
| [verify_report.py](verify_report.py) | 报告链 | **验收自检**：40 项逐条核对 docx / PDF（结构、字体、行距、宽度、页眉页脚、禁项、TOC、PDF 逐页重叠检测），结果写 `out/_verify_report.txt` | `python submission/build/verify_report.py` |
| [make_ppt.py](make_ppt.py) | PPT 链（同事 B 维护） | 答辩 PPT 骨架：`docs/参赛材料骨架.md` §4 → `out/龙瞳分拣_答辩.pptx` | `python submission/build/make_ppt.py` |
| [export_ppt_preview.ps1](export_ppt_preview.ps1) | PPT 链（同事 B 维护） | PowerPoint COM 把 pptx 导成 `out/龙瞳分拣_答辩.pdf` + `out/ppt_preview/slide_XX.png`（现场兜底件 + 排版自查） | `pwsh -File submission/build/export_ppt_preview.ps1` |

**改完报告内容要跑**：`build.ps1`（出 docx + pdf）→ `verify_report.py`（确认全 PASS）。
只改 PPT 就跑 PPT 链那两个，与报告链互不影响（两条链写的是不同产物）。

## 一键跑完

```powershell
pwsh -File submission/build/build.ps1
```

两步串起来，任一步失败即停（退出码非 0），末尾打印产物路径 / 页数 / 大小：

| 步骤 | 脚本 | 做什么 |
|---|---|---|
| 1/2 | [md2docx.py](md2docx.py) | Markdown → DOCX（封面 / 目录 / 正文排版，python-docx） |
| 2/2 | [word_to_pdf.ps1](word_to_pdf.ps1) | DOCX → PDF（Word COM，导出前更新页脚与目录域） |

常用参数：

```powershell
pwsh -File submission/build/build.ps1 -NoPdf     # 本机没装 Word 时只出 docx
python submission/build/md2docx.py -Out <别的.docx> -Stats <别的.json>
pwsh -File submission/build/word_to_pdf.ps1 -InputPath a.docx -OutputPath b.pdf
python submission/build/verify_report.py -Out submission/out/_verify_report.txt
```

## 依赖

| 依赖 | 用途 | 怎么验证 |
|---|---|---|
| Microsoft Word（桌面版） | 转 PDF；脚本走 `Word.Application` COM | `New-Object -ComObject Word.Application` 不报错；本机实测 Word 16 |
| Python 3 + python-docx | 生成 docx | `python -m pip show python-docx`（本机 1.2.0） |
| 中文字体 宋体 / 黑体 / Consolas | 正文与标题、行内代码 | 系统自带，无需安装 |

- 脚本里**不写死解释器路径**：`build.ps1` 按 `python` → `py -3` 在 PATH 上找，
  并且试import python-docx 成功才用。要指定解释器就直接调 `md2docx.py`。
- **只需要 Python 就能出 docx**；PDF 那一步非 Word 不可（本机没有 LibreOffice / pandoc）。
- Word 用只读方式打开源 docx，导出后不改动源文件；不弹窗、全程隐藏。

## 产物

| 产物 | 说明 | 入库 |
|---|---|---|
| `submission/out/龙瞳分拣_作品报告.docx` | 排版源，Word 里可继续改 | ❌ 可重建 |
| `submission/out/龙瞳分拣_作品报告.pdf` | **正式提交件**，改名后上传（见 [提交材料清单](../提交材料清单.md) §5） | ❌ 可重建 |
| `submission/out/_md2docx.json` · `_word_to_pdf.json` | 自检统计（段落 / 表格 / 插图 / 页数），供脚本与人工核对 | ❌ |
| `submission/out/render/` | PDF 逐页 PNG，验收看图用 | ❌ |

初始提交文件名按 [提交材料清单](../提交材料清单.md) §5 改成
`DragonEyeSorter_龙瞳分拣_作品报告_v1.0.pdf`，不要在仓库里改产物本体名。

## 排版口径

A4、上下左右页边距 2.6cm（版心 15.8cm），封面 / 目录 / 正文三节：

- **封面**：作品名（22pt 黑体）、赛道、作品报告、落款「参赛队 / 作品编号 / 2026 年 11 月」。
  无页眉页脚。**不得出现学校名称、校徽、指导老师姓名**（竞赛明文禁项）——
  `md2docx.py` 每次生成后扫一遍全文（含页眉页脚、表格），命中即退出码 1。
- **目录**：Word 域 `TOC \o "1-3" \h \z \u`，并在 `settings.xml` 里置
  `w:updateFields=true`，Word 打开时自动更新；`word_to_pdf.ps1` 导出前再显式
  `$doc.Fields.Update()` + 目录域 `Update()`，保证 PDF 里目录页码与正文一致。
- **正文**：宋体 12pt（小四）、首行缩进 2 字符、两端对齐、**1.5 倍行距**；
  标题黑体 16 / 14 / 12pt（三级加粗）、不缩进、与下段同页、段前 8~12pt、段后 6pt。
- **表格**：四周与内部框线、表头加粗 + 浅灰底纹（`D9D9D9`）、正文 10.5pt（五号）、
  整表 15.6cm 不超版心、跨页重复表头；列宽按内容自适应（数值 / 短串列居中）。
- **插图**：居中、宽 ≤ 14.5cm；源里图下一行的 `*斜体*` 图注渲染成居中、宋体
  10.5pt、**去斜体**；图片段与图注段都设 `keepNext`，保证图注跟图同页。
- **页眉页脚**：正文页眉「龙瞳分拣 · 塑胶传动件在线质检分拣系统」带下框线；
  页脚「第 X 页 / 共 Y 页」用 PAGE / NUMPAGES 域；封面无页眉页脚，正文页码从 1 起。
- 行内 `` `代码` `` 用 Consolas、不加底纹；`**加粗**` 用黑体。

### 行距为什么只用倍数（别改成固定值）

`w:spacing` 的 `lineRule="exact"` / `fixed` 会把行框钉死在指定高度：字号一大
（标题 16pt）或字体回退到字面更高的字形时，上一行会**盖住**下一行。第一版报告就踩了
这个坑——标题 `line=20 auto`（等于单倍 1pt 的荒谬值）、表格单元格 `exact 12pt`
配 10.5pt 中文字，导出的 PDF 里标题压在正文与表格上。
现在全篇用倍数（`lineRule="auto"`，`w:line` = 倍数 × 240），并在自检里拒绝
`exact/atLeast/fixed`。改排版时不要图省事换回固定值。

## 以后怎么加章节 / 加图

**加正文**：直接改 [docs/技术文档.md](../../docs/技术文档.md)，重跑 `build.ps1`。
脚本按源里的写法识别（这是**唯一**要遵守的约定）：

| 源里的写法 | 渲染成 |
|---|---|
| `# 作品名` | 封面标题（**不进**正文标题层级、不进目录） |
| `## 1 作品概述` / `## 附录 A …` | 一级标题；`附录` 自动另起一页 |
| `### 1.1 应用场景` | 二级标题 |
| 普通段落（连续行自动合并成一段） | 正文（首行缩进 2 字符） |
| `**加粗**` · `` `行内代码` `` | 黑体加粗 · Consolas |
| `![alt](figures/xxx.png)` 独占一行 | 插图，宽 ≤ 14.5cm 居中 |
| 图下一行 `*图 2-1 说明*` | 图注（居中、10.5pt、去斜体、跟图同页） |
| `\| … \| … \|` 表格 | 带边框表格（表头自动加粗加底纹、跨页重复） |
| `- ` 列表 · `1. ` 列表 | 悬挂缩进列表（编号照抄，不自动重编号） |
| `---` | 分隔线（不渲染） |

- **标题编号照抄源文本，脚本不自动重编号**：源里写 `### 3.6` 两次就会输出两个
  3.6（当前源确实如此，见「已知问题」）。要改编号改源。
- **加插图**：图放 `docs/figures/`（生成脚本见 [figures/README.md](../../docs/figures/README.md)），
  在技术文档里写一行 `![说明](figures/xxx.png)`，下一行写 `*图 X-Y 说明*`。
  路径**相对 `docs/` 解析**，图不存在脚本直接报错退出（不会静默出个空框）。
- 源里出现本脚本不支持的写法（代码块、引用块、四级标题、图文混排的段落）会**报错退出并指出行号**，
  不会静默丢内容。真要用就先在 `md2docx.py` 里加对应分支。
- 表格宽度由内容估算列宽，整表固定 15.6cm；列很多时脚本会自动往等分靠。

## 代码包（作品重要代码）

```powershell
pwsh -File submission/build/pack_code.ps1
```

- **用途**：生成提交材料清单里的「作品重要代码」压缩包，整仓 `git archive` 导出。
- **产物**：`submission/out/DragonEyeSorter_代码_v1.0.zip`（包内前缀 `DragonEyeSorter/`）。
- **范围**：`HEAD` 上的 tracked 文件。数据集、模型权重、SD 镜像、`BOOT.bin`、编译产物
  本来就被 `.gitignore` 排除，`git archive` 天然不含它们，不需要另维护排除清单。
  **未提交的改动不进包——先提交再打包**（脚本会在工作区脏时警告）。
- **上限判定**：默认 100MB（官方 2026 应用赛道口径）。超限打印警告并以退出码 2 结束，
  不静默放过。本机实测约 25.5MB / 507 个文件，远低于上限。
- 参数：`-Out <zip 路径>`、`-MaxMB <上限>`、`-Rev <git 引用>`。

## 自检与验收

```powershell
pwsh -File submission/build/build.ps1                 # 出 docx + pdf（含生成侧自检）
python submission/build/verify_report.py             # 40 项验收自检 → out/_verify_report.txt
```

- **生成侧自检**（`md2docx.py` 每次生成后自动做）：读回 docx 统计段落数 / 表格数 /
  插图数 / H1、H2 计数 / 页眉页脚文本，扫竞赛禁项，扫全篇有没有 `fixed`/`exact`
  行距（那是标题压字的成因）。任一不过 → 退出码 1，`build.ps1` 随之失败。
- **验收自检**（`verify_report.py`，独立复核用）：把 docx 与 PDF 一起量一遍——
  结构计数与**源文件独立数出来的数**对比、正文/标题字体字号、首行缩进、两端对齐、
  行距、A4 与页边距、每张表 ≤ 版心、表头重复、插图宽度、封面无页眉页脚、
  正文页码从 1 起、TOC 域与 `updateFields`、禁项扫描、PDF 文件头 / 页数 / 页面尺寸、
  **PDF 逐页行框重叠检测**（横向 >5pt 且纵向 >3pt 判为压字）。
  结果三态：`PASS` / `FAIL` / `SKIP`（合理例外，报告里写明原因，不计失败）。
  全 PASS 时退出码 0。
- 人工看图：PDF 转 PNG 用 PyMuPDF 看封面（无校名）、目录（页码对得上正文）、
  表格页（没溢出）、图页（图注跟图同页）、末页。渲染命令示例：

  ```python
  import pymupdf
  doc = pymupdf.open("submission/out/龙瞳分拣_作品报告.pdf")
  for i, page in enumerate(doc, 1):
      page.get_pixmap(dpi=100).save("submission/out/render/page_%02d.png" % i)
  ```

- 页数取 Word 的 `ComputeStatistics(2)`（wdStatisticPages），不是估算。

## 已知问题（源侧，不改 docs/）

1. **`### 3.6` 出现两次**：`3.6 现场输入 des_io` 与 `3.6 资源`。源文件不改，
   报告按原样渲染，目录里会出现两个 3.6——**交件前需由文档负责人定夺**（改成 3.7 或并入 3.6）。
2. §10「总结与展望」正文仍是「待补。」，多处指标标「待测」——属内容侧，等实测数据。
