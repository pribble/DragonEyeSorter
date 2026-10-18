# submission/build/ —— 提交件生成链

把内容源 [docs/技术文档.md](../../docs/技术文档.md) 转成可提交的 Word 与 PDF，
并从仓库打代码包。**技术文档是唯一内容源**：改内容改那里，本目录只负责排版与导出，
不改源文件。

```powershell
pwsh -File submission/build/build.ps1        # 报告：md → docx → pdf（含插图高清化）
```

## 本目录脚本清单

| 脚本 | 归属 | 用途 | 入口 |
|---|---|---|---|
| [build.ps1](build.ps1) | 报告链 | **一键入口**：`md2docx.py` → `word_to_pdf.ps1` → `pdf_images_hires.py`，打印产物路径 / 页数 / 大小 / 插图 dpi | `pwsh -File submission/build/build.ps1` |
| [md2docx.py](md2docx.py) | 报告链 | Markdown → DOCX：封面、目录域、正文排版、表格、插图、**图注净化**；生成后自检（禁项 / 行距 / 计数） | 由 `build.ps1` 调用；也可单独 `python submission/build/md2docx.py` |
| [word_to_pdf.ps1](word_to_pdf.ps1) | 报告链 | DOCX → PDF（Word COM），导出前更新页脚与目录域，回报页数 | 由 `build.ps1` 调用；也可单独跑 |
| [pdf_images_hires.py](pdf_images_hires.py) | 报告链 | **插图高清化**：按出现顺序把 PDF 里每张插图的图像对象换回源图（图框不变），等效 dpi 96 → 514~515；`--report` 只测量体检 | 由 `build.ps1` 第 ③ 步调用；`python submission/build/pdf_images_hires.py --pdf <pdf> --report` |
| [pack_code.ps1](pack_code.ps1) | 报告链 | `git archive` 打「作品重要代码」zip，超 100MB 报警并非零退出 | `pwsh -File submission/build/pack_code.ps1` |
| [verify_report.py](verify_report.py) | 报告链 | **验收自检**：49 项逐条核对 docx / PDF（结构、字体、行距、宽度、页眉页脚、禁项、TOC、**插图等效 dpi ≥ 300**、**插图落版字号 ≥ 7pt**、**逐页留白分析**、PDF 逐页重叠检测），结果写 `out/_verify_report.txt` | `python submission/build/verify_report.py` |
| [make_ppt.py](make_ppt.py) | PPT 链（同事 B 维护） | 答辩 PPT 骨架：`docs/参赛材料骨架.md` §4 → `out/龙瞳分拣_答辩.pptx`；含版面度量与门禁（顶/头/底档、填充率、表格满版心、放大面板字号）、密集图放大裁切 | `python submission/build/make_ppt.py` |
| [export_ppt_preview.ps1](export_ppt_preview.ps1) | PPT 链（同事 B 维护） | PowerPoint COM 把 pptx 导成 `out/龙瞳分拣_答辩.pdf` + `out/ppt_preview/slide_XX.png`（现场兜底件 + 排版自查），随后调 `ppt_pdf_hires.py` | `pwsh -File submission/build/export_ppt_preview.ps1` |
| [ppt_pdf_hires.py](ppt_pdf_hires.py) | PPT 链（同事 B 维护） | **PPT 兜底 PDF 插图高清化**：PowerPoint 只导 200 dpi，这里按源图原生像素回填到 736~810 dpi；兼做 dpi 体检与逐页版面墨迹度量（`--report` / `--json`） | 由 `export_ppt_preview.ps1` 调用；`python submission/build/ppt_pdf_hires.py --pdf <pdf> --report` |

**改完报告内容要跑**：`build.ps1`（出 docx + pdf + 高清插图）→ `verify_report.py`（确认全 PASS）。
只改 PPT 就跑 PPT 链那两个，与报告链互不影响（两条链写的是不同产物）。

