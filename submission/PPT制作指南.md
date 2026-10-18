# PPT 制作指南（决赛答辩用）

> 页纲唯一源是 [参赛材料骨架](../docs/参赛材料骨架.md) §4；本文件只讲**怎么做**：工具怎么选、工作流怎么跑、母版与导出规范怎么落地。
> 数字一律来自 [测试与验证](../docs/测试与验证.md) 与 `docs/log/`，未实测标「待测 ⚠」；封面与内页**不出现学校名称与指导老师姓名**（见 [提交材料清单](提交材料清单.md) §6 检查表）。
> 生成链：`submission/build/make_ppt.py` → `submission/out/龙瞳分拣_答辩.pptx` → PowerPoint COM 导出 PDF 与逐页 PNG 预览。

## 1 工具调研（2026-10-18，本机实测 + 联网核对）

### 1.1 本机可用性（实测）

| 工具 | 本机状态 | 怎么确认的 | 能力边界 |
|---|---|---|---|
| Microsoft PowerPoint | ✅ Office 16（`16.0.20326.20232`），COM 可用 | 注册表 `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\POWERPNT.EXE` → `...\root\Office16\POWERPNT.EXE`；`New-Object -ComObject PowerPoint.Application` 成功，已用它导出 PDF + 16 张 PNG | 精修视觉、动画、母版、PDF 导出的最终环境 |
| python-pptx | ✅ `1.0.2`（Pillow `12.3.0`） | `python -c "import pptx; print(pptx.__version__)"` | 生成结构化骨架：文本、表格、图片、备注；**不做**渲染与视觉检查，也不支持动画 / 切换 |
| WPS Office | ❌ 未安装（只有残留目录） | `HKCU\...\Kingsoft\kaccclear`、`kvip` 有目录，但 `KWPP.Application` ProgID 未注册，`C:\Program Files\WPS Office` 不存在 | 若以后要装，注意它的 pptx 兼容性与字体替换行为需另行验证 |
| Node + pnpm | ✅ Node `v24.15.0`、pnpm `12.3.4` | `node -v` / `pnpm -v` | 可装 Marp / Slidev（本机未装，按需 `pnpm dlx` 即可） |
| LibreOffice | ❌ 未安装 | `soffice.exe` 不存在，`com.sun.star.ServiceManager` 未注册 | 影响：Marp 的 `--pptx-editable` 用不了（见 §1.3） |

### 1.2 代码 / 文本驱动方案

| 方案 | 可复现性 | 中文排版 | 能否导出**可继续编辑**的 pptx | 图表 / 公式 | 离线 | 学习成本 | 结论 |
|---|---|---|---|---|---|---|---|
| **python-pptx** | 最高：脚本 + 纯文本内容，改一字重跑即一致；可进版本库 | 好：直接指定 `微软雅黑`，字号 / 颜色 / 加粗逐 run 控制 | **可以**：生成的就是原生 pptx 对象（文本框、表格、图片），PowerPoint 里逐项可改 | 图片直接嵌 `docs/figures/*.png`；表格原生；公式弱（本作品用不上） | ✅ 全离线 | 中：要先摸清形状 / 占位符 / 表格列宽 API | **主方案** |
| **Marp**（`marp-cli`） | 高：Markdown 单一源 | 好 | 默认 **不行**：普通 `--pptx` 输出的是「预渲染背景图 + 备注」，正文文字不可改；实验性的 `--pptx-editable` 才可编辑，但它要求同时装浏览器与 **LibreOffice Impress**，官方明确警告复杂主题会报错或输出不完整，且**不支持演讲者备注** | 靠 HTML / CSS，图表要外部渲染 | 除 PDF / PPTX / 图片导出需本机浏览器外，其余离线 | 低 | **快速草稿备选**：适合先出结构与讲稿，正式件仍需落到 python-pptx 或 PowerPoint |
| **Slidev** | 中：Vue 项目，依赖 Node 生态 | 好 | **不行**：`slidev export --format pptx` 官方说明每页都是**图片**，文字不可选、不可编辑（备注会带过去）；PDF / PPTX / PNG 导出依赖 Playwright + Chromium | 最强：Vue 组件、KaTeX、Mermaid 都能用 | 导出依赖浏览器 | 高 | 本作品不需要 Web 演示能力，**不采用** |
| **LaTeX Beamer** | 最高：纯文本、可 diff | 可用，但要 `xeCJK` / `ctex` 配字体，中文断行与字体嵌入要单独调 | 产出 PDF 而非 pptx；转 pptx 只能靠外部工具 | 公式与矢量图最强 | ✅ | 高 | 本作品公式少、评审看 pptx / PDF 双份，**不采用** |

