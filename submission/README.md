# submission/ — 提交材料工作区

初赛交**作品报告（PDF）+ 演示视频（MP4）**，线上评审；决赛为**作品介绍演示 + 专家提问 + FPGA 基础编程考核**。官方要求、倒排与待确认事项的唯一核对入口是 [提交材料清单](提交材料清单.md)。

## 目录

| 路径 | 内容 | 入库 |
|---|---|---|
| [提交材料清单.md](提交材料清单.md) | 官方要求 → 要交什么 → 现状 → 倒排 → 打包命名 → 上传前检查 → 决赛准备；唯一核对入口 | ✅ |
| [技术文档.md](技术文档.md) | 作品报告正文（**纯论文式**：标题 + 摘要 + 关键词 + `I.`~`IX.` 节 + 参考文献 + 附录 A，无封面 / 目录 / 页眉；12 图 / 11 表，其中 7 张实物照片为占位图）；`build/` 的 md2docx 只读它，改内容改这里 | ✅ |
| [演示视频脚本.md](演示视频脚本.md) | 演示视频分镜 / 解说 / 拍摄 / 交付唯一源（未拍 ⬜） | ✅ |
| [figures/](figures/) | 报告插图：5 张线稿图 + 7 张实物照片占位图的生成脚本（`figkit.py` / `make_figures.py` / `make_placeholders.py` / 5 个 `fig_*.py` / `placeholder_manifest.json`）入库，生成物（SVG / PNG / `fig_meta.json`）本地重画、不入库，不手工画；用法见 [figures/README.md](figures/README.md) | ✅ 脚本 |
| `build/` | 生成链脚本：报告 Markdown → DOCX → PDF → 插图高清化、报告验收自检、代码打包；版式令牌与用法见 [build/README.md](build/README.md) | ✅ 脚本 |
| `out/` | 生成物（`.docx` / `.pdf` / 预览 PNG / 代码 zip），可随时重建 | ❌ gitignore |

## 内容源（不在本目录重复维护）

| 要交的东西 | 唯一源 |
|---|---|
| 报告正文 | [技术文档.md](技术文档.md)（已收进本目录，与生成链同处） |
| 插图与占位图清单 | [figures/](figures/)（只入库生成脚本；生成物重画即得，见其 `.gitignore`）+ `placeholder_manifest.json`（7 张待拍照片的机位 / 光线 / 分辨率要求） |
| 分镜 / 解说 / 字幕 / 拍摄 / 时长 / 导出规格 | [演示视频脚本.md](演示视频脚本.md)（已收进本目录） |
| 赛题条目 → 材料对照 | [docs/参赛材料骨架.md](../docs/参赛材料骨架.md)（「赛题条目 → 证据」） |
| 实测数字 | [docs/测试与验证.md](../docs/测试与验证.md) |
| 赛题要求与评分 | [docs/ref/安路赛道选题指南_2026.pdf](../docs/ref/安路赛道选题指南_2026.pdf)（选题四 p27~33） |

## 生成链怎么跑

```powershell
pwsh -File submission/build/build.ps1                   # 报告全链：插图 / 占位图重生成 → md → docx → pdf → 插图高清化
python submission/build/verify_report.py                # 验收自检 60 项 → out/_verify_report.txt
python submission/build/verify_report.py --final        # 交件门禁：占位图未全部替换即 FAIL
python submission/figures/make_figures.py               # 12 张插图单独重生成（5 线稿 + 7 占位；build.ps1 会自动跑）
pwsh -File submission/build/pack_code.ps1               # 作品重要代码包（git archive）
```

命令参数、版式令牌、版面口径与依赖见 [build/README.md](build/README.md)。

## 交件前必做

1. **替换 7 张占位图**：按 [placeholder_manifest.json](figures/placeholder_manifest.json) 的 `file` 与 `aspect` 拍好实物照片（机位 / 光线 / 分辨率见同一文件），覆盖同名 `ph_*.png`，并把该项 `replaced` 置为 `true`——之后 `build.ps1` 重跑不会覆盖照片，正文与图注一字不动。
2. `python submission/build/verify_report.py --final` 全 PASS（占位图清零 + 版式 / 数字 / 禁项 / dpi 各项过关）。
3. 按 [提交材料清单](提交材料清单.md) §6 逐项打勾。

## 规矩

1. **生成的二进制不入库**：`out/` 下 docx / pdf / 代码 zip、`figures/` 下的插图与占位图生成物都由脚本重建，仓库里只提交脚本与 Markdown。
2. **数字必须有出处**：报告、视频里的每个数字来自 [docs/测试与验证.md](../docs/测试与验证.md) 或当天实测输出，未实测的标 ⚠ 并写「未测」，不编。
3. **不出现学校与指导老师信息**：报告、视频都不出现学校名称、校徽、指导老师姓名（2026 应用赛道明文规定，FPGA 赛道按最严执行）。
4. **不写主机绝对路径**：遵守 [docs/README.md](../docs/README.md) §写作约定「不写主机路径」。
