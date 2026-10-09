# 打包 BOOT.bin：新比特流 + FSBL + U-Boot（Windows，只用 FD 自带的 BootGen 与交叉工具链）
#
# 只改 PL 逻辑：  .\make_bootbin.ps1 -Bit <TD 生成的 .bit>
# 改了 PS 配置：  .\make_bootbin.ps1 -Bit <.bit> -Fsbl <FD 重编的 soc_fsbl.elf>
# 原样重打包验证：.\make_bootbin.ps1            （用厂家比特流，MD5 应为 41489E57...）
#
# 零件优先级：parts\ 里已有 fsbl.elf / u-boot.elf 就直接用（已入库，2026-10-09 起）；
# 缺失时才从厂家 NPU demo 重取（vendor.bit 始终不入库，从 demo 取）：
#   fsbl.elf   ← demo\soc_sdk\soc_fsbl\build\soc_fsbl.elf
#   u-boot.elf ← 从 demo\boot\BOOT.bin 第 3 分区截出，包成 ELF（载入 / 入口 0x200000）
#   vendor.bit ← demo\soc_prj\soc_prj_Runs\phy_1\soc_prj.bit
param(
    [string]$Bit,
    [string]$Fsbl,
    [string]$Out = "$PSScriptRoot\BOOT.bin",
    [string]$FdRoot = $(if ($env:FD_ROOT) { $env:FD_ROOT } else { 'D:\ANLU\FD' }),
    [string]$VendorDemo = $(if ($env:NPU_DEMO) { $env:NPU_DEMO } else { 'D:\ANLU\02_F3P_DR1M90GEG400\01_start\02_start_Linux\05_application_demo\05-5_NPU演示\demo' })
)
$ErrorActionPreference = 'Stop'
$parts   = Join-Path $PSScriptRoot 'parts'
$bootgen = Join-Path $FdRoot 'tools\bootgen\albg1.exe'
$tc      = Join-Path $FdRoot 'toolchain\gcc\aarch64_linux\bin\aarch64-linux-gnu'
foreach ($p in $bootgen, "$tc-objcopy.exe", "$tc-ld.exe") { if (-not (Test-Path $p)) { throw "找不到 $p，用 -FdRoot 或环境变量 FD_ROOT 指定 FD 安装目录" } }

function Prepare-Parts {
    New-Item -ItemType Directory -Force $parts | Out-Null
    $vb = Join-Path $VendorDemo 'boot\BOOT.bin'
    if (-not (Test-Path $vb)) { throw "找不到厂家 BOOT.bin：$vb（用 -VendorDemo 或环境变量 NPU_DEMO 指定 NPU demo 目录）" }
    if ((Get-FileHash $vb -Algorithm MD5).Hash -ne 'D6B2D387CB405444D1E7EF10E2855285') { throw "厂家 BOOT.bin 的 MD5 不符，U-Boot 偏移可能不同：$vb" }

    if (-not (Test-Path "$parts\fsbl.elf")) { Copy-Item (Join-Path $VendorDemo 'soc_sdk\soc_fsbl\build\soc_fsbl.elf') "$parts\fsbl.elf" }
    if (-not (Test-Path "$parts\vendor.bit")) { Copy-Item (Join-Path $VendorDemo 'soc_prj\soc_prj_Runs\phy_1\soc_prj.bit') "$parts\vendor.bit" }
    if (-not (Test-Path "$parts\u-boot.elf")) {
        # 厂家 BOOT.bin 分区 2 = U-Boot：偏移 0x40e4c0，有效长度 0xe2248（不含末尾 SHA2 校验值）
        $all = [IO.File]::ReadAllBytes($vb)
        $ub = New-Object byte[] 0xe2248
        [Array]::Copy($all, 0x40e4c0, $ub, 0, $ub.Length)
        $tmp = Join-Path $env:TEMP "uboot_$PID"; New-Item -ItemType Directory -Force $tmp | Out-Null
        try {
            [IO.File]::WriteAllBytes("$tmp\uboot.bin", $ub)
            "SECTIONS`n{`n  . = 0x200000;`n  .text : { *(.text) }`n}`n" | Set-Content "$tmp\u.ld" -Encoding ascii
            Push-Location $tmp
            # contents 标志不能省，否则段内容会被丢成全 0
            & "$tc-objcopy.exe" -I binary -O elf64-littleaarch64 -B aarch64 --rename-section .data=.text,contents,alloc,load,readonly,code uboot.bin u.o
            & "$tc-ld.exe" -T u.ld -e 0x200000 u.o -o u-boot.elf
            Pop-Location
            Copy-Item "$tmp\u-boot.elf" "$parts\u-boot.elf"
        } finally { Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue }
    }
    "parts 已准备：$parts"
}
if (-not (Test-Path "$parts\u-boot.elf") -or -not (Test-Path "$parts\fsbl.elf")) { Prepare-Parts }
if (-not $Bit)  { $Bit  = "$parts\vendor.bit" }
if (-not $Fsbl) { $Fsbl = "$parts\fsbl.elf" }

$work = Join-Path $env:TEMP "bootbin_$PID"
New-Item -ItemType Directory -Force $work | Out-Null
try {
    Copy-Item $Fsbl "$work\fsbl.elf"
    Copy-Item $Bit  "$work\system.bit"
    Copy-Item "$parts\u-boot.elf" "$work\u-boot.elf"
    Copy-Item "$PSScriptRoot\boot.bif" "$work\boot.bif"
    Push-Location $work
    & $bootgen -arch dr1p104 -image boot.bif -o BOOT.bin -w | Out-Null
    Pop-Location
    if (-not (Test-Path "$work\BOOT.bin")) { throw "BootGen 打包失败" }
    Copy-Item "$work\BOOT.bin" $Out -Force
    "OK: $Out  ($((Get-Item $Out).Length) bytes, md5 $((Get-FileHash $Out -Algorithm MD5).Hash))"
    "bitstream: $Bit"
    "fsbl:      $Fsbl"
} finally {
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