## 一键跑完

```powershell
pwsh -File submission/build/build.ps1
```

三步串起来，任一步失败即停（退出码非 0），末尾打印产物路径 / 页数 / 大小：

| 步骤 | 脚本 | 做什么 |
|---|---|---|
| 1/3 | [md2docx.py](md2docx.py) | Markdown → DOCX（封面 / 目录 / 正文排版，python-docx） |
| 2/3 | [word_to_pdf.ps1](word_to_pdf.ps1) | DOCX → PDF（Word COM，导出前更新页脚与目录域） |
| 3/3 | [pdf_images_hires.py](pdf_images_hires.py) | PDF 插图换回源图分辨率（Word 导出只有 96 dpi，这里提到 ~514 dpi） |

第 ③ 步的插图顺序取自 `out/_md2docx.json` 的 `figures` 字段（= 正文里 `![](...)` 的出现顺序），
与 PDF 内插图**一一对应**：数量对不上、或任何一张低于 `--min-dpi`（默认 300），脚本非零退出，
`build.ps1` 随之失败并打印是哪一张不达标。

常用参数：

```powershell
pwsh -File submission/build/build.ps1 -NoPdf     # 本机没装 Word 时只出 docx
pwsh -File submission/build/build.ps1 -NoHires   # 出 PDF 但不做插图高清化（只调排版时用；交件前必须跑完整链）
python submission/build/md2docx.py -Out <别的.docx> -Stats <别的.json>
python submission/build/md2docx.py --fig-width 14.5      # 插图整体收窄（默认满版心 15.8cm）
python submission/build/md2docx.py --no-caption-clean    # 关掉图注净化
pwsh -File submission/build/word_to_pdf.ps1 -InputPath a.docx -OutputPath b.pdf
python submission/build/pdf_images_hires.py --pdf submission/out/龙瞳分拣_作品报告.pdf --report
python submission/build/verify_report.py -Out submission/out/_verify_report.txt
python submission/build/verify_report.py --fill --pdf <任意 PDF>   # 只跑逐页留白分析（可比旧 PDF）
```

## 依赖

| 依赖 | 用途 | 怎么验证 |
|---|---|---|
| Microsoft Word（桌面版） | 转 PDF；脚本走 `Word.Application` COM | `New-Object -ComObject Word.Application` 不报错；本机实测 Word 16 |
| Python 3 + python-docx | 生成 docx（第 ① 步） | `python -m pip show python-docx`（本机 1.2.0） |
| **Python 3 + PyMuPDF** | **插图高清化（第 ③ 步）** | `python -c "import pymupdf"`（本机 1.28.2）；没有就 `python -m pip install pymupdf` |
| 中文字体 宋体 / 黑体 / Consolas | 正文与标题、行内代码 | 系统自带，无需安装 |

- 脚本里**不写死解释器路径**：`build.ps1` 按 `python` → `py -3` 在 PATH 上找，
  并且按「该解释器有没有这个模块」分别挑（第 ① 步要 `docx`，第 ③ 步要 `pymupdf`）。
  要指定解释器就直接调对应的 `.py`。
- **缺 PyMuPDF 时第 ③ 步硬失败**（`build.ps1` 随之非零退出），提示装包或换解释器；
  这是故意的：没有高清化的 PDF 图内文字是糊的，不该当成品交出去。临时跳过用 `-NoHires`。
  单独跑 `pdf_images_hires.py` 时，它会自己探测 PATH 上哪个解释器有 PyMuPDF 并打印出来。
- **只需要 Python 就能出 docx**；PDF 那一步非 Word 不可（本机没有 LibreOffice / pandoc）；
  高清化那一步只用 PyMuPDF，不需要 Word。
