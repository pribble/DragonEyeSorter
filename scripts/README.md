# scripts/ — 构建与部署脚本

背景与原理见 [开发环境与构建](../docs/开发环境与构建.md)。

| 目录 | 用途 | 运行环境 |
|---|---|---|
| [deploy/](deploy/) | 板端部署：`manifest.tsv` 登记"板端产品运行时（主程序 / 配置 / 自启链脚本）上到板子哪里"，`deploy.ps1` / `deploy.sh` 做对比 / 部署 / 暂存；测试与调试件不进清单，手动 `scp` | Windows PowerShell / Git Bash |
| [bootbin/](bootbin/) | 用新比特流打包 `BOOT.bin`（FSBL、U-Boot 复用厂家的） | Windows PowerShell + FD |
| [sd_image/](sd_image/) | 写 SD 卡（镜像生成脚本已于 2026-10-15 删除，见下） | Windows 管理员 |

## bootbin

```powershell
cd scripts\bootbin
.\make_bootbin.ps1                      # 原样重打包（验证工具链），MD5 应为 41489E57…
.\make_bootbin.ps1 -Bit <TD 生成的 .bit>  # 只改 PL 逻辑时
.\make_bootbin.ps1 -Bit <.bit> -Fsbl <FD 重编的 soc_fsbl.elf>   # 改了 design_0 的 PS 配置时
```

- 路径不同用 `-FdRoot`（FD 安装目录）；**零件已在仓库内 `parts/`，常规打包不依赖仓库外的厂家 demo 目录**
- 产物 `BOOT.bin` 替换板上 `/mnt/mmcblk0p1/BOOT.bin`：先传到 `/root`，板上旧的复制为 `BOOT.bin.prev`（上一个能启动的版本）再放新的，`sync` 后重启（完整步骤见 [环境与上手](../docs/环境与上手.md)「部署与运行」）
- 与厂家原版差异：分区头全同、FSBL/U-Boot 逐字节同，仅比特流尾 32 字节及其校验值不同（新旧 BootGen 版本差异）

**零件台账**（[parts/MD5SUMS.txt](bootbin/parts/MD5SUMS.txt)）：

| 零件 | 入库 | MD5 | 来源 |
|---|---|---|---|
| `fsbl.elf` | ✔ | `A64A56C0C37003AF025C45ABF0DA2732` | 厂家 `soc_fsbl/build/soc_fsbl.elf`，逐字节相同 |
| `u-boot.elf` | ✔ | `E844648EC910C87EA8A036AD4D3D3E45` | 由厂家 `boot/BOOT.bin` 偏移 `0x40e4c0`、长 `0xe2248` 截出 |

- **换比特流就是唯一日常操作**：`.\make_bootbin.ps1 -Bit <自己的 .bit>`。`-Bit` 是**必填**——厂家基线比特流与「原样重打包验证」那条回归路径已随 `make_sd_image.sh` 一并下线（见 [log/2026-10-15](../docs/log/2026-10-15-厂家boot零件入库与NPU_DEMO下线.md)），不给参数会明确报错
- `u-boot.elf` 每次打包都校验 MD5（它进 BOOT.bin 第 3 分区，静默损坏要到开机失败才发现）；`fsbl.elf` 与厂家原件逐字节相同
- 只有两种情况才需要 `-VendorDemo <厂家 demo 目录>`：① `u-boot.elf` 损坏要重建；② 改 PS 配置后取厂家原始 FSBL。`.env` 里**已不设** `NPU_DEMO`

## sd_image

**镜像生成脚本（`make_sd_image.sh`）已于 2026-10-15 删除**：持久化卡早就做好并上板通过（[log/2026-10-08](../docs/log/2026-10-08-持久化SD与BOOT.bin打包.md)），它是一次性工具，且要厂家 demo 的 `uInitrd.lz4`（72 MB，不入库）才能跑。留下的是烧卡脚本：

```powershell
# Windows 管理员 PowerShell：先 Get-Disk 认准读卡器的磁盘号
# 默认烧 $PSScriptRoot\sd_persistent.img；镜像不在仓库里，用 -Img 指向本地那份
.\scripts\sd_image\flash_to_disk.ps1 -DiskNumber <N>
```

也可用 balenaEtcher / Rufus 烧镜像。

要**新做**一张持久化卡时（现有卡坏了 / 要给第二块板子）：① 直接用厂家预装的卡；② 或照 [开发环境与构建](../docs/开发环境与构建.md)「SD 卡布局」用厂家 demo 的 `boot/` 目录手工复刻——分区布局（p1 FAT32 / p2 ext4）、`boot.scr` 的改动、rootfs 来自 `uInitrd.lz4` 解包，都记在那里。

## sdk_fix_sonames.sh — 修复 Windows 解压丢失的 .so 软链接

