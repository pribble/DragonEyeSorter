# figures/ — 仓库文档用的整机接线图

本目录只剩**整机接线图**这条链：`make_system_wiring.py` 生成 `fig_system_wiring.png`，供 [硬件与接线](../硬件与接线.md) 引用（实物照片的补充，不占报告版心）。

作品报告用的 12 张插图（5 张线稿 + 7 张占位）只在提交件里用，已整体放在 [submission/figures/](../../submission/figures/)：生成脚本入库，PNG / SVG / `fig_meta.json` 由脚本重画、不入库（见其 README）；本目录不再放报告插图。

## 用法

```powershell
python docs/figures/make_system_wiring.py     # 重画整机接线图（输出留在本目录）
```

## 口径

沿用旧口径：引脚号、电压、路径以 [硬件与接线](../硬件与接线.md) 与 [scripts/deploy/manifest.tsv](../../scripts/deploy/manifest.tsv) 为准，**改数值先改文档再改脚本**。尚未按 1:1 字号门槛重画（不占报告版心），图内字号不参与报告自检。
