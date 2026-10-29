# submission/build/ —— 提交件生成链

把内容源 [技术文档.md](../技术文档.md) 转成可提交的 Word / PDF，并从仓库打代码包。**技术文档是唯一内容源**：改内容改那里，本目录只负责排版与导出，不改源文件。

常用命令：`pwsh -File submission/build/build.ps1`（报告全链）、`python submission/build/verify_report.py`（验收自检）、`pwsh -File submission/build/pack_code.ps1`（代码包）。

## 脚本清单

| 脚本 | 用途 | 入口 |
|---|---|---|
| [build.ps1](build.ps1) | 报告**一键入口**：`md2docx.py` → `word_to_pdf.ps1` → `pdf_images_hires.py`，末尾打印产物路径 / 页数 / 大小 / 插图 dpi | `pwsh -File submission/build/build.ps1` |
| [md2docx.py](md2docx.py) | Markdown → DOCX（封面 / 目录域 / 正文 / 表格 / 插图 / 图注净化），生成后自检（禁项 / 行距 / 计数） | 由 `build.ps1` 调用；可单独跑 |
| [word_to_pdf.ps1](word_to_pdf.ps1) | DOCX → PDF（Word COM；导出前更新页眉页脚与目录域，回报页数） | 由 `build.ps1` 调用 |
| [pdf_images_hires.py](pdf_images_hires.py) | 插图高清化：按出现顺序把 PDF 里的图像对象换回源图原生像素，图框 / 文字 / 页码不动；`--report` 只测量 | `build.ps1` 第 ③ 步；`python … --pdf <pdf> --report` |
| [verify_report.py](verify_report.py) | 验收自检 59 项（见「自检与验收」），结果写 `out/_verify_report.txt` | `python submission/build/verify_report.py` |
| [pack_code.ps1](pack_code.ps1) | `git archive` 打「作品重要代码」zip | `pwsh -File submission/build/pack_code.ps1` |
| [figures/](../figures/README.md) | 报告插图的生成脚本与生成物（`figkit.py` / `make_figures.py` / 5 个 `fig_*.py` + PNG / SVG / `fig_meta.json`），不是 `build/` 的脚本但属报告生成链 | `python submission/figures/make_figures.py` |

报告链改完内容跑 `build.ps1` → `verify_report.py`（确认全 PASS）；改了插图脚本再跑 `python submission/figures/make_figures.py` 后重出报告。

## 一键命令与常用参数

```powershell
pwsh -File submission/build/build.ps1               # 三步串跑：md2docx → word_to_pdf → pdf_images_hires
pwsh -File submission/build/build.ps1 -NoPdf        # 本机没装 Word：只出 docx
pwsh -File submission/build/build.ps1 -NoHires      # 出 PDF 但不做高清化（只调排版用；交件前必须跑完整链）
python submission/build/md2docx.py -Out <别的.docx> -Stats <别的.json>
python submission/build/md2docx.py --fig-width 14.5     # 插图整体收窄（默认满版心 15.8cm）
python submission/build/md2docx.py --no-caption-clean   # 关掉图注净化
pwsh -File submission/build/word_to_pdf.ps1 -InputPath a.docx -OutputPath b.pdf
python submission/build/pdf_images_hires.py --pdf submission/out/龙瞳分拣_作品报告.pdf --report
python submission/build/verify_report.py --fill --pdf <任意 PDF>   # 只跑逐页留白分析（可对比旧 PDF）
```

- `build.ps1` 任一步失败即停（退出码非 0），末尾打印产物路径 / 页数 / 大小。
- 第 ③ 步的插图顺序取自 `out/_md2docx.json` 的 `figures`（= 正文里 `![](...)` 的出现顺序），与 PDF 内插图一一对应：数量对不上、或任一张低于 `--min-dpi`（默认 300）即非零退出，并打印哪一张不达标。加图 / 删图不用手工维护清单。

## 依赖

- **报告链**：Microsoft Word 桌面版（走 `Word.Application` COM 转 PDF；没有 Word 用 `-NoPdf` 只出 docx）；Python 3 + `python-docx`（生成 docx）；Python 3 + **PyMuPDF**（插图高清化，缺了第 ③ 步硬失败）；字体宋体 / 黑体 / Times New Roman / Consolas（系统与 Office 自带）。
- **插图生成**：Python 3 + matplotlib（`python -c "import matplotlib"`），脚本与生成物都在 [figures/](../figures/)。
- 安装位置、版本与逐项验证命令见 [环境与上手](../../docs/环境与上手.md) §1「需要哪些环境」。
- 脚本**不写死解释器**：`build.ps1` 按 `python` → `py -3` 在 PATH 上找，并按「该解释器有没有这个模块」分别挑（① 要 `docx`，③ 要 `pymupdf`）。要指定解释器就直接调对应的 `.py`。
- **缺 PyMuPDF 时第 ③ 步硬失败是故意的**：没有高清化的 PDF 图内文字是糊的，不能当成品交；临时跳过用 `-NoHires`。
- Word 以只读方式打开源 docx，导出后不改源文件、不弹窗；高清化**原地重写 PDF**（换图像对象，页面尺寸 / 文字 / 书签 / 目录不动），文件会明显变大（插图本来的像素），仍远低于 50MB 上限。

