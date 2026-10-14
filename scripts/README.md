# scripts/ — 构建与部署脚本

背景与原理见 [开发环境与构建](../docs/开发环境与构建.md)。

| 目录 | 用途 | 运行环境 |
|---|---|---|
| [bootbin/](bootbin/) | 用新比特流打包 `BOOT.bin`（FSBL、U-Boot 复用厂家的） | Windows PowerShell + FD |
| [sd_image/](sd_image/) | 生成持久化 SD 卡镜像（FAT 启动分区 + ext4 根分区），并写卡 | WSL（root）/ Windows 管理员 |

## bootbin

```powershell
cd scripts\bootbin
.\make_bootbin.ps1                      # 原样重打包（验证工具链），MD5 应为 41489E57…
.\make_bootbin.ps1 -Bit <TD 生成的 .bit>  # 只改 PL 逻辑时
.\make_bootbin.ps1 -Bit <.bit> -Fsbl <FD 重编的 soc_fsbl.elf>   # 改了 design_0 的 PS 配置时
```

- 路径不同用 `-FdRoot` / `-VendorDemo` 或环境变量 `FD_ROOT` / `NPU_DEMO`；首次运行才需从厂家 NPU demo 准备零件到 `parts/`（不入库）
- 产物 `BOOT.bin` 替换板上 `/mnt/mmcblk0p1/BOOT.bin`，`sync` 后重启
- 与厂家原版差异：分区头全同、FSBL/U-Boot 逐字节同，仅比特流尾 32 字节及其校验值不同（新旧 BootGen 版本差异）

⚠ `fsbl.elf` / `u-boot.elf` 已在 `scripts/bootbin/parts/`（白名单入库）：**日常用不到从 NPU demo 提取**，只首次运行或改 PS 配置时需要。

## sd_image

```bash
# WSL 里（需 root）：生成 4GB 镜像 sd_persistent.img
sudo apt install lz4 u-boot-tools mtools dosfstools parted
wsl -u root -e bash scripts/sd_image/make_sd_image.sh
```

```powershell
# Windows 管理员 PowerShell：先 Get-Disk 认准读卡器的磁盘号
.\scripts\sd_image\flash_to_disk.ps1 -DiskNumber <N>
```

也可以 balenaEtcher / Rufus 烧 `sd_persistent.img`；回退厂家 initramfs 见 [README_回退.txt](sd_image/README_回退.txt)。

## sdk_fix_sonames.sh — 修复 Windows 解压丢失的 .so 软链接

Windows 解压 tar.gz / zip 会丢 `.so` 软链接（变独立文件或损坏）：链接器找不到 `libopencv_core.so`（只有 `libopencv_core.so.4.7.0`），运行时动态链接器报 `error while loading shared libraries`。SDK 库布局与背景见 [环境与上手](../docs/环境与上手.md)。

```bash
# 1. 判据：`libopencv_core.so` / `libopencv_core.so.4.7` 是 `lrwxrwxrwx` 才正常，`-rwxr-xr-x` 或缺失即需修复
ls -l $SDK_LIBS/ffmpeg_opencv4.7.0_aarch64/lib/libopencv_core.so*
# lrwxrwxrwx ... libopencv_core.so -> libopencv_core.so.4.7
# lrwxrwxrwx ... libopencv_core.so.4.7 -> libopencv_core.so.4.7.0
# -rwxr-xr-x ... libopencv_core.so.4.7.0

# 2. 修复：自动扫描并重建软链接
sh scripts/sdk_fix_sonames.sh <SDK_LIBS路径> <readelf路径>

# 3. 验证
ls -l $SDK_LIBS/ffmpeg_opencv4.7.0_aarch64/lib/libopencv_core.so*   # 应全部显示为 lrwxrwxrwx（软链接）
make -C ps des_app                                                 # 编译 PS 应用，应无 “cannot find -l...” 错误
```

- `<SDK_LIBS路径>` - 厂家 NPU demo 的 `face_detection/libs/`（含 `armnn_lib/` 和 `ffmpeg_opencv4.7.0_aarch64/` 子目录），**或任意单个库目录**（如 `$BOARD_LIBS`）
- `<readelf路径>` - 交叉编译工具链的 `aarch64-linux-gnu-readelf` 路径

```bash
# 两个参数都用 .env 里的变量（先 source scripts/env_loader.sh 或直接展开）
sh scripts/sdk_fix_sonames.sh \
  "$SDK_LIBS" \
  "$FD_ROOT/toolchain/gcc/aarch64_linux/bin/aarch64-linux-gnu-readelf.exe"
```

脚本做的事：

1. 扫描库目录中所有 `lib*.so*` 文件（含无版本号的 `libX.so`）
2. 用 `readelf -d` 读每个库的 `SONAME` 字段（如 `libopencv_core.so.4.7`）
3. 创建软链接：`libopencv_core.so.4.7 -> libopencv_core.so.4.7.0`
4. 创建无版本号链接：`libopencv_core.so -> libopencv_core.so.4.7`

**何时运行**：必须 —— 在 Windows 上首次解压 NPU demo 压缩包后、编译 ps/des_app 报 `cannot find -lopencv_core` 等；不必 —— Linux/WSL 中解压的 tar.gz（软链接保留）、已运行过一次、用预打包的 SDK 目录（均已修复）。

**常见问题**

- 报 `readelf: command not found`：第二个参数 `readelf` 路径不对，检查 FD 安装目录，完整路径应为 `<FD安装目录>/toolchain/gcc/aarch64_linux/bin/aarch64-linux-gnu-readelf.exe`
- 修复后仍链接失败，检查：1. `.env` 中 `SDK_LIBS` 是否指向正确目录 2. 运行 `bash scripts/generate_env_make.sh` 重新生成 Makefile 配置 3. `make clean -C ps && make -C ps des_app` 清理重新编译
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
