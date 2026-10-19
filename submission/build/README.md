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
| [verify_report.py](verify_report.py) | 报告链 | **验收自检**：58 项逐条核对 docx / PDF（结构计数与 `_md2docx.json`、源文件三方对账、新版式口径的字体 / 字号 / 行距 / 缩进、**设计令牌**（章标题青绿左色块、表头浅青灰底、三线表、图注编号加粗）、宽度 / 页眉页脚 / 禁项 / TOC、**插图等效 dpi ≥ 300**、**插图落版字号 ≥ 7pt**、**逐页留白与空白页分析**、PDF 逐页重叠检测），结果写 `out/_verify_report.txt` | `python submission/build/verify_report.py` |
| [make_ppt.py](make_ppt.py) | PPT 链（同事 B 维护） | 答辩 PPT 生成器：`docs/参赛材料骨架.md` §4 → `out/龙瞳分拣_答辩.pptx`（16:9、16 页浅色编辑风、13 种版式；设计令牌在 `C_*` / `SIZE_*`；插图整图原生像素嵌入，重点框取 `fig_meta.json` 的 `regions`）。生成后读回自检（尺寸 / 页数 / 备注 / 禁项 / 主机路径 / 越界 / 文本溢出估算 / 插图 dpi / 页脚安全区），并写 `out/_ppt_images.json`（插图清单 + theme）供导出链用 | `python submission/build/make_ppt.py` |
| [export_ppt_preview.ps1](export_ppt_preview.ps1) | PPT 链（同事 B 维护） | PowerPoint COM 把 pptx 导成 `out/龙瞳分拣_答辩.pdf` + `out/ppt_preview/slide_XX.png`（现场兜底件 + 排版自查），随后调 `ppt_pdf_hires.py` 做插图回填与逐页版面度量；需要 Windows + 桌面版 PowerPoint | `pwsh -File submission/build/export_ppt_preview.ps1` |
| [ppt_pdf_hires.py](ppt_pdf_hires.py) | PPT 链（同事 B 维护） | **PPT 兜底 PDF 插图回填 + 体检**：PowerPoint 导 PDF 只有约 200 dpi，这里按 `_ppt_images.json` 的「页号 + 放置矩形」回填源图原生像素（回填后 308~432 dpi，门禁 ≥300 dpi）；并从 `manifest.theme` 读底色量逐页版面（头档 / 底档 / 填充率，`--report` / `--json`） | 由 `export_ppt_preview.ps1` 调用；`python submission/build/ppt_pdf_hires.py --pdf <pdf> --report` |

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
| 中文字体 微软雅黑 / Consolas | 正文、标题与行内代码 | 系统自带（Word 自带微软雅黑），无需安装 |

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

与答辩 PPT 同一套设计语言：**白底、墨色文字、青绿强调、琥珀警示、灰蓝次级**，
只用 python-docx 能表达的东西（段落边框 / 底纹 / 字色），不引第三方库。
设计令牌集中在 `md2docx.py` 顶部（`C_*` 颜色、`FONT_*` 字体、`*_PT` 字号），
改版式先改那里。A4、上下左右页边距 2.6cm（版心 15.8cm），封面 / 目录 / 正文三节：

- **设计令牌**：#0E1B2B 墨（标题 / 正文）、#0D9488 青绿（章标题色块、列表标记、表头分隔线、
  封面短线）、#B45309 琥珀（警示 / 待测，留给后续强调）、#5F7285 灰蓝（页眉页脚、图注）、
  #DCE3EA 细线、#E6F4F2 表头浅青灰底、#F6F9FA 斑马纹、#F1F6F5 行内代码底纹。
  中文统一**微软雅黑**（标题靠字重拉层级，不再混黑体 / 宋体两种中文字体），
  数字与行内代码用 Consolas。
- **封面**：白底 + 青绿短发短线，自上而下「赛事 / 赛道信息（小号灰字 + 字距）→
  「作品报告」青绿标签 → 中文主标题 / 英文名 / 技术副标题三层 → 底部细线 +
  参赛队 / 作品编号 / 年月」。不出现校名校徽与指导老师字样。封面独立成节，
  **无页眉页脚**（`md2docx.py` 每次生成后扫一遍全文，命中竞赛禁项即退出码 1）。
- **目录**：Word 域 `TOC \o "1-3" \h \z \u`，并在 `settings.xml` 里置
  `w:updateFields=true`，Word 打开时自动更新；`word_to_pdf.ps1` 导出前再显式
  `$doc.Fields.Update()` + 目录域 `Update()`，保证 PDF 里目录页码与正文一致。
  「目录」标题与章标题同一套青绿色块语言（CONTENTS 小标签 + 青绿左色块 + 下细线）。
  **TOC 1 / TOC 2 / TOC 3 样式单独设**（`TOC_STYLES`：单倍行距、右对齐点线制表位在版心
  右边界；字号 / 段后距由 `plan_toc_styles()` 按章 / 节条目数挑档位）——
  微软雅黑行高偏大，36 条目录很容易撑成两页；档位算的是「一页装得下」，
  不这么设会出现「目录第二页只有几条」甚至只有页眉的空白目录页。
  样式名与 styleId 用 Word 内置的 `toc 1` / `TOC1`（并去掉 `w:customStyle`），
  Word 更新 TOC 域时才认这几个样式。
