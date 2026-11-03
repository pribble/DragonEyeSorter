<#
.SYNOPSIS
    用 git archive 把仓库代码打成提交用代码包：先出 zip，再用 WinRAR 命令行转成 RAR（官方要求）。

.DESCRIPTION
    官方 2026 提交页口径：全部代码，RAR 格式，≤200MB。
    打包范围是**整仓 tracked 文件**，再排除第三方版权资料（docs/ref 下的 PDF 原件）：
    数据集、模型权重、SD 镜像、BOOT.bin、编译产物本来就被 .gitignore 排除，git archive 只导已入库的文件。

    git archive 用 HEAD（不是工作区）：未提交的改动不进包——**先提交再打包**，否则包里是旧代码。
    仓库根目录由 $PSScriptRoot 上溯两级推导，脚本内不写主机绝对路径；Rar.exe 从 PATH 或 WinRAR 默认安装目录找。

    判定：超过上限警告并以非零码退出（2）；找不到 Rar.exe 时只出 zip 并以退出码 3 提示。

.PARAMETER OutDir
    输出目录，缺省 = ../out（相对本脚本位置）。

.PARAMETER Name
    包名（不含扩展名），缺省 = 基于FPGA的工业智能检测系统_代码_v1.0（与官网作品名一致）。

.PARAMETER MaxMB
    大小上限（MB），缺省 200。

.PARAMETER Rev
    git 版本引用，缺省 HEAD。

.EXAMPLE
    pwsh -File submission/build/pack_code.ps1
#>
[CmdletBinding()]
param(
    [string] $OutDir,
    [string] $Name = '基于FPGA的工业智能检测系统_代码_v1.0',
    [double] $MaxMB = 200,
    [string] $Rev = 'HEAD'
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

# 不进代码包的已入库文件（git pathspec）：第三方版权资料原件、AI 协作说明
$Exclude = @(':(exclude)docs/ref/*.pdf', ':(exclude)AGENTS.md')

$BuildDir = $PSScriptRoot
$Repo = Split-Path -Parent (Split-Path -Parent $BuildDir)
if (-not $OutDir) { $OutDir = Join-Path (Split-Path -Parent $BuildDir) 'out' }
$OutDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutDir)
if (-not (Test-Path -LiteralPath (Join-Path $Repo '.git'))) { throw "仓库根里没有 .git：$Repo" }
if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw 'PATH 上找不到 git' }
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

$revName = (git -C $Repo rev-parse --short $Rev) -join ''
$dirty = @(git -C $Repo status --porcelain)
if ($dirty.Count -gt 0) { Write-Warning ("工作区有 {0} 项未提交改动；按 {1} 导出，这些改动不在包里" -f $dirty.Count, $Rev) }
$files = @(git -C $Repo ls-files -- . @Exclude)
Write-Host ("[pack_code] 版本 {0}，入包 {1} 个文件（已排除：{2}）" -f $revName, $files.Count, ($Exclude -join ' '))

# --- zip ---------------------------------------------------------------------
$zip = Join-Path $OutDir "$Name.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
git -C $Repo archive --format=zip --prefix='DragonEyeSorter/' -o $zip $Rev -- . @Exclude
if ($LASTEXITCODE -ne 0) { throw "git archive 失败（退出码 $LASTEXITCODE）" }
Write-Host ("ZIP : {0}（{1:N2} MB）" -f $zip, ((Get-Item -LiteralPath $zip).Length / 1MB))

# --- rar ---------------------------------------------------------------------
$rarExe = (Get-Command Rar.exe -ErrorAction SilentlyContinue).Source
if (-not $rarExe) {
    foreach ($c in @("$env:ProgramFiles\WinRAR\Rar.exe", "${env:ProgramFiles(x86)}\WinRAR\Rar.exe")) {
        if ($c -and (Test-Path -LiteralPath $c)) { $rarExe = $c; break }
    }
}
if (-not $rarExe) { Write-Warning '找不到 Rar.exe（装 WinRAR 后重跑）；本次只有 zip'; exit 3 }

$tmp = Join-Path ([IO.Path]::GetTempPath()) ("pack_code_" + [Guid]::NewGuid().ToString('N'))
try {
    Expand-Archive -LiteralPath $zip -DestinationPath $tmp
    $rar = Join-Path $OutDir "$Name.rar"
    if (Test-Path -LiteralPath $rar) { Remove-Item -LiteralPath $rar -Force }
    Push-Location $tmp
    & $rarExe a -r -m5 -ma5 -idq $rar 'DragonEyeSorter'
    $code = $LASTEXITCODE
    Pop-Location
    if ($code -ne 0) { throw "Rar.exe 失败（退出码 $code）" }
    & $rarExe t -idq $rar
    if ($LASTEXITCODE -ne 0) { throw "RAR 自检失败（退出码 $LASTEXITCODE）" }
} finally {
    if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Recurse -Force }
}

$mb = (Get-Item -LiteralPath $rar).Length / 1MB
Write-Host ("RAR : {0}（{1:N2} MB，上限 {2} MB，自检通过）" -f $rar, $mb, $MaxMB) -ForegroundColor Cyan
if ($mb -gt $MaxMB) { Write-Host '[!] 超过上限，不要提交' -ForegroundColor Red; exit 2 }
Write-Host '完成。' -ForegroundColor Green
exit 0
