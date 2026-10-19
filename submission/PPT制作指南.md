# PPT 制作指南（决赛答辩用）

> 页纲唯一源是 [参赛材料骨架](../docs/参赛材料骨架.md) §4；本文件只讲**怎么做**：环境要什么、生成链怎么跑、设计规范与自检验收怎么落地。
> 数字一律来自 [测试与验证](../docs/测试与验证.md) 与 `docs/log/`，未实测标「待测 ⚠」；封面与内页**不出现学校名称与指导老师姓名**（见 [提交材料清单](提交材料清单.md) §6）。
> 生成链：`submission/build/make_ppt.py` → `submission/out/龙瞳分拣_答辩.pptx` → PowerPoint COM 导出 PDF 与逐页 PNG（再回填插图、量版面）。
> 本文件以 `submission/build/` 下的脚本为准；脚本改了先同步这里，两处不一致时按脚本执行。

## 1 概览

| 项 | 内容 |
|---|---|
| 用途 | 决赛现场「作品介绍演示」；初赛官方只要求作品报告 + 演示视频，同一份 PPT 作备用 |
| 页数 / 画幅 | 16 页，16:9（13.333 × 7.5 in，≈960 × 540 pt） |
| 页纲 | [参赛材料骨架](../docs/参赛材料骨架.md) §4 的 15 页，另把「数据流与时间基准」拆成独立一页 |
| 内容实现 | 全部在 `submission/build/make_ppt.py` 的 `slide_specs()`；13 种版式在 `BUILDERS` 分派 |
| 数字来源 | [测试与验证](../docs/测试与验证.md)「已实测数据汇总」；口径与定性结论见 [技术文档](../docs/技术文档.md) |
| 插图来源 | [docs/figures/](../docs/figures/) 下由 `make_figures.py` 生成的 PNG，不手工画 |
| 禁项 | 不出现学校名称、校徽、指导老师姓名；幻灯片正文不写仓库路径（出处进 speaker notes） |
| 可重跑 | 页面内容与版式只在脚本里改，重跑即一致；PowerPoint 只做最后的视觉抛光与动效 |

## 2 需要什么环境

这条链分三段，每段的环境要求不同：

1. **生成 pptx**（Python）：画页面、嵌插图、写备注、读回自检。
2. **导出 PDF / 逐页 PNG**（Windows + 桌面版 PowerPoint）：走 COM 自动化；**仅 Windows 可用**。
3. **插图回填与版面度量**（Python + PyMuPDF）：把 PDF 里的插图换回源图原生像素，并量逐页墨迹。

三段之外还要求两个中文字体。

机器相关的安装位置与外部依赖统一在 [环境与上手](../docs/环境与上手.md) §1「需要哪些环境」表维护（换机器先照那张表配齐）；本节只写这条链要什么、怎么验证、在哪一步用。

| 要什么 | 从哪拿 | 怎么验证 | 哪一步要 |
|---|---|---|---|
| **Python 3 + python-pptx + Pillow** | Python 3 从 python.org 安装；仓库不锁定解释器路径，PATH 上的 `python` 即可。`python -m pip install python-pptx Pillow` | `python -c "import pptx, PIL; print(pptx.__version__, PIL.__version__)"` | **生成**：`make_ppt.py` 画页面、嵌图、写备注、读回自检 |
| **PowerShell 7（`pwsh`）+ Windows** | PowerShell 7 官方安装包（或 `winget install Microsoft.PowerShell`）；COM 自动化只在 Windows 上可用 | `pwsh -v` | **导出**：`export_ppt_preview.ps1` 的入口；非 Windows 上导不了 PDF / PNG |
| **Microsoft PowerPoint 桌面版（COM）** | 随 Microsoft 365 / Office 桌面版权安装；必须注册 `PowerPoint.Application` 这个 COM 组件 | `$app = New-Object -ComObject PowerPoint.Application; $app.Quit()` 不报错；PowerPoint 能手工打开 `submission/out/龙瞳分拣_答辩.pptx` | **导出**：把 pptx 另存为 PDF、逐页导出 1600×900 PNG。**没有 PowerPoint 时只能生成 pptx，不能导 PDF / PNG，也跑不了渲染侧门禁** |
| **PyMuPDF + Pillow** | `python -m pip install pymupdf Pillow` | `python -c "import pymupdf; print(pymupdf.__version__)"`（旧包名 `fitz` 亦可，脚本已做兼容导入） | **后处理**：`ppt_pdf_hires.py` 回填 PDF 插图、量插图 dpi 与逐页版面 |
| **中文字体：微软雅黑 + 等宽 Consolas** | Windows 与 Microsoft Office 自带；非 Windows 环境需自行取得并安装（注意字体授权） | PowerPoint 的字体下拉里能选到「微软雅黑」与「Consolas」；`Get-ChildItem "$env:WINDIR\Fonts" -Filter "msyh*"`、`Get-ChildItem "$env:WINDIR\Fonts" -Filter "consola*"` 能找到文件 | **生成与导出全程**：缺字体会被替换成别的字体，折行与字号观感随之变化，排版漂移 |
| **（汇总入口）机器相关依赖清单** | [环境与上手](../docs/环境与上手.md) §1 | 同表的「怎么验证」列 | 换机器 / 新人接手先过一遍 |

