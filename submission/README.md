# submission/ — 提交材料工作区

初赛交**作品报告（PDF）+ 演示视频（MP4）**，线上评审；决赛为**作品介绍演示 + 专家提问 + FPGA 基础编程考核**。官方要求、倒排与待确认事项的唯一核对入口是 [提交材料清单](提交材料清单.md)。

## 目录

| 路径 | 内容 | 入库 |
|---|---|---|
| [提交材料清单.md](提交材料清单.md) | 官方要求 → 要交什么 → 现状 → 倒排 → 打包命名 → 上传前检查 → 决赛准备；唯一核对入口 | ✅ |
| [技术文档.md](技术文档.md) | 作品报告正文（内容源；`build/` 的 md2docx 只读它，改内容改这里） | ✅ |
| [演示视频脚本.md](演示视频脚本.md) | 演示视频分镜 / 解说 / 拍摄 / 交付唯一源（未拍 ⬜） | ✅ |
| [figures/](figures/) | 报告插图：生成脚本（`figkit.py` / `make_figures.py` / 5 个 `fig_*.py`）+ 生成物（SVG / PNG / `fig_meta.json`）同处，不手工画；用法见 [figures/README.md](figures/README.md) | ✅ |
| `build/` | 生成链脚本：报告 Markdown → DOCX → PDF、报告验收自检、代码打包；脚本清单与用法见 [build/README.md](build/README.md) | ✅ 脚本 |
| `out/` | 生成物（`.docx` / `.pdf` / 预览 PNG / 代码 zip），可随时重建 | ❌ gitignore |

## 内容源（不在本目录重复维护）

| 要交的东西 | 唯一源 |
|---|---|
| 报告正文 | [技术文档.md](技术文档.md)（已收进本目录，与生成链同处） |
| 插图 | [figures/](figures/)（生成脚本与生成物同处、入库；不手工画） |
| 分镜 / 解说 / 字幕 / 拍摄 / 时长 / 导出规格 | [演示视频脚本.md](演示视频脚本.md)（已收进本目录） |
| 赛题条目 → 材料对照 | [docs/参赛材料骨架.md](../docs/参赛材料骨架.md)（「赛题条目 → 证据」） |
| 实测数字 | [docs/测试与验证.md](../docs/测试与验证.md) |
| 赛题要求与评分 | [docs/ref/安路赛道选题指南_2026.pdf](../docs/ref/安路赛道选题指南_2026.pdf)（选题四 p27~33） |

## 生成链怎么跑

```powershell
pwsh -File submission/build/build.ps1                 # 报告：md → docx → pdf → 插图高清化
python submission/build/verify_report.py              # 报告验收自检（59 项）→ out/_verify_report.txt
python submission/figures/make_figures.py             # 报告插图重生成（改了图脚本才需要）→ submission/figures/
pwsh -File submission/build/pack_code.ps1             # 作品重要代码包（git archive）
```

命令参数、依赖、产物命名与报告排版口径见 [build/README.md](build/README.md)。

## 规矩

1. **生成的二进制不入库**：`out/` 下 docx / pdf / 代码 zip 由脚本重建，仓库里只提交脚本与 Markdown。
2. **数字必须有出处**：报告、视频里的每个数字来自 [docs/测试与验证.md](../docs/测试与验证.md) 或当天实测输出，未实测的标 ⚠ 并写「未测」，不编。
3. **不出现学校与指导老师信息**：报告、视频都不出现学校名称、校徽、指导老师姓名（2026 应用赛道明文规定，FPGA 赛道按最严执行）。
4. **不写主机绝对路径**：遵守 [docs/README.md](../docs/README.md) §写作约定「不写主机路径」。
