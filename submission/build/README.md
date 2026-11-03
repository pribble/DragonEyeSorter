# submission/build/ —— 提交件生成链

把内容源 [技术文档.md](../技术文档.md) 转成可提交的 Word / PDF，并从仓库打代码包。**技术文档是唯一内容源**：改内容改那里，本目录只负责排版与导出，不改源文件。

常用命令：`pwsh -File submission/build/build.ps1`（报告全链）、`python submission/build/verify_report.py`（验收自检）、`pwsh -File submission/build/pack_code.ps1`（代码包）。

## 脚本清单

| 脚本 | 用途 | 入口 |
|---|---|---|
| [build.ps1](build.ps1) | 报告**一键入口**：`make_figures.py` → `md2docx.py` → `word_to_pdf.ps1` → `pdf_images_hires.py`，末尾打印产物路径 / 页数 / 大小 / 插图 dpi | `pwsh -File submission/build/build.ps1` |
| [md2docx.py](md2docx.py) | Markdown → DOCX（单节版式：标题块 / 一~三级标题 / 正文 / 三线表（列宽与对齐可由分隔行提示）/ 插图 / 图注表注 / 页眉官方 logo / 页脚页码），生成后自检（禁项 / 字号 / 行距 / 三线表 / 页眉页脚） | 由 `build.ps1` 调用；可单独跑 |
| [word_to_pdf.ps1](word_to_pdf.ps1) | DOCX → PDF（Word COM；导出前更新页脚 PAGE 页码域，回报页数） | 由 `build.ps1` 调用 |
| [pdf_images_hires.py](pdf_images_hires.py) | 插图高清化：按出现顺序把 PDF 里的图像对象换回源图原生像素，图框 / 文字 / 页码不动；`--report` 只测量 | `build.ps1` 第 ③ 步；`python … --pdf <pdf> --report` |
| [verify_report.py](verify_report.py) | 验收自检（官方模板五部分报告，docx + PDF + 源三方对账），结果写 `out/_verify_report.txt` | `python submission/build/verify_report.py` |
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
- 第 ③ 步的插图顺序取自 `out/_md2docx.json` 的 `figures`（= 正文里 `![](...)` 的出现顺序），与 PDF 内**正文插图**一一对应（页眉 logo 按「出现在 ≥ 一半页面」自动豁免，不参与对账也不替换）：数量对不上、或任一张低于 `--min-dpi`（默认 300）即非零退出，并打印哪一张不达标。加图 / 删图不用手工维护清单。

## 依赖

- **报告链**：Microsoft Word 桌面版（走 `Word.Application` COM 转 PDF；没有 Word 用 `-NoPdf` 只出 docx）；Python 3 + `python-docx`（生成 docx）；Python 3 + **PyMuPDF**（插图高清化，缺了第 ③ 步硬失败）；字体楷体 / 黑体 / Times New Roman / Consolas（系统与 Office 自带）。
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

交件文件名按 [提交材料清单](../提交材料清单.md) §5 改名（如 `基于FPGA的工业智能检测系统_技术文档_v1.0.pdf`），不要在仓库里改产物本体名。

## 报告排版必须守住的结论

**官方模板五部分单节版式**（结构重排决策见 [log](../../docs/log/2026-11-01-技术文档按官方模板重排.md)，字体 / 行距规格见 [log](../../docs/log/2026-11-02-技术文档楷体版式.md)；字号 / 段距令牌集中在 `md2docx.py` 顶部「设计令牌」区，改版式先改那里）：无封面、无目录，页眉为官方模板 logo（`figures/logo_header.png`，入库），A4 单节到底。

