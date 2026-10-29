# figures/ — 作品报告插图（脚本生成，不手工画）

本目录是报告插图的**唯一位置**：7 张图的生成脚本入库——5 张线稿图（`fig_*.py`）+ 2 张「待插入实物照片」占位图（`make_placeholders.py` + `placeholder_manifest.json`）；生成物（SVG / PNG / `fig_meta.json`）由 `make_figures.py` 重画、不入库（见 `.gitignore`）。报告 7 张图按编号：图 1 系统架构、图 2 时间基准、图 3 PL 寄存器与引脚、图 4 抓取 / 导流时序、图 5 HDMI 界面五页、图 6 七类缺陷样件、图 7 工位布局。只有作品报告用这一套——[技术文档](../技术文档.md) 按 `figures/fig_*.png`、`figures/ph_ui.png`、`figures/ph_samples.png` 引用，[build.ps1](../build/build.ps1) 每次先重画再排进 PDF，[verify_report.py](../build/verify_report.py) 读本目录的 `fig_meta.json` 复核落版字号（只登记报告实际引用的 7 条）。

**其余 5 项实物照片**（整机全景 / 开发板特写 / 机械臂 / 导流挡板 / 测试现场）**不进报告版面**：作品照片是单独必交材料，报告不插这 5 张；它们只作为「作品照片 + 演示视频」的拍摄清单保留在本目录（`placeholder_manifest.json` 里 `in_report: false` 的 5 项 + 本文「作品照片与视频拍摄清单」一节），**不生成 PNG、不占报告图号**。

仓库工程文档用的整机接线图不在这里：它在 [docs/figures/](../../docs/figures/)（`make_system_wiring.py`，给 [硬件与接线](../../docs/硬件与接线.md) 用，不占报告版心）。

## 一条必须守住的规则：按落版尺寸作图

画布单位是 **pt，画布宽固定 447.9pt = 报告落版宽 15.8cm**，所以**图里写的字号就是纸上的字号**。

插图必须**按落版尺寸 1:1 作画**：若按 16in（1152pt）画布作图、再缩到 15.8cm 落版，图里 8pt 的字落到纸面只剩约 3.1pt，整片文字读不动。作图门槛：**任何一张图的最小字号 ≥ 7.5pt**（设计值）；报告侧自检按本目录 `fig_meta.json` 折算的等效落版字号，门禁 **≥ 7pt**（`verify_report.py` 的 `MIN_FIG_PT=7.0`）。配套三条约定：

- 字号只取 `figkit.FS` 的档位：框内正文 8.5pt / 次级与注释 7.5pt / 组标题 9.5pt，不在画图代码里写别的数字（占位图同理：9.5 / 8.5 / 7.5pt 三档）。
- 标签短句化：每行 ≤ 12 个汉字当量，细节写进正文；**不画图内标题**（标题由报告图注承担）。占位图的「说明 / 拍摄要求」两行是交件前照着拍的提示、不是图内标签，按整行排（≤ 40 汉字当量），不受 12 字限制。
- 文字一律用 `figkit.box()` / `text()` / `group()` 画：保存前核对**是否越出盒子或画布**（> 0.5pt 报错，< 2pt 提示「偏紧」），并打印每张图的最小字号与最小盒内余量。排版靠数据，不靠目测。

## 渲染：期刊线稿风（唯一一套）

白底、方框细边、灰色虚线分组框、黑色细箭头、Times 西文 + 宋体中文；颜色不参与信息表达，只用黑 / 灰，分组靠标签与虚线框。数据表型插图（图 3 寄存器段表）用**三线表**（顶线 / 底线 + 行间极浅分隔），列不加竖线、不用行卡；交换协议 / 方向必须说明时才加 7.5pt 标签。占位图沿用同一套观感（白底 + 灰色虚线边框 + 极浅灰填充），只是框里没有图元、换成三行提示字。

## 用法

```powershell
python submission/figures/make_figures.py                # 重画全部 7 张（5 线稿 + 2 报告内占位）
python submission/figures/make_figures.py arch time      # 只重画名字里含 arch / time 的线稿图
python submission/figures/make_figures.py ph             # 只重画 2 张报告内占位图（图 5 / 图 6）
python submission/figures/make_figures.py ui             # 只重画 id 含 ui 的占位图（图 5）
python submission/figures/fig_timebase.py                # 单张线稿调试（跑单张 + 打印该图门禁）
python submission/figures/make_placeholders.py           # 单跑报告内占位图（2 张 + 打印每张三行字宽度）
```