### 1.3 AI 生成方案

| 工具 | 定位 | 关键限制 |
|---|---|---|
| WPS AI PPT | WPS 套件内的 AI 生成 | 免费额度用完需会员；生成结果可在 WPS 演示里改，但本机未装 WPS |
| 讯飞智文 / 百度文库 AI / 美图 AI PPT / Kimi PPT 助手 | 国内一键生成，中文模板多 | 模板不可控、版式与官方配色难对齐；导出后常需大改；数据与结论要靠人核 |
| Gamma / Canva | 海外，视觉表现强 | 模板与素材有授权与商用条款约束；导出可编辑性受限；团队协作与离线不可控 |

**共同问题（对本作品是硬伤）**：

1. **数据不可复现**：本 PPT 里每个数字都要能追到 [测试与验证](../docs/测试与验证.md) 或当天实测输出；AI 生成稿会「顺手编数」或把合成集口径写成实物精度，评审一问就露。
2. **模板不可控 + 合规风险**：模板常带作者署名、水印、外部素材，且未必允许竞赛场景使用；一旦带入校名 / 导师信息即触发禁项。
3. **现场可靠性**：决赛现场网络与账号状态不可控，AI 服务的额度、登录、在线模板都会成为现场风险点。

结论：**AI 工具只用来找排版灵感**（配色、图表样式、页面节奏），模板与文案一律不落地到正式件。

## 2 推荐工作流（三步）

```
① python-pptx 生成骨架            ② PowerPoint 精修视觉           ③ 导出双份 + 预览自查
submission/build/make_ppt.py  →   out/龙瞳分拣_答辩.pptx        →  out/*.pdf + out/ppt_preview/*.png
（页纲、要点、图、数据表、备注）      （母版、配色、动画、字体微调）      （PDF 兜底 / PNG 逐页核对）
```

| 步 | 谁做 | 做什么 | 不要做什么 |
|---|---|---|---|
| ① 骨架 | 脚本（可重跑） | 16 页标题、2~5 条要点、插图、数据表、speaker notes | 不要手工在 PowerPoint 里补一页——会被下次重跑覆盖 |
| ② 精修 | 人（PowerPoint） | 母版统一、动画（少而稳）、换 projector 实测配色、图片裁切 | 不要改数字；数字要改就改 `make_ppt.py` 里的表再重跑 |
| ③ 导出 | 脚本 + COM | 导 PDF 兜底、导 PNG 逐页核对、写最终文件名 | 不要用「另存为图片」逐张点——漏页且尺寸不齐 |

**为什么以 python-pptx 为主、PowerPoint 为辅**

- 骨架是**可复现**的：页纲来自 `docs/参赛材料骨架.md` §4，数字来自 `docs/测试与验证.md`，两者改一次重跑一次，PPT 不会和报告口径不一致（评审最常抓的就是这里）。
- 备注（speaker notes）随脚本一起写，讲稿不会丢；AI 工具与 Markdown 转换器在这一点上要么不支持、要么不可控。
- PowerPoint 只做它擅长的事：母版、动画、字体细节与投影效果。它**不可复现**，所以只做最后一遍视觉抛光，不承担内容。
- 备选路径：时间紧时用 Marp 从同一份要点出草稿（`--pptx` + `--notes`）快速对齐讲稿节奏，正式件仍由本脚本产出。

