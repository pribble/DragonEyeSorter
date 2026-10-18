<#
.SYNOPSIS
    用 git archive 把「作品重要代码」打成 submission/out/DragonEyeSorter_代码_v1.0.zip。

.DESCRIPTION
    官方 2026 应用赛道口径：代码包 ≤100MB、RAR；本机没有 RAR 命令行工具，用 zip
    （Windows 资源管理器 / 官网都能收），内容与大小按后者准备。
    打包范围是**整仓 tracked 文件**：数据集、模型权重、SD 镜像、BOOT.bin、编译产物
    本来就被 .gitignore 排除，git archive 只导已入库的文件，天然不含它们，
    也不需要维护一份排除清单。仓库 tracked 文件见 `git ls-files | measure`。

    git archive 用 HEAD（不是工作区）：未提交的改动不进包——**先提交再打包**，
    否则包里是旧代码。仓库根目录由 $PSScriptRoot 上溯两级推导，脚本内不写主机绝对路径。

    判定：默认上限 100MB，超限警告并以非零码退出（2）；文件数与大小都打印出来。

.PARAMETER Out
    输出 zip 路径，缺省 = ../out/DragonEyeSorter_代码_v1.0.zip（相对本脚本位置）。

.PARAMETER MaxMB
    大小上限（MB），缺省 100。

.PARAMETER Rev
    git 版本引用，缺省 HEAD。

.EXAMPLE
    pwsh -File submission/build/pack_code.ps1

.EXAMPLE
    pwsh -File submission/build/pack_code.ps1 -Out ../out/code.zip -MaxMB 50
#>
[CmdletBinding()]
param(
    [string] $Out,
    [double] $MaxMB = 100,
    [string] $Rev = 'HEAD'
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$BuildDir = $PSScriptRoot                              # submission/build/
$Repo = Split-Path -Parent (Split-Path -Parent $BuildDir)   # 仓库根（上溯两级）
if (-not $Out) {
    $Out = Join-Path (Join-Path (Split-Path -Parent $BuildDir) 'out') `
        'DragonEyeSorter_代码_v1.0.zip'
}
$Out = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Out)

if (-not (Test-Path -LiteralPath (Join-Path $Repo '.git'))) {
    throw "按相对位置推出来的仓库根里没有 .git：$Repo（脚本应放在 <仓库>/submission/build/ 下）"
}
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "PATH 上找不到 git"
}
$outDir = Split-Path -Parent $Out
if (-not (Test-Path -LiteralPath $outDir)) {
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}

# --- 先看清要打的内容（tracked 文件数与合计大小）------------------------------
$tracked = @(git -C $Repo ls-files)
if ($LASTEXITCODE -ne 0) { throw "git ls-files 失败（退出码 $LASTEXITCODE）" }
$revName = (git -C $Repo rev-parse --short $Rev) -join ''
$dirty = @(git -C $Repo status --porcelain)
if ($LASTEXITCODE -ne 0) { throw "git status 失败（退出码 $LASTEXITCODE）" }
if ($dirty.Count -gt 0) {
    $msg = "工作区有 {0} 项未提交改动；git archive 按 {1} 导出，这些改动不在包里" -f `
        $dirty.Count, $Rev
    Write-Warning $msg
}

$trackedBytes = 0
foreach ($f in $tracked) {
    $p = Join-Path $Repo $f
    if (Test-Path -LiteralPath $p -PathType Leaf) {
        $trackedBytes += (Get-Item -LiteralPath $p).Length
    }
}
Write-Host ("[pack_code] 仓库    : {0}" -f $Repo)
Write-Host ("[pack_code] 版本    : {0}" -f $revName)
Write-Host ("[pack_code] tracked : {0} 个文件，合计 {1:N2} MB（未压缩）" -f `
    $tracked.Count, ($trackedBytes / 1MB))

# --- 打包 ---------------------------------------------------------------------
if (Test-Path -LiteralPath $Out) { Remove-Item -LiteralPath $Out -Force }
Write-Host ("[pack_code] 打包    : git archive --format=zip -o {0} {1}" -f $Out, $Rev)
git -C $Repo archive --format=zip --prefix='DragonEyeSorter/' -o $Out $Rev
if ($LASTEXITCODE -ne 0) { throw "git archive 失败（退出码 $LASTEXITCODE）" }

$zip = Get-Item -LiteralPath $Out
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead($Out)
try {
    $entries = @($archive.Entries | Where-Object { $_.Length -gt 0 -or $_.Name -ne '' })
    $entryCount = $archive.Entries.Count
} finally {
    $archive.Dispose()
}

$mb = $zip.Length / 1MB
Write-Host ''
Write-Host '=== 产物 ===' -ForegroundColor Cyan
Write-Host ("ZIP  : {0}" -f $zip.FullName)
Write-Host ("       条目 {0} 个（tracked {1} 个 + 目录项）" -f $entryCount, $tracked.Count)
Write-Host ("       大小 {0:N0} 字节（{1:N2} MB），上限 {2} MB" -f $zip.Length, $mb, $MaxMB)

if ($mb -gt $MaxMB) {
    Write-Host ("[!] 超过上限 {0} MB —— 不要提交，先精简 tracked 内容" -f $MaxMB) -ForegroundColor Red
    exit 2
}
Write-Host ("占上限 {0:N1}%" -f (100 * $mb / $MaxMB))
Write-Host '完成。' -ForegroundColor Green
exit 0