- **页面**：A4 21×29.7cm，上下左右页边距 **2.4cm**（版心 16.2×24.9cm）；页眉 logo 距边 1.2cm、页脚距边 1.6cm；**单节**（全文一个 sectPr），页码从 1 起。
- **字体底色**：中文正文一律**楷体**（正文 / 二·三级章节标题 / 图注表注 / 表格 / 元信息值），作品名、元信息标签与**一级章节标题**用**黑体**（Windows 楷体没有粗体字面，一级标题只有换黑体才真正变粗），西文与数字一律 **Times New Roman**，行内代码 Consolas（作者 11-02 确认保留等宽）；全篇只有这四族。
- **标题块**：源里唯一 `#` 标题（= 官网报名作品名）→ 黑体 **17.5pt** 居中（标题块不随楷体规格变），段前 24pt / 段后 10pt；随后连续 `**标签**：值` 行 → 元信息 **10pt**、行距 1.0、段后 1pt，标签黑体、冒号后楷体；元信息最多 6 行，遇第一个空行结束。
- **章节标题（`##` / `###` / `####`）**：**一级标题黑体 15pt 居中加粗、2 倍行距**（楷体无粗体字面，`<w:b/>` 对楷体 run 在 Word 导 PDF 时不生效，换黑体才真变粗）；**二级 / 三级标题楷体四号 14pt 加粗、2 倍行距、左对齐**；层级靠字体 / 字号 / 对齐与段距区分——一级段前段后 14/8、二级 12/6、三级 10/5，均与下段同页。
- **加粗是合成粗体**（11-03 实测）：Windows 楷体没有粗体字面，Word 导 PDF 时用**合成粗体**实现 `w:b`——同一片段画两遍（fill + stroke），描边 ≈ **0.029 em**（9pt→0.257pt、10.5→0.302、12→0.343、14→0.401、15→0.429）。所以二 / 三级标题、图注表注、正文 `**强调**` 的加粗**可见但偏细**，共存于同一字体名（`KaiTi` / `SimHei`），嵌入字体里没有 `-Bold` 字面。**测法**：PyMuPDF 的 span `flags` / 字体列表测不出合成粗，要看 `page.get_texttrace()` 里 `type=1` 的描边 span。要让标题明显变粗，唯一有效手段是换**黑体**（一级标题即如此）；别为"看着不粗"去改 `w:b`。
- **正文**：楷体**小四 12pt** + 西文 Times New Roman，两端对齐，首行缩进 **2 字符 = 480 twips**，行距 **1.5 倍**，段前段后 0；行内代码 Consolas 11pt、无底纹（表格内降 9pt）；`**强调**` 中文楷体加粗、西文 Times Bold；`__强调__` 为楷体下划线（关键结论句用，11-03 起）。
- **行距一律倍数、不用固定值**：固定行距把行框钉死，字号大或字体回退时上一行会压住下一行，生成侧自检拒绝 `exact / atLeast / fixed`；全篇倍数只允许 **240（单倍，标题块 / 元信息 / 图注表注 / 表格 / 页眉页脚）/ 360（正文与列表 1.5 倍）/ 480（章节标题 2 倍）**，另有表后那一行 144（0.6 倍）是故意的细空行。楷体单倍行高系数与宋体相近（≈1.29×字号），只用于估算、不作判定。
- **图注 / 表注**：楷体**加粗五号 10.5pt 居中**；图注在图下一行（段前 0 / 段后 6），表注在**表格上方**（段前 6 / 段后 3）且与表同页；编号与说明之间用全角空格 `　`。
- **插图**：居中、宽 **15.8cm**（现有图按 1:1 落版），段前 8 / 段后 3，与图注同页；整表 / 整图的单块不可分割会带来结构性底部留白（见「已知问题」）。
- **三线表**：顶 / 底 1.5pt 黑、表头下 0.75pt 细线，无竖线 / 行线 / 底纹 / 斑马纹；表头**楷体**加粗 9pt、表体**楷体** 9pt（维持 9pt 是防宽表撑爆，作者 11-02 确认）；整表 15.6cm、固定布局、跨页重复表头、行不跨页、单元格左右内边距 108 twips；**单元格垂直顶对齐**（多行不再上下居中撑高，行高只按内容）。列宽与列内对齐可由表格分隔行控制（见「以后怎么加章节 / 加图」的源写法表）；分隔行没写权重提示时走自动列宽。
- **页脚**：居中 `PAGE` 域，Times New Roman 11pt 黑字；**只有页码**，不写「第 X 页 / 共 Y 页」。
- **页眉**：官方模板每页的「芯辰大海 FPGA 点亮未来」logo——`figures/logo_header.png`（官方 docx 模板里的原始素材，564×103，白底入库），居中 6.5cm，下方 0.75pt 细线；页眉里不能有文字（生成侧与验收两处把关）。Word 导出会把 logo 重采样到 96 dpi（与正文插图同病），第 ③ 步的 `--header` 用**原地改写图像对象**的方式把它换回源像素（564×103 ≈ 221 dpi，官方素材的分辨率上限）——不能走 `Page.replace_image`，它会在页面资源里留孤儿对象并打乱插图对账。
- **颜色**：全篇只有黑色（`000000`），不许灰底、彩线、斑马纹（页眉 logo 图除外，它是模板原件）。
- **半磅粒度**：OOXML 的 `w:sz` 只能存 0.5pt 的整数倍。当前令牌 17.5 / 15 / 14 / 12 / 10.5 / 11 / 9 都可表示；改字号只能落在 .0 / .5 上，别写 17.3 这种值。
- **stylesWithEffects 部件**：python-docx 默认模板带 `word/stylesWithEffects.xml`（仅 Word 2010 读取的旧式样式副本），其 docDefaults 是 Word 默认 `after=200 / line=276`，`setup_doc_defaults` 够不到它——`md2docx.py` 保存后用 `sync_effects_part()` 原地重写成单倍，否则「规格外行距」检查会扫出 276。
- **元信息行的字距微调**：元信息某行按 10pt 一行放不下时，`md2docx.py` 按需收紧字符间距（最多 0.4pt/字）保证每条各占一行；再长就要在内容源里把该行写短，不要靠缩字号。
- **图注净化**：默认开着（旧行为），按括号删掉「生成脚本 / SVG / PPT / figures/」这类构建说明；新源图注就是最终文本，正常不会命中。要关用 `--no-caption-clean`，要改规则改 `CAPTION_KEEP_RULES` / `CAPTION_STRIP_RULES` 或 `--caption-rules <json>`。