## 3 生成链怎么跑

```powershell
# 1) 生成骨架（16:9，16 页，含备注）；脚本自带读回自检，有问题返回非 0
python submission/build/make_ppt.py

# 2) 导出 PDF 兜底 + 逐页 PNG 预览（用 PowerPoint COM，16:9 → 1600×900）
pwsh -File submission/build/export_ppt_preview.ps1
```

产物：

| 路径 | 用途 | 入库 |
|---|---|---|
| `submission/out/龙瞳分拣_答辩.pptx` | 决赛现场演示件；可继续在 PowerPoint 里编辑 | ❌ `out/` 已 gitignore |
| `submission/out/龙瞳分拣_答辩.pdf` | **现场兜底**：PowerPoint 打不开 / 字体缺失 / 动画出问题时直接放这个 | ❌ |
| `submission/out/ppt_preview/slide_XX.png` | 逐页排版自查与内部互审 | ❌ |

脚本约定：用 `pathlib` 相对 `__file__` 定位仓库根，**不写主机绝对路径**（[AGENTS.md](../AGENTS.md) 硬性约定第 9 条）；不锁定 python 解释器路径，系统 Python 或运行时自带 Python 都能跑（两者都已装 python-pptx）。

## 4 制作规范

### 4.1 页面与母版

| 项 | 规范 | 落地位置 |
|---|---|---|
| 画幅 | 16:9，13.333 × 7.5 in（960 × 540 pt） | `make_ppt.py` 的 `SLIDE_W_IN` / `SLIDE_H_IN` |
| 页数 | 16 页（页纲 15 页 + 把「架构」拆出「数据流与时间基准」一页） | `slide_specs()` |
| 版心 | 左右边距 0.62 in，正文区 y = 1.30~7.00 in，页脚 7.08 in | `HEADER_*` / `BODY_*` |
| 母版 | 骨架页用空白版式（`slide_layouts[6]`）自绘，避免主题占位符在不同 Office 版本上漂移 | `build_slide()` |

### 4.2 字体

| 用途 | 字体 | 字号 |
|---|---|---|
| 中文正文 / 标题 | **微软雅黑**（`msyh.ttc` / `msyhbd.ttc`，本机已装） | 正文 18pt（**下限**），标题 28pt，封面主标题 54pt |
| 等宽（寄存器名、命令、文件名） | Consolas | 与所在层级一致，不单独缩小 |
| 加粗 | 标题、表头、关键数字 | 正文不整段加粗（投影上会糊） |

- 不要用宋体做投影正文：笔画细、远距离对比度不够。
- 一页里不要混排两种中文字体；`make_ppt.py` 统一由 `FONT_CN` 控制。
- 需要把文件发到别的电脑时，PDF 里的字体已嵌入，PPTX 侧则要求对方也装微软雅黑（Windows 自带）。

### 4.3 配色（深蓝底 + 青色强调）

| 角色 | 色值 | 用在哪 |
|---|---|---|
| 背景（深蓝） | `#0B2545` | 所有内页底 |
| 封面背景 | `#071B33` | 封面，比内页更深一档 |
| 强调（青） | `#22D3EE` | 标题左侧色条、要点符号、结论条文字、页顶装饰线 |
| 强调（暗青） | `#1B8FA8` | 标题下分隔线 |
| 正文 | `#E8F0F8` | 要点文字 |
| 次要 / 页脚 | `#93B2CE` | 图注、页脚 |
| 图卡底 | `#FFFFFF` + 描边 `#C7D6E5` | `docs/figures/` 的图是白底，放在白色圆角卡片上，避免糊在深蓝上 |

对比度：深蓝底（REL 亮度约 0.02）配 `#E8F0F8`（约 0.86）与 `#22D3EE`（约 0.55）远高于 WCAG AA 的 4.5:1；不要用中灰或低饱和的蓝紫做正文。

