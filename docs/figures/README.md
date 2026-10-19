# figures/ — 技术文档、作品报告与 PPT 的插图（脚本生成，不手工画）

插图由脚本生成，数值与标签写在脚本里，改完重跑即与技术文档一致。生成结果（`fig_*.svg` / `fig_*.png`）与 `fig_meta.json` 入库，插图的**引用位置**见 [技术文档](../技术文档.md) 与 [硬件与接线](../硬件与接线.md)。

## 一条必须守住的规则：按落版尺寸作图

画布单位是 **pt，画布宽固定 447.9pt = 报告落版宽 15.8cm**，所以**图里写的字号就是纸上 / 屏幕上的字号**。

旧版插图按 16in（1152pt）画布作图、再缩到 15.8cm 落版，缩放 0.389 倍：图里 8pt 的字落到纸面只剩 3.1pt，整片文字读不动（这是 2026-10 重画的起因）。现在的硬门槛是**任何一张图的最小字号 ≥ 7.5pt**（报告正文 12pt 的 62%）。

配套的三条约定：

- 字号只取 `figkit.FS` 的档位：箱内正文 8.5pt / 次级与注释 7.5pt / 组标题与面板标题 9.5pt；不要在画图代码里写别的数字。
- 标签短句化：每行 ≤ 12 个汉字当量，细节写进正文。**不画图内标题**（标题由报告图注、PPT 页标题承担）。
- 文字一律用 `figkit.box()` / `text()` / `group()` 画：保存前会核对**是否越出盒子或画布**（> 0.5pt 报错，< 1.5pt 提示"偏紧"），并打印每张图的最小字号与最小盒内余量。排版靠数据，不靠目测。

## 文件

| 文件 | 用途 |
|---|---|
| `figkit.py` | 公共画布 / 图元（框、组、箭头、折线）/ 越界与字号校验 / 导出 SVG+PNG+`fig_meta.json` |
| `make_figures.py` | 一键重画全部 5 张：`python docs/figures/make_figures.py`（可跟关键字只画其中几张） |
| `fig_architecture.py` | 图 2-1 系统架构（另一张新图的写法照它抄） |
| `fig_timebase.py` | 图 2-2 时间基准（拍摄瞬间锁存带位置） |
| `fig_pl_regs.py` | 图 3-1 PL 自研模块、寄存器窗口与管脚 |
| `fig_pick_sequence.py` | 图 4-1 良品抓取 / 不良品翻板时序 |
| `fig_layout.py` | 图 7-1 工位布局（俯视，按比例） |
| `make_system_wiring.py` | `fig_system_wiring.png`（整机接线：市电 / 24V / 急停 / 步进 / 舵机供电 / 电平转换 / J12 引脚；实线已接、虚线待接） |
| `make_step_wiring.py` | `fig_step_wiring.png`（步进驱动器四根信号线在 J12 上的位置，叠加厂家位号图） |
| `make_servo_lv_wiring.py` | `fig_servo_lv_wiring.png`（BSS138 低压侧接 J12 的三针，叠加厂家位号图） |

单张调试：`python docs/figures/fig_timebase.py`（跑单张 + 打印该图门禁）。导出：SVG（矢量，文字可编辑）+ PNG 宽 3200px（报告满版心 15.8cm 时 ≈514 dpi；PPT 按整图原生像素嵌入，7.40in 宽时约 432 dpi）。

`fig_meta.json` 是生成物的副产物，也是下游的依据：画布尺寸、`px_per_pt`、每张图的最小字号、以及 **PPT 重点框区域**（`regions`，源 PNG 像素、左上原点；`make_ppt.py` 用它在上画重点框，不裁切）。`submission/build/make_ppt.py`（`regions`）与 `submission/build/verify_report.py`（`min_fs_pt` 落版字号）都读它——**改插图后重跑 `make_figures.py`，PPT 与自检会一起跟上**。

## 接线图（厂家资料叠加）

`make_step_wiring.py` / `make_servo_lv_wiring.py` 的位号图 PDF 路径不写死在脚本里，用命令行参数传；仓库内不放厂家资料。`make_system_wiring.py` 与这三张接线图沿用旧口径（引脚号、电压、路径以 [硬件与接线](../硬件与接线.md) 与 [scripts/deploy/manifest.tsv](../../scripts/deploy/manifest.tsv) 为准；改数值先改文档，再改脚本重跑），尚未按上面的 1:1 字号门槛重画——它们是实物照片的补充，不占报告版心。