**不要用 WPS 代替 PowerPoint**：导出脚本走的是 `PowerPoint.Application` COM 与 PowerPoint 的 PDF / PNG 导出接口；WPS 演示的 ProgID 与导出行为不同，脚本不会自动回退。没有 PowerPoint 的机器只跑 `make_ppt.py`（拿到 pptx），把 pptx 拷到有桌面版 PowerPoint 的 Windows 上再导 PDF。

### 版本口径

文档不写死版本号，也不假设某台机器装了什么：能写的是**用到的 API 门槛**，实际版本一律用命令记录。

| 组件 | 用到的 API 门槛 | 记录实际版本 |
|---|---|---|
| python-pptx | 备注页、形状阴影继承、圆角 adjustment、`a:ea` 中文字体元素注入（生成器按 1.0 线 API 编写） | `python -c "import pptx; print(pptx.__version__)"` |
| Pillow | 读插图原生像素尺寸（算 dpi）、RGBA → RGB 白底合成 | `python -c "import PIL; print(PIL.__version__)"` |
| PyMuPDF | 图片对象扫描与 XObject 流改写（`Page.get_images` / `get_image_bbox`、`Document.update_stream` / `xref_set_key`） | `python -c "import pymupdf; print(pymupdf.__version__)"` |
| PowerShell 7 | `$PSScriptRoot`、`Join-Path -LiteralPath`、`Marshal::ReleaseComObject`、UTF-8 输出 | `pwsh -v` |
| PowerPoint | COM `Presentations.Open` / `SaveAs(…, 32)`（PDF）/ `Slides.Item(i).Export(…, "PNG", 1600, 900)` | PowerPoint「文件 → 账户 → 关于」 |
| 字体 | `FONT_CN = 微软雅黑`、`FONT_MONO = Consolas` 必须有同名字体 | 字体下拉可选中，或上表字体目录命令 |

## 3 生成链

### 3.1 两条命令

```powershell
# ① 生成 pptx（16 页、16:9），生成后自动读回自检；有问题返回非 0
python submission/build/make_ppt.py

# ② 导出 PDF 兜底 + 1600×900 逐页 PNG，随后自动做插图高清化与逐页版面度量
pwsh -File submission/build/export_ppt_preview.ps1
```

- `make_ppt.py` 参数：`--no-verify` 只生成不自检；`--out <路径>` 换输出文件。
- `export_ppt_preview.ps1` 要求先有 `submission/out/龙瞳分拣_答辩.pptx` 与 `submission/out/_ppt_images.json`（都由第 ① 步产出），缺任一个直接报错退出。
- 两条命令的失败都以退出码判定：第 ① 步自检有问题返回 1；第 ② 步导出后处理失败返回 1。

### 3.2 产物与中间文件

| 文件 | 内容 / 用途 | 入库 |
|---|---|---|
| `submission/out/龙瞳分拣_答辩.pptx` | 现场演示件；16:9、16 页，可在 PowerPoint 里继续编辑 | ❌ |
| `submission/out/龙瞳分拣_答辩.pdf` | **现场兜底件**：PowerPoint 打不开 / 字体缺失 / 动效出错时直接放这个；插图回填后 ≥300 dpi | ❌ |
| `submission/out/ppt_preview/slide_XX.png` | 1600×900 逐页预览，用于排版自查与内部互审 | ❌ |
| `submission/out/_ppt_images.json` | 插图清单（页号 / 放置矩形 / 源图 / 原生像素）+ `theme`（页面与封面底色、分隔线下沿、页脚位置）；导出链据此回填与度量 | ❌ |
| `submission/out/_ppt_pdf_hires.json` | 插图回填前后的 dpi 对比与逐页版面度量（原始证据） | ❌ |

现场带两套：笔记本 + 两个 U 盘各存一份 PPTX 与 PDF，并确认 PDF 用浏览器也能全屏播放；动效按无动效排练一遍（PDF 无动画）。

