# 2026-10-15 厂家 boot 零件入库、NPU_DEMO 下线

## 结论（最终状态）

- `scripts/bootbin/parts/` 只保留 `fsbl.elf`、`u-boot.elf`、`MD5SUMS.txt`；仓库体积回到 24.6 MB 量级（净增约 2 KB 台账 + log）。
- 厂家基线 `vendor.bit` 与 `vendor-BOOT.bin` 都已删除：BOOT.bin 第 2 分区一律接我们自己的比特流，`make_bootbin.ps1 -Bit` **必填**。
- 零件防损坏改用 MD5 校验：`u-boot.elf`（`E844648EC910C87EA8A036AD4D3D3E45`）**每次打包都查**，不符即报错并给重建步骤，`test_env.ps1` 同项列 `[必需]`；`fsbl.elf` 与厂家原件 `soc_fsbl/build/soc_fsbl.elf` 逐字节相同。
- `.env` 删除 `NPU_DEMO`；`.env.example` 保留为注释掉的可选项（做 SD 镜像、重建零件时才填）。
- `-VendorDemo` 逃生口语义收窄为「零件损坏时的重建路径」：从 `<demo>/boot/BOOT.bin` 按偏移 `0x40e4c0`、长度 `0xe2248` 重新截取 `u-boot.elf`，对该文件仍做 MD5 硬校验（`D6B2D387CB405444D1E7EF10E2855285`），重建完再校验产物。

## 决定沿革（同日改判）

1. 最初：`parts/` 只有两个 `.elf` 入库，`vendor.bit` / `vendor-BOOT.bin` 都不入库，靠 `.env` 的 `NPU_DEMO` 兜底去仓库外的厂家 demo 目录取件。
2. 当日第一版决定：判「兜底保险丝太廉价」，**`vendor-BOOT.bin` 入库（4.94 MB）**——它是 `u-boot.elf` 的**唯一来源**（按偏移 `0x40e4c0`、长度 `0xe2248` 从第 2 分区截出，脚本对它做 MD5 硬校验），入库后打 BOOT.bin 不再依赖仓库外目录；`vendor.bit` 只登记 MD5 不入库；`.env` 删 `NPU_DEMO`。
3. 同日追加：`vendor.bit` 与 `make_sd_image.sh` 删除——**BOOT.bin 第 2 分区一律接我们自己的比特流，`-Bit` 改为必填**。
4. 同日再追加（最终）：`vendor-BOOT.bin` 也删除——它与已入库的 `u-boot.elf`（0.95 MB）是同一份数据；改为**用 MD5 校验 `u-boot.elf` 本身防损坏**（每次打包都查），`-VendorDemo` 语义收窄为重建路径。`parts/` 只剩两个 `.elf` + `MD5SUMS.txt`。

## 历史数字（留痕）

| 文件 | 字节数 | MD5 | 现状 |
|---|---|---|---|
| 厂家 `vendor-BOOT.bin` | 5,179,264 | `D6B2D387CB405444D1E7EF10E2855285` | 已删除（4.94 MB 只为留一条重建 `u-boot.elf` 的路） |
| `u-boot.elf` | 992,360 | `E844648EC910C87EA8A036AD4D3D3E45` | 入库，每次打包校验 |
| `fsbl.elf` | 1,767,704 | `A64A56C0C37003AF025C45ABF0DA2732` | 入库 |
| 厂家 `vendor.bit` | 4,052,202 | `9C611D2724C8464BD3C0B887D5A3EB1A` | 已删除 |

## 途中发现：`phy_1/soc_prj.bit` 不是厂家原件

脚本原先从 `<demo>/soc_prj/soc_prj_Runs/phy_1/soc_prj.bit` 取「厂家比特流」，实测该路径 MD5 是 `3B5DE345E5C18DD1F3245E6FCF9618DF`，是本地重编覆盖掉的产物（`soc_prj_Runs/` 是编译输出目录）。干净原件在 **`soc_prj_Runs/best_result/soc_prj.bit`**（`9C611D27…`），与 `best_result/readMe.txt` 的说明相符。结论：**从 demo 取厂家比特流一律认 `best_result/`**；`phy_1/` 只在我们从未重编过时才等于厂家件。已写进 `parts/MD5SUMS.txt` 与脚本报错提示。

## 代码与文档改动

