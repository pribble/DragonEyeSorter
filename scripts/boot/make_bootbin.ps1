# 打包 BOOT.bin：新比特流 + FSBL + U-Boot（Windows，只用 FD 自带的 BootGen 与交叉工具链）
#
# 日常（只改 PL 逻辑）：.\make_bootbin.ps1 -Bit <TD 生成的 .bit>
# 改了 PS 配置：        .\make_bootbin.ps1 -Bit <.bit> -Fsbl <FD 重编的 soc_fsbl.elf>
#
# 比特流必须显式给：接的是我们自己的 PL 逻辑，脚本不再有"原样重打包"的默认值
# （厂家基线比特流与那条回归路径已于 2026-10-15 随 make_sd_image.sh 一并下线）。
#
# 零件来源（都在 scripts\boot\parts\，台账见 parts\MD5SUMS.txt）：
#   fsbl.elf    已入库，与厂家原件逐字节相同
#   u-boot.elf  已入库，由厂家 BOOT.bin 第 2 分区解出；脚本每次打包都校验它的 MD5
#
# 本脚本**不依赖仓库外的厂家 demo 目录**：常规打包（-Bit + parts 里的两个零件）零外部路径。
# 只有两种情况才需要外部目录：① parts\u-boot.elf 丢失要重建；② 改 PS 配置后取厂家原始 FSBL。
# 两者都用 -VendorDemo <dir> 显式指定。
param(
    [string]$Bit,
    [string]$Fsbl,
    [string]$Out = "$PSScriptRoot\BOOT.bin",
    [string]$FdRoot,
    [string]$VendorDemo
)
$ErrorActionPreference = 'Stop'
# 只取 FD_ROOT（BootGen 与交叉工具链）；零件走仓库内的 parts\，不再读 .env 的 NPU_DEMO
if (-not $DES_ENV_LOADED -and (Test-Path "$PSScriptRoot\..\..\.env")) {
    . "$PSScriptRoot\..\env\env_loader.ps1"
}
if (-not $FdRoot) {
    if (-not $env:FD_ROOT) { throw "FD_ROOT 未设置：用 -FdRoot <FD 安装目录>，或在仓库根 .env 里配好（本脚本会经 env_loader.ps1 读它）" }
    $FdRoot = $env:FD_ROOT
}
$parts   = Join-Path $PSScriptRoot 'parts'
$bootgen = Join-Path $FdRoot 'tools\bootgen\albg1.exe'
$tc      = Join-Path $FdRoot 'toolchain\gcc\aarch64_linux\bin\aarch64-linux-gnu'
foreach ($p in $bootgen, "$tc-objcopy.exe", "$tc-ld.exe") { if (-not (Test-Path $p)) { throw "找不到 $p，用 -FdRoot 或环境变量 FD_ROOT 指定 FD 安装目录" } }

# 厂家 BOOT.bin 的基准 MD5：u-boot.elf 的截取偏移按这一份算死，换文件必须重算
$VENDOR_BOOT_MD5 = 'D6B2D387CB405444D1E7EF10E2855285'
# u-boot.elf 的基准 MD5：入库零件，每次打包都用它；不符就说明文件被换/损坏
$UBOOT_ELF_MD5   = 'E844648EC910C87EA8A036AD4D3D3E45'

function Get-VendorDir {
    # 仓库外的厂家 NPU demo 目录：显式参数 > 环境变量 NPU_DEMO > 报错
    if ($VendorDemo) { return $VendorDemo }
    if ($env:NPU_DEMO) { return $env:NPU_DEMO }
    throw @"
需要仓库外的厂家 NPU demo 目录，但没有指定。
  用 -VendorDemo <dir> 显式指定，或在 .env 里设 NPU_DEMO=<dir>
  （常规打包不需要它：零件都在 scripts\boot\parts\；
    只有重建 u-boot.elf、或改 PS 配置后取原始 FSBL 时才要）
"@
}

function New-UBootElf {
    # 仅在 parts\u-boot.elf 丢失时重建：从厂家 demo 的 boot\BOOT.bin 第 2 分区解出 U-Boot，
    # 包成 ELF（载入 / 入口 0x200000）。分区布局：偏移 0x40e4c0，有效长度 0xe2248（不含末尾 SHA2）
    $vd  = Get-VendorDir
    $src = Join-Path $vd 'boot\BOOT.bin'
    if (-not (Test-Path $src)) { throw "找不到厂家 BOOT.bin：$src（用 -VendorDemo <dir> 指定厂家 NPU demo 目录）" }
    $h = (Get-FileHash $src -Algorithm MD5).Hash
    if ($h -ne $VENDOR_BOOT_MD5) {
        throw "厂家 BOOT.bin 的 MD5 不符（实际 $h，期望 $VENDOR_BOOT_MD5）：截取偏移 0x40e4c0 是按原文件算的，换文件会截出错内容"
    }
    $all = [IO.File]::ReadAllBytes($src)
    $ub = New-Object byte[] 0xe2248
    [Array]::Copy($all, 0x40e4c0, $ub, 0, $ub.Length)
    $tmp = Join-Path $env:TEMP "uboot_$PID"; New-Item -ItemType Directory -Force $tmp | Out-Null
    try {
        [IO.File]::WriteAllBytes("$tmp\uboot.bin", $ub)
        "SECTIONS`n{`n  . = 0x200000;`n  .text : { *(.text) }`n}`n" | Set-Content "$tmp\u.ld" -Encoding ascii
        Push-Location $tmp
        try {
            # contents 标志不能省，否则段内容会被丢成全 0
            & "$tc-objcopy.exe" -I binary -O elf64-littleaarch64 -B aarch64 --rename-section .data=.text,contents,alloc,load,readonly,code uboot.bin u.o
            & "$tc-ld.exe" -T u.ld -e 0x200000 u.o -o u-boot.elf
        } finally { Pop-Location }
        Copy-Item "$tmp\u-boot.elf" "$parts\u-boot.elf" -Force
    } finally { Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue }
    $h2 = (Get-FileHash "$parts\u-boot.elf" -Algorithm MD5).Hash
    if ($h2 -ne $UBOOT_ELF_MD5) { throw "重建出的 u-boot.elf MD5 不符（实际 $h2，期望 $UBOOT_ELF_MD5）——厂家 BOOT.bin 版本可能变了，截取偏移要重算" }
}