### 3.3 脚本约定

- 脚本用 `pathlib` 相对 `__file__` 定位仓库根，不写主机绝对路径（[AGENTS.md](../AGENTS.md) 硬性约定第 9 条）；不锁定 Python 解释器路径，PATH 上的 `python` 能导入 §2 的三个包即可。
- 页面内容全部由 `slide_specs()` 定义：**改内容改脚本，改完重跑**。手工在 PowerPoint 里加的页、改的字会被下一次重跑覆盖；PowerPoint 只做最后一遍视觉抛光与动效。
- `out/` 整目录已 gitignore；交件前按 [提交材料清单](提交材料清单.md) §5 把 PPTX / PDF 改成 `DragonEyeSorter_龙瞳分拣_答辩.*`。

## 4 设计规范

整体是**浅色编辑风**：浅灰画布 + 白色卡片 + 墨色文字 + 青绿强调 + 琥珀警示；深墨色只用于封面与个别深色面板。

### 4.1 画幅与版式骨架

| 项 | 规范 | 落地位置 |
|---|---|---|
| 画幅 | 16:9，13.3333 × 7.5 in，所有页面统一 | `SLIDE_W` / `SLIDE_H` |
| 版心 | 左右边距 0.62 in，版心宽 12.0933 in | `MARGIN` / `CONTENT_W` |
| 页眉骨架 | kicker（`NN · 小节名`，11pt 青绿加粗）→ 断言式标题（28pt，过长自动降到 26 / 24）→ 分隔线（细线 + 左侧 1.05 in 青绿短线） | `_header`：`KICKER_Y=0.40`、`TITLE_Y=0.64`、`TITLE_H=0.74`、`RULE_Y=1.46` |
| 正文区 | 内容从 1.74 in 起、到 6.96 in 止，不压页脚线 | `BODY_TOP` / `BODY_BOTTOM` |
| 页脚 | 细线在 7.05 in；左侧「龙瞳分拣 · DragonEyeSorter」，右侧「NN / 16」 | `FOOT_RULE_Y` / `FOOT_TEXT_Y`、`_footer` |
| kicker 编号 | kicker 的两位编号 = 页码（第 2 页起 02~16，与页脚页码一致）；封面第 1 页不编号。编号与文案手写在 `slide_specs()` 里，调页序时一起改 | `slide_specs()` |
| 母版 | 每页用空白版式（`slide_layouts[6]`）自绘，不依赖主题占位符，跨 Office 版本不漂移 | 各 `build_*` |

- 封面是唯一深色页：深墨底 + 右侧「龙瞳」同心环装饰 + 左侧文字块；其余 15 页都是浅色画布。
- 页眉 / 页脚由 `_header` / `_footer` 统一画，各版式只排正文区，不要自己再画一遍。

### 4.2 字体与字号

| 用途 | 字体 | 字号（pt） | 代码 |
|---|---|---|---|
| 页标题 | 微软雅黑 加粗 | 28（放不下自动 26 / 24） | `SIZE_TITLE`、`_fit_size` |
| kicker / 小节标签 | 微软雅黑 加粗 | 11 | `SIZE_KICKER` |
| 一级正文 / 要点 | 微软雅黑 | 13 | `SIZE_BODY` |
| 次级正文 | 微软雅黑 | 11.5 | `SIZE_SMALL` |
| 注释 / 图注 | 微软雅黑 | 10 / 9.5 | `SIZE_TINY` |
| 页脚 | 微软雅黑 | 9 | `SIZE_FOOT` |
| 条目小标题 | 微软雅黑 加粗 | 15.5（部分版式 16） | `_feature_items` 默认 |
| KPI 数值 | 微软雅黑 加粗 | 28（放不下自动 26 / 24 / 22） | `_kpi_card` |
| 等宽（寄存器名 / 命令 / 文件名） | Consolas | 随所在层级，不单独缩小 | `FONT_MONO` |

版式内的特例字号（改版式时对照代码）：封面主标题 66、封面英文名 18、封面副题 22、赛事行 12、赛道 14、技术线 13；流水线卡标题 13.5~14.5、指标数值 20~27；测试数据页数值 17、指标名 12、类别 15；创新点卡标题 15.5（上排）/ 16（下排）；结论条 12~16；卡片内小注 9.2~10。

