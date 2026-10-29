# submission/build/ —— 提交件生成链

把内容源 [技术文档.md](../技术文档.md) 转成可提交的 Word / PDF，并从仓库打代码包。**技术文档是唯一内容源**：改内容改那里，本目录只负责排版与导出，不改源文件。

常用命令：`pwsh -File submission/build/build.ps1`（报告全链）、`python submission/build/verify_report.py`（验收自检）、`pwsh -File submission/build/pack_code.ps1`（代码包）。

## 脚本清单

| 脚本 | 用途 | 入口 |
|---|---|---|
| [build.ps1](build.ps1) | 报告**一键入口**：`make_figures.py` → `md2docx.py` → `word_to_pdf.ps1` → `pdf_images_hires.py`，末尾打印产物路径 / 页数 / 大小 / 插图 dpi | `pwsh -File submission/build/build.ps1` |
| [md2docx.py](md2docx.py) | Markdown → DOCX（论文单节版式：标题块 / 一级 / 二级标题 / 正文 / 三线表 / 插图 / 图注表注 / 页脚页码），生成后自检（禁项 / 字号 / 行距 / 三线表 / 页脚） | 由 `build.ps1` 调用；可单独跑 |
| [word_to_pdf.ps1](word_to_pdf.ps1) | DOCX → PDF（Word COM；导出前更新页脚 PAGE 页码域，回报页数） | 由 `build.ps1` 调用 |
| [pdf_images_hires.py](pdf_images_hires.py) | 插图高清化：按出现顺序把 PDF 里的图像对象换回源图原生像素，图框 / 文字 / 页码不动；`--report` 只测量 | `build.ps1` 第 ③ 步；`python … --pdf <pdf> --report` |
| [verify_report.py](verify_report.py) | 验收自检（SPEC v1 论文式报告，docx + PDF + 源三方对账），结果写 `out/_verify_report.txt` | `python submission/build/verify_report.py` |
| [pack_code.ps1](pack_code.ps1) | `git archive` 打「作品重要代码」zip | `pwsh -File submission/build/pack_code.ps1` |
| [figures/](../figures/README.md) | 报告插图的生成脚本（`figkit.py` / `make_figures.py` / 每图脚本，入库）+ 生成物（PNG / SVG / `fig_meta.json`，不入库、可重建），不是 `build/` 的脚本但属报告生成链 | `python submission/figures/make_figures.py` |

报告链改完内容跑 `build.ps1` → `verify_report.py`（确认全 PASS）；`build.ps1` 第 0 步会自动按当前脚本重画插图（含占位图），改了图脚本直接重出报告即可。

## 一键命令与常用参数

```powershell
pwsh -File submission/build/build.ps1               # 四步串跑：make_figures → md2docx → word_to_pdf → pdf_images_hires
pwsh -File submission/build/build.ps1 -NoPdf        # 本机没装 Word：只出 docx（插图仍会重画）
pwsh -File submission/build/build.ps1 -NoHires      # 出 PDF 但不做高清化（只调排版用；交件前必须跑完整链）
python submission/build/verify_report.py            # 全套验收自检（SPEC §8 各项）
python submission/build/verify_report.py --final    # 交件门禁：仍有占位图残留判 FAIL
python submission/build/md2docx.py -Out <别的.docx> -Stats <别的.json>
python submission/build/md2docx.py --fig-width 14.5      # 插图整体收窄（默认 15.8cm）
python submission/build/md2docx.py --no-caption-clean    # 关掉旧图注净化（新源图注即最终文本）
pwsh -File submission/build/word_to_pdf.ps1 -InputPath a.docx -OutputPath b.pdf
python submission/build/pdf_images_hires.py --pdf submission/out/龙瞳分拣_作品报告.pdf --report
python submission/build/verify_report.py --fill --pdf <任意 PDF>   # 只跑逐页留白分析（可对比旧 PDF）
```

