# selftest.ps1 — check_docs / check_sources 自测：在临时副本上注入坏样例，断言退出码与关键报错
#
# 用法（仓库根）：
#   pwsh -File scripts/check/selftest.ps1          # 全部通过 exit 0，任一失败 exit 1
#   pwsh -File scripts/check/selftest.ps1 -Keep    # 保留临时副本便于排查
#
# 原理：把当前工作区里 git 跟踪的文件复制到 %TEMP% 临时目录（含未提交的检查器改动），在副本上
# 逐项注入文档/源码缺陷，跑副本里的检查器并断言 exit code 与报错关键字；每项测完立即还原。
# 只改临时副本：不写主工作区、不联网、不碰板子。改了检查器规则后必须重跑。

param([switch]$Keep)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$tmp = Join-Path $env:TEMP ("des-check-selftest_" + $PID)
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

$script:pass = 0
$script:fail = 0
$script:skip = 0
$script:failNames = New-Object System.Collections.ArrayList

function Write-Head([string]$Text) {
    Write-Host "`n$Text" -ForegroundColor Cyan
}

function Copy-TrackedTree {
    if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Recurse -Force }
    New-Item -ItemType Directory -Path $tmp -Force | Out-Null
    $files = @(& git -C $repoRoot ls-files 2>$null)
    if (($LASTEXITCODE -ne 0) -or ($files.Count -eq 0)) { throw 'git ls-files 失败：selftest 需要 git 工作区' }
    # 未跟踪但未忽略的文件也要复制（例如尚未提交的 selftest.ps1 自身、新文档）
    $untracked = @(& git -C $repoRoot ls-files --others --exclude-standard 2>$null)
    foreach ($u in $untracked) { if ($files -notcontains $u) { $files += $u } }
    foreach ($f in $files) {
        $src = Join-Path $repoRoot $f
        if (-not (Test-Path -LiteralPath $src -PathType Leaf)) { continue }
        $dst = Join-Path $tmp $f
        $dir = Split-Path -Parent $dst
        if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
        Copy-Item -LiteralPath $src -Destination $dst -Force
    }
    # check_sources 用 git ls-files 判 out/ 入库状态；临时副本只要把 out/ 加进索引即可
    & git -C $tmp init -q 2>$null | Out-Null
    & git -C $tmp add -- scripts/deploy/out 2>$null | Out-Null
}

function Invoke-Check([string]$RelScript, [string[]]$Extra = @()) {
    $path = Join-Path $tmp $RelScript
    $out = & pwsh -NoProfile -File $path @Extra 2>&1 | Out-String
    return [pscustomobject]@{ Exit = $LASTEXITCODE; Out = $out }
}

function Add-Append([string]$Path, [string]$Text) {
    [System.IO.File]::AppendAllText($Path, $Text, $utf8NoBom)
}