### 4.4 每页信息密度上限

| 项 | 上限 | 理由 |
|---|---|---|
| 要点 | ≤ 5 条，每条 **≤ 20 字，且单行不折行** | 折行后要点块会顶到页脚；脚本按列宽做了字宽估算自检 |
| 每页指标数 | ≤ 4 行表格（或 4 个数字） | 一页讲 25~40 秒，念不了更多 |
| 插图 | 1 张 / 页 | 多张图会缩到看不清 |
| 备注 | 3~5 句，含数字出处 | 讲稿要点，不是逐字稿 |

### 4.5 图表

- 一律用 [docs/figures/](../docs/figures/) 下的 PNG（由 `make_figures.py` 生成，**不手工画**）。本版用到 `fig_architecture.png`、`fig_timebase.png`、`fig_pl_regs.png`、`fig_pick_sequence.png`、`fig_layout.png`。
- 图按原始宽高比缩放（脚本读像素尺寸算），不拉伸变形；PNG 3200px 宽，1600×900 导出下清晰。
- SVG 版在 `docs/figures/*.svg`，在 PowerPoint 里可「插入 → 图片」后右键转形状做二次编辑；脚本默认用 PNG 以保证跨机器一致。
- 每张图配一行图注，写明「图 X-Y 名称（来源路径）」。

### 4.6 动画

少而稳：优先用**淡入**或「出现」，一页最多两处；不用飞入 / 旋转 / 弹跳。现场备用机不一定装了相同版本的 PowerPoint，动画越多越容易在陌生机器上错位。

### 4.7 导出与兜底

| 件 | 规格 | 命令 |
|---|---|---|
| PPTX | 16:9，文件名 `DragonEyeSorter_龙瞳分拣_答辩.pptx`（上传时按 [提交材料清单](提交材料清单.md) §5 改名） | `python submission/build/make_ppt.py` |
| PDF | 同页数，字体已嵌入 | `pwsh -File submission/build/export_ppt_preview.ps1`（COM `SaveAs($pdf, 32)`） |
| PNG | 1600×900 / 页，用于自查 | 同上（COM `Slides.Item(i).Export(...)`） |

**现场用 PDF 兜底**：自带笔记本 + U 盘双份（PDF + PPTX），并确认 PDF 用浏览器也能全屏播放。若现场只能用自己的机器，提前在相同投影比例下过一遍配色。

### 4.8 讲稿节奏（决赛介绍约 5~8 分钟）

| 页 | 内容 | 建议时长 |
|---|---|---|
| 1 | 封面 | 15~25s |
| 2~3 | 场景痛点 / 系统总览与指标 | 各 30~40s |
| 4~5 | 架构 / 数据流与时间基准 | 各 35~45s（时间基准是创新点，可讲满） |
| 6~7 | 舵机总线 + 步进 / des_vision | 各 35~45s |
| 8~9 | 跟踪抓取 / 视觉测量 | 各 35~45s |
| 10~12 | AI 与 NPU / HMI 与存储 / CAN 联动 PLC | 各 30~40s |
| 13 | 测试数据（四类） | 40~50s |
| 14~15 | 可靠性与安全 / 创新点 | 各 30~40s |
| 16 | 总结与展望 | 25~35s |

合计约 8 分 30 秒（按每页上限）。**按每页 25~40 秒准备**，超时就把 7、9、11 页压到 25 秒；讲稿要点在每页 speaker notes 里，数字必须与 [测试与验证](../docs/测试与验证.md) 一致。

## 5 自动化自检（重跑后必看）

`make_ppt.py` 每次跑完会读回生成的 pptx 并打印：页数、每页形状数与图片数、备注字数，并检查：

