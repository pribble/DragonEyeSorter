# scripts/deploy/ — 板端部署清单与上板

> 板端要跑的东西来自三处（脚本、配置、编译产物），以前只在文档的几条 `scp` 命令里；本目录把它们登记成**一份清单**，并提供一个部署器。**二进制不入库**（AGENTS.md 硬性约定第 7 条）。
>
> 本目录只管"**哪份上到哪**"（机器读的 `manifest.tsv`）；板上整体的结构与规矩（两个分区、四类文件、各件缺了会怎样、行尾 / 权限、路径与相对引用）见 [docs/板端布局](../../docs/板端布局.md)。

## 谁进清单：只放产品运行时

判据是**用途，不是名字**。清单里只有"项目跑通后板上必须存在"的东西，一共 10 个：

| 类别 | 文件 | 源（仓库内） |
|---|---|---|
| **路径源** | **`des.env`** —— 板端路径唯一来源，`.sh` 与 `des_app` 都从它取 | `ps/board/` |
| 主程序 | `des_app` | `ps/build/`（交叉编译，不入库） |
| 配置 | `des_app.json`、`recipes.json`、`label_list.json` | `ps/src/`、`models/` |
| NPU 模型 | `part_cls.json`、`part_cls_uint8.onnx` | `models/`（`train/export_npu.py` 导出，不入库） |
| 自启链脚本 | `des_service.sh` → `can_up.sh` + `des_app.sh` | `ps/board/` |
| 板端目录 | `/root/des/`（全部 10 个都落这里） | — |

**不进清单、要用时手动传上去**（用完可以删，别在板子上积）：

- 调试 / 陪测：`desreg`（寄存器、舵机总线、步进）、`canplc`（扮「产线 PLC」）
- 测试程序：`test_scheduler`、`test_calib`、`test_measure`、`test_belt_hil`、`test_can`、`test_npu`、`bench_cma`
- 测试脚本：`pl_check.sh`（换比特流回归）、`soak.sh`（长跑）、`display_check.sh`（HDMI 黑屏对照）

```bash
scp ps/build/desreg ps/build/canplc ps/build/test_* $BOARD_HOST:/root/bin/
scp ps/board/pl_check.sh ps/board/soak.sh ps/board/display_check.sh $BOARD_HOST:/root/des/
```

（完整清单与理由写在 [`manifest.tsv`](manifest.tsv) 末尾。）测试件走本目录只会让"板上跑的到底是哪一份"更难看清，而且它们不是产品的一部分。

**板上生成、别覆盖**：`autostart.conf`、`ui_params.json`、`calib.json`、`data/`（CSV、NG 图、`service.log`）。
**系统与厂家件（我们不传也不动）**：`/opt`（ArmNN、OpenCV、厂家 `.ko`）、`/usr/share/fonts/wqy-zenhei`（UI 中文）、`/etc/init.d/S90des`（软链接）、`/mnt/mmcblk0p1/BOOT.bin`（换比特流走 [../bootbin/](../bootbin/)）。

## 用法

```bash
# Windows
.\scripts\deploy\deploy.ps1 -DryRun      # 只列差异（板上 vs 本地/仓库），不传文件
.\scripts\deploy\deploy.ps1              # 部署：刷新 out/ → 差异 → 传输 → 校验
.\scripts\deploy\deploy.ps1 -Stage       # 只刷新 out/（要上板的那一份，拷 U 盘/打包用）
.\scripts\deploy\deploy.ps1 -Only des_app -WithService   # 只推路径含 des_app 的条目，并自动停/起自启服务
.\scripts\deploy\deploy.ps1 -Dest /root/des/test         # 整份另装到该目录，试新版而不覆盖生产件

# Git Bash / WSL 同一个脚本
bash scripts/deploy/deploy.sh --dry-run
```

| 场景 | 用哪条 |
|---|---|
| "板上跑的到底是哪一份？和仓库差在哪？" | `--dry-run`（文本类比较前去掉 CR，不会因 CRLF/LF 误报；只读，不写盘） |
| 改完代码要上板 | `make -C ps all` 后跑部署；`--with-service` 可让脚本自己停/起自启服务 |
| **先在板上试新版，不动现在跑的这份** | `--dest /root/des/test`（见下节） |
| 板子没网、要给队友一份、或要拷 U 盘 | `--stage`（**不联网**，板子不在线也能攒；产物在 `out/`，文本件入库、二进制不入库） |