- `build.ps1` 任一步失败即停（退出码非 0），末尾打印产物路径 / 页数 / 大小。
- 第 0 步先跑 `make_figures.py` 重画插图（生成物不入库，见 [figures/README.md](../figures/README.md)）；缺 matplotlib 时这一步报错并提示安装（`python -m pip install matplotlib`）。
- 第 ③ 步的插图顺序取自 `out/_md2docx.json` 的 `figures`（= 正文里 `![](...)` 的出现顺序），与 PDF 内插图一一对应：数量对不上、或任一张低于 `--min-dpi`（默认 300）即非零退出，并打印哪一张不达标。加图 / 删图不用手工维护清单。

## 依赖

- **报告链**：Microsoft Word 桌面版（走 `Word.Application` COM 转 PDF；没有 Word 用 `-NoPdf` 只出 docx）；Python 3 + `python-docx`（生成 docx）；Python 3 + **PyMuPDF**（插图高清化，缺了第 ③ 步硬失败）；字体宋体 / 黑体 / Times New Roman / Consolas（系统与 Office 自带）。
- **插图生成**：Python 3 + matplotlib（`python -c "import matplotlib"`）；`build.ps1` 第 0 步用它重画插图（生成物不入库，脚本与用法见 [figures/](../figures/README.md)）。
- 安装位置、版本与逐项验证命令见 [环境与上手](../../docs/环境与上手.md) §1「需要哪些环境」。
- 脚本**不写死解释器**：`build.ps1` 按 `python` → `py -3` 在 PATH 上找，并按「该解释器有没有这个模块」分别挑（① 要 `docx`，③ 要 `pymupdf`）。要指定解释器就直接调对应的 `.py`。
- **缺 PyMuPDF 时第 ③ 步硬失败是故意的**：没有高清化的 PDF 图内文字是糊的，不能当成品交；临时跳过用 `-NoHires`。
- Word 以只读方式打开源 docx，导出后不改源文件、不弹窗；高清化**原地重写 PDF**（换图像对象，页面尺寸 / 文字 / 书签不动），文件会明显变大（插图本来的像素），仍远低于 50MB 上限。

## 产物与命名

| 产物 | 说明 | 入库 |
|---|---|---|
| `submission/out/龙瞳分拣_作品报告.docx` | 排版源，可在 Word 继续改 | ❌ 可重建 |
| `submission/out/龙瞳分拣_作品报告.pdf` | **正式提交件** | ❌ 可重建 |
| `submission/out/_md2docx.json` · `_word_to_pdf.json` · `_verify_report.txt` | 自检统计与验收结果（`_md2docx.json` 含 `figures` 顺序、`placeholders` 占位图、`captions` 图注表注文本，供第 ③ 步与验收对账） | ❌ |
| `submission/out/render/` | PDF 逐页 PNG，验收看图用 | ❌ |

交件文件名按 [提交材料清单](../提交材料清单.md) §5 改名（如 `DragonEyeSorter_龙瞳分拣_作品报告_v1.0.pdf`），不要在仓库里改产物本体名。

## 报告排版必须守住的结论

**纯论文式单节版式**（重构决策见 [log](../../docs/log/2026-10-29-报告重构纯论文式.md)；字体 / 字号 / 段距令牌集中在 `md2docx.py` 顶部「设计令牌」区，改版式先改那里）：无封面、无目录、无页眉，A4 单节到底。字体 / 字号 / 段距令牌集中在 `md2docx.py` 顶部「设计令牌」区，改版式先改那里。

