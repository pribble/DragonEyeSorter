# submission/ — 提交材料工作区

初赛交**作品报告（PDF）+ 演示视频（MP4）**，决赛现场为**作品介绍演示 + 专家提问 + FPGA 基础编程考核**。官方条目、格式规格、倒排计划与待确认事项见 **[提交材料清单](提交材料清单.md)**（交件前逐项核对这里）。

## 目录

| 路径 | 内容 | 入库 |
|---|---|---|
| [提交材料清单.md](提交材料清单.md) | 官方要求 → 要交什么 → 内容来源 → 现状 → 倒排；唯一核对入口 | ✅ |
| [视频拍摄指南.md](视频拍摄指南.md) | 怎么拍 / 录屏 / 收音 / 剪辑 / 导出 / 合规检查（**分镜与解说词在 [docs/演示视频脚本.md](../docs/演示视频脚本.md)**，本文件不重复） | ✅ |
| [PPT制作指南.md](PPT制作指南.md) | 工具调研结论、推荐工作流、母版与导出规范；页纲见 [参赛材料骨架](../docs/参赛材料骨架.md) §4 | ✅ |
| `build/` | 生成链脚本：技术文档 Markdown → DOCX → PDF、PPT 骨架、代码打包 | ✅ 脚本入库 |
| `out/` | 生成物（`.docx` / `.pdf` / `.pptx` / 封面图等），可随时重建 | ❌ 已 gitignore |

## 内容源（不在本目录重复维护）

| 要交的东西 | 唯一源 |
|---|---|
| 报告正文 | [docs/技术文档.md](../docs/技术文档.md) |
| 插图 | [docs/figures/](../docs/figures/)（脚本生成，不手工画） |
| 分镜 / 解说 / 字幕 / 时长 | [docs/演示视频脚本.md](../docs/演示视频脚本.md) |
| PPT 页纲 | [docs/参赛材料骨架.md](../docs/参赛材料骨架.md) §4 |
| 实测数字 | [docs/测试与验证.md](../docs/测试与验证.md) |
| 赛题要求与评分 | [docs/ref/安路赛道选题指南_2026.pdf](../docs/ref/安路赛道选题指南_2026.pdf)（选题四 p27~33） |

## 生成链怎么跑

```powershell
# 报告：docs/技术文档.md → out/龙瞳分拣_作品报告.docx → .pdf → 插图高清化（Word 只导 96dpi，换回源图 ~514dpi）
#   需本机 Microsoft Word；高清化需一个带 PyMuPDF 的 Python（缺了会打印提示并非 0 退出）
pwsh -File submission/build/build.ps1

# 报告验收自检（48 项：产物、版式、禁项、页码、插图 dpi≥300、逐页留白）→ out/_verify_report.txt
python submission/build/verify_report.py

# PPT 骨架 + 兜底 PDF + 逐页预览（导出后自动做插图高清化与版面度量）
python submission/build/make_ppt.py
pwsh -File submission/build/export_ppt_preview.ps1

# 作品重要代码包：git archive → out/DragonEyeSorter_代码_v1.0.zip（不含数据集 / 权重 / BOOT.bin）
pwsh -File submission/build/pack_code.ps1
```

细节与依赖见 [build/README.md](build/README.md)（脚本清单、排版口径、怎么加章节与插图、已知权衡）。

## 规矩

1. **生成的二进制不入库**：`out/` 下的 docx / pdf / pptx 由脚本重建；只提交脚本与 Markdown。
2. **数字必须有出处**：报告、视频、PPT 里的每个数字来自 [docs/测试与验证.md](../docs/测试与验证.md) 或当天实测输出，未实测的标 ⚠ 并写「未测」，不编。
3. **不出现学校和指导老师信息**：2026 应用赛道明文规定报告与视频中不得出现学校名称、指导老师信息（FPGA 赛道待官方 2026 文本确认，按最严执行）。
4. **不写主机绝对路径**：本目录文档同样遵守 [AGENTS.md](../AGENTS.md) 硬性约定第 9 条。