- 中文字体只用一个：`FONT_CN = 微软雅黑`；每个 run 都同时写 `latin` 与 `a:ea` 字体（`_style` 注入 `a:ea`），避免中文落到主题默认字体。
- 不用宋体做投影正文（笔画细、远距离对比度不够）；一页里不混排两种中文字体。
- 加粗只给标题、表头、关键数字与结论，正文不整段加粗。
- 字号是投影可读性的底线：正文最小 9pt 只用于页脚 / 图注；现场投影比例下若最小字号看不清，优先删内容而不是缩字号。
- 需要把文件发到别的电脑时：PDF 由 PowerPoint 导出并嵌入字体；PPTX 换机器仍要求同名字体，否则会被替换。

### 4.3 配色（设计令牌）

| 令牌 | 色值 | 用途 |
|---|---|---|
| `C_BG` | `#F7F8FA` | 内页画布（浅灰底） |
| `C_CARD` | `#FFFFFF` | 白色卡片、表格斑马条 |
| `C_CARD_ALT` | `#F1F5F9` | 次级卡片、分层卡、表头底 |
| `C_LINE` | `#E1E8F0` | 分隔线、卡片描边、图片边框 |
| `C_INK` | `#0E1B2B` | 标题、KPI 数值、表头文字 |
| `C_BODY` | `#3F5266` | 正文、要点 |
| `C_MUTED` | `#5F7285` | 页脚、图注、次要说明 |
| `C_FAINT` | `#7C8B9D` | 更弱的注释（图注、来源行；仍保证投影可读） |
| `C_TEAL` | `#0D9488` | 强调：kicker、编号、色条、圆点符号、重点框 |
| `C_TEAL_DEEP` | `#0B6E69` | 青绿浅底上的文字（结论条） |
| `C_TEAL_TINT` | `#E7F3F1` | 结论条底、选中 chip 底 |
| `C_AMBER` | `#B45309` | 警示：「待测 ⚠」、口径提示 |
| `C_AMBER_TINT` | `#FDF3E5` | 警示条底 |
| `C_NAVY` / `C_NAVY_2` | `#102A43` / `#173959` | 深色面板（痛点页右侧、总结页收束条）及其上的次级块 |
| `C_WHITE` | `#FFFFFF` | 深色底反白文字 |
| 封面专用 | `C_COVER_BG #081726`、`C_COVER_RING` 六档深青环、`C_COVER_MUTED #7E97AE`、`C_COVER_SOFT #A9BFD2`、`C_COVER_TEAL #2DD4BF`、`C_COVER_FOOT #7C93A8` | 封面深色底、装饰环、副标题与页脚 |

对比度原则（配色是给投影读的，不是给屏幕看的）：

- 正文与标题只用墨色系（`C_INK` / `C_BODY`）：对浅画布对比度约 7.5:1 ~ 17:1，远超 WCAG AA 的 4.5:1。
- 青绿 `C_TEAL` 只做编号、短强调、色条与符号：对白底约 3.7:1，不够长正文；青绿浅底上的文字用 `C_TEAL_DEEP`（约 4.9:1）。
- `C_MUTED` / `C_FAINT` 只做页脚、图注、来源行等辅助信息（对白底约 5.3:1 / 4.0:1），**不承载结论**；对比度要够，避免投影上「像禁用态」。
- 警示只用琥珀（`C_AMBER` + `C_AMBER_TINT`），并固定搭配「待测 ⚠」文案；不用大红色（投影上易糊，语义也过强）。
- 深色块按需少用（一页至多一处深色面板），避免投影时明暗反复适应。
- 颜色全从 `C_*` 令牌取，不在 builder 里写死 hex；改配色只改令牌一处。

### 4.4 13 种版式