| 检查 | 判定 |
|---|---|
| 画幅 | 13.333 × 7.5 in（比值 1.7778） |
| 备注齐全 | 16 页都要有 speaker notes |
| 禁项 | 页面正文不得出现「大学 / 学院 / 指导老师 / 指导教师 / 导师 / 校徽」；官方赛事名整串剔除后再查 |
| 主机路径 | 页面正文不得出现 `C:\` 这类主机绝对路径 |
| 越界 | 任何形状不得超出页面 |
| 要点折行 | 按中文字宽估算，超过要点列宽即报警（改短要点或调版心） |
| 表格折行 | 表格单元格文字超过列宽即报警 |

有报警时脚本返回非 0，**当作失败处理**。剩余人工检查项：用 `read_image` 看 `out/ppt_preview/` 里至少 4 页（封面、一页图页、一页表页、结尾页），确认文字不溢出、图不糊、封面无校名、配色在投影上可读。

## 6 冻结前 TODO

1. 实物数据集与标定完成后，把 `待测 ⚠` 的位置补上实测值（分类准确率、测量误差与重复性、多舵机同步误差、机械臂重复定位、跟踪误差、接实物 2h）。
2. 补拍实物照片替换/补充插图页（整机全局 + 开发板特写），照片要求见 [提交材料清单](提交材料清单.md) §6。
3. 在决赛同款投影比例下实测配色与最小可读字号。
4. 导出最终文件名 `DragonEyeSorter_龙瞳分拣_答辩.pptx` + 同名 PDF，与官网作品名逐字对齐。

## 7 调研来源（2026-10-18 核对）

§1 每条结论都能在这里找到对应页面；核对日期 2026-10-18。

- python-pptx 能读写 pptx、加图 / 表 / 图表 / 备注，但**不做渲染与视觉检查、不支持动画**，所以它只出骨架、视觉精修交给 PowerPoint → [python-pptx — python-pptx documentation](https://python-pptx.readthedocs.io/en/latest/)（2026-10-18）
- 普通 `marp --pptx` 输出的是预渲染背景图、**正文不可编辑**；可编辑要加实验性的 `--pptx-editable`，它要求同时装浏览器与 **LibreOffice Impress**，复杂主题会报错或输出不完整，且**不支持演讲者备注** → [GitHub - marp-team/marp-cli: A CLI interface for Marp and Marpit based converters](https://github.com/marp-team/marp-cli)（2026-10-18）
- `slidev export --format pptx` 官方说明每页都导出为**图片**，因此文字不可选、不可编辑，另需 Chromium —— 本作品需要可继续编辑与备注，故不采用 → [导出 | Slidev](https://cn.sli.dev/guide/exporting.html)（2026-10-18）
- WPS AI PPT 生成结果可在 WPS 演示里继续改、可另存 PPTX，但免费额度用完即需会员 / AI 权益，视觉要求高的稿子仍需手工调整 → [WPS AI PPT 怎么用？功能、收费与演示生成指南](https://www.miaopian.cn/wpsaippt.html)（2026-10-18）
- AI 生成工具普遍存在模板与素材的授权 / 商用条款、额度与水印限制 → [Gamma 高频问题答疑：关于额度、水印与版权](https://www.php.cn/faq/2623669.html)（2026-10-18）

下面三条只在检索结果中出现、**未逐字核对原文全文**，引用前请复核（也正因如此，结论里没有拿它们当依据）：

- AI 生成 PPT 导出 pptx 后可编辑性的横向对比 → [How to Export AI Slides to PowerPoint (2026)](https://cognitivefuture.ai/how-to-export-ai-slides-to-powerpoint/)（检索日 2026-10-18）
- Gamma 的定价与商用注意点 → [Gamma AIの使い方と料金を解説｜商用利用の注意点まで | Hakky Handbook](https://book.st-hakky.com/purpose/gamma-ai-usage-and-pricing)（检索日 2026-10-18）
- Marp 与其它 Markdown → PowerPoint 路径的比较 → [Markdown to PowerPoint: Best Conversion Paths Compared](https://deckary.com/blog/markdown-to-powerpoint)（检索日 2026-10-18）