New-Item -ItemType Directory -Force $parts | Out-Null

# u-boot.elf：已入库，正常不会进这个分支；丢了才重建
if (-not (Test-Path "$parts\u-boot.elf")) {
    "parts\u-boot.elf 缺失，从 vendor-BOOT.bin 重建"
    New-UBootElf
    "重建完成：md5 $((Get-FileHash "$parts\u-boot.elf" -Algorithm MD5).Hash)（台账登记值 E844648EC910C87EA8A036AD4D3D3E45）"
}

# fsbl.elf：已入库；缺失才从厂家 demo 取
if (-not (Test-Path "$parts\fsbl.elf")) {
    $vd = Get-VendorDir
    $src = Join-Path $vd 'soc_sdk\soc_fsbl\build\soc_fsbl.elf'
    if (-not (Test-Path $src)) { throw "找不到厂家 FSBL：$src（用 -VendorDemo 指定厂家 NPU demo 目录）" }
    Copy-Item $src "$parts\fsbl.elf"
    "已取回 parts\fsbl.elf：md5 $((Get-FileHash "$parts\fsbl.elf" -Algorithm MD5).Hash)（台账登记值 A64A56C0C37003AF025C45ABF0DA2732，不符请在 parts\MD5SUMS.txt 更新）"
}

# u-boot.elf：入库零件，每次打包都校验 MD5（它进了 BOOT.bin 第 3 分区，静默损坏要到开机失败才发现）
$ubHash = (Get-FileHash "$parts\u-boot.elf" -Algorithm MD5).Hash
if ($ubHash -ne $UBOOT_ELF_MD5) {
    throw @"
parts\u-boot.elf 的 MD5 不符（实际 $ubHash，期望 $UBOOT_ELF_MD5）。
  它是入库零件，正常不该变。若是被误改/损坏，删掉它后用 -VendorDemo 重建：
    Remove-Item parts\u-boot.elf
    .\make_bootbin.ps1 -VendorDemo <厂家 NPU demo 目录> -Bit <你的 .bit>
  若确认是有意替换（例如换了 U-Boot），请同步更新 parts\MD5SUMS.txt 与本脚本的 `$UBOOT_ELF_MD5
"@
}

# 比特流：必须显式给（接自己的 PL 逻辑；厂家基线那条回归路径已下线）
if (-not $Bit) {
    throw @"
没给 -Bit。BOOT.bin 的第 2 分区就是 PL 比特流，请显式指定：
  .\make_bootbin.ps1 -Bit <TD 生成的 .bit>
  例：-Bit ..\..\pl\soc_base\soc_prj_Runs\phy_1\soc_prj.bit
（脚本不再有默认比特流：厂家基线比特流与"原样重打包验证"已于 2026-10-15 下线）
"@
}
if (-not (Test-Path $Bit)) { throw "找不到比特流：$Bit" }
if (-not $Fsbl) { $Fsbl = "$parts\fsbl.elf" }

$work = Join-Path $env:TEMP "bootbin_$PID"
New-Item -ItemType Directory -Force $work | Out-Null
try {
    Copy-Item $Fsbl "$work\fsbl.elf"
    Copy-Item $Bit  "$work\system.bit"
    Copy-Item "$parts\u-boot.elf" "$work\u-boot.elf"
    Copy-Item "$PSScriptRoot\boot.bif" "$work\boot.bif"
    Push-Location $work
    try {
        & $bootgen -arch dr1p104 -image boot.bif -o BOOT.bin -w | Out-Null
    } finally { Pop-Location }
    if (-not (Test-Path "$work\BOOT.bin")) { throw "BootGen 打包失败" }
    Copy-Item "$work\BOOT.bin" $Out -Force
    "OK: $Out  ($((Get-Item $Out).Length) bytes, md5 $((Get-FileHash $Out -Algorithm MD5).Hash))"
    "bitstream: $Bit  (md5 $((Get-FileHash $Bit -Algorithm MD5).Hash))"
    "fsbl:      $Fsbl  (md5 $((Get-FileHash $Fsbl -Algorithm MD5).Hash))"
} finally {
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