### 先试再上：`--dest <目录>`

清单照旧，只把**落点**整份换到该目录下（文件名平铺，不改清单、不改 `out/`），所以生产线上的 `/root/des/*` 一个字节都不动、自启服务也不用停。板上那份怎么跑：

```bash
ssh $BOARD_HOST /root/des/test/des_app.sh live -n 20     # 起测试那份（-c 取自测试目录自己的 des.env）
ssh $BOARD_HOST /root/des/test/des_service.sh start      # 测试目录自己的服务：管测试那份，不碰生产
```

**路径从哪来：`des.env`（板端路径唯一来源）**

整套部署目录里**只有 `des.env` 含路径**，其余 `.sh` 与 `des_app` 都从它取（源文件 [`ps/board/des.env`](../../ps/board/des.env)，源码里不许再写死）：

- **脚本**：`SELF=$(readlink -f "$0")` 算出真实目录 → `DES_HOME=…` → `. "$DES_HOME/des.env"`。`readlink -f` 保证经 `/etc/init.d/S90des` **软链**调用时也认得自己在 `/root/des/`；`DES_HOME` 由脚本自己算、不吃继承值（不会被别的目录带偏）；`des.env` 缺失时**直接报错**，不静默退化。
- **`des_app`**：不读这个文件，读**source 后 export 的环境变量**——`-c` 缺省 = `$DES_CONFIG`（没设就是当前目录的 `des_app.json`，**不再写死 `/root/des/…`**），`collect` 落盘 = `$DES_COLLECT`（`--out` 优先）。
- **派生规则**：本目录自己的路径全由 `DES_HOME` 派生（`$DES_HOME/data/…`、`autostart.conf`、PID 文件…）→ 拷到别处部署就跟着走；厂家系统件与全板共用工具（`$DES_MIPICAM_KO`、`$DES_TOOLS=/root/bin`）是跨目录的，按绝对值登记在 `des.env`，换镜像 / 换板子只改那一处。
- 每个变量都可先 `export` 覆盖：改路径不用动脚本。

于是测试目录是**自洽**的：`/root/des/test/des_service.sh` 管测试那份（日志与 PID 文件都在 `/root/des/test/data/`，PID 文件不再全局唯一，两份服务不打架）。另外几点：

- 配置里的相对引用（`label_list`、`recipes.json`、`calib.json`、`ui_params.json`、NPU 模型名、`storage.dir`）全部按**配置所在目录**解析（`ps/src/app/config.cpp` 的 `dir_of`）；
- **真跑之前先停自启服务**：相机 DMA、舵机总线、步进只有一个主人（`des_app.sh` 自己也会拒绝在 `des_app` 已在跑时启动）；
- ⚠ 别裸跑 `./des_app`（不带 `-c`）：缺省是**当前目录**的 `des_app.json`（环境 `DES_CONFIG` 优先）——在别处裸跑会响亮报"打不开配置"，不再静默用生产配置；一律用 `./des_app.sh`；
- `--dest` 与 `--with-service` 互斥（测试目录不该动自启服务，脚本会直接报错）。

`out/` 就是要上板的那一份，**文件平铺**（不再按 `des/`、`bin/` 分层——那点嵌套没有信息量）：

- **每次部署都会先 `rm -rf` 重建它**（`make -C ps all` 也会顺手刷一遍），所以编出来的二进制不用手工攒，**也别往里手放东西**（会被抹掉；测试件直接 `scp`，不要借道这里）；
- 哪个文件该去哪，看同目录的 `MD5SUMS.txt`：左列是哈希（文本类去掉 CR 后算），右列是它的板端路径；
- 拷 U 盘 / 给队友：整份拷走即可；要落位就跑部署器（它按 `manifest.tsv` 放，不依赖目录结构）。
- 清单里的文件本地没有（`des_app` 没编、模型没导出）会明确提示：模型缺了板上 NPU 分类就是关的（日志 `npu: cannot open …`）。

## `manifest.tsv`（TAB 分隔，5 列）

