# submission/ — 提交材料工作区

初赛交**作品报告（PDF）+ 演示视频（MP4）**，线上评审；决赛为**作品介绍演示 + 专家提问 + FPGA 基础编程考核**。官方要求、倒排与待确认事项的唯一核对入口是 [提交材料清单](提交材料清单.md)。

## 目录

| 路径 | 内容 | 入库 |
|---|---|---|
| [提交材料清单.md](提交材料清单.md) | 官方要求 → 要交什么 → 现状 → 倒排 → 打包命名 → 上传前检查 → 决赛准备；唯一核对入口 | ✅ |
| [技术文档.md](技术文档.md) | 作品报告正文（内容源；`build/` 的 md2docx 只读它，改内容改这里） | ✅ |
| [演示视频脚本.md](演示视频脚本.md) | 演示视频分镜 / 解说 / 拍摄 / 交付唯一源（未拍 ⬜） | ✅ |
| [PPT制作指南.md](PPT制作指南.md) | 答辩 PPT 生成链怎么跑、验收硬门禁、人工看图、讲稿节奏、排错 | ✅ |
| `build/` | 生成链脚本：报告 Markdown → DOCX → PDF、答辩 PPT 生成与导出、代码打包；脚本清单与用法见 [build/README.md](build/README.md) | ✅ 脚本 |
| `out/` | 生成物（`.docx` / `.pdf` / `.pptx` / 预览 PNG / 代码 zip），可随时重建 | ❌ gitignore |

## 内容源（不在本目录重复维护）

| 要交的东西 | 唯一源 |
|---|---|
| 报告正文 | [技术文档.md](技术文档.md)（已收进本目录，与生成链同处） |
| 插图 | [docs/figures/](../docs/figures/)（脚本生成，不手工画） |
| 分镜 / 解说 / 字幕 / 拍摄 / 时长 / 导出规格 | [演示视频脚本.md](演示视频脚本.md)（已收进本目录） |
| 赛题条目 → 材料对照、PPT 页纲 | [docs/参赛材料骨架.md](../docs/参赛材料骨架.md)（「赛题条目 → 证据」与 §4 PPT 提纲） |
| 实测数字 | [docs/测试与验证.md](../docs/测试与验证.md) |
| 赛题要求与评分 | [docs/ref/安路赛道选题指南_2026.pdf](../docs/ref/安路赛道选题指南_2026.pdf)（选题四 p27~33） |

## 生成链怎么跑

```powershell
pwsh -File submission/build/build.ps1                 # 报告：md → docx → pdf → 插图高清化
python submission/build/verify_report.py              # 报告验收自检（59 项）→ out/_verify_report.txt
python submission/build/make_ppt.py                   # 答辩 PPT 生成 + 读回自检
pwsh -File submission/build/export_ppt_preview.ps1    # PPT → 兜底 PDF + 逐页 PNG + 插图回填与版面度量
pwsh -File submission/build/pack_code.ps1             # 作品重要代码包（git archive）
```

命令参数、依赖、产物命名与报告排版口径见 [build/README.md](build/README.md)；PPT 链门禁与排错见 [PPT制作指南.md](PPT制作指南.md)。

## 规矩

1. **生成的二进制不入库**：`out/` 下 docx / pdf / pptx / zip 由脚本重建，仓库里只提交脚本与 Markdown。
2. **数字必须有出处**：报告、视频、PPT 里的每个数字来自 [docs/测试与验证.md](../docs/测试与验证.md) 或当天实测输出，未实测的标 ⚠ 并写「未测」，不编。
3. **不出现学校与指导老师信息**：报告、视频、PPT 都不出现学校名称、校徽、指导老师姓名（2026 应用赛道明文规定，FPGA 赛道按最严执行）。
4. **不写主机绝对路径**：遵守 [docs/README.md](../docs/README.md) §写作约定「不写主机路径」。