# 通用用例：可选先改一个文件（Edit 接收绝对路径），跑检查，断言退出码与输出关键字，最后还原
function Invoke-Case {
    param(
        [string]$Name,
        [string]$RelScript,
        [string]$File = '',
        [scriptblock]$Edit = $null,
        [int]$WantExit = 0,
        [string[]]$Match = @(),
        [string[]]$NotMatch = @(),
        [string[]]$Extra = @()
    )
    $full = ''
    $orig = $null
    try {
        if ($File -ne '') {
            $full = Join-Path $tmp $File
            if (Test-Path -LiteralPath $full) { $orig = [System.IO.File]::ReadAllBytes($full) }
            if ($null -ne $Edit) { & $Edit $full }
        }
        $r = Invoke-Check $RelScript $Extra
        $problems = New-Object System.Collections.ArrayList
        if ($r.Exit -ne $WantExit) { [void]$problems.Add("exit=$($r.Exit)，期望 $WantExit") }
        foreach ($m in $Match) {
            if ($r.Out -notmatch [regex]::Escape($m)) { [void]$problems.Add("输出缺少「$m」") }
        }
        foreach ($m in $NotMatch) {
            if ($r.Out -match [regex]::Escape($m)) { [void]$problems.Add("输出不应出现「$m」") }
        }
        if ($problems.Count -gt 0) {
            $script:fail++
            [void]$script:failNames.Add($Name)
            Write-Host "  FAIL  $Name" -ForegroundColor Red
            foreach ($p in $problems) { Write-Host "        $p" -ForegroundColor DarkRed }
            if ($r.Out.Length -gt 0) {
                Write-Host '        --- 检查器输出 ---' -ForegroundColor DarkGray
                ($r.Out.Trim() -split "`n" | Select-Object -First 12) | ForEach-Object { Write-Host "        $_" -ForegroundColor DarkGray }
            }
        } else {
            $script:pass++
            Write-Host "  PASS  $Name" -ForegroundColor Green
        }
    } catch {
        $script:fail++
        [void]$script:failNames.Add($Name)
        Write-Host "  FAIL  $Name :: $($_.Exception.Message)" -ForegroundColor Red
    } finally {
        if ($full -ne '') {
            if ($null -ne $orig) {
                [System.IO.File]::WriteAllBytes($full, $orig)
            } elseif (Test-Path -LiteralPath $full) {
                Remove-Item -LiteralPath $full -Force -ErrorAction SilentlyContinue
            }
        }
    }
}

Write-Host "检查器自测：基于 $repoRoot 的工作区副本（临时目录 $tmp）" -ForegroundColor Yellow
Copy-TrackedTree

# ---------- 基线 ----------
Write-Head '基线（当前仓库内容应全过）'
Invoke-Case -Name '基线 check_docs' -RelScript 'scripts/check/check_docs.ps1' -WantExit 0
Invoke-Case -Name '基线 check_sources' -RelScript 'scripts/check/check_sources.ps1' -WantExit 0

# ---------- check_docs：链接 / 图片 / 行内路径 ----------
Write-Head 'check_docs：链接与行内路径'
Invoke-Case -Name '断链应报错' -RelScript 'scripts/check/check_docs.ps1' `
    -File 'docs/README.md' -WantExit 1 -Match @('链接目标不存在') `
    -Edit { param($p) $t = [System.IO.File]::ReadAllText($p); [System.IO.File]::WriteAllText($p, $t.Replace('系统概述.md', '系统概述X.md'), $utf8NoBom) }
Invoke-Case -Name '行内代码里的失效 scripts/ 路径应报错（P1 盲区）' -RelScript 'scripts/check/check_docs.ps1' `
    -File 'scripts/README.md' -WantExit 1 -Match @('行内路径不存在') `
    -Edit { param($p) Add-Append $p ("`n" + '`scripts/boot/no_such.ps1`' + "`n") }
Invoke-Case -Name '围栏代码块里的失效 scripts/ 路径应报错' -RelScript 'scripts/check/check_docs.ps1' `
    -File 'scripts/README.md' -WantExit 1 -Match @('行内路径不存在') `
    -Edit { param($p) Add-Append $p ("`n``````powershell`npwsh -File scripts/no_such_script.ps1`n``````" + "`n") }
Invoke-Case -Name '行内代码里的合法 scripts/ 路径应通过' -RelScript 'scripts/check/check_docs.ps1' `
    -File 'scripts/README.md' -WantExit 0 `
    -Edit { param($p) Add-Append $p ("`n" + '`scripts/boot/make_bootbin.ps1`' + "`n") }
Invoke-Case -Name '嵌套图片链接的外层目标失效应报错' -RelScript 'scripts/check/check_docs.ps1' `
    -File 'docs/板端布局.md' -WantExit 1 -Match @('链接目标不存在') `
    -Edit { param($p) Add-Append $p ("`n[![布局](figures/fig_layout.png)](no_such_target.md)`n") }
Invoke-Case -Name '引用式链接目标失效应报错' -RelScript 'scripts/check/check_docs.ps1' `
    -File 'docs/板端布局.md' -WantExit 1 -Match @('链接目标不存在') `
    -Edit { param($p) Add-Append $p ("`n[x][r]`n`n[r]: no_such_ref.md`n") }