- **页面**：A4 21×29.7cm，上下左右页边距 **2.4cm**（版心 16.2×24.9cm）；页脚距边 1.6cm；**单节**（全文一个 sectPr），页码从 1 起。
- **标题块**：源里唯一 `#` 标题 → 黑体 **17.5pt** 居中（参照实测 17.28pt，见下面「半磅粒度」），段前 24pt / 段后 10pt；随后连续 `**标签**：值` 行 → 元信息 **10pt**、行距 1.0、段后 1pt，标签黑体、冒号后正文；元信息最多 6 行，遇第一个空行结束。
- **一级标题（`##`）**：黑体 **14.5pt** 左对齐，段前 14 / 段后 8，与下段同页；`## 附录 A` 另起一页。
- **二级标题（`###`）**：黑体 **12pt** 左对齐，段前 12 / 段后 6，与下段同页。
- **正文**：宋体 **11pt** + 西文 Times New Roman，两端对齐，首行缩进 **2 字符 = 440 twips**，行距**单倍**，段前段后 0；行内代码 Consolas 10pt、无底纹；`**强调**` 中文黑体、西文 Times Bold。
- **行距一律倍数、不用固定值**：固定行距把行框钉死，字号大或字体回退时上一行会压住下一行，生成侧自检拒绝 `exact / atLeast / fixed`；只有表后那一行 144（0.6 倍）是故意的细空行。宋体单倍行高约 1.286×字号，正文 11pt 单倍实测行距约 13.9pt（Word 单倍≈1.286em 与参照 13.55pt 的这点差异予以接受，不压成固定值）。
- **图注 / 表注**：宋体**加粗 10pt 居中**；图注在图下一行（段前 0 / 段后 6），表注在**表格上方**（段前 6 / 段后 3）且与表同页；编号与说明之间用全角空格 `　`。
- **插图**：居中、宽 **15.8cm**（现有图按 1:1 落版），段前 8 / 段后 3，与图注同页；整表 / 整图的单块不可分割会带来结构性底部留白（见「已知问题」）。
- **三线表**：顶 / 底 1.5pt 黑、表头下 0.75pt 细线，无竖线 / 行线 / 底纹 / 斑马纹；表头宋体加粗 9pt、表体宋体 9pt；整表 15.6cm、固定布局、跨页重复表头、行不跨页、单元格左右内边距 108 twips。
- **页脚**：居中 `PAGE` 域，Times New Roman 11pt 黑字；**只有页码**，不写「第 X 页 / 共 Y 页」，也没有页眉。
- **颜色**：全篇只有黑色（`000000`），不许灰底、彩线、斑马纹。
- **半磅粒度**：OOXML 的 `w:sz` 只能存 0.5pt 的整数倍，参照实测的 17.28 / 14.35pt 存不进去——取最近值 **17.5 / 14.5pt**（与参照论文实测 17.28 / 14.35pt 差 0.22 / 0.15pt，肉眼不可分）。改字号只能落在 .0 / .5 上，别写 17.3 这种值。
- **元信息行的字距微调**：元信息某行按 10pt 一行放不下时，`md2docx.py` 按需收紧字符间距（最多 0.4pt/字）保证每条各占一行；再长就要在内容源里把该行写短，不要靠缩字号。
- **图注净化**：默认开着（旧行为），按括号删掉「生成脚本 / SVG / PPT / figures/」这类构建说明；新源图注就是最终文本，正常不会命中。要关用 `--no-caption-clean`，要改规则改 `CAPTION_KEEP_RULES` / `CAPTION_STRIP_RULES` 或 `--caption-rules <json>`。

### 与参照论文的刻意差异（三条）

参照系是 LaTeX 期刊论文（A4 / 2.4cm 边距 / 11pt 衬线 / 图注加粗 / 三线表）。本报告刻意保留三处差异：

1. **正文首行缩进 2 字符**：中文排版规范要求段首缩进；参照论文译文没有缩进是其 Markdown→LaTeX 转换器的行为，不是排版意图。
2. **一级标题保留中文序号体系、改用罗马数字**（`I.` `II.` …），图表用阿拉伯顺序号（`图 1` / `表 1`）；参照论文表号用罗马数字（`表 I`），中文报告里与章节罗马号混在一起易混，统一为阿拉伯数字。
3. **关键词用「关键词」标题**，不用参照论文的「索引术语 / Index Terms」。

## 以后怎么加章节 / 加图

加正文直接改 [技术文档.md](../技术文档.md)，重跑 `build.ps1`。脚本只认这些源写法，遇到代码块 / 引用块 / 四级标题 / 项目符号列表 / 图文混排会**报错退出并指出行号**，不会静默丢内容：