- Word 用只读方式打开源 docx，导出后不改动源文件；不弹窗、全程隐藏。
- 高清化是**原地重写 PDF**（`--inplace`）：换的是图像对象，页面尺寸、文字、书签、目录都不动；
  代价是文件从 ~0.8MB 涨到 ~3MB（插图本来的像素），远低于 50MB 上限。

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
  **TOC 1 / TOC 2 / TOC 3 样式单独设**（`TOC_STYLES`：单倍行距、段后 4 / 2pt、
  章条 12pt 黑体加粗、节条 11pt 宋体、右对齐点线制表位在版心右边界）——
  不这么设，条目会继承正文 `Normal` 的 1.5 倍行距 + 段后距，36 条目录能占两页、
  第二页只填一半。样式名与 styleId 用 Word 内置的 `toc 1` / `TOC1`（并去掉
  `w:customStyle`），Word 更新 TOC 域时才认这几个样式。
- **正文**：宋体 12pt（小四）、首行缩进 2 字符、两端对齐、**1.5 倍行距**、
  **孤行 / 寡行控制开**（`widowControl=1`，避免段首或段尾单行被甩到相邻页）；
  标题黑体 16 / 14 / 12pt（三级加粗）、不缩进、**与下段同页**（`keepNext` +
  `keepLines`，标题不会孤零零留在页尾）、段前 8~12pt、段后 6pt。
- **表格**：四周与内部框线、表头加粗 + 浅灰底纹（`D9D9D9`）、正文 10.5pt（五号）、
  整表 15.6cm 不超版心、跨页重复表头、**行不跨页**（`cantSplit`：整行挪到下一页，
  不会在行中间被拦腰截断）；列宽按内容自适应（数值 / 短串列居中）。
- **插图**：居中、**宽 = 满版心 15.8cm**（`FIG_MAX_CM = TEXT_W_CM`）；源里图下一行的
  `*斜体*` 图注渲染成居中、宋体 10.5pt、**去斜体**；图片段 `keepNext` + 图注段
  `keepLines`，保证图注跟图同页。
  想窄一点：单张用 `FIG_WIDTH_OVERRIDE`（按文件名给 cm），整篇用 `--fig-width <cm>`。
  为什么要满版心：Word 导出 PDF 会把插图重采样到 **96 dpi**（3200px 原图只剩 547~597px），
  图框越大像素丢得越多、图内小字越糊；第 ③ 步再把像素换回源图。
- **图注净化**（`md2docx.py` 生成侧，源 Markdown 不动）：图注里的**构建说明**是写给
  维护者的，不该出现在提交报告里。规则按括号逐组判断，`CAPTION_KEEP_RULES` 优先于
  `CAPTION_STRIP_RULES`：

  | 括号内容 | 处置 | 例 |
  |---|---|---|
  | 命中 keep：`src/`、`docs/`、`实测 / 测量 / 取自 / 默认配置 / 按比例 / 示意` | **保留**（有证据价值） | `（管脚取自 src/constraint/io.adc）`、`（俯视，按比例）`、`（示意，时长取默认配置）` |
  | 命中 strip：`make_*.py`、`figures/`、`.svg`、`SVG`、`PPT`、`md2docx / build.ps1 / make_ppt`、`生成` | **删掉**（构建说明） | `（figures/make_figures.py 生成，SVG 可直接放进 PPT）` |
  | 都不命中 | 原样保留（保守，宁可不删） | `（v11）` |

  改规则：改 `md2docx.py` 里那两个元组，或 `--caption-rules <json>` 传
  `{"strip": [...], "keep": [...]}`，或 `--no-caption-clean` 整体关闭。
  每次生成会把删掉的括号打印出来（也写进 `_md2docx.json` 的 `caption_cleanups`）。
- **页眉页脚**：正文页眉「龙瞳分拣 · 塑胶传动件在线质检分拣系统」带下框线；
  页脚「第 X 页 / 共 Y 页」用 PAGE / NUMPAGES 域；封面无页眉页脚，正文页码从 1 起。