- **标题**：一级标题左对齐、**左侧 4pt 青绿竖色块 + 下细线**（16pt 加粗，章号青绿、
  标题墨色）；二级标题墨色 13pt 加粗；标题**与下段同页**（`keepNext` + `keepLines`，
  不落在页底）、段前 20 / 10pt、段后 10 / 6pt、行距 1.18 倍。
- **正文**：微软雅黑 10.5pt、首行缩进 2 字符（420 twips）、两端对齐、**孤行 / 寡行控制开**
  （`widowControl=1`）、段前 0 / 段后 6pt；列表用青绿 `•` / 序号标记 + 悬挂缩进
  （左缩进 840 twips / 悬挂 420 twips，标记与正文首行缩进对齐）。
  **行距 1.3 倍**——原因见下节「行距为什么是 1.3 倍」。
- **表格**：**三线表**——表头浅青灰底（`E6F4F2`）+ 墨色粗体 + 青绿表头分隔线；
  表体只画 `#DCE3EA` 细横线（上粗 1.5pt、下细 0.75pt、**无竖线**），正文行可选浅斑马纹；
  整表 15.6cm 按版心、固定布局（`tblLayout=fixed`）、跨页重复表头（`tblHeader`）、
  **行不跨页**（`cantSplit`）；小表整表同页、大表至少表头 + 前两行同页。
  列宽按内容估算（数值 / 短串居中，长文字左对齐），表头行最小高度 0.81cm。
- **插图**：居中、**宽 = 满版心 15.8cm**（`FIG_MAX_CM`）；源里图下一行的 `*斜体*` 图注
  渲染成居中、9pt 灰字、**编号加粗墨色**（「图 2-1」与说明拉开层级）；
  图片段 `keepNext` + 图注段 `keepLines`，保证图注跟图同页。
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
- **页眉页脚**：页眉左「龙瞳分拣 · 塑胶传动件在线质检分拣系统」+ 右标签（目录页「目录」、
  正文页「作品报告」，青绿 8.5pt）+ 下细线；页脚「第 X 页 / 共 Y 页」用 PAGE / NUMPAGES 域
  （页码墨色加粗、其余灰字）+ 上细线；封面无页眉页脚，正文页码从 1 起。
- **附录**：`## 附录 …` 段前分页（`pageBreakBefore`），不在半页处起附录。
- **行内代码** `` `…` `` 用 Consolas 9.5pt + 极浅底纹（`#F1F6F5`）+ 青绿深字色；
  `**加粗**` 用微软雅黑加粗。

### 行距为什么是 1.3 倍，而不用固定的 1.5 倍 / 固定值

两个坑叠在一起，改排版时别踩回去：

1. **固定行距（`lineRule="exact"` / `fixed`）**会把行框钉死在指定高度：字号一大
   （标题 16pt）或字体回退到字面更高的字形时，上一行会**盖住**下一行。因此全篇只用
   倍数行距（`lineRule="auto"`），自检里拒绝 `exact / atLeast / fixed`。
2. **微软雅黑的单倍行高约为字号的 1.72 倍**（Word 实测：10.5pt 单倍行距 = 18pt 行距，
   由字体自身行高指标决定，和宋体约 1.3 倍不同）。同一段正文：
   「微软雅黑 10.5pt × 1.5 倍行距」= **27pt 行距**，明显过松、读起来像双倍行距。所以
   正文行距取 **1.3 倍**（10.5 × 1.72 × 1.3 ≈ 23.4pt），与常见中文报告的视觉密度一致；
   `md2docx.py` 里的 `MS_YAHEI_LINE = 1.72` 就是这条换算的唯一出处。改字体或改字号要
   一起重算行距；目录条目的 `plan_toc_styles()` 也用这个系数估算一页装不装得下。

## 以后怎么加章节 / 加图

**加正文**：直接改 [docs/技术文档.md](../../docs/技术文档.md)，重跑 `build.ps1`。
脚本按源里的写法识别（这是**唯一**要遵守的约定）：

