# PPT 制作指南（决赛答辩用）

> **页纲唯一源**是 [参赛材料骨架](../docs/参赛材料骨架.md) 的 PPT 提纲；本文件只讲这条链怎么做、怎么验收、现场怎么用。内容全部写在 `submission/build/make_ppt.py` 的 `slide_specs()`：改内容改脚本、重跑即一致；手工在 PowerPoint 里改的页与字会被下一次重跑覆盖，PowerPoint 只做最后一遍视觉抛光与动效。
> 数字只来自 [测试与验证](../docs/测试与验证.md) 与 `docs/log/`，未实测标「待测 ⚠」；封面与内页**不出现学校名称、校徽、指导老师姓名**。

## 1 PPT 链怎么跑

环境：Windows + 桌面版 PowerPoint（导出走 `PowerPoint.Application` COM，**WPS 不能替代**）；Python 3 + `python-pptx` / `Pillow` / `PyMuPDF`。逐项验证命令见 [环境与上手](../docs/环境与上手.md) §1「需要哪些环境」。

```powershell
# ① 生成 pptx（16 页、16:9），生成后自动读回自检；有问题返回非 0
python submission/build/make_ppt.py

# ② 导出兜底 PDF + 1600×900 逐页 PNG，随后自动插图回填与逐页版面度量
pwsh -File submission/build/export_ppt_preview.ps1
```

| 产物（`submission/out/`） | 用途 |
|---|---|
| `龙瞳分拣_答辩.pptx` / `.pdf` | 现场演示件 / **现场兜底件**：PowerPoint 打不开、字体缺失、动效出错时直接放 PDF |
| `ppt_preview/slide_XX.png` | 1600×900 逐页预览，排版自查与人工看图用 |
| `_ppt_images.json` · `_ppt_pdf_hires.json` | 插图清单（页号 / 放置矩形 / 源图 / 原生像素）+ 回填后 dpi 与逐页版面度量（原始证据） |

- `make_ppt.py` 参数：`--no-verify` 只生成不自检；`--out <路径>` 换输出。`export_ppt_preview.ps1` 要求先有第 ① 步的 pptx 与 `_ppt_images.json`，缺任一个直接报错退出。两条命令都按退出码判定成败。
- 现场带两套：笔记本 + 两个 U 盘各存 PPTX 与 PDF；确认 PDF 用浏览器也能全屏播放，并按无动效排练一遍（PDF 没有动画）。
- 交件前按 [提交材料清单](提交材料清单.md) §5 改名 `DragonEyeSorter_龙瞳分拣_答辩.pptx` + 同名 PDF。

## 2 验收硬门禁

**硬门禁 = 16 页、16:9（13.333 × 7.5 in）、插图等效 dpi ≥300、禁项为零、页面度量在阈值内；任一项触发即非零退出。** 生成侧（`make_ppt.py` 读回）另查每页有 speaker notes、正文无主机绝对路径、形状不越界、文本溢出估算、正文不压 7.05 in 的页脚线；导出侧（`export_ppt_preview.ps1` → `ppt_pdf_hires.py`）判插图 dpi 与版面度量。

- **页数**：等于 `slide_specs()` 的页数（当前 16）；改页纲加页要同步改脚本，页数只由脚本决定，不要手工增删页。
- **禁项**：页面正文（含表格）不得出现「大学 / 学院 / 指导老师 / 指导教师 / 导师 / 校徽」；官方赛事名「全国大学生嵌入式芯片与系统设计竞赛」整串豁免（自检先剔除再查）。出处、数据来源、口径说明写进 speaker notes，正文不写仓库路径。
- **插图 dpi**：每张插图按原生像素 / 放置宽度算等效 dpi，**<300 即失败**。PPTX 里嵌的是全分辨率；PowerPoint 导 PDF 会降采样到约 200 dpi，所以导出脚本按 `_ppt_images.json` 的「页号 + 放置矩形」把源图原生像素回填进 PDF（图框、文字、页码不动），回填后 308~432 dpi。
- **版面度量**（底色与页脚位置取自 `_ppt_images.json` 的 `theme`，**换主题后必须重跑生成器再量**，否则会按旧底色把整页当成墨迹）：

| 指标 | 目标区间 | 硬门禁（触发非零退出） |
|---|---|---|
| 头档（分隔线下沿 → 正文首行） | ≤0.80 in | — |
| 底档（内容底沿 → 页底） | ≤0.98 in | >1.10 in |
| 面积填充（墨迹 bbox / 页面面积） | 55%~82% | <45% |
| 插图等效 dpi | ≥300 | <300 |
| 封面 | 豁免（设计留白） | — |

- 目标区间外的页在输出里打 `FAIL:` 提示，但脚本只在硬门禁触发时返回非 0，末行汇总也只覆盖 dpi 与硬门禁——**看结果以逐页判定行为准，逐页数值见 `_ppt_pdf_hires.json`**。单独复量：

```powershell
python submission/build/ppt_pdf_hires.py --pdf "submission/out/龙瞳分拣_答辩.pdf" `
    --manifest submission/out/_ppt_images.json --report --preview submission/out/ppt_preview `
    --json submission/out/_ppt_pdf_hires.json
```

