# figures/ — 作品报告插图（脚本生成，不手工画）

本目录是报告插图的**唯一位置**：5 张图的生成脚本入库，生成物（SVG / PNG / `fig_meta.json`）由 `make_figures.py` 重画、不入库（见 `.gitignore`）。只有作品报告用这一套——[技术文档](../技术文档.md) 按 `figures/fig_*.png` 引用，[build.ps1](../build/build.ps1) 每次先重画再排进 PDF，[verify_report.py](../build/verify_report.py) 读本目录的 `fig_meta.json` 复核落版字号。

仓库工程文档用的整机接线图不在这里：它在 [docs/figures/](../../docs/figures/)（`make_system_wiring.py`，给 [硬件与接线](../../docs/硬件与接线.md) 用，不占报告版心）。

## 一条必须守住的规则：按落版尺寸作图

画布单位是 **pt，画布宽固定 447.9pt = 报告落版宽 15.8cm**，所以**图里写的字号就是纸上的字号**。

插图必须**按落版尺寸 1:1 作画**：若按 16in（1152pt）画布作图、再缩到 15.8cm 落版，图里 8pt 的字落到纸面只剩约 3.1pt，整片文字读不动。作图门槛：**任何一张图的最小字号 ≥ 7.5pt**（设计值）；报告侧自检按本目录 `fig_meta.json` 折算的等效落版字号，门禁 **≥ 7pt**（`verify_report.py` 的 `MIN_FIG_PT=7.0`）。配套三条约定：

- 字号只取 `figkit.FS` 的档位：框内正文 8.5pt / 次级与注释 7.5pt / 组标题 9.5pt，不在画图代码里写别的数字。
- 标签短句化：每行 ≤ 12 个汉字当量，细节写进正文；**不画图内标题**（标题由报告图注承担）。
- 文字一律用 `figkit.box()` / `text()` / `group()` 画：保存前核对**是否越出盒子或画布**（> 0.5pt 报错，< 2pt 提示「偏紧」），并打印每张图的最小字号与最小盒内余量。排版靠数据，不靠目测。

## 渲染：期刊线稿风（唯一一套）

白底、方框细边、灰色虚线分组框、黑色细箭头、Times 西文 + 宋体中文；颜色不参与信息表达，只用黑 / 灰，分组靠标签与虚线框。数据表型插图（图 3-1 寄存器段表）用**三线表**（顶线 / 底线 + 行间极浅分隔），列不加竖线、不用行卡；交换协议 / 方向必须说明时才加 7.5pt 标签。

## 用法

```powershell
python submission/figures/make_figures.py                # 重画全部 5 张
python submission/figures/make_figures.py arch time      # 只重画名字里含 arch / time 的
python submission/figures/fig_timebase.py                # 单张调试（跑单张 + 打印该图门禁）
```

导出 SVG（矢量、文字可编辑）+ PNG 宽 3200px（报告满版心 15.8cm 时 ≈514 dpi），同时写 `fig_meta.json`（画布尺寸、`px_per_pt`、最小字号）。**改插图后重跑一次即可**（只有一套图）；生成物不入库（重画即得），本目录只提交脚本与 README。`build.ps1` 每次出报告前会自动先跑本脚本（第 0/3 步），所以改完图脚本直接重出报告也行。

## 文件

| 文件 | 用途 |
|---|---|
| `figkit.py` | 公共画布 / 图元（框、组、箭头、折线）/ 线稿风渲染 / 越界与字号校验 / 导出 SVG+PNG+`fig_meta.json` |
| `make_figures.py` | 一键重画全部 5 张（可跟关键字只画其中几张） |
| `fig_architecture.py` | 图 2-1 系统架构（另一张新图的写法照它抄） |
| `fig_timebase.py` | 图 2-2 时间基准（拍摄瞬间锁存带位置） |
| `fig_pl_regs.py` | 图 3-1 PL 自研模块、寄存器窗口与管脚 |
| `fig_pick_sequence.py` | 图 4-1 良品抓取 / NG 导流挡板时序 |
| `fig_layout.py` | 图 7-1 工位布局（俯视，按比例） |
| `.gitignore` | 忽略下面的生成物：本目录只入库脚本 + README |
| `fig_*.svg` · `fig_*.png` · `fig_meta.json` | 生成物（**不入库**，跑 `make_figures.py` 重建）：技术文档引用 PNG，SVG 留作矢量编辑 |