### 与参照论文的刻意差异（两条）

参照系原是 LaTeX 期刊论文（A4 / 2.4cm 边距 / 11pt 衬线 / 图注加粗 / 三线表）；10-31 起结构对齐官方模板，11-02 起字体 / 字号 / 行距按作者给定规格改为楷体系（沿革见上述两条 log），下列两条结构差异仍成立：

1. **正文首行缩进 2 字符**：中文排版规范要求段首缩进；参照论文译文没有缩进是其 Markdown→LaTeX 转换器的行为，不是排版意图。
2. **图表用阿拉伯顺序号（`图 1` / `表 1`）**，章节用官方模板的「第 X 部分 / 1.1 / 2.2.1」编号；参照论文表号用罗马数字，中文报告里与章节号混排易混，统一为阿拉伯数字。（原第三条差异「关键词标题」随官方模板无关键词位而取消。）

## 以后怎么加章节 / 加图

加正文直接改 [技术文档.md](../技术文档.md)，重跑 `build.ps1`。脚本只认这些源写法，遇到代码块 / 引用块 / 五级标题 / 项目符号列表 / 图文混排会**报错退出并指出行号**，不会静默丢内容：

| 源里的写法 | 渲染成 |
|---|---|
| `# 作品名称`（全文唯一，= 官网报名作品名） | 标题块（黑体 17.5pt 居中，不随楷体规格变），后面紧跟元信息行 |
| `**标签**：值`（**仅**标题后、第一条正文前的连续行，上限 6 行） | 元信息行（10pt，标签黑体、值楷体，行距 1.0，段后 1pt） |
| 正文段行首 `**标签**：说明…`（如 `**舵机总线主机**：机械臂…`） | 正文段：行首 `**标签**` 为粗体引导词 + 全角冒号，**不再报错**；元信息行只认「标题后、第一条正文前」的连续行，同形不同位 |
| `## 摘要` / `## 第一部分　作品概述` / `## 第五部分　参考文献` | 一级标题（**黑体 15pt 居中加粗、2 倍行距**；脚本不自动编号） |
| `### 1.1 功能与特性` | 二级标题（楷体四号 14pt 加粗、2 倍行距、左对齐） |
| `#### 2.2.1 硬件整体介绍` | 三级标题（楷体四号 14pt 加粗、2 倍行距、左对齐） |
| 普通段落（连续行合并；段间空行） | 正文（楷体小四 12pt、首行缩进 2 字符、1.5 倍行距） |
| `**强调**` · `` `行内代码` `` | 楷体加粗（西文 Times Bold）· Consolas 11pt（无底纹，表格内 9pt） |
| `__强调__` | 楷体**下划线**（西文 Times 下划线、不加粗）：正文关键结论句用，11-03 起；表格内同样生效 |
| `![alt](figures/fig_xxx.png)` 独占一行 | 插图，宽 15.8cm 居中；路径**相对源文件（技术文档.md）所在目录解析**（即 `submission/figures/`），图不存在直接报错 |
| 图下一行 `**图 N**　说明。` | 图注（楷体加粗 10.5pt 居中，跟图同页，编号与说明间全角空格，句号收尾） |
| 表格上一行 `**表 N**　说明` | 表注（楷体加粗 10.5pt 居中，位于表格正上方，不带句号） |
| `\| … \|` 表格（第 2 行分隔行） | 三线表（楷体 9pt、无底色无竖线、跨页重复表头、行不跨页、单元格顶对齐） |
| 分隔行写提示：`\|:---\|:-------\|:------------------\|` | **列宽权重 + 列内对齐**：破折号个数 = 该列相对宽度（按整表 15.6cm 归一化），冒号 = 对齐（`:---` 左 / `:---:` 中 / `---:` 右）。破折号个数全表相同时回退自动列宽；没写冒号的列用默认对齐（首列左、其余短串居中 / 长文左）。上例 = 3:7:18；`\|:---\|:---:\|:---:\|` 表示第 1 列窄且左对齐、后两列等宽且居中。每列另有「最长不可断 token + 内边距」硬下限，长型号 / 路径不会被拆开 |
| `1. ` 列表（仅参考文献 / 附录） | 黑色编号 + 悬挂缩进 1.5 字符（编号照抄，不自动重编号） |
| `---` | 分隔线（不渲染） |