- **附录**：`## 附录 …` 段前分页（`pageBreakBefore`），不在半页处起附录。
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
| `![alt](figures/xxx.png)` 独占一行 | 插图，宽 = 满版心 15.8cm 居中 |
| 图下一行 `*图 2-1 说明*` | 图注（居中、10.5pt、去斜体、跟图同页、过一遍净化规则） |
| `\| … \| … \|` 表格 | 带边框表格（表头自动加粗加底纹、跨页重复） |
| `- ` 列表 · `1. ` 列表 | 悬挂缩进列表（编号照抄，不自动重编号） |
| `---` | 分隔线（不渲染） |

- **标题编号照抄源文本，脚本不自动重编号**：源里写 `### 3.6` 两次就会输出两个
  3.6（当前源确实如此，见「已知问题」）。要改编号改源。
- **加插图**：图放 `docs/figures/`（生成脚本见 [figures/README.md](../../docs/figures/README.md)），
  在技术文档里写一行 `![说明](figures/xxx.png)`，下一行写 `*图 X-Y 说明*`。
  路径**相对 `docs/` 解析**，图不存在脚本直接报错退出（不会静默出个空框）。
  **加图/删图后第 ③ 步会自动跟上**：插图顺序来自 `_md2docx.json` 的 `figures`，
  数量与 PDF 对不上就报错退出（不会错位替换、也不会静默跳过）。
  新图建议也是 ≥ 3200px 宽的 PNG（满版心 15.8cm 时 3200px ≈ 514 dpi；源图小于 1900px
  就会跌破 300 dpi 门槛，第 ③ 步会失败并告诉你差多少）。**新图必须按落版尺寸 1:1 作图**：
  画布宽 447.9pt = 15.8cm、最小字号 ≥ 7.5pt（见 [figures/README.md](../../docs/figures/README.md)），
  否则会出现"清晰度够但字太小"——`verify_report.py` 的第 12 项会按 `fig_meta.json` 拦下来。
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
pwsh -File submission/build/build.ps1                 # 出 docx + pdf + 高清插图（含生成侧自检）
python submission/build/verify_report.py             # 49 项验收自检 → out/_verify_report.txt
python submission/build/verify_report.py --fill --pdf <PDF>   # 只跑逐页留白分析（可对比旧 PDF）
python submission/build/pdf_images_hires.py --pdf <PDF> --report   # 只看插图 dpi
```

- **生成侧自检**（`md2docx.py` 每次生成后自动做）：读回 docx 统计段落数 / 表格数 /
  插图数 / H1、H2 计数 / 页眉页脚文本，扫竞赛禁项，扫全篇有没有 `fixed`/`exact`
  行距（那是标题压字的成因）。任一不过 → 退出码 1，`build.ps1` 随之失败。
- **验收自检**（`verify_report.py`，独立复核用）：把 docx 与 PDF 一起量一遍——
  结构计数与**源文件独立数出来的数**对比、正文/标题字体字号、首行缩进、两端对齐、
  行距、A4 与页边距、每张表 ≤ 版心、表头重复、插图宽度（满版心 15.8cm）、封面无页眉页脚、
  正文页码从 1 起、TOC 域与 `updateFields`、禁项扫描、PDF 文件头 / 页数 / 页面尺寸、
  **PDF 逐页行框重叠检测**（横向 >5pt 且纵向 >3pt 判为压字）、
  **插图等效 dpi ≥ 300**（逐张打印实测值；量法与 `pdf_images_hires.py` 共用同一份代码）、
  **正文页不被撑空**（填充 < 50% 判 FAIL），并打印**逐页留白表**。
  结果三态：`PASS` / `FAIL` / `SKIP`（合理例外，报告里写明原因，不计失败）。
  全 PASS 时退出码 0。
- **留白口径**（`--fill`，改版前后用同一条命令对比才可比）：内容 = 正文文字块 + 图片块
  （按 y 排除页眉页脚，版心 73.7~768.2pt）；`fill% = 内容高度 / 版心高度(24.5cm)`；
  底部空白 = 版心底 − 内容最低点。表里逐页给数值，并列出底部空白 > 20% 的页与原因
  （下一页首块是什么：满版心插图是整块不可分割，放不下就会把上一页顶出空白）。
- 人工看图：PDF 转 PNG 用 PyMuPDF 看封面（无校名）、目录（页码对得上正文）、
  表格页（没溢出）、图页（图注跟图同页）、末页。渲染命令示例：

  ```python
  import pymupdf
  doc = pymupdf.open("submission/out/龙瞳分拣_作品报告.pdf")
  for i, page in enumerate(doc, 1):
      page.get_pixmap(dpi=110).save("submission/out/render/page_%02d.png" % i)
  ```

- 页数取 Word 的 `ComputeStatistics(2)`（wdStatisticPages），不是估算。
- **插图清晰度怎么看**（不看数字也能确认）：把同一张图在旧 PDF / 新 PDF 里按图内同一
  相对区域裁剪、放大到同样像素宽并排，图内小字应当从糊变清。参考图见
  `out/render/dpi_compare_*.png`（不入库，可重建）。

## 已知问题与权衡（源侧不改 docs/）

1. **`### 3.6` 出现两次**：源里曾有 `3.6 现场输入 des_io` 与 `3.6 资源`，报告按原样渲染，
   目录里会出现两个 3.6——**交件前需由文档负责人定夺**。当前源已是 3.6 / 3.7。
