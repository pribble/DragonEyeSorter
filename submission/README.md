# submission/ — 提交材料工作区

官方提交页（10-31 核实）要求：**技术文档（PDF ≤50MB，按官方模板）+ 演示视频（MP4 / AVC，≤3min，≤300MB）+ 全部代码（RAR ≤200MB）+ 参赛作品承诺及使用授权书（PDF）**，海报选填，线上评审；决赛为**作品介绍演示 + 专家提问 + FPGA 基础编程考核**。官方要求、倒排与待确认事项的唯一核对入口是 [提交材料清单](提交材料清单.md)。

## 目录

| 路径 | 内容 | 入库 |
|---|---|---|
| [提交材料清单.md](提交材料清单.md) | 官方要求 → 要交什么 → 现状 → 倒排 → 打包命名 → 上传前检查 → 决赛准备；唯一核对入口 | ✅ |
| [技术文档.md](技术文档.md) | 技术文档正文（**官方模板五部分**：作品名称 + 摘要 + 第一~五部分，首页标题 = 官网报名作品名，各节有字数上限）；`build/` 的 md2docx 只读它，改内容改这里；当前 **21 页 / 14 图 / 12 表**，其中 6 张报告内占位（图 8 ROI 对比、图 9 样件、图 10 全景、图 11 机械臂、图 12 开发板、图 13 界面）待替换 | ✅ |
| [演示视频脚本.md](演示视频脚本.md) | 演示视频分镜 / 解说 / 拍摄 / 交付唯一源（未拍 ⬜） | ✅ |
| [figures/](figures/) | 报告插图：8 张线稿图 + 6 张报告内占位图的生成脚本（`figkit.py` / `make_figures.py` / `make_placeholders.py` / 8 个 `fig_*.py` / `placeholder_manifest.json`，后者另含 2 项报告外实物照片拍摄清单）与页眉 logo（`logo_header.png`，取自官方模板，入库）入库；生成物（SVG / PNG / `fig_meta.json`）本地重画、不入库，不手工画；用法见 [figures/README.md](figures/README.md) | ✅ 脚本 + logo |
| `build/` | 生成链脚本：报告 Markdown → DOCX → PDF → 插图高清化、报告验收自检、代码打包；版式令牌与用法见 [build/README.md](build/README.md) | ✅ 脚本 |
| `out/` | 生成物（`.docx` / `.pdf` / 预览 PNG / 代码 zip），可随时重建 | ❌ gitignore |

## 内容源（不在本目录重复维护）

| 要交的东西 | 唯一源 |
|---|---|
| 报告正文 | [技术文档.md](技术文档.md)（已收进本目录，与生成链同处）；官方模板 = [docs/ref/作品报告模板_2026.pdf](../docs/ref/作品报告模板_2026.pdf) |
| 插图与占位图清单 | [figures/](figures/)（只入库生成脚本；生成物重画即得，见其 `.gitignore`）+ `placeholder_manifest.json`（报告内 6 张占位与报告外 2 项实物照片的机位 / 光线 / 分辨率要求；整机全景一张两用） |
| 分镜 / 解说 / 字幕 / 拍摄 / 时长 / 导出规格 | [演示视频脚本.md](演示视频脚本.md)（已收进本目录） |
| 赛题条目 → 材料对照 | [docs/参赛材料骨架.md](../docs/参赛材料骨架.md)（「赛题条目 → 证据」） |
| 实测数字 | [docs/测试与验证.md](../docs/测试与验证.md) |
| 赛题要求与评分 | [docs/ref/安路赛道选题指南_2026.pdf](../docs/ref/安路赛道选题指南_2026.pdf)（选题四 p27~33） |

## 生成链怎么跑

```powershell
pwsh -File submission/build/build.ps1                   # 报告全链：插图 / 占位图重生成 → md → docx → pdf → 插图高清化
python submission/build/verify_report.py                # 验收自检 65 项 → out/_verify_report.txt
python submission/build/verify_report.py --final        # 交件门禁：报告内占位图未全部替换即 FAIL
python submission/figures/make_figures.py               # 14 张插图单独重生成（8 线稿 + 6 报告内占位；build.ps1 会自动跑）
pwsh -File submission/build/pack_code.ps1               # 作品重要代码包（git archive；官方要求 RAR 格式，压成 .rar 后交）
```

命令参数、版式令牌、版面口径与依赖见 [build/README.md](build/README.md)。

## 交件前必做

1. **替换报告内 6 张占位图**（图 8 整幅 vs ROI 对比 `ph_roi.png`、图 9 七类样件 `ph_samples.png`、图 10 整机全景 `ph_overview.png`、图 11 机械臂 `ph_arm.png`、图 12 开发板特写 `ph_board.png`、图 13 界面五页 `ph_ui.png`）：按 [placeholder_manifest.json](figures/placeholder_manifest.json) 的 `file` 与 `aspect` 拍好 / 截图（图 8 由已有采集图导出，无需拍摄），覆盖同名 `ph_*.png`，并把该项 `replaced` 置为 `true`——之后 `build.ps1` 重跑不会覆盖照片，正文与图注一字不动。其余 2 项报告外实物照片（导流挡板、测试现场）按 `placeholder_manifest.json` 里 `in_report=false` 的条目拍摄，供决赛展示；整机全景一张两用（报告图 10 + 作品照片主图）。
2. `python submission/build/verify_report.py --final` 全 PASS（报告内占位图清零 + 模板骨架 / 字数 / 版式 / 数字 / 禁项 / dpi 各项过关）。
3. 按 [提交材料清单](提交材料清单.md) §6 逐项打勾。

## 规矩

1. **生成的二进制不入库**：`out/` 下 docx / pdf / 代码 zip、`figures/` 下的插图与占位图生成物都由脚本重建，仓库里只提交脚本与 Markdown；页眉 logo（`logo_header.png`）是唯一入库的图片——它取自官方模板、无仓库内生成源，构建必需。
2. **数字必须有出处**：报告、视频里的每个数字来自 [docs/测试与验证.md](../docs/测试与验证.md) 或当天实测输出，未实测的标 ⚠ 并写「未测」，不编。
3. **不出现学校与指导老师信息**：报告、视频都不出现学校名称、校徽、指导老师姓名（官方提交页红字明文）。
4. **不写主机绝对路径**：遵守 [docs/README.md](../docs/README.md) §写作约定「不写主机路径」。