- **标题编号照抄源文本**，脚本不自动编号；`##` 用「第一部分」体例，`###` / `####` 用 `1.1` / `2.2.1`（见上表）。
- **交叉引用**写法：`如图 1 所示`、`见表 2`、`2.3.2 节`；图 / 表编号按出现顺序全文唯一。
- **新图按落版尺寸 1:1 作图**：画布宽 447.9pt（= 15.8cm）、图内最小字号 ≥7.5pt、PNG 建议 ≥3200px 宽（满版心约 514dpi；源图 <1900px 会跌破 300dpi 门禁）。图由 [figures/](../figures/) 的脚本按 [figures/README.md](../figures/README.md) 的约定生成到 `submission/figures/`，改图后重跑 `python submission/figures/make_figures.py`（生成物不入库，`build.ps1` 也会自动重画）。
- **占位图 / 实物图项**：文件名以 `ph_` 开头（现为图 2 / 7 / 11~19 共 11 张 `ph_*`）的插图会被记进 `_md2docx.json` 的 `placeholders`；实物 / 截图替换后只换 PNG、不动图注文本，交件前 `verify_report.py --final` 门禁要求全部 `replaced: true`（报告内 11 张；另有 2 项实物照片不进报告，见 [figures/placeholder_manifest.json](../figures/placeholder_manifest.json) 的 `in_report=false`）。

## 代码包（作品重要代码）

`pwsh -File submission/build/pack_code.ps1` → `submission/out/DragonEyeSorter_代码_v1.0.zip`（包内前缀 `DragonEyeSorter/`）。

- 范围：`HEAD` 上的 tracked 文件（`git archive`）。数据集、模型权重、SD 镜像、`BOOT.bin`、编译产物本就被 `.gitignore` 排除，不需要另维护排除清单；**未提交的改动不进包——先提交再打包**（工作区脏时脚本警告）。
- 上限默认 100MB，超限打印警告并以**退出码 2** 结束。参数：`-Out <zip>`、`-MaxMB <上限>`、`-Rev <git 引用>`。
- 命名与交件规矩见 [提交材料清单](../提交材料清单.md) §5。

## 自检与验收

```powershell
python submission/build/verify_report.py            # 全套验收自检（65 项） → out/_verify_report.txt
python submission/build/verify_report.py --fill --pdf <PDF>     # 只跑逐页留白分析（可对比旧 PDF）
python submission/build/pdf_images_hires.py --pdf <PDF> --report   # 只看插图 dpi
```