| 文件 | 改动 |
|---|---|
| `scripts/bootbin/make_bootbin.ps1` | 删掉 D 盘硬编码兜底，零件改从 `parts/` 取；`-Bit` 必填 + `Test-Path` 检查；`Get-VendorDir` 缺参数时显式报错并给指引；新增 `Assert-VendorBoot`（MD5 硬校验，独立成函数）；输出打印三个输入的 MD5；补齐 UTF-8 BOM + CRLF |
| `scripts/test_env.ps1` | 检查 `parts/` 四项 + `u-boot.elf` 的 MD5（现 16 项）；顺带修掉 `Test-Path "$PSScriptRoot\..\env"` 恒真的 bug（应为 `$DES_ENV_LOADED`，原来每次都重复加载 `.env`） |
| `scripts/sd_image/make_sd_image.sh` | **已删除**（一次性工具；要新做卡须厂家 `uInitrd.lz4`，72 MB 不入库，脚本留着也跑不通）。`flash_to_disk.ps1`、`boot.cmd`、`README_回退.txt` 保留（`-Img` 可指向本地镜像） |
| `.env` / `.env.make` / `.env.example` / `scripts/env_loader.*` | 去掉 `NPU_DEMO` 的必需性检查、声明与打印 |
| `.gitignore` | `parts/*` 白名单增补；`vendor.bit` 的说明随文件删除一并去掉 |
| 文档 | `scripts/README.md`、`docs/环境与上手.md`、`docs/开发环境与构建.md`、`AGENTS.md` 第 7 条同步零件台账、入库范围与「哪些场合才需要厂家 demo 目录」 |

⚠ **副作用（未消除）**：删除 `make_sd_image.sh` 后仓库里再没有任何造卡工具。现有卡正常工作时无影响；一旦卡损坏或要给第二块板子用，只能 ① 用厂家预装卡，或 ② 照 `docs/开发环境与构建.md`「SD 卡布局」手工复刻（分区表、`boot.scr`、rootfs 来源都记在那节）。

## 追加（同日）：`SDK_LIBS` 拆成 `ARMNN_LIB` + `OPENCV_LIB`

原 `.env` 只有一个 `SDK_LIBS`，指向厂家包里语义为零的中间目录 `face_detection/app/face_detection/libs`；`ps/Makefile` 从没用过这个「根」，只用派生的两个子目录 —— 「一个根 + 两个硬编码子目录名」是假抽象。改为一个库一个变量，主机端目录与板端同名同级：

| 改动 | 前 | 后 |
|---|---|---|
| 变量 | `SDK_LIBS`（一个根） | `ARMNN_LIB`、`OPENCV_LIB`（各一个根） |
| 主机目录 | `des-deps/face_detection/app/face_detection/libs/{armnn_lib,ffmpeg_opencv4.7.0_aarch64}` | `des-deps/sdk/{armnn_lib,ffmpeg_opencv4.7.0_aarch64}` |
| 板端对应 | `/opt/armnn_lib`、`/opt/ffmpeg_opencv4.7.0_aarch64` | 不变（现在三者同名同级） |
| `Makefile` | `SDK ?= $(SDK_LIBS)` + 两行派生 | `ARMNN := $(ARMNN_LIB)` / `OPENCV := $(OPENCV_LIB)`，删掉 `SDK` 中间层 |
| `check-sdk` | 查 `SDK_LIBS` 一个目录 | 查两个目录，报错分别指名 |

- 目录移动是纯 `Move-Item`，软链接链完好（复核 `libarmnn.so → .so.32 → .so.32.1` 仍是 `<SymbolicLink>`）；`face_detection/` 下 demo 源码、`inputs/`、`scripts/`、`build/` 都没动，空掉的 `libs/` 已删。
- `BOARD_LIBS` 保持不变：它装从板上 rootfs 抠下来的 libfreetype / libpng16 / libz，用途是**链接期**解析 `-lfreetype` 及其 SONAME 依赖，程序上板后用的是板子自己的 `/usr/lib`，既不是板端路径也不是要上板的库，改名反而引入新歧义。
- 顺带修隐患：`test_env.ps1` 原来只查库目录存在、不查库文件（Windows 解压丢 `.so` 软链接时目录在、`-lopencv_core` 找不到文件，要到编译期才暴露），新增直接查链接名 `$ARMNN_LIB/lib/libarmnn.so`、`$OPENCV_LIB/lib/libopencv_core.so`。

验证：`make -C ps check-sdk check-board` 四项 ok（exit 0）；`build/test_calib`、`build/test_npu` 通过；`build/des_app` **全量重编通过**（19.2 MB，零错误）；`scripts/test_env.ps1` 17 项全过 0 失败。⚠ `des_app` MD5 与改动前不同（`48D0BDF3…` vs `B2677C6D…`）：库路径进了 debug 信息，属预期；**功能未变，但也未重新上板验证**。