2. §10「总结与展望」正文仍是「待补。」，多处指标标「待测」——属内容侧，等实测数据。
3. **满版心插图前的结构性留白**（正文第 2 页 22.3%、第 4 页 32.4%、第 9 页 21.5%；
   末页 31.2% 属正常收尾）。成因是**下一页首块 =
   章/节标题 + 满版心插图 + 图注**这一整块（约 300~360pt，图片不可分割），本页剩下的
   空间放不下它，只能整块挪走。要填满这几页得把插图缩到 6~11cm 宽（比满版心小
   30%~60%，图内小字反而更小），或往源里补一段正文（内容侧定）。
4. **孤行 / 寡行控制的代价**（A/B 实测）：打开后正文中段 5 页的页尾余量从 1~2% 变成
   9~13%（每页多留 2~4 行，仍在 20% 阈值内）；关掉它们那几页会填满，但段首 / 段尾单行
   会落单。要求是「打开」，所以保留。
5. **表格行不跨页（`cantSplit`）没有布局代价**：A/B 实测关掉它逐页填充率一模一样
   （这些行本来就没被切开），所以留着当保险。
6. **改版前后（留白口径，`--fill` 同一条命令）**：总页 19 → 18（目录 2 页压到 1 页），
   正文 16 页不变；末页填充 24.6% → 66.8%，正文第 13 页 25.5% → 5.0%；
   正文空白合计 1264pt → 1278pt（分布变了，量几乎没变——中段页尾余量增加是孤行控制的代价，
   见上一条）。
7. **插图重画（2026-10-18，写字号太小那次）**：5 张图从「16in 画布 → 缩到 15.8cm」改成
   「按 15.8cm 落版 1:1 作图」，图内字号 3.1pt → 7.5pt（[figures/README](../../docs/figures/README.md)）。
   连带影响：① 插图高宽比变了（图 2-1 9.9 → 10.6cm 高），逐页留白分布随之变化
   （正文第 4 页 39.1% → 32.4%，第 2/9 页基本不变）；② PDF 从 2.96MB 涨到 4.50MB
   （新图线条更细、抗锯齿像素更多，仍远低于 50MB 上限）；③ 第 12 项自检按
   `docs/figures/fig_meta.json` 核对「落版字号 ≥7pt」，改图后必须重跑 `make_figures.py`。