- **生成侧自检**（`md2docx.py` 生成后自动做）：读回 docx 复核——单节 / A4 / 页边距 2.4cm / 页眉距边 1.2cm / 页脚距边 1.6cm / 页脚 PAGE 域 11pt 居中且无 NUMPAGES / 页眉恰 1 张 logo 图且无文字 / 无 TOC 域 / 标题与元信息字号与段距 / 正文 12pt·首行缩进 480·1.5 倍行距 / 章节标题一级黑体 15pt 居中 / 二·三级楷体 14pt 左对齐·均 2 倍行距 / 图注表注加粗 10.5pt 居中且表注在表上方 / 三线表线条与楷体 9pt·单元格顶对齐 / 图宽 15.8cm / 全篇纯黑 / 无固定行距 / 禁项扫描；表格另查两项——有破折号权重提示时实测列宽与权重偏差 >8% 判失败（长 token 下限顶开属规则内让位，只告警）、单元格估算 >6 行告警（提示源里写太长）；任一不过退出码 1。统计写 `out/_md2docx.json`（`figures` 顺序供第 ③ 步替换，`placeholders` / `captions` 供验收对账，`table_warnings` 记表格告警）。
- **验收自检**（`verify_report.py`，独立复核）：docx + PDF + 源 Markdown 三方对账——官方模板骨架（标题 = 官网报名作品名、H1 摘要+五部分、H2 1.1~4.2、H3 2.2.x/2.3.x/3.2.x）与模板字数上限（摘要 800，不计「关键词」行 / 1.1~1.3 400 / 1.4~1.6 200 / 4.1 300 / 4.2 1000）；结构计数与 `_md2docx.json`、源文件对账；全部版式令牌（一级标题黑体 15pt 居中、二·三级楷体 14pt 左对齐、正文楷体 12pt 1.5 倍、标题 2 倍口径见上）；三线表（线条 / 表头重复 / 行不跨页 / 内边距 / 楷体 9pt）；插图 15.8cm 与等效 dpi ≥300、占位图清单；页脚 PAGE 域页码 1..N、页眉仅 logo、无目录域、参考文献 1~20 篇；PDF 字体嵌入与四族（楷体 / 黑体 / Times / Consolas）齐全、行框无重叠、无彩色；逐页留白（正文页填充 <50% FAIL）；数字守恒；禁项与生成链卫生。`--final` 模式额外要求占位图清零。结果三态 `PASS` / `FAIL` / `SKIP`，全 PASS 退出码 0。
- **留白口径**（改版前后用同一条命令才可比）：内容 = 正文文字块 + 图片块（按 y 排除页脚，版心 68.03~773.86pt）；`fill% = 内容高度 / 版心高度（24.9cm）`；底部空白 = 版心底 − 内容最低点；逐页列数值，并列出底部空白 >20% 的页与原因。
- 页数取 Word 的 `ComputeStatistics(2)`（wdStatisticPages），不是估算。人工看图：第 1 页（标题块 + 元信息 + 摘要）、图页（图注跟图同页）、表格页（不溢出、三线清晰）、末页。要逐页 PNG 时用 PyMuPDF 渲染：

```powershell
python -c "import pymupdf; d=pymupdf.open('submission/out/龙瞳分拣_作品报告.pdf'); [p.get_pixmap(dpi=110).save('submission/out/render/page_%02d.png' % i) for i, p in enumerate(d, 1)]"
```

## 已知问题（改排版前先看）

1. **行距按规格给倍数，不压固定值**：11-02 规格定了正文 1.5 倍、章节标题 2 倍（`w:line=360 / 480 auto`），其余维持单倍——一律倍数、不用固定值（固定值会裁字，且验收明确要求「全篇无固定行距」，白名单 240/360/480）。
2. **字号只有半磅粒度**：17.3 / 14.4pt 在 OOXML 里不可表示（见上「半磅粒度」）；验收脚本核对字号时按落版的实际值。
3. **元信息行过长会收紧字距**：10pt 一行放不下时自动收紧（上限 0.4pt/字），再长就必须把源里那行写短，不要缩字号。
4. **满版心插图前的结构性留白**：下一页首块是「满版心插图 + 图注」整块（约 300~360pt、不可分割）时，上一页底部会留白；要填满只能缩小插图或往源里补正文，不建议在排版侧硬凑。
5. **孤行 / 寡行控制开着**（`widowControl=1`），个别页尾会多留 2~4 行；关掉会填得更满，但段首 / 段尾会落单，要求是打开。
6. **表格分页**：小表整表同页；大表只保证「表注 + 表头 + 前两行」同页，其余允许跨页（重复表头兜底）；行不跨页（`cantSplit`）没有布局代价。
7. **表格列宽提示与长单元格**：分隔行破折号个数 = 列宽权重（全表相同时回退自动列宽）；每列「最长不可断 token + 内边距」是硬下限，长型号 / 路径列会比权重更宽（生成侧只告警，不算失败）。单元格估算超过 6 行说明源里写得太长——靠列宽硬撑会让表高失控，应精简源文本（删限定语、拆成「（1）…」或改表），不要缩字号。源里写了冒号就按冒号对齐（`:---` 左 / `:---:` 中 / `---:` 右），没写的列按默认（首列左、其余短串居中 / 长文左）。
8. **图 / 表编号顺序**：脚本照抄源文本、不自动编号；新增图 / 表要自己保证按出现顺序连续（验收脚本按源里顺序核对）。
9. **改插图后重跑生成脚本**：`python submission/figures/make_figures.py`（`build.ps1` 第 0 步自动跑；生成物不入库）；验收按 `submission/figures/fig_meta.json` 核对落版字号。
10. **正文引导词与元信息行同形**：都以行首 `**标签**：` 开头，只能靠位置区分——「标题后、第一条正文前」的连续行是元信息行（≤6 行、遇空行结束），其余一律按正文段渲染（行首粗体引导词 + 全角冒号），不再报错。我们的文档标题后紧跟 `## 摘要`，所以实际没有元信息行。