Invoke-Case -Name '转义的反例链接不应误报' -RelScript 'scripts/check/check_docs.ps1' `
    -File 'docs/板端布局.md' -WantExit 0 `
    -Edit { param($p) Add-Append $p ("`n" + '\[x](no_such.md)' + "`n") }
Invoke-Case -Name '4 空格缩进代码块里的反例不应误报' -RelScript 'scripts/check/check_docs.ps1' `
    -File 'docs/板端布局.md' -WantExit 0 `
    -Edit { param($p) Add-Append $p ("`n    [x](no_such.md)`n") }

# ---------- check_docs：锚点 ----------
Write-Head 'check_docs：锚点（GitHub 语义）'
Invoke-Case -Name '重复标题的 -1 后缀锚点应通过' -RelScript 'scripts/check/check_docs.ps1' `
    -File 'docs/板端布局.md' -WantExit 0 `
    -Edit { param($p) Add-Append $p ("`n## 自测锚点`n## 自测锚点`n`n[去第一个](#自测锚点-1)`n") }
Invoke-Case -Name '不存在的 -2 后缀锚点应报错' -RelScript 'scripts/check/check_docs.ps1' `
    -File 'docs/板端布局.md' -WantExit 1 -Match @('锚点不存在') `
    -Edit { param($p) Add-Append $p ("`n## 自测锚点`n## 自测锚点`n`n[去不存在的](#自测锚点-2)`n") }
Invoke-Case -Name '连续两个空格的标题锚点应是两个连字符' -RelScript 'scripts/check/check_docs.ps1' `
    -File 'docs/板端布局.md' -WantExit 0 `
    -Edit { param($p) Add-Append $p ("`n## 两个  空格`n`n[跳转](#两个--空格)`n") }

# ---------- check_docs：关键数字 / 索引 ----------
Write-Head 'check_docs：关键数字与索引完整性'
Invoke-Case -Name '关键数字无来源应告警' -RelScript 'scripts/check/check_docs.ps1' `
    -File 'docs/板端布局.md' -WantExit 0 -Match @('关键数字') `
    -Edit { param($p) Add-Append $p ("`n99.31 与 catalog/ 无关。`n") }
Invoke-Case -Name '关键数字带来源链接不应告警（同段落）' -RelScript 'scripts/check/check_docs.ps1' `
    -File 'docs/板端布局.md' -WantExit 0 -NotMatch @('关键数字') `
    -Edit { param($p) Add-Append $p ("`n99.31，见 [测试与验证](测试与验证.md)。`n") }
Invoke-Case -Name 'docs/ 下未索引的 md 应报错' -RelScript 'scripts/check/check_docs.ps1' `
    -File 'docs/临时孤儿.md' -WantExit 1 -Match @('未索引') `
    -Edit { param($p) [System.IO.File]::WriteAllText($p, "# 临时孤儿`n", $utf8NoBom) }
Invoke-Case -Name '-WarnOnly 有 error 也退 0' -RelScript 'scripts/check/check_docs.ps1' `
    -File 'docs/README.md' -WantExit 0 -Extra @('-WarnOnly') `
    -Edit { param($p) $t = [System.IO.File]::ReadAllText($p); [System.IO.File]::WriteAllText($p, $t.Replace('系统概述.md', '系统概述X.md'), $utf8NoBom) }

# ---------- check_sources：寄存器 / CSV / 类别表 / 清单 ----------
Write-Head 'check_sources：寄存器 / CSV / 类别表 / 部署清单'
Invoke-Case -Name '文档独有偏移应报错（P1 死代码）' -RelScript 'scripts/check/check_sources.ps1' `
    -File 'docs/PL设计.md' -WantExit 1 -Match @('文档有、头文件没有的偏移') `
    -Edit { param($p) Add-Append $p ("`n测试伪偏移 0x2F7`n") }
Invoke-Case -Name '头文件独有寄存器应报错' -RelScript 'scripts/check/check_sources.ps1' `
    -File 'ps/src/hw/des_regs.h' -WantExit 1 -Match @('寄存器未在文档记载') `
    -Edit { param($p) Add-Append $p ("`n#define DES_SELFTEST_REG 0x2E7u`n") }
