# scripts/ — 构建与部署脚本

背景与原理见 [开发环境与构建](../docs/开发环境与构建.md)；板端文件的落点与规矩见 [板端布局](../docs/板端布局.md)。

按「什么时候跑 / 碰不碰板子」分四个目录，脚本不再平铺在根上：

| 目录 | 用途 | 运行环境 |
|---|---|---|
| [env/](env/) | 环境准备：`.env` 加载 / `.env.make` 生成 / 环境自检 / SDK 库软链接修复 | PowerShell / Bash / WSL |
| [check/](check/) | 文档机械检查与「文档 ↔ 源码唯一源」检查 | PowerShell 7 |
| [boot/](boot/) | 启动介质：打包 `BOOT.bin`（原 bootbin）+ 写 SD 卡（原 sd_image） | Windows PowerShell（烧卡需管理员） |
| [deploy/](deploy/) | 板端部署：`manifest.tsv` 登记「产品运行时上到板子哪里」，部署器做对比 / 部署 / 暂存 | Windows PowerShell / Git Bash |

## env — 环境准备

| 脚本 | 用途 |
|---|---|
| `env_loader.ps1` / `env_loader.sh` | 把 `.env` 加载进当前会话并报告关键路径是否存在（PowerShell / Bash·WSL） |
| `generate_env_make.sh` | 从 `.env` 生成 `ps/Makefile` 用的 `.env.make`；换机 / 改 `.env` 后必须重生 |
| `test_env.ps1` | 逐项自检关键路径（工具链、BOOT 零件、库、板端连接；编 `des_app` 前跑一次） |
| `sdk_fix_sonames.sh` | 修复 Windows 解压丢掉的 `.so` 软链接：按 `readelf -d` 的 SONAME 建两级链接名 |

初次配置 `.env`、依赖从哪拿、每种报错怎么修见 [环境与上手](../docs/环境与上手.md) §2 / §3 / §6；[boot/make_bootbin.ps1](boot/make_bootbin.ps1) 与 `ps/Makefile` 会自动加载 `.env`。

## check — 文档与源码一致性

```powershell
pwsh -File scripts/check/check_docs.ps1        # 链接/锚点、索引完整性、路径卫生、行数预算、结构卫生、数字复述、版本口径
pwsh -File scripts/check/check_sources.ps1     # 文档 ↔ 源码：寄存器表 ↔ ps/src/hw/des_regs.h、类别表 ↔ label_list.json、CSV 列数、部署清单 ↔ deploy/out/
```

规则与例外在同目录 [doc_check.json](check/doc_check.json) / [source_check.json](check/source_check.json)，例外必须写 reason；`-WarnOnly` 只报告不失败，`-Verbose` 打印通过项。只查机械一致性，不替代「数字只来自实测」的事实纪律。

## boot — 启动介质（打包 BOOT.bin + 写 SD 卡）

```powershell
cd scripts\boot
.\make_bootbin.ps1 -Bit <TD 生成的 .bit>                        # 只改 PL 逻辑（唯一日常操作）
.\make_bootbin.ps1 -Bit <.bit> -Fsbl <FD 重编的 soc_fsbl.elf>    # 改了 design_0 的 PS 配置时
```

- 产物 `BOOT.bin` 替换板上 `/mnt/mmcblk0p1/BOOT.bin`：先传到 `/root`，板上旧的改名为 `BOOT.bin.prev`（上一个能启动的版本）再放新的，`sync` 后重启（完整步骤见 [开发环境与构建](../docs/开发环境与构建.md)「部署与运行」）。
- **`-Bit` 必填**：比特流分区一律接我们自己的比特流，脚本没有默认值，不给参数会明确报错。
- 零件在 `parts/`：`u-boot.elf` 每次打包都校验 MD5（静默损坏要到开机失败才发现）；`fsbl.elf` 与厂家原件逐字节相同。台账见 [parts/MD5SUMS.txt](boot/parts/MD5SUMS.txt)。
- 只有两种情况才需要 `-VendorDemo <厂家 demo 目录>`：① `parts/u-boot.elf` 丢失要重建；② 取厂家原始 `fsbl.elf`。`.env` 里可选的 `NPU_DEMO` 与它等效，不是必需项。
- `boot.cmd` 是板上 `boot.scr` 的源（U-Boot 脚本）：改启动参数时改它、用 `mkimage` 重新生成 `boot.scr`（p1 布局见 [开发环境与构建](../docs/开发环境与构建.md)「SD 卡布局与烧卡」）。

**写 SD 卡**（Windows 管理员；镜像不在仓库里）：

```powershell
Get-Disk                                              # 先认准读卡器的磁盘号
.\scripts\boot\flash_to_disk.ps1 -DiskNumber <N>      # 默认烧 $PSScriptRoot\sd_persistent.img，-Img 可指向本地那份
```

也可用 balenaEtcher / Rufus。**镜像生成脚本已删除**（一次性工具，要厂家 `uInitrd.lz4` 才能跑，72 MB 不入库）；要新做持久化卡照 [开发环境与构建](../docs/开发环境与构建.md)「SD 卡布局与烧卡」手工复刻。

## deploy — 板端部署

[deploy/](deploy/) 的清单与部署器：哪些文件属于板端产品运行时（`des_app` / 配置 / 自启链脚本）、分别上到哪，用法与 `-Stage` / `-Dest` 见 [deploy/README](deploy/README.md)。测试与调试件（`des_tool`、`desreg`、`canplc`、`test_*`、`pl_check.sh` / `soak.sh` / `display_check.sh`）不进清单，手动 `scp`。

## 编码

`.ps1` 必须存为 **UTF-8 带 BOM**（Windows PowerShell 5.1 不带 BOM 时按本地代码页读，中文注释会让语法解析出错）。`.sh` 用 LF 换行。
