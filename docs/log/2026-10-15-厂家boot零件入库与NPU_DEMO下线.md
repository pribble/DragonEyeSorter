# 2026-10-15 厂家 boot 零件入库、NPU_DEMO 下线

## 背景

`.env` 里的 `NPU_DEMO` 是一条"廉价保险丝"：常规打包用不到它（零件已在 `parts/` 且缓存命中），只在 `parts/` 缺件、换机器、克隆仓库时才兜底去仓库外的厂家 demo 目录取件。用户判断这条保险丝太廉价——既然 `vendor-BOOT.bin` 本身不大且**不随 PL/应用重编而变**，就该让它进仓库，把兜底换成真零件。

## 决定

1. **`vendor-BOOT.bin` 入库**（4.94 MB）。它是 `u-boot.elf` 的**唯一来源**（按偏移 `0x40e4c0`、长度 `0xe2248` 从第 2 分区截出），且脚本对它做 MD5 硬校验。入库后打 BOOT.bin 彻底不依赖仓库外的厂家 demo 目录。
2. **`vendor.bit` 不入库**，只在 `parts/MD5SUMS.txt` 登记 MD5。它 3.86 MB 且只服务「原样重打包验证」一条回归路径，没有唯一性价值（我们自己的比特流在 `pl/soc_base/`）。
3. **`parts/MD5SUMS.txt` 入库**：零件台账，登记 4 个文件的 MD5 / 字节数 / 来源 / 是否入库。
4. **`.env` 删除 `NPU_DEMO`**。`.env.example` 里保留为注释掉的可选项（做 SD 镜像、重建零件时才填）。
5. **`make_sd_image.sh` 不受影响但改为显式传参**：它要厂家 demo 的 `boot/` 目录（`uInitrd.lz4` 一个就 72 MB，不进仓库），这条线仍然需要外部目录。

## 关键数字（实测）

| 文件 | 字节数 | MD5 | 入库 |
|---|---|---|---|
| `vendor-BOOT.bin`（厂家 `boot/BOOT.bin`） | 5,179,264 | `D6B2D387CB405444D1E7EF10E2855285` | ✔ |
| `u-boot.elf` | 992,360 | `E844648EC910C87EA8A036AD4D3D3E45` | ✔ |
| `fsbl.elf` | 1,767,704 | `A64A56C0C37003AF025C45ABF0DA2732` | ✔ |
| `vendor.bit`（厂家基线比特流） | 4,052,202 | `9C611D2724C8464BD3C0B887D5A3EB1A` | ✘ |

- `parts/fsbl.elf` 与厂家原件 `soc_fsbl/build/soc_fsbl.elf` **逐字节相同**（MD5 复核）。
- `vendor-BOOT.bin` 的 MD5 与脚本里原有硬校验值一致，无需改常量。

## 途中发现：`phy_1/soc_prj.bit` 不是厂家原件

脚本原先从 `<demo>/soc_prj/soc_prj_Runs/phy_1/soc_prj.bit` 取"厂家比特流"，但实测该路径的 MD5 是 `3B5DE345E5C18DD1F3245E6FCF9618DF`，**不是**厂家原件——它是本地重编后覆盖掉的产物（`soc_prj_Runs/` 是编译输出目录）。真正的干净原件在 **`soc_prj_Runs/best_result/soc_prj.bit`**（`9C611D27…`），与 `best_result/readMe.txt` 那句 "The implemented result of phy_1 is saved as best result." 相符。

结论：从 demo 取厂家比特流一律认 `best_result/`；`phy_1/` 只在我们从未重编过时才等于厂家件。已写进 `parts/MD5SUMS.txt` 与脚本报错提示。

## 代码改动