Invoke-Case -Name 'csv_header 内的带引号注释不影响列数' -RelScript 'scripts/check/check_sources.ps1' `
    -File 'ps/src/app/part_log.cpp' -WantExit 0 -NotMatch @('CSV 列数口径过期') `
    -Edit {
        param($p)
        $raw = [System.IO.File]::ReadAllText($p)
        $nl = if ($raw.Contains("`r`n")) { "`r`n" } else { "`n" }
        $lines = $raw -split "\r?\n"
        $idx = -1
        for ($j = 0; $j -lt $lines.Count; $j++) { if ($lines[$j] -match 'const char \*csv_header') { $idx = $j; break } }
        if ($idx -lt 0) { throw 'part_log.cpp 里找不到 csv_header' }
        $j = $idx
        while (($j -lt $lines.Count) -and ($lines[$j].Trim() -ne '{')) { $j++ }
        $newLines = @()
        $newLines += $lines[0..$j]
        $newLines += '    // 自测 "a,b"'
        if ($j + 1 -lt $lines.Count) { $newLines += $lines[($j + 1)..($lines.Count - 1)] }
        [System.IO.File]::WriteAllText($p, ($newLines -join $nl), $utf8NoBom)
    }
Invoke-Case -Name 'CSV 列数变化应报全（含 pc/report/README.md）' -RelScript 'scripts/check/check_sources.ps1' `
    -File 'ps/src/app/part_log.cpp' -WantExit 1 -Match @('CSV 列数口径过期', 'pc/report/README.md') `
    -Edit { param($p) $t = [System.IO.File]::ReadAllText($p); [System.IO.File]::WriteAllText($p, $t.Replace(',beam_err_mm"', '"'), $utf8NoBom) }
Invoke-Case -Name '类别表少一层应报错' -RelScript 'scripts/check/check_sources.ps1' `
    -File 'scripts/deploy/out/label_list.json' -WantExit 1 -Match @('文档多出类别') `
    -Edit {
        param($p)
        $j = Get-Content -LiteralPath $p -Raw -Encoding UTF8 | ConvertFrom-Json
        $labels = @($j.labels)
        $j.labels = @($labels[0..($labels.Count - 2)])
        [System.IO.File]::WriteAllText($p, ($j | ConvertTo-Json -Depth 10), $utf8NoBom)
    }
Invoke-Case -Name '普通表格里的「| 0 | `px` |」不应误判类别表' -RelScript 'scripts/check/check_sources.ps1' `
    -File 'docs/视觉与检测.md' -WantExit 0 -NotMatch @('类别表') `
    -Edit { param($p) Add-Append $p ("`n| 缺陷 | 做法 |`n|---|---|`n| 0 | ``px`` | 0.25mm |`n") }
Invoke-Case -Name 'out/ 新建未入库文件应报错' -RelScript 'scripts/check/check_sources.ps1' `
    -File 'scripts/deploy/out/zz_selftest_new.txt' -WantExit 1 -Match @('out/ 文件未登记') `
    -Edit { param($p) [System.IO.File]::WriteAllText($p, "selftest`n", $utf8NoBom) }

# ---------- 汇总 ----------
Write-Host ''
Write-Host ('=' * 60) -ForegroundColor Gray
Write-Host ("检查器自测：{0} 通过 / {1} 失败 / {2} 跳过" -f $script:pass, $script:fail, $script:skip) `
    -ForegroundColor $(if ($script:fail -gt 0) { 'Red' } else { 'Green' })
if ($script:fail -gt 0) {
    Write-Host '失败项：' -ForegroundColor Red
    foreach ($n in $script:failNames) { Write-Host "  - $n" -ForegroundColor Red }
}

if ($Keep) {
    Write-Host "临时副本保留在：$tmp" -ForegroundColor Yellow
} else {
    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
}

if ($script:fail -gt 0) { exit 1 }
exit 0