## 产物与命名

| 产物 | 说明 | 入库 |
|---|---|---|
| `submission/out/龙瞳分拣_作品报告.docx` | 排版源，可在 Word 继续改 | ❌ 可重建 |
| `submission/out/龙瞳分拣_作品报告.pdf` | **正式提交件** | ❌ 可重建 |
| `submission/out/_md2docx.json` · `_word_to_pdf.json` · `_verify_report.txt` | 自检统计与验收结果 | ❌ |
| `submission/out/render/` | PDF 逐页 PNG，验收看图用 | ❌ |

交件文件名按 [提交材料清单](../提交材料清单.md) §5 改名（如 `DragonEyeSorter_龙瞳分拣_作品报告_v1.0.pdf`），不要在仓库里改产物本体名。

## 报告排版必须守住的结论

学术报告版式：**黑白灰、无装饰色**。颜色 / 字体 / 字号等设计令牌集中在 `md2docx.py` 顶部，改版式先改那里。

- **版面**：A4，页边距上下左右 2.6cm（版心 15.8cm）；封面 / 目录 / 正文三节，封面无页眉页脚、目录 1 页、正文页码从 1 起。
- **字体字号**：正文宋体 10.5pt、首行缩进 2 字符、两端对齐；一 / 二 / 三级标题黑体 16 / 13 / 11.5pt、左对齐、无装饰线、与下段同页；西文与数字 Times New Roman；行内代码 Consolas 9.5pt 无底纹；`**强调**` 的中文走黑体。
- **行距 1.5 倍、不用固定值**：固定行距把行框钉死，字号大或字体回退时上一行会压住下一行，自检拒绝 `exact / atLeast / fixed`；宋体单倍行高约为字号的 1.286 倍（10.5pt 单倍 = 13.5pt），`LINE_FACTOR = 1.286` 是这条换算的唯一出处，改字体或字号要一起重算。
- **表格**：三线表——顶 / 底线 1.5pt 黑、表头下 0.75pt 细线，无竖线 / 行线 / 底纹 / 斑马纹；表头黑体 9pt、表体宋体 9pt；整表 15.6cm、固定布局、跨页重复表头、行不跨页。
- **插图**：居中、宽 = 满版心 15.8cm；图下一行 `*图 X-Y 说明*` 渲染为居中 9pt 宋体黑字，图注跟图同页。
- **页眉页脚**：页眉左作品名 + 右标签（目录页「目录」、正文页「作品报告」），页脚「第 X 页 / 共 Y 页」；封面无页眉页脚。生成后扫禁项（校名 / 校徽 / 指导老师），命中即退出码 1。
- **目录**：Word TOC 域（`TOC \o "1-3" \h \z \u`）+ `w:updateFields=true`，导出 PDF 前显式更新域；全部目录条目必须一页装下（否则会出现只有几条的第二页目录或整页空白的目录页），TOC 样式名用 Word 内置的 `toc 1` / `TOC1`。

## 以后怎么加章节 / 加图

加正文直接改 [技术文档.md](../技术文档.md)，重跑 `build.ps1`。脚本只认这些源写法，遇到代码块 / 引用块 / 四级标题 / 图文混排会**报错退出并指出行号**，不会静默丢内容：

| 源里的写法 | 渲染成 |
|---|---|
| `# 作品名` | 封面标题（不进正文层级、不进目录） |
| `## 1 作品概述` / `## 附录 A …` | 一级标题（黑体 16pt；`附录` 另起一页） |
| `### 1.1 应用场景` | 二级标题（黑体 13pt） |
| 普通段落（连续行合并） | 正文（宋体 10.5pt、首行缩进 2 字符） |
| `**强调**` · `` `行内代码` `` | 黑体 · Consolas（无底纹） |
| `![alt](figures/fig_xxx.png)` 独占一行 | 插图，宽 = 满版心 15.8cm 居中；路径**相对源文件（技术文档.md）所在目录解析**（即 `submission/figures/`），图不存在直接报错 |
| 图下一行 `*图 2-1 说明*` | 图注（居中 9pt 黑字，跟图同页，过一遍净化规则） |
| `\| … \|` 表格 | 三线表（无底色无竖线，跨页重复表头） |
| `- ` / `1. ` 列表 | 黑色标记 + 悬挂缩进（编号照抄，不自动重编号） |
| `---` | 分隔线（不渲染） |