| 版式键 | 名称 | 页 | 结构 |
|---|---|---|---|
| `cover` | 封面 | 1 | 深墨底；左侧赛事行 / 作品名 / 英文名 / 副题 / 技术线 / 赛道；右侧同心环装饰；页脚作品名 + 01/16 |
| `pain` | 痛点对比 | 2 | 左侧 3 条产线现状（两位编号 + 标题 + 一句说明）+ 领域说明；右侧深色面板「单板闭环」5 步流程；底部结论条 |
| `overview` | 总览看板 | 3 | 顶部一句话定位 + 4 张 KPI 卡 + 3 张分层卡（PL / PS / NPU，卡内「分工 / 依据 / 证据」三行）+ 口径警示条 |
| `figure_side` | 左图右文 | 4、6、9 | 左 7.40 in 整图（第 6 页叠重点框）；右侧 3 条「条目小标题 + 说明」+ 数据行 + 结论条 + 可选警示条 |
| `figure_full` | 全幅示意图 | 5、8 | 顶部 lead 一句 + 可选右侧数据行；整幅图（有图注 4.52 in 高、无图注 4.86 in）；有图注时底部居中一行图注 |
| `pipeline` | 流水线 | 7 | 4 段流程卡（箭头串联）+ 4 个小指标卡 + 2×2 要点 + 结论条 |
| `ai` | AI / NPU 改造 | 10 | 4 步流水 + 3 张对比 KPI 卡（中间标「已部署」）+ 3 张「为什么」卡 + 警示条 |
| `hmi` | 人机与存储 | 11 | 5 个标签 chip（首项选中）+ 4 张 KPI 卡 + 3 张特性卡（标题 / 说明 / 佐证）+ 结论条 |
| `can` | 总线联动 | 12 | 两端设备卡 + 中间双向箭头（CAN 2.0A · 500 kbit/s）+ 下发 / 回传两张列表卡 + 3 个数字 + 结论条与警示条并排 |
| `metrics` | 测试数据表 | 13 | 三列表（类别 / 已实测指标 / 条件口径）+ 4 行斑马纹 + 底部「待测清单」警示条 |
| `reliability` | 可靠性与安全 | 14 | 2 张长稳指标卡 + 4 张安全设计卡 + 结论条 + 警示条 |
| `innovations` | 创新点矩阵 | 15 | 5 张证据卡（上 3 下 2）+ 结论条；卡内固定三行：编号 / 标题 / 「证据」标签 + 证据 |
| `summary` | 总结 | 16 | 3 栏（已完成 / 待补齐 ⚠ / 下一步）+ 深色收束条（一句话 + 致谢） |

改版式时：先在 `BUILDERS` 里找到对应 builder，再对照 §4.1 的页眉 / 正文区 / 页脚常量摆坐标；同一版式的多页共用一份 builder，改一处会同时改所有用到它的页。

### 4.5 内容规范

- **一页一个断言式标题**：标题说结论、说判断，不写名词短语。例：「检测与分拣，不该是两道工序」（采用）/「场景与痛点介绍」（不采用）。
- **每页 3~5 条「结论 + 证据」**：先给结论，再给数字、条件或出处；一页讲 25~40 秒，念不完就删。
- **数字只来自实测来源**：[测试与验证](../docs/测试与验证.md)「已实测数据汇总」与 [技术文档](../docs/技术文档.md)；未实测的项目标「待测 ⚠」，不猜、不把合成集数字写成实物精度。
- **口径随数字一起出现**：合成集 / 实物、指令分辨率 / 实测精度、采集显示实时性 / 推理按物料触发，三类口径不能混着报。
- **禁项**：正文（含表格）不得出现「大学 / 学院 / 指导老师 / 指导教师 / 导师 / 校徽」；官方赛事名「全国大学生嵌入式芯片与系统设计竞赛」整串豁免（自检先剔除再查）。封面与内页都不出现学校与导师信息。
- **不在幻灯片上写仓库路径**：图源、数据出处、口径说明写进 speaker notes（每页 3~5 句，带出处）；正文只留结论与数字。
- **重点数量受控**：一页指标 ≤4 个、要点 ≤5 条；表格 / 卡片页的列数按版式固定，不临时加列。

### 4.6 插图规范

| 项 | 规范 |
|---|---|
| 图源 | `docs/figures/*.png`，由 `python docs/figures/make_figures.py` 生成，不手工画；SVG 版仅作矢量备份 |
| 版式语言 | 与 PPT 同一套浅色编辑风：浅灰分组面板（小标题 + 细线）、卡片白底 + 2pt 强调色条、中性灰细箭头；不用高饱和整块填充与粗虚线框；图内不画大标题（标题由页标题 / 图注承担），详见 [figures/README](../docs/figures/README.md) |
| 本版用图 | 第 4 页 `fig_architecture.png`、第 5 页 `fig_timebase.png`、第 6 页 `fig_pl_regs.png`（重点框 `modules`）、第 8 页 `fig_pick_sequence.png`、第 9 页 `fig_layout.png` |
| 嵌入方式 | **整图按源图原生像素嵌入**，等比缩放、居中，白底图加 `C_LINE` 细边框；不做局部放大、不做像素裁切 |
| 清晰度 | 源图宽 3200px：左图右文页放在 7.40 in 宽 ≈ **432 dpi**；全幅页实际放置 10.27~10.38 in 宽 ≈ **308~312 dpi**；全篇门禁 **≥300 dpi** |
| 重点框 | `_add_region_highlight` 从 `docs/figures/fig_meta.json` 的 `regions` 取矩形（源 PNG 像素、左上原点），按 `png_w` / `png_h` 换算到放置矩形，在图上画 `C_TEAL` 圆角框；**只画框，不裁切、不放大**，框错位说明 fig_meta 与源图不同步。当前只有第 6 页配了 `region` |
| 图注 | 一行，只写画面说明（图 X-Y 名称 + 视角 / 条件），如「图 2-2 时间基准：拍摄瞬间锁存带位置，按图像行插值」；不写仓库路径与构建说明 |
| 缺图 / 缺 meta | 插图缺失时脚本抛 `FileNotFoundError`；`fig_meta.json` 缺失或没有对应插图 / 区域键时脚本退出并提示先跑 `make_figures.py` |
| 新增图 | 图放 `docs/figures/` 并由 `make_figures.py` 登记 → 重跑生成 `fig_meta.json` → 在 `slide_specs()` 里用 `figure_side` / `figure_full` 加页；3200px 源图放置宽度不要超过约 **10.6 in**（否则跌破 300 dpi） |