| 文件 | 改动 |
|---|---|
| `.gitignore` | `parts/*` 白名单增补 `vendor-BOOT.bin`、`MD5SUMS.txt`；注明 `vendor.bit` 为何仍忽略（含 MD5） |
| `scripts/bootbin/make_bootbin.ps1` | 删掉 D 盘硬编码兜底；零件改从 `parts/` 取；`Get-VendorDir` 缺参数时**显式报错并给指引**（不再静默指向不存在的路径）；新增 `Assert-VendorBoot`（MD5 硬校验，独立成函数）；`vendor.bit` 缺失时给出「用 `-Bit`」或「从 `best_result/` 取回」两条指引；输出改为打印三个输入的 MD5。顺带按 `AGENTS.md` 补齐 UTF-8 BOM 与 CRLF（原文件是 LF 无 BOM，与同目录其它 `.ps1` 不一致） |
| `scripts/bootbin/parts/MD5SUMS.txt` | 新增（台账） |
| `scripts/test_env.ps1` | 「NPU Demo 目录 / BOOT.bin / uInitrd」三项改为检查 `parts/` 四项 + `vendor-BOOT.bin` 的 MD5 校验；`vendor.bit` 列为 `[可选]`（缺失只影响基线回归）；顺带修掉 `Test-Path "$PSScriptRoot\..\env"`（应为 `$DES_ENV_LOADED`，原来恒为真、每次都重复加载 `.env`） |
| `.env` / `.env.make` | 删除 `NPU_DEMO`（`.env.make` 由脚本格式手工同步，原因见下） |
| `.env.example` | `NPU_DEMO` 改为注释掉的可选项并说明用途 |
| `scripts/env_loader.sh` / `.ps1` | 去掉 `NPU_DEMO` 的必需性检查与打印 |
| `scripts/sd_image/make_sd_image.sh` | 路径来源改为「位置参数优先 > `NPU_DEMO` 环境变量」；错误提示写明需要哪些文件、`.env` 已不设该变量 |
| `scripts/README.md`、`docs/环境与上手.md`、`docs/开发环境与构建.md`、`AGENTS.md` 第 7 条 | 同步零件台账、入库范围与「哪些场合才需要厂家 demo 目录」 |

## 验证

- `.gitignore`：`git check-ignore -q` 退出码 + `git status --ignored` 双重确认——`vendor-BOOT.bin` 未跟踪可入库（exit 1），`vendor.bit` 被忽略（exit 0）。
- `make_bootbin.ps1` / `test_env.ps1`：`Parser::ParseFile` 语法检查通过。
- 零参数重打包：见下节「回归结果」。

## 遗留

- `make_sd_image.sh` 的 bash 语法检查本机做不了：`bash.exe` 报 `WSL E_ACCESSDENIED`（WSL 未配置好）。改动是纯 shell 参数展开，待 WSL 可用时 `bash -n` 复核一次。
- `.env.make` 本应由 `bash scripts/generate_env_make.sh` 重新生成，因同一个 WSL 问题改为按脚本产物格式手工同步（只删了 `NPU_DEMO` 一行）。WSL 恢复后建议重跑一次该脚本确认产物一致。

## 追加（同日）：SD 镜像脚本与厂家基线比特流下线

用户决定：`make_sd_image.sh` 已无用（持久化卡早已生成并上板通过，[log/2026-10-08](2026-10-08-持久化SD与BOOT.bin打包.md)，且仓库内无 `.img` 留底）；`vendor.bit` 也无用——**BOOT.bin 的第 2 分区以后一律接我们自己的 PL 逻辑**。据此：

| 处理 | 对象 | 理由 |
|---|---|---|
| 删除 | `scripts/sd_image/make_sd_image.sh`（3,600 B） | 一次性工具，跑一次就不再需要；要新做卡须厂家 demo 的 `uInitrd.lz4`（72 MB，不入库），脚本留着也跑不通 |
| 删除 | `scripts/bootbin/parts/vendor.bit`（4,052,202 B） | 只服务「原样重打包验证」，而该路径随比特流改为必填后自然消失 |
| 保留 | `scripts/sd_image/flash_to_disk.ps1`、`boot.cmd`、`README_回退.txt` | 烧卡与回退说明仍有独立价值（`-Img` 可指向本地镜像） |

连带改动：