| 列 | 含义 |
|---|---|
| 源 | 仓库内相对路径；缺了就报"本地缺产物"并给编译提示 |
| 板端路径 | 绝对路径 |
| 类别 | `script` / `config` / `data` / `bin` / `model`；只决定装完的权限（`script`/`bin` 700，其余 600）——比哈希去不去 CR 由**文件类型**定，不看这一列 |
| 重启服务 | `yes` = `des_app` 跑着时改完要 `des_service.sh restart` 才生效 |
| 说明 | 给人看的备注 |

**加新上板文件时**：先问"项目跑通后还需要它吗"——是产品件才往 `manifest.tsv` 加一行，再跑 `--dry-run` 验证；**板端脚本的唯一源仍是 `ps/board/`，配置的唯一源是 `ps/src/` 与 `models/`** —— 本目录只登记"哪份上到哪"，不放副本。

## 三个坑（脚本已经替你绕开，手工 scp 时要记住）

1. **行尾**：仓库工作区里 `*.json` / `*.md` 是 CRLF（`.gitattributes` 的 `* text=auto`），板上习惯是 LF。直接 `md5sum` 对比会把每一个文本文件都报成"不同"——`deploy.sh` 对文本类（`.sh`/`.env`/`.json`/`.tsv`/`.conf`/`.txt`）一律先 `tr -d '\r'` 再比，二进制（`des_app`、`.onnx`）按字节比。
   **脚本上板一定不会带 CR**：`.gitattributes` 给 `*.sh` 钉了 `text eol=lf`，Windows 工作区里拿到的就是 LF（实测 `out/` 里三个 `.sh` 的 CR 字节数为 0），`scp` 按字节搬，所以板上 `#!/bin/sh` 能正常执行。三个 `*.json` 会带 CR 上板——JSON 里 CR 是合法空白，`nlohmann` 解析不受影响（`des_app` 的配置、`label_list.json` 都走它）；只是别拿"文本编辑器另存"的手工副本替换，CRLF 的 `.sh` 在板上会直接报 `not found`。
2. **Text file busy**：`/root/des/des_app` 正在运行时覆盖它会失败。脚本检测到 `pidof des_app` 且要更新 `des_app` 时会**直接报错退出**（不会传到一半），要么先 `des_service.sh stop`，要么加 `--with-service`。
3. **覆盖正在跑的 `des_service.sh`**：`cp -f` 是原地截断同一个 inode，而 `ash` 是边读边执行的，会读到半新半旧；而且新旧版的 PID 文件位置不同（新版在 `<目录>/data/des_service.pid`，旧版在 `/var/run/des_service.pid`）。**正式部署**时检测到监督进程在跑会**拦下**，要 `--with-service`（stop 由旧脚本执行、读的正是旧 PID 文件，起服务由新脚本执行）；`--dest` 写测试目录不受此限制（生产没被碰）。

## 不做什么

- **不编译**：产物来自 `ps/build/`（`make -C ps all`）。`make -C ps all` 结束时会让本脚本刷一次 `out/`，编不出来就只是提示，不影响编译结果。
- **`out/` 里只有能编出来的二进制不入库**：`out/` 的**脚本与配置入库**（它们就是发到板子上的那一份，是"这一版部署了什么"的凭据，改一次提交一次），被 `.gitignore` 挡住的只有 `scripts/deploy/out/des_app` 与 `ps/build/`（换机器 `make -C ps all` 重新编，或从队友的 `out/` 拷）。所以 `--stage` / `make` 刷新后 `git status` 出现文本件 diff 是正常的，提交它们即可。
- **不动系统件**：`/opt`、`/etc`、`/mnt/mmcblk0p1` 一概不碰；开机自启的安装/卸载见 [环境与上手](../../docs/环境与上手.md) §4「启动脚本」。
- **不管测试 / 调试件**：它们不走本目录（手动 `scp`），所以 `--dry-run` 只对上面那 10 个文件负责——板上的 `test_npu`、`soak.sh` 是哪一版，本目录不跟踪。
- **不反向拉取**：板端新建/手改的文件不会被自动取回仓库；要在板上改脚本，改完把内容搬回 `ps/board/` 再提交（`--dry-run` 能告出"板上与仓库不一致"的文件，但只对清单里已有的那些）。