`fig_meta.json` 里的 `min_fs_pt`（每图最小字号 ≥7.5pt）是报告链的落版字号门槛，PPT 侧读的是同一文件的 `regions`；改插图后重跑 `make_figures.py`，报告与 PPT 的自检会一起跟上（见 [figures/README](../docs/figures/README.md)）。

### 4.7 动效建议（少而稳）

- `make_ppt.py` 不管动画：动效只在 PowerPoint 里手工加，属于内容冻结后的最后一道抛光；之后不要再重跑生成器，否则手工改动被覆盖。
- 用**淡入 / 出现**，一页最多两处；不用飞入、旋转、弹跳、自动连播。切换用「无」或淡入。
- 动效只强调结论条与关键数字，不逐条飞入；现场备用 PDF 没有动画，按无动效排练一遍。

## 5 自检与验收

分两层：生成侧（脚本读回 pptx 查结构）和导出侧（PowerPoint 导出后查 PDF 插图清晰度与逐页版面）。两层都过，再做人工看图。

### 5.1 生成侧自检（`make_ppt.py` 读回）

每次生成后脚本会重新打开产物，打印文件、尺寸、页数、每页形状数 / 图片数 / 备注字数，并逐项检查：

| 检查 | 判定 |
|---|---|
| 尺寸 | 13.333 × 7.5 in，比值 1.7778 |
| 页数 | 等于 `slide_specs()` 的页数（当前 16） |
| 备注 | 每页都有 speaker notes |
| 禁项 | 页面正文（含表格）不得出现「大学 / 学院 / 指导老师 / 指导教师 / 导师 / 校徽」；官方赛事名先整串剔除再查 |
| 主机路径 | 页面正文不得出现 `C:\` / `D:\` 这类主机绝对路径 |
| 越界 | 形状不得超出页面（整幅背景 / 装饰形状允许出血） |
| 文本溢出估算 | 按中文字宽与行高估算所需高度，超出文本框高度 + 0.08 in 即报警 |
| 插图 dpi | 每张插图按原生像素 / 放置宽度算等效 dpi，低于 300 报警 |
| 页脚安全区 | 正文内容底沿不得压过 7.05 in 的页脚线 |

有报警时脚本返回非 0，**当作失败处理**。用法：

```powershell
python submission/build/make_ppt.py              # 生成 + 自检
python submission/build/make_ppt.py --no-verify  # 只生成（调试排版时用，交件前必须跑完整自检）
```

### 5.2 导出与渲染侧自检（`export_ppt_preview.ps1` + `ppt_pdf_hires.py`）

`export_ppt_preview.ps1` 串起五件事：

| 步 | 做什么 | 产物 / 判定 |
|---|---|---|
| ① pptx → PDF | PowerPoint COM `SaveAs($pdf, 32)` | `out/龙瞳分拣_答辩.pdf`（现场兜底件） |
| ② 逐页 PNG | COM `Slides.Item(i).Export(…, "PNG", 1600, 900)` | `out/ppt_preview/slide_XX.png` |
| ③ 插图回填 | `ppt_pdf_hires.py` 按 `_ppt_images.json` 的「页号 + 放置矩形」对位，把源图原生像素写回 PDF 的图片对象（先写临时文件，校验通过才原地覆盖） | PDF 插图恢复到原生像素，回填后 **308~432 dpi** |
| ④ 清晰度门禁 | 逐张算等效 dpi | 任一张 **<300 dpi** → 非零退出 |
| ⑤ 版面度量 | 从 1600×900 预览 PNG 量墨迹（顶档 / 头档 / 底档 / 面积填充） | 写 `out/_ppt_pdf_hires.json`（原始证据） |

为什么需要第 ③ 步：PowerPoint 导 PDF 会把插图降采样到约 200 dpi（3200px 图放进 7.40 in 宽的框只剩约 1480px）。PPTX 里嵌的仍是全分辨率，问题只在 PDF 兜底件，所以按清单对位回填，图框、文字、页码都不动。

版面度量的两个口径（底色与页脚位置取自 `_ppt_images.json` 的 `theme`，**换主题后必须重跑生成器再量**，否则会按旧底色把整页当成墨迹）：

| 指标 | 目标区间（打印 PASS / FAIL） | 硬门禁（触发非零退出） |
|---|---|---|
| 头档（分隔线下沿 → 正文首行） | ≤0.80 in | — |
| 底档（内容底沿 → 页底） | ≤0.98 in | >1.10 in |
| 面积填充（墨迹 bbox / 页面面积） | 55%~82% | <45% |
| 插图等效 dpi | ≥300 | <300 |
| 封面 | 豁免（设计留白） | — |

目标区间外的页在输出里打 `FAIL:` 提示；脚本只在硬门禁被触发时返回非 0，末行汇总也只覆盖 dpi 与硬门禁——**看结果以逐页判定行为准，逐页数值见 `_ppt_pdf_hires.json`**。只量不改：

```powershell
python submission/build/ppt_pdf_hires.py --pdf "submission/out/龙瞳分拣_答辩.pdf" `
    --manifest submission/out/_ppt_images.json --report --preview submission/out/ppt_preview `
    --json submission/out/_ppt_pdf_hires.json
