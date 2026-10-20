# scripts/deploy/ — 板端部署清单与上板

> 板端要跑的东西来自三处（脚本、配置、编译产物）。本目录把它们登记成**一份清单**并提供一个部署器；**二进制不入库**（[AGENTS.md](../../AGENTS.md)「红线」· 入库边界）。本目录只管「**哪份上到哪**」（`manifest.tsv`）；板上整体结构、两个分区、四类文件、各件缺了会怎样、行尾 / 权限、路径解析见 [docs/板端布局](../../docs/板端布局.md)。

## 谁进清单：只放产品运行时

判据是**用途，不是名字**。清单只放「项目跑通后板上必须存在」的东西，**条目数以 [`manifest.tsv`](manifest.tsv) 为准**：

| 类别 | 文件 | 源（仓库内） |
|---|---|---|
| 路径源 | `des.env` —— 板端路径唯一来源，`.sh` 与 `des_app` 都从它取 | [`out/`](out/)（**就地维护**） |
| 主程序 | `des_app` | `ps/build/`（交叉编译，不入库） |
| 配置 | `des_app.json`、`recipes.json`、`label_list.json` | `ps/src/`、`models/` |
| NPU 模型 | `part_cls.json`、`part_cls_uint8.onnx` | `models/`（`train/export_npu.py` 导出，不入库） |
| 自启链脚本 | `des_service.sh` → `can_up.sh` + `des_app.sh`；`shutdown.sh` | [`out/`](out/)（**就地维护**） |
| 板端目录 | 全部落 `/root/des/` | — |

**板端脚本与 `des.env` 的唯一源就是 `out/`**：它们住在要上板的目录里，直接改、直接提交，不再有「源 + 副本」两份。配置与主程序仍是「别处是源、`out/` 是刷进来的那一份」：改配置改 `ps/src/`，改完刷新。

**不住在清单里、但要跟产品脚本一起上板**（住在 `out/`，部署器不传，手动 `scp`）：

- 测试脚本：`pl_check.sh`（换比特流回归）、`soak.sh`（长跑）、`display_check.sh`（HDMI 黑屏对照）、`des_tool.sh`（`des_tool` 的启动器）
- 运维 / 调试 / 陪测 / 测试程序：`des_tool`、`desreg`、`canplc`、`test_*`（四层产物的划分见 [ps/README](../../ps/README.md)）

```bash
scp ps/build/desreg ps/build/canplc ps/build/des_tool ps/build/test_* $BOARD_HOST:/root/bin/
scp scripts/deploy/out/pl_check.sh scripts/deploy/out/soak.sh scripts/deploy/out/display_check.sh scripts/deploy/out/des_tool.sh $BOARD_HOST:/root/des/
```

测试件走本目录只会让「板上跑的到底是哪一份」更难看清，而且它们不是产品的一部分。

## 用法

```bash
# Windows
.\scripts\deploy\deploy.ps1 -DryRun      # 只列差异（板上 vs 本地/仓库），不传文件
.\scripts\deploy\deploy.ps1              # 部署：刷新 out/（脚本与 des.env 就地不动）→ 差异 → 传输 → 校验
.\scripts\deploy\deploy.ps1 -Stage       # 只刷 out/（拷 U 盘/打包用），不碰板子
.\scripts\deploy\deploy.ps1 -Only des_app -WithService   # 只推路径含 des_app 的条目，并自动停/起自启服务
.\scripts\deploy\deploy.ps1 -Dest /root/des/test         # 整份另装到该目录，试新版而不覆盖生产件

# Git Bash / WSL 同一个脚本
bash scripts/deploy/deploy.sh --dry-run
```

| 场景 | 用哪条 |
|---|---|
| 「板上跑的到底是哪一份？和仓库差在哪？」 | `--dry-run`（文本类比较前去掉 CR，不会因 CRLF/LF 误报；只读，不写盘） |
| 改完代码要上板 | `make -C ps all` 后跑部署；`--with-service` 可让脚本自己停 / 起自启服务 |
| **先在板上试新版，不动现在跑的这份** | `--dest /root/des/test`（见下节） |
| 板子没网、要给队友一份、或要拷 U 盘 | `--stage`（**不联网**，板子不在线也能攒；产物在 `out/`，文本件入库、二进制不入库） |
| 板端脚本 / `des.env` 改了 | 直接改 `out/` 里那一份并提交（它就是产品的源），再按上面任一条上板 |

### 先试再上：`--dest <目录>`

清单照旧，只把**落点**整份换到该目录下（文件名平铺，不改清单、不改 `out/`），所以生产线上的 `/root/des/*` 一个字节都不动、自启服务也不用停：

```bash
ssh $BOARD_HOST /root/des/test/des_app.sh live -n 20     # 起测试那份（-c 取自测试目录自己的 des.env）
ssh $BOARD_HOST /root/des/test/des_service.sh start      # 测试目录自己的服务：管测试那份，不碰生产
```

测试目录是**自洽**的：日志与 PID 文件都在 `/root/des/test/data/`，两份服务不打架。注意：配置里的相对引用按**配置所在目录**解析（`ps/src/app/config.cpp` 的 `dir_of`）；**真跑之前先停自启服务**（相机 DMA、舵机总线、步进只有一个主人）；别裸跑 `./des_app`（不带 `-c` 时缺省是当前目录的 `des_app.json`，一律用 `./des_app.sh`）；`--dest` 与 `--with-service` 互斥。