- `make_bootbin.ps1`：删掉 `parts\vendor.bit` 默认值，`-Bit` 改为**必填**，不给就报错并给出 `pl/soc_base` 的示例路径；新增 `Test-Path $Bit` 检查。
- `test_env.ps1`：去掉 `vendor.bit` 的 `[可选]` 检查项（现为 16 项）。
- `.gitignore`：去掉 `vendor.bit` 的说明注释（文件已不存在，`parts/*` 通配仍然覆盖它）。
- `AGENTS.md` 第 7 条：写明「BOOT.bin 第 2 分区一律接自己的比特流，`-Bit` 必填」。
- 文档同步：`scripts/README.md`（sd_image 章节改写为"生成脚本已删、烧卡脚本仍在"）、`docs/环境与上手.md` §5 改为「SD 卡」小节、`docs/开发环境与构建.md`「SD 卡布局」、根 `README.md` 目录表。
- 本文件开头那份「零件入库」决定中关于 `vendor.bit` 的一条**已被本次追加取代**：当时决定「不入库只留 MD5」，现在连 MD5 记录本身也一并从台账的入库清单里移除，改为在台账末尾注明其已下线。

⚠ **副作用（未消除）**：删除 `make_sd_image.sh` 后，仓库里再没有任何造卡工具。现有卡正常工作时无影响；一旦卡损坏或要给第二块板子用，只能 ① 用厂家预装卡，或 ② 照 `docs/开发环境与构建.md`「SD 卡布局」手工复刻（分区表、`boot.scr`、rootfs 来源都记在那节）。

## 追加（同日）：`vendor-BOOT.bin` 也删除，改用 MD5 守住 `u-boot.elf`

上面第 1 条（`vendor-BOOT.bin` 入库 4.94 MB）**被本条取代**。用户判定它同样无用——它不是 PL 逻辑件，只是 `u-boot.elf` 的提取来源；而 `u-boot.elf` 本身（0.95 MB）已入库，等于同一份数据在仓库里存了两遍。

改为：**删掉 `vendor-BOOT.bin`，用 MD5 校验 `u-boot.elf` 本身来防损坏。**

| 处理 | 对象 | 说明 |
|---|---|---|
| 删除 | `scripts/bootbin/parts/vendor-BOOT.bin`（5,179,264 B） | 4.94 MB 只为留一条"能重建 u-boot.elf"的路 |
| 新增校验 | `u-boot.elf` 的 MD5（`E844648EC910C87EA8A036AD4D3D3E45`） | `make_bootbin.ps1` **每次打包都查**；不符即报错并给出重建步骤。`test_env.ps1` 同项列为 `[必需]` |

这条交易是划算的：原来保护的是"**能不能重建**"（4.94 MB），现在保护的是"**现有零件有没有坏**"（0 字节）。而 `u-boot.elf` 损坏才是真正会咬人的场景——它进 BOOT.bin 第 3 分区，静默损坏要到刷完板子开机失败才发现。

`-VendorDemo` 逃生口保留，但语义收窄为"零件损坏时的重建路径"：脚本从 `<demo>/boot/BOOT.bin` 重新截取，且**对该文件仍做 MD5 硬校验**（`D6B2D387CB405444D1E7EF10E2855285`，偏移 `0x40e4c0` 按它算死），重建完再校验一次产物 MD5。

`parts/` 最终只剩：`fsbl.elf`（1.69 MB）+ `u-boot.elf`（0.95 MB）+ `MD5SUMS.txt`。仓库体积回到 24.6 MB 量级（净增约 2 KB 台账 + log）。

### 三次决定的沿革（留痕）

1. 最初：`parts/` 只有两个 `.elf` 入库，`vendor.bit` / `vendor-BOOT.bin` 都不入库，靠 `.env` 的 `NPU_DEMO` 兜底去仓库外取件。
2. 上一轮：判"保险丝太廉价"，`vendor-BOOT.bin` 入库；`vendor.bit` 只留 MD5。
3. 本轮：`make_sd_image.sh`、`vendor.bit`、`vendor-BOOT.bin` 全删——**BOOT.bin 只接我们自己的 PL 逻辑，`-Bit` 必填**；零件防损坏改用 MD5 校验。

## 追加（同日）：第三方库变量拆分，`SDK_LIBS` → `ARMNN_LIB` + `OPENCV_LIB`

