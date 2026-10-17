# figures/ — 技术文档与 PPT 的插图（脚本生成，不手工画）

插图由脚本生成，数值与标签写在脚本里，改完重跑即与技术文档一致。生成结果（`fig_*.svg` / `fig_*.png`）入库，插图的**引用位置**见 [技术文档](../技术文档.md) 与 [硬件与接线](../硬件与接线.md)。

| 脚本 | 产出 | 依赖 | 用法 |
|---|---|---|---|
| `make_figures.py` | `fig_architecture`、`fig_layout`、`fig_timebase`、`fig_pl_regs`、`fig_pick_sequence`（各 SVG + PNG） | matplotlib + 中文字体 | `python docs/figures/make_figures.py` |
| `make_system_wiring.py` | `fig_system_wiring.png`（整机接线：市电 / 24V / 急停 / 步进 / 舵机供电 / 电平转换 / J12 引脚；实线已接、虚线待接） | matplotlib + 中文字体 | `python docs/figures/make_system_wiring.py` |
| `make_step_wiring.py` | `fig_step_wiring.png`（步进驱动器四根信号线在 J12 上的位置，叠加厂家位号图） | PyMuPDF + matplotlib + 厂家硬件资料包（位号图 PDF 路径作参数） | `python docs/figures/make_step_wiring.py <位号图.pdf>` |
| `make_servo_lv_wiring.py` | `fig_servo_lv_wiring.png`（BSS138 低压侧接 J12 的三针，叠加厂家位号图） | 同上 | `python docs/figures/make_servo_lv_wiring.py <位号图.pdf>` |

约定：

- 图里的引脚号、电压、路径以 [硬件与接线](../硬件与接线.md) 与 [scripts/deploy/manifest.tsv](../../scripts/deploy/manifest.tsv) 为准；改数值先改文档，再改脚本重跑。
- 需要厂家资料的脚本（位号图 PDF）路径不写死在脚本里，用命令行参数传；仓库内不放厂家资料。