| 源里的写法 | 渲染成 |
|---|---|
| `# 论文标题`（全文唯一） | 标题块（黑体 17.5pt 居中），后面紧跟元信息行 |
| `**标签**：值`（标题后连续行，上限 6 行） | 元信息行（10pt，标签黑体，行距 1.0，段后 1pt） |
| `## I. 引言` / `## 摘要` / `## 附录 A …` | 一级标题（黑体 14.5pt；`附录` 另起一页；脚本不自动编号） |
| `### A. 应用背景与检测对象` | 二级标题（黑体 12pt） |
| 普通段落（连续行合并；段间空行） | 正文（宋体 11pt、首行缩进 2 字符、单倍行距） |
| `**强调**` · `` `行内代码` `` | 黑体（西文 Times Bold）· Consolas 10pt（无底纹） |
| `![alt](figures/fig_xxx.png)` 独占一行 | 插图，宽 15.8cm 居中；路径**相对源文件（技术文档.md）所在目录解析**（即 `submission/figures/`），图不存在直接报错 |
| 图下一行 `**图 N**　说明。` | 图注（宋体加粗 10pt 居中，跟图同页，编号与说明间全角空格，句号收尾） |
| 表格上一行 `**表 N**　说明` | 表注（宋体加粗 10pt 居中，位于表格正上方，不带句号） |
| `\| … \|` 表格（第 2 行 `\|---\|---\|`） | 三线表（9pt、无底色无竖线、跨页重复表头、行不跨页） |
| `1. ` 列表（仅参考文献 / 附录） | 黑色编号 + 悬挂缩进 1.5 字符（编号照抄，不自动重编号） |
| `---` | 分隔线（不渲染） |

- **标题编号照抄源文本**，脚本不自动编号；`##` 用 `I.` `II.` …，`###` 用 `A.` `B.` …（见上表）。
- **交叉引用**写法：`如图 1 所示`、`见表 2`、`第 III-A 节`；图 / 表编号按出现顺序全文唯一。
- **新图按落版尺寸 1:1 作图**：画布宽 447.9pt（= 15.8cm）、图内最小字号 ≥7.5pt、PNG 建议 ≥3200px 宽（满版心约 514dpi；源图 <1900px 会跌破 300dpi 门禁）。图由 [figures/](../figures/) 的脚本按 [figures/README.md](../figures/README.md) 的约定生成到 `submission/figures/`，改图后重跑 `python submission/figures/make_figures.py`（生成物不入库，`build.ps1` 也会自动重画）。
- **占位图**：文件名以 `ph_` 开头（`ph_overview.png` 等）的插图会被记进 `_md2docx.json` 的 `placeholders`；实物照片替换后只换 PNG、不动图注文本，交件前 `verify_report.py --final` 门禁要求清零。

## 代码包（作品重要代码）

`pwsh -File submission/build/pack_code.ps1` → `submission/out/DragonEyeSorter_代码_v1.0.zip`（包内前缀 `DragonEyeSorter/`）。

- 范围：`HEAD` 上的 tracked 文件（`git archive`）。数据集、模型权重、SD 镜像、`BOOT.bin`、编译产物本就被 `.gitignore` 排除，不需要另维护排除清单；**未提交的改动不进包——先提交再打包**（工作区脏时脚本警告）。
- 上限默认 100MB，超限打印警告并以**退出码 2** 结束。参数：`-Out <zip>`、`-MaxMB <上限>`、`-Rev <git 引用>`。
- 命名与交件规矩见 [提交材料清单](../提交材料清单.md) §5。

## 自检与验收

```powershell
python submission/build/verify_report.py                       # 全套验收自检（60 项） → out/_verify_report.txt
python submission/build/verify_report.py --fill --pdf <PDF>     # 只跑逐页留白分析（可对比旧 PDF）
python submission/build/pdf_images_hires.py --pdf <PDF> --report   # 只看插图 dpi
```