### 问题

`.env` 里只有一个 `SDK_LIBS`，指向厂家包里那个语义为零的中间目录 `face_detection/app/face_detection/libs`，两个库名藏在它下面。而 `ps/Makefile` 从来没用过这个"根"——它只用派生出来的两个子目录：

```make
ARMNN  := $(SDK)/armnn_lib
OPENCV := $(SDK)/ffmpeg_opencv4.7.0_aarch64
```

即"一个根 + 两个硬编码子目录名"是**假抽象**：`.env` 里写一个路径，真实语义却是两个。而且板端本来就是两个平级目录（`/opt/armnn_lib`、`/opt/ffmpeg_opencv4.7.0_aarch64`，Makefile 的 `RPATH` 就是它们），主机端却不同构。

### 决定：一个库一个变量，主机端目录与板端同名同级

| 改动 | 前 | 后 |
|---|---|---|
| 变量 | `SDK_LIBS`（一个根） | `ARMNN_LIB`、`OPENCV_LIB`（各一个根） |
| 主机目录 | `des-deps/face_detection/app/face_detection/libs/{armnn_lib,ffmpeg_opencv4.7.0_aarch64}` | `des-deps/sdk/{armnn_lib,ffmpeg_opencv4.7.0_aarch64}` |
| 板端对应 | `/opt/armnn_lib`、`/opt/ffmpeg_opencv4.7.0_aarch64` | 不变（现在三者同名同级） |
| Makefile | `SDK ?= $(SDK_LIBS)` + 两行派生 | `ARMNN := $(ARMNN_LIB)` / `OPENCV := $(OPENCV_LIB)`，删掉 `SDK` 中间层 |
| `check-sdk` | 查 `SDK_LIBS` 一个目录 | 查 `ARMNN_LIB`、`OPENCV_LIB` 两个目录，报错分别指名 |

目录移动是纯 `Move-Item`，**软链接链完好**（复核 `libarmnn.so → .so.32 → .so.32.1` 仍是 `<SymbolicLink>`）。`face_detection/` 下的 demo 源码、`inputs/`、`scripts/`、`build/` 都没动，`docs` 里对 demo 源码的引用不失效；空掉的 `libs/` 目录已删。

### `BOARD_LIBS` 保持不变（评估过，名字没有歧义）

它装的是**从板上 rootfs 抠下来的 libfreetype / libpng16 / libz**，用途是**链接期**给链接器解析 `-lfreetype` 及其 SONAME 依赖；程序上板后用的是板子自己的 `/usr/lib`。所以它既不是"板端路径"也不是"要上板的库"，`BOARD_LIBS`（板上库的本地拷贝）这个描述是准的，改名反而会引入新歧义。真正含糊的是 `SDK_LIBS`，已拆。

### 顺带修掉的一个隐患

`test_env.ps1` 原来只查"库目录存在"，不查库文件。而 Windows 解压会丢 `.so` 软链接，此时目录在、`-lopencv_core` 却找不到文件，错误要到编译期才暴露。新增两项检查，直接查链接名：`$ARMNN_LIB/lib/libarmnn.so`、`$OPENCV_LIB/lib/libopencv_core.so`。

### 验证

| 项 | 结果 |
|---|---|
| `make -C ps check-sdk check-board` | `ARMNN_LIB ok` / `OPENCV_LIB ok` / `FREETYPE_INC ok` / `BOARD_LIBS ok`，exit 0 |
| `make -C ps build/test_calib` | 通过（只链 `OPENCV_LIB`） |
| `make -C ps build/test_npu` | 通过（链 `ARMNN_LIB` + `OPENCV_LIB`） |
| `make -C ps build/des_app` | **全量重编通过**，19.2 MB，零错误（链四个变量全部） |
| `scripts/test_env.ps1` | 17 项全过 0 失败 |

⚠ `des_app` 二进制 MD5 与改动前不同（`48D0BDF3…` vs `B2677C6D…`）：库路径变了，debug 信息里的路径跟着变，属预期；**功能未变，但也未重新上板验证**。
