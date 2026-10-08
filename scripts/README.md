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

⚠ 不要用 `mountvol X: /p` 之类的方式卸载卷再写盘：会去掉盘符且写到一半时 Windows 重新扫描分区表导致“设备未就绪”。

## 编码

`.ps1` 必须存为 **UTF-8 带 BOM**（Windows PowerShell 5.1 不带 BOM 时按本地代码页读，中文注释会让语法解析出错）。`.sh` 用 LF 换行。