导出 SVG（矢量、文字可编辑）+ PNG 宽 3200px（报告满版心 15.8cm 时 ≈514 dpi），同时写 `fig_meta.json`（画布尺寸、`px_per_pt`、最小字号；只登记报告引用的 7 张，已移出报告的旧记录会被清掉）。**改插图后重跑一次即可**（只有一套图）；生成物不入库（重画即得），本目录只提交脚本 + README + 清单。`build.ps1` 每次出报告前会自动先跑本脚本（第 0/3 步），所以改完图脚本直接重出报告也行。

## 报告内占位图（2 张待拍实物照片）

报告只保留 2 张确有必要（评分直接证据）的实物照片占位：图 5 对应硬性② 人机界面，图 6 对应硬性④ 缺陷分类。占位图白底、灰色虚线边框、极浅灰填充，居中三行字——9.5pt 黑体「【待插入实物照片】」、8.5pt 说明（标题 / 机位 / 建议分辨率）、7.5pt 拍摄要求。**图内不写「图 N」编号**（图号由 PDF 里图下方的正式图注承担，避免重复编号）；**图注写最终说明**，替换实物照片时正文与图注一字不动。

| 图号 | 文件 | 内容 | 落版（宽 × 高 pt） | 目标像素 | 拍摄机位 | 章节 |
|---|---|---|---|---|---|---|
| 图 5 | `ph_ui.png` | HDMI 操作界面五页 | 447.92 × 251.96 | 3200 × 1800 | S 采集卡录屏 | IV-F 人机界面 |
| 图 6 | `ph_samples.png` | 零件与七类缺陷样件 | 447.92 × 298.61 | 3200 × 2133 | B 俯拍 | V-C 数据集 |

逐项的机位、光线、画面内容与拍摄要求（白平衡 / 快门 / ISO）在 [`placeholder_manifest.json`](placeholder_manifest.json) 的 `camera` / `light` / `scene` / `shoot` 字段；机位代号 A/B/C/D/E/S 与光线参数沿用 [演示视频脚本](../演示视频脚本.md) §3 与 §5。

占位图与线稿**同一条命令重建**：`python submission/figures/make_figures.py` 先画 5 张线稿、再画 2 张占位；每张 PNG 宽 3200 px，信息并入 `fig_meta.json`（与 5 张线稿同一结构，`min_fs_pt` = 7.5，供 verifier 算落版字号与 dpi）。

### 拍了实物照片之后：只换这 2 张 PNG，不动正文

正文里的 `![](figures/ph_ui.png)`、`![](figures/ph_samples.png)` 与图注已按最终说明写好，替换时**只动图、不动 [技术文档](../技术文档.md)**：

1. 按上表「目标像素」裁切 / 缩放照片（16:9 → 3200 × 1800；3:2 → 3200 × 2133；等效 514 dpi，相机 / 手机原图直出即可），存到本目录、覆盖同名 `ph_ui.png` / `ph_samples.png`。
2. 打开 `placeholder_manifest.json`，把该项 `"replaced"` 从 `false` 改 `true`。**必须改**：否则下次 `make_figures.py`（`build.ps1` 第 0 步会自动跑）会用占位图把照片盖掉。改后该项跳过不画，只按实物图实际像素更新 `fig_meta.json` 的尺寸。
3. 重出报告：`pwsh -File submission/build/build.ps1`（其余图照常重画）。

替换后的实物 PNG 建议入库留档：`.gitignore` 挡的是生成物，照片要 `git add -f submission/figures/ph_ui.png submission/figures/ph_samples.png` 强制加。

### 替换后怎么验

- `python submission/build/verify_report.py --final`：报告插图等效 dpi ≥ 300、落版最小字号 ≥ 7pt；`--final` 模式下 `in_report=true` 的占位图仍有 `replaced: false` 判 FAIL（交件前必须清零；报告外 5 项不参与该门禁）。
- 只验占位图本身：`python submission/figures/make_placeholders.py` 打印每张的最小字号（7.5pt）与三行字的实测宽度 / 左右余量；有越界或字号不足会打 `ERR` 并以退出码 1 结束。
- 核对 `fig_meta.json`：7 条齐全（5 线稿 + 图 5 / 图 6 两张占位），每条 `px_per_pt × 72 ≈ 514 dpi`（3200 px / 15.8cm）。