```

### 5.3 人工看图清单

机检只量边界，读不读得清、对不对得上要靠看图。全部 16 页逐张看 `submission/out/ppt_preview/`（必要时在 PowerPoint 里 100% 视图对照）：

| 检查 | 看什么 | 通过标准 |
|---|---|---|
| 封面（01） | 赛事行、作品名、英文名、赛道 | 无学校名称 / 校徽 / 导师信息；深色底文字在投影上不糊 |
| 图页（04 / 05 / 06 / 08 / 09） | 整图、重点框（第 6 页）、图注、右侧条目 | 图清晰不糊；无裁切或拉伸；重点框落在目标模块 / 区域；图注与数据行不压页脚 |
| 文字 / 卡片页（02 / 03 / 07 / 10 / 11 / 12 / 14 / 15 / 16） | 标题、支撑条目、结论条、警示条 | 无溢出、无重叠；「待测 ⚠」可见；结论条不被截断 |
| 表格页（13） | 三列对齐、数值与条件 | 数值与口径列对得上，无换行挤压，无「待测」误色 |
| 全篇 | kicker、页脚、配色 | kicker 编号连续；页脚页码 02/16~16/16 连续；浅画布 / 白卡 / 青绿 / 琥珀四色一致 |
| PDF 兜底件 | 页数、打开方式、插图 | 16 页；浏览器可离线打开；插图 ≥300 dpi；字体显示正常 |

投影前再用决赛现场的比例过一遍最小字号（正文 9~13pt、图内 7.5pt 在远距离的观感），必要时删内容而不是缩字号。

## 6 讲稿节奏（5~8 分钟）

| 页 | 内容 | 建议时长（8 分钟版） |
|---|---|---|
| 1 | 封面 | 15s |
| 2 | 场景与痛点 | 25s |
| 3 | 系统总览与指标 | 30s |
| 4 | 系统架构（PL / PS / NPU 分工） | 35s |
| 5 | 数据流与时间基准（创新点 1） | 40s |
| 6 | PL：舵机总线与步进 | 35s |
| 7 | PL：des_vision 硬件预处理 | 30s |
| 8 | 跟踪抓取与挡板分流（创新点 2） | 35s |
| 9 | 视觉测量（创新点 3） | 30s |
| 10 | AI 与 NPU 部署（创新点 4） | 35s |
| 11 | HMI 与存储 | 25s |
| 12 | CAN 联动 PLC | 25s |
| 13 | 测试数据（四类） | 40s |
| 14 | 可靠性与安全 | 30s |
| 15 | 创新点汇总 | 30s |
| 16 | 总结与展望 | 25s |

合计约 8 分钟。每页 speaker notes 里已写讲稿要点与数字出处，照 notes 练即可。**不要按每页上限（40~50s）念**——那是给问答留的余量，全套上限会到 11 分钟。压到 5 分钟：只讲 1、2、3、4、5、8、10、13、14、15、16 这 11 页，每页 25~30 秒；7、9、11、12 页留给专家提问。

## 7 冻结前 TODO

1. **补实测数字**：实物分类准确率与混淆矩阵、对卡尺的测量误差与重复性、多舵机同步误差、机械臂重复定位 / 抓取成功率、跟踪误差、接实物 2h——先落 [测试与验证](../docs/测试与验证.md)，再改 `slide_specs()` 里的数字与「待测 ⚠」标记。
2. **补实物照片**：整机全局 + 开发板特写，替换或补充插图页；拍摄要求见 [提交材料清单](提交材料清单.md) §6。
3. **现场投影确认**：用决赛同款投影比例过一遍配色与最小字号，确认图内 7.5pt 与正文 9~11.5pt 看得清。
4. **最终命名**：导出后改成 `DragonEyeSorter_龙瞳分拣_答辩.pptx` + 同名 PDF，与官网作品名逐字对齐；PDF 与 PPTX 各拷两个 U 盘。

## 8 常见排错

| 现象 | 原因 | 处置 |
|---|---|---|
| `New-Object -ComObject PowerPoint.Application` 报错 | 没装桌面版 PowerPoint（只有 WPS / 网页版）；Office 安装不完整；COM 未注册 | 装桌面版 PowerPoint；WPS 不能替代；暂时只跑 `make_ppt.py` 生成 pptx |
| COM 可用但 `SaveAs` / `Export` 失败 | 目标 PDF / PNG 被占用；PowerPoint 弹了受保护视图或首次启动向导；有残留 `POWERPNT.EXE` 进程 | 关掉已打开的 PDF / pptx，结束残留 PowerPoint 进程，确认 `submission/out/` 可写后重跑 |
| `缺少 python-pptx` / `缺少 Pillow` / `缺少 PyMuPDF` | 当前 `python` 解释器没装包 | `python -m pip install python-pptx Pillow pymupdf`；确认 `pwsh` 里调到的也是同一个 `python`（脚本走 PATH） |
| 控制台中文乱码 | 终端代码页不是 UTF-8 | 用 PowerShell 7（`pwsh`）跑；脚本已设 `PYTHONIOENCODING=utf-8` 并调 UTF-8 输出 |
| 文字换成了别的字体、折行或版式错位 | 目标机缺微软雅黑 / Consolas，PowerPoint 或渲染端自动替换 | 在目标机装同名字体，PowerPoint 字体下拉里确认；交付时用 PDF 兜底（PowerPoint 导出时已嵌入字体），PPTX 换机器仍需装字体 |
| `插图缺失：…` | `docs/figures/` 下缺 PNG | `python docs/figures/make_figures.py` 重新生成全部插图 |
| `缺少 …fig_meta.json` / `fig_meta.json 里没有插图 …` | 改图后没重跑生成脚本；新增图未登记；`regions` 键名写错 | 重跑 `make_figures.py`；核对 `fig_meta.json` 的 `figures` 与 `regions` 键，与 `slide_specs()` 里的 `region` 对齐 |
| 版面度量结果整页都像墨迹 / 底色不对 | 拿来量的 PDF 与 `_ppt_images.json` 不是同一版；或先生成后导出顺序反了 | 先 `make_ppt.py` 再 `export_ppt_preview.ps1`；不要拿旧 PDF / 旧预览 PNG 配对新的 manifest |
| PDF 页数不是 16 | 手工在 PowerPoint 里加 / 删页；pptx 被手工改过 | 重跑 `make_ppt.py` 再导出；页数只由 `slide_specs()` 决定，不要手改 |
| 某页 `FAIL: 填充率出界` / `FAIL: 底档>0.98` | 该页内容偏少或偏多、图片尺寸变了 | 看预览图，调 `slide_specs()` 的内容或版式高度后重跑；目标区间见 §5.2，硬门禁才让脚本失败 |
| 某张插图 dpi <300 | 源图被换成小图；放置宽度变大（版面改宽） | 检查 `_ppt_images.json` 的 `px` 与 `rect_in`；重跑 `make_figures.py` 出 3200px 图，或收窄放置宽度 |
| 自检报「文本可能溢出」 | 要点过长 / 标题过长 / 文本框高度不足 | 删字或改短标题（结论先行）；不要靠缩字号硬塞 |
| 自检报「形状越界」/「内容压过页脚线」 | 版式坐标算错，或内容变长把块顶出界 | 调对应 builder 的 y 坐标或内容长度；整幅背景允许出血，其余形状不能出页 |
| 自检报禁项「大学」 | 命中了官方赛事名以外的文字 | 官方赛事名整串出现是允许的（自检会先剔除）；否则删掉校名 / 导师信息 |