Windows 解压 tar.gz / zip 会丢 `.so` 软链接（变独立文件或损坏）：链接器找不到 `libopencv_core.so`（只有 `libopencv_core.so.4.7.0`），运行时动态链接器报 `error while loading shared libraries`。库目录布局与背景见 [环境与上手](../docs/环境与上手.md)。

```bash
# 1. 判据：`libopencv_core.so` 和它指向的 SONAME 名（实测是 `libopencv_core.so.407`）都得是 `lrwxrwxrwx`，`-rwxr-xr-x` 或缺失即需修复
ls -l $OPENCV_LIB/lib/libopencv_core.so*
# lrwxrwxrwx ... libopencv_core.so -> libopencv_core.so.407
# lrwxrwxrwx ... libopencv_core.so.407 -> libopencv_core.so.4.7.0
# -rwxr-xr-x ... libopencv_core.so.4.7.0

# 2. 修复：自动扫描并重建软链接（两个库各跑一次）
sh scripts/sdk_fix_sonames.sh "$ARMNN_LIB"  <readelf路径>
sh scripts/sdk_fix_sonames.sh "$OPENCV_LIB" <readelf路径>

# 3. 验证
ls -l $OPENCV_LIB/lib/libopencv_core.so*   # 应全部显示为 lrwxrwxrwx（软链接）
make -C ps des_app                         # 编译 PS 应用，应无 “cannot find -l...” 错误
```

- `<库目录>` - 给**单个库目录**（`$ARMNN_LIB` / `$OPENCV_LIB` / `$BOARD_LIBS` 都是），扫它自己的 `lib/`
- `<readelf路径>` - 交叉编译工具链的 `aarch64-linux-gnu-readelf` 路径

```bash
# 两个参数都用 .env 里的变量（先 source scripts/env_loader.sh 或直接展开）
sh scripts/sdk_fix_sonames.sh \
  "$ARMNN_LIB" \
  "$FD_ROOT/toolchain/gcc/aarch64_linux/bin/aarch64-linux-gnu-readelf.exe"
```

脚本做的事：

1. 扫描库目录中所有 `lib*.so*` 文件（含无版本号的 `libX.so`）
2. 用 `readelf -d` 读每个库的 `SONAME` 字段（OpenCV 实测是 `libopencv_core.so.407`；**别按 `.so.4.7` 去猜**，以 readelf 输出为准）
3. 创建 SONAME 名软链接：`libopencv_core.so.407 -> libopencv_core.so.4.7.0`
4. 创建无版本号链接：`libopencv_core.so -> libopencv_core.so.407`

**何时运行**：必须 —— 在 Windows 上首次解压 NPU demo 压缩包后、编译 ps/des_app 报 `cannot find -lopencv_core` 等；不必 —— Linux/WSL 中解压的 tar.gz（软链接保留）、已运行过一次、用预打包的库目录（均已修复）。

**常见问题**

- 报 `readelf: command not found`：第二个参数 `readelf` 路径不对，检查 FD 安装目录，完整路径应为 `<FD安装目录>/toolchain/gcc/aarch64_linux/bin/aarch64-linux-gnu-readelf.exe`
- 修复后仍链接失败，检查：1. `.env` 中 `ARMNN_LIB` / `OPENCV_LIB` 是否各指向库目录本身 2. 运行 `bash scripts/generate_env_make.sh` 重新生成 Makefile 配置 3. `make clean -C ps && make -C ps des_app` 清理重新编译
- 能不修复直接编译吗？不能：Makefile 链接标志为 `-lopencv_core`（查找 `libopencv_core.so`），没有软链接就找不到库；除非在 Makefile 写死版本号（不推荐）

## 板上库（`$BOARD_LIBS`）也要两套名字

从板上 `scp` 下来的 `libfreetype` / `libpng16` / `libz` 同样**两套名字都要有**，缺哪套报哪种错：

| 缺的名字 | 症状 |
|---|---|
| 链接名 `libfreetype.so` | 链接报 `cannot find -lfreetype` |
| SONAME 名 `libpng16.so.16` / `libz.so.1` | 链接报 `undefined reference to png_*@PNG16_0` / `inflate*`，或 warning `libpng16.so.16, needed by .../libfreetype.so, not found` |

```bash
# 同一个脚本：给普通目录就扫它自己（给 SDK 根才扫 armnn_lib/ 与 ffmpeg_opencv4.7.0_aarch64/）
sh scripts/sdk_fix_sonames.sh "$BOARD_LIBS" \
  "$FD_ROOT/toolchain/gcc/aarch64_linux/bin/aarch64-linux-gnu-readelf.exe"
ls "$BOARD_LIBS"/libfreetype.so*     # 链接名与 SONAME 名都在
```

已存在的名字不覆盖，可反复跑。

## 编码

`.ps1` 必须存为 **UTF-8 带 BOM**（Windows PowerShell 5.1 不带 BOM 时按本地代码页读，中文注释会让语法解析出错）。`.sh` 用 LF 换行。