## 3 人工看图清单

机检只量边界，读不读得清、对不对得上要靠看图：全部 16 页逐张看 `submission/out/ppt_preview/`（必要时在 PowerPoint 100% 视图对照）。

| 检查 | 看什么 | 通过标准 |
|---|---|---|
| 封面（01） | 赛事行、作品名、英文名、赛道 | 无学校名称 / 校徽 / 导师信息；深色底文字投影上不糊 |
| 图页（04 / 05 / 06 / 08 / 09） | 整图、第 6 页重点框、图注、右侧条目 | 图清晰不糊、无裁切或拉伸；重点框落在目标模块；图注与数据行不压页脚 |
| 文字 / 卡片页（02 / 03 / 07 / 10 / 11 / 12 / 14 / 15 / 16） | 标题、支撑条目、结论条、警示条 | 无溢出、无重叠；「待测 ⚠」可见；结论条不被截断 |
| 表格页（13） | 三列对齐、数值与条件 | 数值与口径列对得上，无换行挤压，无「待测」误色 |
| 全篇 + PDF 兜底 | kicker、页脚、配色、离线打开 | kicker 编号与页脚页码 02/16~16/16 连续；PDF 16 页、浏览器可离线打开、字体正常 |

投影前再用决赛现场的比例过一遍最小字号（正文 9~13pt、图内 7.5pt 在远距离的观感），看不清优先删内容而不是缩字号。

## 4 讲稿节奏（5~8 分钟）

| 段落 | 页 → 建议时长（8 分钟版） |
|---|---|
| 开场 | 1 封面 15s · 2 痛点 25s · 3 总览 30s |
| 技术主体 | 4 架构 35s · 5 数据流与时间基准 40s · 6 舵机总线 35s · 7 des_vision 30s · 8 跟踪抓取 35s · 9 视觉测量 30s · 10 AI / NPU 35s |
| 外围与数据 | 11 HMI 25s · 12 CAN 联动 25s · 13 测试数据 40s · 14 可靠性 30s |
| 收尾 | 15 创新点 30s · 16 总结 25s |

合计约 8 分钟；每页 speaker notes 已写讲稿要点与数字出处，照 notes 练即可。**不要按每页上限（40~50s）念**——那是给问答留的余量，全套上限会到 11 分钟。压到 5 分钟：只讲 1、2、3、4、5、8、10、13、14、15、16 这 11 页，每页 25~30 秒；7、9、11、12 页留给专家提问。

## 5 冻结前 TODO

1. **补实测数字**：实物分类准确率与混淆矩阵、测量误差与重复性、多舵机同步误差、机械臂重复定位 / 抓取成功率、跟踪误差、接实物 2h——先落 [测试与验证](../docs/测试与验证.md)，再改 `slide_specs()` 里的数字与「待测 ⚠」标记。
2. **补实物照片**：整机全局 + 开发板特写，替换或补充插图页；拍摄要求见 [提交材料清单](提交材料清单.md) §6。
3. **现场投影确认**：用决赛同款投影比例过一遍配色与最小字号，确认图内 7.5pt 与正文 9~11.5pt 看得清。
4. **最终命名与备份**：`DragonEyeSorter_龙瞳分拣_答辩.pptx` + 同名 PDF，与官网作品名逐字对齐；各拷两个 U 盘。

## 6 常见排错（前 5 条）

| 现象 | 原因 | 处置 |
|---|---|---|
| `New-Object -ComObject PowerPoint.Application` 报错 | 没装桌面版 PowerPoint（只有 WPS / 网页版）；Office 安装不完整；COM 未注册 | 装桌面版 PowerPoint；WPS 不能替代；暂时只跑 `make_ppt.py` 生成 pptx |
| COM 可用但 `SaveAs` / `Export` 失败 | 目标 PDF / PNG 被占用；PowerPoint 弹了受保护视图或首次启动向导；有残留 `POWERPNT.EXE` 进程 | 关掉已打开的 PDF / pptx，结束残留 PowerPoint 进程，确认 `submission/out/` 可写后重跑 |
| `缺少 python-pptx` / `Pillow` / `PyMuPDF` | 当前 `python` 解释器没装包 | `python -m pip install python-pptx Pillow pymupdf`；确认 `pwsh` 里调到的也是同一个 `python` |
| 控制台中文乱码 | 终端代码页不是 UTF-8 | 用 PowerShell 7（`pwsh`）跑；脚本已设 `PYTHONIOENCODING=utf-8` 并调 UTF-8 输出 |
| 文字换成别的字体、折行或版式错位 | 目标机缺微软雅黑 / Consolas，PowerPoint 或渲染端自动替换 | 在目标机装同名字体；交付用 PDF 兜底（PowerPoint 导出已嵌入字体），PPTX 换机器仍需装字体 |

插图缺失或 `fig_meta.json` 不同步时 `make_ppt.py` 直接报错退出：重跑 `python docs/figures/make_figures.py`，并核对 `fig_meta.json` 的 `figures` / `regions` 与 `slide_specs()` 里的 `region` 对齐。
