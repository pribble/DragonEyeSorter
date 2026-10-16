# scripts/deploy/deploy.ps1 —— Windows 侧入口：只把参数转给 deploy.sh（逻辑一份，不在这里重复实现）
#
#   .\scripts\deploy\deploy.ps1 -DryRun              只列板上与本地/仓库的差异
#   .\scripts\deploy\deploy.ps1                      部署
#   .\scripts\deploy\deploy.ps1 -Stage               刷新 out/（要上板的那一份，拷 U 盘用）
#   .\scripts\deploy\deploy.ps1 -Only des_app        只处理匹配的条目
#   .\scripts\deploy\deploy.ps1 -WithService         传 des_app 前停自启服务，校验完再起
#   .\scripts\deploy\deploy.ps1 -Dest /root/des/test 整份落到该目录（试新版，不覆盖生产件、不动自启服务）
#
# 用法与清单说明见同目录 README.md；WSL 里直接 `bash scripts/deploy/deploy.sh` 即可。
[CmdletBinding()]
param(
    [switch]$DryRun,
    [switch]$Stage,
    [string]$Only,
    [string]$Dest,
    [switch]$WithService
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$sh = Join-Path $here 'deploy.sh'

# 优先 Git Bash：C:\Windows\system32\bash.exe 是 WSL 入口，会换到另一套文件系统视图（/mnt/c/...）
$cands = @(
    "$env:ProgramFiles\Git\bin\bash.exe",
    "$env:ProgramFiles\Git\usr\bin\bash.exe",
    "${env:ProgramFiles(x86)}\Git\bin\bash.exe",
    "$env:LOCALAPPDATA\Programs\Git\bin\bash.exe"
)
$bash = $cands | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $bash) {
    Write-Error '找不到 Git Bash。装在别处就改上面的候选列表，或直接在 WSL 里跑 bash scripts/deploy/deploy.sh'
    exit 1
}

# deploy.sh 里全是 POSIX 路径运算，脚本路径先转成 /c/... 形式再交给 bash
$full = (Resolve-Path $sh).Path
$posix = '/' + $full.Substring(0, 1).ToLower() + ($full.Substring(2) -replace '\\', '/')

$shArgs = @($posix)
if ($DryRun) { $shArgs += '--dry-run' }
if ($Stage) { $shArgs += '--stage' }
if ($Only) { $shArgs += @('--only', $Only) }
if ($Dest) { $shArgs += @('--dest', $Dest) }
if ($WithService) { $shArgs += '--with-service' }

& $bash @shArgs
exit $LASTEXITCODE
