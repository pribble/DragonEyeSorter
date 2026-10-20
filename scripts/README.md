# scripts/ — 构建与部署脚本

背景与原理见 [开发环境与构建](../docs/开发环境与构建.md)；板端文件的落点与规矩见 [板端布局](../docs/板端布局.md)。

| 目录 | 用途 | 运行环境 |
|---|---|---|
| [deploy/](deploy/) | 板端部署：`manifest.tsv` 登记「产品运行时（主程序 / 配置 / 自启链脚本）上到板子哪里」，`deploy.ps1` / `deploy.sh` 做对比 / 部署 / 暂存；测试与调试件不进清单，手动 `scp` | Windows PowerShell / Git Bash |
| [bootbin/](bootbin/) | 用新比特流打包 `BOOT.bin`（FSBL、U-Boot 复用仓库内零件） | Windows PowerShell + FD |
| [sd_image/](sd_image/) | 写 SD 卡 | Windows 管理员 |

## bootbin

```powershell
cd scripts\bootbin
.\make_bootbin.ps1 -Bit <TD 生成的 .bit>                        # 只改 PL 逻辑（唯一日常操作）
.\make_bootbin.ps1 -Bit <.bit> -Fsbl <FD 重编的 soc_fsbl.elf>    # 改了 design_0 的 PS 配置时
```

- 路径不同用 `-FdRoot`（FD 安装目录）；**零件已在仓库内 `parts/`，常规打包不依赖仓库外的厂家 demo 目录**。
- 产物 `BOOT.bin` 替换板上 `/mnt/mmcblk0p1/BOOT.bin`：先传到 `/root`，板上旧的改名为 `BOOT.bin.prev`（上一个能启动的版本）再放新的，`sync` 后重启（完整步骤见 [开发环境与构建](../docs/开发环境与构建.md)「部署与运行」）。
- **`-Bit` 必填**：比特流分区一律接我们自己的比特流，脚本没有默认值，不给参数会明确报错。
- `u-boot.elf` 每次打包都校验 MD5（静默损坏要到开机失败才发现）；`fsbl.elf` 与厂家原件逐字节相同。两个零件的 MD5 台账见 [parts/MD5SUMS.txt](bootbin/parts/MD5SUMS.txt)。
- 只有两种情况才需要 `-VendorDemo <厂家 demo 目录>`：① `parts/u-boot.elf` 丢失要重建；② 取厂家原始 `fsbl.elf`。`.env` 里可选的 `NPU_DEMO` 与它等效，不是必需项。
- 与厂家原版的差异：分区头全同、FSBL / U-Boot 逐字节同，仅比特流尾 32 字节及其校验值不同。

## sd_image

**镜像生成脚本（`make_sd_image.sh`）已删除**：它是一次性工具，且要厂家 demo 的 `uInitrd.lz4`（72 MB，不入库）才能跑；持久化卡早已做好并上板通过。留下的是烧卡脚本：

```powershell
# Windows 管理员 PowerShell：先 Get-Disk 认准读卡器的磁盘号
# 默认烧 $PSScriptRoot\sd_persistent.img；镜像不在仓库里，用 -Img 指向本地那份
.\scripts\sd_image\flash_to_disk.ps1 -DiskNumber <N>
```

也可用 balenaEtcher / Rufus。要**新做**一张持久化卡（现有卡坏了 / 给第二块板子）：用厂家预装的卡，或照 [开发环境与构建](../docs/开发环境与构建.md)「SD 卡布局与烧卡」用厂家 demo 的 `boot/` 目录手工复刻（分区布局 p1 FAT32 / p2 ext4、`boot.scr` 改动、rootfs 来自 `uInitrd.lz4` 解包）。

## sdk_fix_sonames.sh — 修复 Windows 解压丢失的 .so 软链接

Windows 解压 tar.gz / zip 会丢 `.so` 软链接：链接器找不到 `libopencv_core.so`（只有 `libopencv_core.so.4.7.0`），编译报 `cannot find -l...`，运行时动态链接器报 `error while loading shared libraries`。

```bash
# 1. 判据：链接名 libopencv_core.so 与 SONAME 名 libopencv_core.so.407 都得是 lrwxrwxrwx
ls -l $OPENCV_LIB/lib/libopencv_core.so*
# 2. 修复：自动扫描并重建软链接（两个库各跑一次）
sh scripts/sdk_fix_sonames.sh "$ARMNN_LIB"  <readelf 路径>
sh scripts/sdk_fix_sonames.sh "$OPENCV_LIB" <readelf 路径>
# 3. 验证
make -C ps build/des_app                   # 应无 "cannot find -l..." 错误
```

- 第一个参数给**单个库目录**（`$ARMNN_LIB` / `$OPENCV_LIB` / `$BOARD_LIBS` 都是），扫它自己的 `lib/`；给同时含两个库子目录的父目录时两个都扫（兼容写法，`.env` 不用这样配）。第二个参数是交叉工具链的 `aarch64-linux-gnu-readelf` 路径（`$FD_ROOT/toolchain/gcc/aarch64_linux/bin/aarch64-linux-gnu-readelf.exe`）。
- 脚本按 `readelf -d` 的 SONAME 建两级软链接（`libopencv_core.so.407 -> libopencv_core.so.4.7.0`、`libopencv_core.so -> libopencv_core.so.407`）；**别按 `.so.4.7` 去猜**，以 readelf 输出为准。
- **何时运行**：必须 —— Windows 上首次解压 NPU demo 压缩包后；不必 —— Linux / WSL 解压（软链接保留）、已运行过一次、用预打包的库目录。
- 报 `readelf: command not found` = 第二个参数路径不对；修复后仍链接失败，检查 `.env` 的 `ARMNN_LIB` / `OPENCV_LIB` 是否各指向库目录本身，再 `make clean -C ps && make -C ps build/des_app`。

**板上库（`$BOARD_LIBS`）也要两套名字**：从板上 `scp` 下来的 `libfreetype` / `libpng16` / `libz` 同样两套名字都要有 —— 缺链接名报 `cannot find -lfreetype`，缺 SONAME 名报 `undefined reference to png_*@PNG16_0` / `inflate*` 或 warning `libpng16.so.16 ... not found`。同一个脚本，直接给平铺的 `$BOARD_LIBS` 目录。已存在的名字不覆盖，可反复跑。

## 编码

`.ps1` 必须存为 **UTF-8 带 BOM**（Windows PowerShell 5.1 不带 BOM 时按本地代码页读，中文注释会让语法解析出错）。`.sh` 用 LF 换行。
