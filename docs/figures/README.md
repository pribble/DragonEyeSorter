# figures/ — 技术文档、作品报告与 PPT 的插图（脚本生成，不手工画）

插图由脚本生成，数值与标签写在脚本里，改完重跑即与文档一致。生成结果（`fig_*.svg` / `fig_*.png` 与两套 `fig_meta.json`）入库；插图的引用位置见 [技术文档](../../submission/技术文档.md) 与 [硬件与接线](../硬件与接线.md)。

## 一条必须守住的规则：按落版尺寸作图

画布单位是 **pt，画布宽固定 447.9pt = 报告落版宽 15.8cm**，所以**图里写的字号就是纸上的字号**。

插图必须**按落版尺寸 1:1 作画**：若按 16in（1152pt）画布作图、再缩到 15.8cm 落版，图里 8pt 的字落到纸面只剩约 3.1pt，整片文字读不动。作图门槛：**任何一张图的最小字号 ≥ 7.5pt**（设计值）；报告侧自检按 `paper/fig_meta.json` 折算的等效落版字号，门禁 **≥ 7pt**（`verify_report.py` 的 `MIN_FIG_PT=7.0`）。配套三条约定：

- 字号只取 `figkit.FS` 的档位：框内正文 8.5pt / 次级与注释 7.5pt / 组标题与面板标题 9.5pt，不在画图代码里写别的数字。
- 标签短句化：每行 ≤ 12 个汉字当量，细节写进正文；**不画图内标题**（标题由报告图注、PPT 页标题承担）。
- 文字一律用 `figkit.box()` / `text()` / `group()` 画：保存前核对**是否越出盒子或画布**（> 0.5pt 报错，< 1.5pt 提示「偏紧」），并打印每张图的最小字号与最小盒内余量。排版靠数据，不靠目测。

## 两套主题：同一份脚本、同一套坐标

五张图只画一次坐标，`figkit` 按主题换渲染。两套图的画布、坐标、最小字号完全相同，**只有装饰不同**：

| 主题 | 输出目录 | 用在哪 | 视觉语言 |
|---|---|---|---|
| `slide`（默认） | `figures/` | 答辩 PPT | 浅色编辑风：卡片 + 左侧强调色条、浅灰分组面板、彩色箭头标签 |
| `paper` | `figures/paper/` | 技术文档 / 作品报告 | 期刊线稿风：白底、方框细边、虚线分组框、黑色细线箭头、Times 西文 + 宋体中文 |

```powershell
python docs/figures/make_figures.py            # slide 主题 → figures/（PPT 用）
python docs/figures/make_figures.py --paper    # paper 主题 → figures/paper/（报告用）
```

**改图后两套都要重跑**：报告引用 `figures/paper/…`，PPT 引用 `figures/…`；`verify_report.py` 按 `paper/fig_meta.json` 复核落版字号，`make_ppt.py` 按 `fig_meta.json` 取重点框。

- slide：白画布 + 浅灰分组面板（左上角强调色小标题 + 0.7pt 细线）；卡片极浅同色底 + 左侧 2pt 强调色条 + 0.7pt 灰边，框内文字 8.5pt 粗体名称 / 7.5pt 次级灰；强调色 PL 青绿 `#0D9488`、PS 蓝 `#2563EB`、NPU / 警示琥珀 `#B45309`、安全红 `#B91C1C`、外部设备灰蓝 `#64748B`，只用在色条、小标题与箭头标签；中性灰 `#8A99A8` 0.9pt 细线 + 小箭头。
- paper：白底无面板，分组用灰色虚线圆角框 + 黑色小标题；卡片方角 0.7pt 深灰细边、白底、不放色条，NPU / 警示块用浅灰底；西文数字 Times New Roman、中文宋体，粗体处中文走黑体；黑色 0.7~0.9pt 细线 + 小箭头。
- 数据表型插图（图 3-1 寄存器段表）：slide 用行卡 + 偏移 chip + 斑马底；paper 用**三线表**（顶线 / 底线 + 行间极浅分隔），列不加竖线、不用行卡。交换协议 / 方向必须说明时才加 7.5pt 标签。

## 文件

| 文件 | 用途 |
|---|---|
| `figkit.py` | 公共画布 / 图元（框、组、箭头、折线）/ 两套主题 / 越界与字号校验 / 导出 SVG+PNG+`fig_meta.json` |
| `make_figures.py` | 一键重画全部 5 张（`--paper` 出报告那套；可跟关键字只画其中几张） |
| `fig_architecture.py` | 图 2-1 系统架构（另一张新图的写法照它抄） |
| `fig_timebase.py` | 图 2-2 时间基准（拍摄瞬间锁存带位置） |
| `fig_pl_regs.py` | 图 3-1 PL 自研模块、寄存器窗口与管脚 |
| `fig_pick_sequence.py` | 图 4-1 良品抓取 / NG 导流挡板时序 |
| `fig_layout.py` | 图 7-1 工位布局（俯视，按比例） |
| `paper/` | `--paper` 主题的输出（5 张 SVG + PNG + `fig_meta.json`），报告引用这一套 |
| `make_system_wiring.py` | `fig_system_wiring.png`（整机接线：市电 / 24V / 急停 / 步进 / 舵机 6.8V / 5V / 编码器 / 电平转换 / 挡板 / J12 引脚 / J11 CAN；实线已接、虚线待接） |
| `make_step_wiring.py` / `make_servo_lv_wiring.py` | 步进四信号、BSS138 低压侧三针在 J12 上的位置（叠加厂家位号图） |

单张调试：`python docs/figures/fig_timebase.py`（跑单张 + 打印该图门禁），加 `--paper` 出报告那套。导出 SVG（矢量、文字可编辑）+ PNG 宽 3200px（报告满版心 15.8cm 时 ≈514 dpi）。两套 `fig_meta.json` 是下游依据：画布尺寸、`px_per_pt`、最小字号，以及 slide 版的 **PPT 重点框区域**（`regions`，源 PNG 像素、左上原点）。`submission/build/make_ppt.py` 读 `fig_meta.json`，`verify_report.py` 读 `paper/fig_meta.json` 的 `min_fs_pt`。**改插图后两套都重跑。**

## 接线图（厂家资料叠加）

`make_step_wiring.py` / `make_servo_lv_wiring.py` 的位号图 PDF 路径用命令行参数传，仓库内不放厂家资料。`make_system_wiring.py` 与三张接线图沿用旧口径（引脚号、电压、路径以 [硬件与接线](../硬件与接线.md) 与 [scripts/deploy/manifest.tsv](../../scripts/deploy/manifest.tsv) 为准；改数值先改文档再改脚本），尚未按 1:1 字号门槛重画——它们是实物照片的补充，不占报告版心。