**路径从哪来：`out/des.env`**（唯一源；细节见 [板端布局](../../docs/板端布局.md) §4）：脚本用 `readlink -f` 算真实目录并 source 它，缺了直接报错；`des_app` / `des_tool` 读 source 后 export 的变量（`-c` 缺省 `$DES_CONFIG`，`collect` 落盘 `$DES_COLLECT`）；跨目录绝对值只在 `des.env` 登记（`DES_TOOLS`、`DES_MIPICAM_KO`）；每个变量都可先 `export` 覆盖。

## `out/` 是什么

`out/` 就是要上板的那一份，**文件平铺**，同时是**板端脚本与 `des.env` 的唯一源**：

- **刷新是「刷 + 清」，不是重建**：`--stage` / 部署 / `make -C ps all` 会把源在 `out/` 之外的条目拷进来（`ps/build/des_app`、`ps/src` 与 `models` 的配置、导出的模型），把不属于清单与手动清单的文件清掉（打印清掉了什么），**手写的脚本与 `des.env` 就地不动**。
- 同一份 `out/` 里三类文件要分清：**手写源**（`des.env`、自启链 4 个脚本、手动上板的 3 个测试脚本与 `des_tool.sh`）、**刷进来的**（别在这里改，改了下次刷新就没了）、**生成的**（`MD5SUMS.txt`）。
- 哪个文件该去哪看 `MD5SUMS.txt`：左列哈希（文本类去掉 CR 后算），右列板端路径；最后一节是手动 `scp` 的测试脚本与 `des_tool.sh`。要落位就跑部署器（按 `manifest.tsv` 放，不依赖目录结构）；清单里的文件本地没有（`des_app` 没编、模型没导出）会明确提示：模型缺了板上 NPU 分类就是关的。

## `manifest.tsv`（TAB 分隔，5 列）

| 列 | 含义 |
|---|---|
| 源 | 仓库内相对路径；缺了就报「本地缺产物」并给编译提示 |
| 板端路径 | 绝对路径 |
| 类别 | `script` / `config` / `data` / `bin` / `model`；只决定装完的权限（`script` / `bin` 700，其余 600）—— 比哈希时去不去 CR 由**文件类型**定，不看这一列 |
| 重启服务 | `yes` = `des_app` 跑着时改完要 `des_service.sh restart` 才生效 |
| 说明 | 给人看的备注 |

**加新上板文件时**：先问「项目跑通后还需要它吗」—— 是产品件才往 `manifest.tsv` 加一行（源写 `scripts/deploy/out/<名>`，把文件放在 `out/`），再跑 `--dry-run` 验证；只是测试 / 手动件就加进 `deploy.sh` 的 `manual_files`（否则刷新时会当垃圾清掉）。

## 手工 scp 的三个易错点（部署器已处理）

1. **行尾**：仓库工作区里 `*.json` / `*.md` 是 CRLF，板上习惯 LF。直接 `md5sum` 对比会把每个文本文件都报成「不同」——部署器对文本类（`.sh` / `.env` / `.json` / `.tsv` / `.conf` / `.txt`）先 `tr -d '\r'` 再比，二进制按字节比。`.gitattributes` 给 `*.sh` 钉了 `eol=lf`，脚本上板一定不带 CR；三个 `*.json` 带 CR 上板无妨（JSON 里 CR 是合法空白）。别用「文本编辑器另存」的手工副本替换：CRLF 的 `.sh` 在板上直接报 `not found`。
2. **Text file busy**：`/root/des/des_app` 正在运行时覆盖它会失败。脚本检测到 `pidof des_app` 且要更新 `des_app` 时**直接报错退出**（不会传到一半），要么先 `des_service.sh stop`，要么加 `--with-service`。
3. **覆盖正在跑的 `des_service.sh`**：`cp -f` 原地截断同一 inode，而 `ash` 边读边执行，会读到半新半旧。正式部署检测到监督进程在跑会**拦下**，要 `--with-service`（stop 由旧脚本执行、起服务由新脚本执行）；`--dest` 写测试目录不受此限制。

## 不做什么

- **不编译**：产物来自 `ps/build/`（`make -C ps all`）。`make -C ps all` 结束时会刷新一次 `out/`，编不出来只提示，不影响编译结果。
- **`out/` 里只有能编出来的二进制不入库**：脚本、`des.env` 与配置入库（它们就是发到板子上的那一份，改一次提交一次）；被 `.gitignore` 挡住的只有 `scripts/deploy/out/des_app` 与 `ps/build/`。刷新后 `git status` 出现文本件 diff 是正常的，提交即可。
- **不动系统件**：`/opt`、`/etc`、`/mnt/mmcblk0p1` 一概不碰；开机自启的安装 / 卸载见 [环境与上手](../../docs/环境与上手.md) §4。
- **不管测试 / 调试件**：它们不走部署（手动 `scp`），`--dry-run` 只对清单里的条数负责，不跟踪板上 `test_npu` / `soak.sh` 是哪一版。
- **不反向拉取**：板端新建 / 手改的文件不会被自动取回仓库；要在板上改脚本，改完把内容搬回 `scripts/deploy/out/` 再提交（`--dry-run` 只对清单里已有的文件告警）。