- **生成侧自检**（`md2docx.py` 生成后自动做）：读回 docx 复核——单节 / A4 / 页边距 2.4cm / 页脚距边 1.6cm / 页脚 PAGE 域 11pt 居中且无 NUMPAGES / 无页眉引用 / 无 TOC 域 / 标题与元信息字号与段距 / 正文 11pt·首行缩进 440·单倍行距 / 一二级标题字号 / 图注表注加粗 10pt 居中且表注在表上方 / 三线表线条与 9pt / 图宽 15.8cm / 全篇纯黑 / 无固定行距 / 禁项扫描；任一不过退出码 1。统计写 `out/_md2docx.json`（`figures` 顺序供第 ③ 步替换，`placeholders` / `captions` 供验收对账）。
- **验收自检**（`verify_report.py`，独立复核）：docx + PDF + 源 Markdown 三方对账——结构计数与 `_md2docx.json`、源文件对账；全部版式令牌；三线表（线条 / 表头重复 / 行不跨页 / 内边距 / 9pt）；插图 15.8cm 与等效 dpi ≥300、占位图清单；页脚 PAGE 域页码 1..N、无页眉、无目录域、附录前分页；PDF 字体嵌入与四族齐全、行框无重叠、无彩色；逐页留白（正文页填充 <50% FAIL）；数字守恒；禁项与生成链卫生。`--final` 模式额外要求占位图清零。结果三态 `PASS` / `FAIL` / `SKIP`，全 PASS 退出码 0。
- **留白口径**（改版前后用同一条命令才可比）：内容 = 正文文字块 + 图片块（按 y 排除页脚，版心 68.03~773.86pt）；`fill% = 内容高度 / 版心高度（24.9cm）`；底部空白 = 版心底 − 内容最低点；逐页列数值，并列出底部空白 >20% 的页与原因。
- 页数取 Word 的 `ComputeStatistics(2)`（wdStatisticPages），不是估算。人工看图：第 1 页（标题块 + 元信息 + 摘要）、图页（图注跟图同页）、表格页（不溢出、三线清晰）、末页。要逐页 PNG 时用 PyMuPDF 渲染：

```powershell
python -c "import pymupdf; d=pymupdf.open('submission/out/龙瞳分拣_作品报告.pdf'); [p.get_pixmap(dpi=110).save('submission/out/render/page_%02d.png' % i) for i, p in enumerate(d, 1)]"
```

## 已知问题（改排版前先看）

1. **Word 单倍行距比 LaTeX 略高**：正文 11pt 单倍实测约 13.9pt（≈1.26em），参照论文是 13.55pt（1.24em）。本报告接受这点差异——行距令牌按「单倍」给（`w:line=240 auto`），不压成固定值（固定值会裁字、且验收明确要求「全篇无固定行距」）。
2. **字号只有半磅粒度**：17.3 / 14.4pt 在 OOXML 里不可表示，生成侧用 17.5 / 14.5pt（见上「半磅粒度」）；验收脚本核对字号时按这个实际值。
3. **元信息行过长会收紧字距**：10pt 一行放不下时自动收紧（上限 0.4pt/字），再长就必须把源里那行写短，不要缩字号。
4. **满版心插图前的结构性留白**：下一页首块是「满版心插图 + 图注」整块（约 300~360pt、不可分割）时，上一页底部会留白；要填满只能缩小插图或往源里补正文，不建议在排版侧硬凑。
5. **孤行 / 寡行控制开着**（`widowControl=1`），个别页尾会多留 2~4 行；关掉会填得更满，但段首 / 段尾会落单，要求是打开。
6. **表格分页**：小表整表同页；大表只保证「表注 + 表头 + 前两行」同页，其余允许跨页（重复表头兜底）；行不跨页（`cantSplit`）没有布局代价。
7. **图 / 表编号顺序**：脚本照抄源文本、不自动编号；新增图 / 表要自己保证按出现顺序连续（验收脚本按源里顺序核对）。
8. **改插图后重跑生成脚本**：`python submission/figures/make_figures.py`（`build.ps1` 第 0 步自动跑；生成物不入库）；验收按 `submission/figures/fig_meta.json` 核对落版字号。