## 作品照片与视频拍摄清单（5 项，不进报告）

作品照片是单独必交材料（见 [提交材料清单](../提交材料清单.md)），以下 5 项**已从报告版面移出**，只按「作品照片 + 演示视频」拍摄使用；文件名沿用 `ph_*.png` 便于归档，但报告不引用、`make_figures.py` 不生成。逐项机位 / 光线 / 画面内容 / 分辨率见 `placeholder_manifest.json` 对应项（`in_report: false`）。

| 序号 | 拍摄内容 | 建议文件名 | 比例 / 像素 | 机位 | 用途 |
|---|---|---|---|---|---|
| 1 | 整机实物全景 | `ph_overview.png` | 16:9 · ≥3200×1800 | A 主三脚架（右前方 45°、高 1.2~1.4m） | 作品照片主图；V1 全景镜头 |
| 2 | 开发板与主要接口特写 | `ph_board.png` | 3:2 · ≥3200×2133 | E 微距 / B 俯拍（垂直向下） | 作品照片-硬件细节 |
| 3 | 六轴总线舵机机械臂与夹爪 | `ph_arm.png` | 3:2 · ≥3200×2133 | C 机械臂近景（安全半径外） | 作品照片-执行机构；V7 近景 |
| 4 | 侧边导流挡板与分流动作 | `ph_flap.png` | 3:2 · ≥3200×2133 | B 俯拍 + C 近景 | 作品照片-分流机构；V7 挡板镜头 |
| 5 | 测试现场（逻辑分析仪 / 卡尺） | `ph_test.png` | 3:2 · ≥3200×2133 | D 屏幕 / 仪器 + E 微距 | 作品照片-测试；V2 / V8 镜头 |

公共拍摄要求（5 项通用，细节见 manifest 每项 `shoot`）：关顶灯、两侧条形光为主、白卡纸底；白平衡手动 5600K 并全程锁定；快门 1/100~1/120（50 Hz 避频闪）；ISO 相机 ≤400、手机 ≤200；手动对焦锁死、拍摄中不变焦；1080p60 或原图直出。

## 文件

| 文件 | 用途 |
|---|---|
| `figkit.py` | 公共画布 / 图元（框、组、箭头、折线）/ 线稿风渲染 / 越界与字号校验 / 导出 SVG+PNG+`fig_meta.json`（`write_meta(keep=…)` 可只保留报告引用的图） |
| `make_figures.py` | 一键重画全部 7 张（5 线稿 + 2 报告内占位；可跟关键字只画其中几张） |
| `make_placeholders.py` | 报告内 2 张实物照片占位图（读 `placeholder_manifest.json` 的 `in_report: true`；其余 5 项不生成 PNG；`replaced` 项跳过并登记实物图尺寸） |
| `placeholder_manifest.json` | 7 项清单：报告内 2 项（`in_report: true`，图 5 / 图 6）+ 报告外 5 项作品照片 / 视频拍摄清单（`in_report: false`，`figure_no: null`） |
| `fig_architecture.py` | 图 1 系统架构（另一张新图的写法照它抄） |
| `fig_timebase.py` | 图 2 时间基准（拍摄瞬间锁存带位置） |
| `fig_pl_regs.py` | 图 3 PL 自研模块、寄存器窗口与引脚 |
| `fig_pick_sequence.py` | 图 4 良品抓取 / NG 导流挡板时序 |
| `fig_layout.py` | 图 7 工位布局（俯视，按比例） |
| `.gitignore` | 忽略下面的生成物：本目录只入库脚本 + README + 清单 |
| `fig_*.svg` · `fig_*.png` · `ph_ui.png` · `ph_ui.svg` · `ph_samples.png` · `ph_samples.svg` · `fig_meta.json` | 生成物（**不入库**，跑 `make_figures.py` 重建）：技术文档引用 PNG，SVG 留作矢量编辑；`fig_meta.json` 只登记报告实际引用的 7 条 |
