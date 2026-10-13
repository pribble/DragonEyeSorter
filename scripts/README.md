# scripts/ — 构建与部署脚本

背景与原理见 [docs/09-开发环境与构建.md](../docs/09-开发环境与构建.md)。

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

- 首次运行从厂家 NPU demo 准备零件到 `parts/`（不入库）。路径不同时用 `-FdRoot` / `-VendorDemo`，或设环境变量 `FD_ROOT` / `NPU_DEMO`
- 产物 `BOOT.bin` 替换板上 `/mnt/mmcblk0p1/BOOT.bin`，`sync` 后重启
- 与厂家原版的差异：分区头全同、FSBL/U-Boot 逐字节同，仅比特流尾 32 字节及其校验值不同（新旧 BootGen 版本差异）

⚠ **注意**：`fsbl.elf` 和 `u-boot.elf` 已在仓库 `scripts/bootbin/parts/` 中（白名单入库），**日常使用不需要从 NPU demo 提取**。只在首次运行或改动 PS 配置时需要。

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

也可以用 balenaEtcher / Rufus 烧 `sd_persistent.img`。回退到厂家 initramfs 的方法见 [sd_image/README_回退.txt](sd_image/README_回退.txt)。

## sdk_fix_sonames.sh — 修复 Windows 解压丢失的 .so 软链接

### 问题背景

厂家 NPU demo 的 SDK 库（`face_detection/libs/` 目录）包含大量共享库（`.so` 文件）及其版本软链接，例如：

```
libopencv_core.so -> libopencv_core.so.4.7 -> libopencv_core.so.4.7.0
```

**在 Windows 上解压 tar.gz 或 zip 时，软链接会丢失**（变成独立文件或损坏），导致：
- 编译时链接器找不到 `libopencv_core.so`（只有 `libopencv_core.so.4.7.0`）
- 运行时动态链接器报错 `error while loading shared libraries`

### 检测是否需要修复

```bash
# 检查 SDK 库目录中的 .so 文件
ls -l $SDK_LIBS/ffmpeg_opencv4.7.0_aarch64/lib/libopencv_core.so*

# 正常情况（有软链接）：
# lrwxrwxrwx ... libopencv_core.so -> libopencv_core.so.4.7
# lrwxrwxrwx ... libopencv_core.so.4.7 -> libopencv_core.so.4.7.0
# -rwxr-xr-x ... libopencv_core.so.4.7.0

# 异常情况（Windows 解压后）：
# -rwxr-xr-x ... libopencv_core.so        <-- 变成独立文件
# -rwxr-xr-x ... libopencv_core.so.4.7    <-- 或完全缺失
# -rwxr-xr-x ... libopencv_core.so.4.7.0
```

如果 `libopencv_core.so` 和 `libopencv_core.so.4.7` 不是软链接（`-rwxr-xr-x` 而非 `lrwxrwxrwx`），需要修复。

### 修复方法

运行 `sdk_fix_sonames.sh` 脚本，自动扫描并重建软链接：

```bash
sh scripts/sdk_fix_sonames.sh <SDK_LIBS路径> <readelf路径>
```

**参数说明**：
- `<SDK_LIBS路径>` - 厂家 SDK 库根目录，包含 `armnn_lib/` 和 `ffmpeg_opencv4.7.0_aarch64/` 子目录
- `<readelf路径>` - 交叉编译工具链中的 `aarch64-linux-gnu-readelf` 可执行文件路径

**示例**：

```bash
sh scripts/sdk_fix_sonames.sh \
  D:/ANLU/work/face_detection_src/app/face_detection/libs \
  D:/Anlogic/FD_Release_2026.1_SP1/toolchain/gcc/aarch64_linux/bin/aarch64-linux-gnu-readelf.exe
```

**脚本工作原理**：
1. 扫描 SDK 库目录中所有 `.so.*` 文件
2. 用 `readelf -d` 读取每个库的 `SONAME` 字段（如 `libopencv_core.so.4.7`）
3. 创建软链接：`libopencv_core.so.4.7 -> libopencv_core.so.4.7.0`
4. 创建无版本号链接：`libopencv_core.so -> libopencv_core.so.4.7`

### 何时运行

**必须运行的情况**：
- 在 Windows 上首次解压 NPU demo 压缩包后
- 编译 ps/des_app 时报错 `cannot find -lopencv_core` 等

**不需要运行的情况**：
- 在 Linux/WSL 中解压的 tar.gz（软链接保留）
- 已运行过一次（软链接已修复）
- 使用预打包的 SDK 目录（已修复）

### 验证修复结果

```bash
# 检查软链接是否正确
ls -l $SDK_LIBS/ffmpeg_opencv4.7.0_aarch64/lib/libopencv_core.so*
# 应全部显示为 lrwxrwxrwx（软链接）

# 尝试编译 PS 应用
make -C ps des_app
# 应无 “cannot find -l...” 错误
```

### 常见问题

**Q: 脚本报 “readelf: command not found”**

A: 第二个参数提供的 `readelf` 路径不正确，检查 FD 安装目录，完整路径应为：
```
<FD安装目录>/toolchain/gcc/aarch64_linux/bin/aarch64-linux-gnu-readelf.exe
```

---

**Q: 脚本运行后仍然链接失败**

A: 检查：
1. `.env` 中 `SDK_LIBS` 是否指向正确目录
2. 运行 `bash scripts/generate_env_make.sh` 重新生成 Makefile 配置
3. `make clean -C ps && make -C ps des_app` 清理重新编译

---

**Q: 可以不修复直接编译吗？**

A: 不能。Makefile 中链接器标志为 `-lopencv_core`（查找 `libopencv_core.so`），没有软链接会找不到库。除非手动改 Makefile 写死版本号（不推荐）。

## 编码

`.ps1` 必须存为 **UTF-8 带 BOM**（Windows PowerShell 5.1 不带 BOM 时按本地代码页读，中文注释会让语法解析出错）。`.sh` 用 LF 换行。