| 源里的写法 | 渲染成 |
|---|---|
| `# 作品名` | 封面标题（**不进**正文标题层级、不进目录） |
| `## 1 作品概述` / `## 附录 A …` | 一级标题（青绿左色块 + 下细线）；`附录` 自动另起一页 |
| `### 1.1 应用场景` | 二级标题（墨色 13pt 加粗） |
| 普通段落（连续行自动合并成一段） | 正文（微软雅黑 10.5pt、首行缩进 2 字符） |
| `**加粗**` · `` `行内代码` `` | 微软雅黑加粗 · Consolas + 极浅底纹 |
| `![alt](figures/xxx.png)` 独占一行 | 插图，宽 = 满版心 15.8cm 居中 |
| 图下一行 `*图 2-1 说明*` | 图注（居中、9pt 灰字、编号加粗、跟图同页、过一遍净化规则） |
| `\| … \| … \|` 表格 | 三线表（表头浅青灰底 + 粗体 + 青绿分隔线，无竖线，跨页重复表头） |
| `- ` 列表 · `1. ` 列表 | 青绿标记 + 悬挂缩进列表（编号照抄，不自动重编号） |
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
python submission/build/verify_report.py             # 58 项验收自检 → out/_verify_report.txt
python submission/build/verify_report.py --fill --pdf <PDF>   # 只跑逐页留白分析（可对比旧 PDF）
python submission/build/pdf_images_hires.py --pdf <PDF> --report   # 只看插图 dpi
```

- **生成侧自检**（`md2docx.py` 每次生成后自动做）：读回 docx 统计段落数 / 表格数 /
  插图数 / H1、H2 计数 / 页眉页脚文本，扫竞赛禁项，扫全篇有没有 `fixed`/`exact`
  行距（那是标题压字的成因），并核对 `_md2docx.json` 的统计字段完整
  （`paragraphs / tables / images / headings / forbidden_hits / figures`，`figures` 顺序
  供第 ③ 步按图替换）。任一不过 → 退出码 1，`build.ps1` 随之失败。
- **验收自检**（`verify_report.py`，独立复核用）：把 docx 与 PDF 一起量一遍——
  结构计数与 `_md2docx.json` 对账（H1 / 表 / 图再与**源文件独立数出来的数**对比，
  只给下限兜底、不写死数字，内容源改章表数量不会误伤）、新版式口径的正文 / 标题字体字号、
  行距、首行缩进、列表缩进、**设计令牌**（章标题青绿左色块、表头浅青灰底、三线表无竖线、
  图注编号加粗、列表青绿标记、页眉右标签）、A4 与页边距、每张表 ≤ 版心 / 固定布局 / 表头重复、
  插图宽度（满版心 15.8cm）、封面无页眉页脚、正文页码从 1 起、TOC 域与 `updateFields`、
  禁项扫描、PDF 文件头 / 页数（与 `_word_to_pdf.json` 对账）/ 页面尺寸、
  **PDF 逐页行框重叠检测**（横向 >5pt 且纵向 >3pt 判为压字）、
  **插图等效 dpi ≥ 300**（逐张打印实测值；量法与 `pdf_images_hires.py` 共用同一份代码）、
  **正文页不被撑空**（填充 < 50% 判 FAIL）、**封面 / 目录各 1 页且无整页空白页**，
  并打印**逐页留白表**。结果三态：`PASS` / `FAIL` / `SKIP`（合理例外，报告里写明原因，
  不计失败）。全 PASS 时退出码 0。
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

1. **节号唯一性**：新增 / 调整 `###` 时保证编号唯一；目录按编号生成，重复会出现两个
   同名条目。
2. 多处指标仍标「待测」——属内容侧，等实测数据。
3. **满版心插图前的结构性留白**（版式实测：正文第 3 / 11 / 17 页
   底部空白 22.1% / 23.5% / 40.5%）。成因是**下一页首块 = 满版心插图 + 图注**
   这一整块（约 300~360pt，图片不可分割）本页放不下，只能整块挪走；
   这几页正文填充仍 ≥ 59%（没跌破 50% 硬门槛）。要填满就得把插图缩到 13cm 以下
   （图内小字更糊）或往源里补一段正文（内容侧定），都不建议在排版侧硬凑。
4. **附录前留白 31.4%**（正文第 21 页）：`## 附录` 按惯例段前分页，属排版约定，
   不是漏排。想压掉可以去掉 `md2docx.py` 里的 `pageBreakBefore`，代价是附录从半页
   中间开始——交件前由内容负责人定。
5. **孤行 / 寡行控制的代价**：开着 `widowControl`，个别页尾会多留 2~4 行（实测在
   20% 阈值内）；关掉那几页会填得更满，但段首 / 段尾单行会落单。要求是「打开」，保留。
6. **表格分页控制**：小表（估算高度 ≤260pt）靠行内 `keepNext` 串成整表同页；
   大表只保证「表头 + 前两行」同页，其余允许跨页（有重复表头兜底）。
   行不跨页（`cantSplit`）没有布局代价：整行挪到下一页，不会在行中间被截断。
7. **版式语言（与答辩 PPT 同一套）**：微软雅黑 10.5pt + **1.3 倍行距**（微软雅黑
   视觉等效 1.5 倍宋体，见上文），封面 / 目录 / 标题 / 表格 / 图注 / 页眉页脚统一
   设计令牌。当前产出 24 页 = 封面 1 + 目录 1 + 正文 22，无整页空白页；页数随内容源
   长度变化，以最终 build 为准。
8. **插图**：5 张图按 15.8cm 落版 1:1 作图（图内最小字号 7.5pt），PNG 宽 3200px、
   落版约 514dpi；报告与 PPT 共用同一批图，改图后必须重跑 `make_figures.py`，第 12 项
   自检按 `docs/figures/fig_meta.json` 核对「落版字号 ≥7.5pt」。PDF 约 4.48MB（远低于
   50MB 上限）。