- **标题编号照抄源文本**，脚本不自动重编号；新增 `###` 要保证编号唯一（目录按编号生成，重复会出现两个同名条目）。
- **新图按落版尺寸 1:1 作图**：画布宽 447.9pt（= 15.8cm）、图内最小字号 ≥7.5pt、PNG 建议 ≥3200px 宽（满版心约 514dpi；源图 <1900px 会跌破 300dpi 门禁）。图由 [figures/](../figures/) 的脚本按 [figures/README.md](../figures/README.md) 的约定生成到 `submission/figures/`，改图后重跑 `python submission/figures/make_figures.py`（入库 PNG + SVG + `fig_meta.json`）。
- **图注净化**：图注里的构建说明（`figures/`、`make_*.py`、`.svg`、`生成` 等）在生成 docx 时自动删掉，源 Markdown 不必手改；要调整改 `md2docx.py` 的 `CAPTION_KEEP_RULES` / `CAPTION_STRIP_RULES`，或用 `--caption-rules <json>`、`--no-caption-clean`。

## 代码包（作品重要代码）

`pwsh -File submission/build/pack_code.ps1` → `submission/out/DragonEyeSorter_代码_v1.0.zip`（包内前缀 `DragonEyeSorter/`）。

- 范围：`HEAD` 上的 tracked 文件（`git archive`）。数据集、模型权重、SD 镜像、`BOOT.bin`、编译产物本就被 `.gitignore` 排除，不需要另维护排除清单；**未提交的改动不进包——先提交再打包**（工作区脏时脚本警告）。
- 上限默认 100MB，超限打印警告并以**退出码 2** 结束。参数：`-Out <zip>`、`-MaxMB <上限>`、`-Rev <git 引用>`。
- 命名与交件规矩见 [提交材料清单](../提交材料清单.md) §5。

## 自检与验收

```powershell
python submission/build/verify_report.py                       # 59 项验收自检 → out/_verify_report.txt
python submission/build/verify_report.py --fill --pdf <PDF>     # 只跑逐页留白分析（可对比旧 PDF）
python submission/build/pdf_images_hires.py --pdf <PDF> --report   # 只看插图 dpi
```

- **生成侧自检**（`md2docx.py` 生成后自动做）：读回 docx 统计段落 / 表格 / 插图 / H1、H2 计数与页眉页脚文本，扫禁项与 `fixed` / `exact` 行距，核对 `_md2docx.json` 字段完整（`figures` 顺序供第 ③ 步按图替换）；任一不过退出码 1。
- **验收自检**（`verify_report.py`，独立复核）：docx 与 PDF 一起量——结构计数与 `_md2docx.json`、源文件三方对账；字体 / 字号 / 行距 / 缩进；三线表、图注、页眉页脚、页码、目录域；A4 与页边距；**插图等效 dpi ≥300**、插图落版最小字号 ≥7pt；PDF 逐页行框重叠检测（横向 >5pt 且纵向 >3pt 判压字）；**正文页填充 <50% 判 FAIL**（末页除外）；封面 / 目录各 1 页且无整页空白。结果三态 `PASS` / `FAIL` / `SKIP`，全 PASS 退出码 0。
- **留白口径**（改版前后用同一条命令才可比）：内容 = 正文文字块 + 图片块（按 y 排除页眉页脚，版心 73.7~768.2pt）；`fill% = 内容高度 / 版心高度（24.5cm）`；底部空白 = 版心底 − 内容最低点；逐页列数值，并列出底部空白 >20% 的页与原因。
- 页数取 Word 的 `ComputeStatistics(2)`（wdStatisticPages），不是估算。人工看图：封面（无校名）、目录（页码对得上正文）、表格页（不溢出）、图页（图注跟图同页）、末页。要逐页 PNG 时用 PyMuPDF 渲染：

```powershell
python -c "import pymupdf; d=pymupdf.open('submission/out/龙瞳分拣_作品报告.pdf'); [p.get_pixmap(dpi=110).save('submission/out/render/page_%02d.png' % i) for i, p in enumerate(d, 1)]"
```

## 已知问题（改排版前先看）

1. **节号唯一性**：新增 / 调整 `###` 要保证编号唯一，目录按编号生成。
2. **满版心插图前的结构性留白**：下一页首块是「满版心插图 + 图注」整块（约 300~360pt、不可分割）时，上一页底部会留白（实测 25.4%，正文填充仍 ≥74%）；要填满只能缩小插图或往源里补正文，不建议在排版侧硬凑。
3. **孤行 / 寡行控制开着**（`widowControl=1`），个别页尾会多留 2~4 行（在 20% 阈值内）；关掉会填得更满，但段首 / 段尾会落单，要求是打开。
4. **表格分页**：小表整表同页；大表只保证「表头 + 前两行」同页，其余允许跨页（重复表头兜底）；行不跨页（`cantSplit`）没有布局代价。
5. **页数随内容源长度变化，以最终 build 为准**；多处指标仍标「待测」属内容侧。
6. **改插图后重跑生成脚本**：`python submission/figures/make_figures.py`（脚本与生成物同在 `submission/figures/`）；第 12 项自检按 `submission/figures/fig_meta.json` 核对落版字号。
