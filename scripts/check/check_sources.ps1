# check_sources.ps1 — 文档 ↔ 源码唯一源一致性检查（DragonEyeSorter P0）
#
# 检查「文档说的事实」和「源码里的事实」是否同一份，专治：
#   1) 寄存器映射：docs/PL设计.md ↔ ps/src/hw/des_regs.h（外加 core 读地址表 RTL des_regs.v）
#   2) 类别表：docs/视觉与检测.md ↔ scripts/deploy/out/label_list.json
#   3) CSV 列数：文档声明 ↔ ps/src/app/part_log.cpp 的 csv_header()
#   4) 部署清单：scripts/deploy/manifest.tsv ↔ scripts/deploy/out/ 实存文件
#
# 用法：
#   pwsh -File scripts/check/check_sources.ps1              # 有 error 时退出 1
#   pwsh -File scripts/check/check_sources.ps1 -WarnOnly    # 只报告、总是退出 0
#   pwsh -File scripts/check/check_sources.ps1 -Verbose     # 打印通过项统计
#
# 路径与例外在同目录 source_check.json（例外必须写 reason）。只读：不会修改任何文件。

param(
    [switch]$WarnOnly,
    [switch]$Verbose
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$configPath = Join-Path $PSScriptRoot 'source_check.json'
if (-not (Test-Path -LiteralPath $configPath)) {
    Write-Host "缺少配置: $configPath" -ForegroundColor Red
    exit 2
}
$config = Get-Content -Raw -LiteralPath $configPath -Encoding UTF8 | ConvertFrom-Json

$script:errors = New-Object System.Collections.ArrayList
$script:warnings = New-Object System.Collections.ArrayList
$script:stats = @{ Registers = 0; CsvClaims = 0; Labels = 0; ManifestRows = 0; OutFiles = 0 }

function Add-Finding {
    param([string]$Level, [string]$File, [int]$Line, [string]$Message)
    $item = [pscustomobject]@{ Level = $Level; File = $File; Line = $Line; Message = $Message }
    if ($Level -eq 'error') { [void]$script:errors.Add($item) } else { [void]$script:warnings.Add($item) }
}

function Test-Glob {
    param([string]$Path, [string]$Pattern)
    $p = $Path.Replace('\', '/')
    $g = $Pattern.Replace('\', '/')
    if ($g -eq '**') { return $true }
    if ($g.EndsWith('/**')) {
        $prefix = $g.Substring(0, $g.Length - 3)
        if ($p -eq $prefix) { return $true }
        return $p.StartsWith($prefix + '/', [System.StringComparison]::OrdinalIgnoreCase)
    }
    $ps = $p.Split('/')
    $gs = $g.Split('/')
    if ($ps.Length -ne $gs.Length) { return $false }
    for ($i = 0; $i -lt $ps.Length; $i++) {
        if ($ps[$i] -notlike $gs[$i]) { return $false }
    }
    return $true
}

function Test-AnyGlob {
    param([string]$Path, $Patterns)
    if ($null -eq $Patterns) { return $false }
    foreach ($g in @($Patterns)) {
        if ([string]::IsNullOrWhiteSpace([string]$g)) { continue }
        if (Test-Glob $Path ([string]$g)) { return $true }
    }
    return $false
}

function Read-Utf8Lines {
    param([string]$Path)
    return [System.IO.File]::ReadAllLines($Path, [System.Text.Encoding]::UTF8)
}

function Test-InRanges {
    param([int]$Addr, $Ranges)
    foreach ($r in @($Ranges)) {
        if (($Addr -ge [int]$r.Start) -and ($Addr -le [int]$r.End)) { return $true }
    }
    return $false
}

function Get-RelPath {
    param([string]$Full)
    return $Full.Substring($script:repoRoot.Length + 1).Replace('\', '/')
}

# ================= 1) 寄存器映射 =================
$reg = $config.register
$headerRel = [string]$reg.header
$docRel = [string]$reg.doc
$headerPath = Join-Path $repoRoot $headerRel
$docPath = Join-Path $repoRoot $docRel
$headerAddrs = @{}   # name -> addr
if (-not (Test-Path -LiteralPath $headerPath -PathType Leaf)) {
    Add-Finding 'error' $headerRel 0 '寄存器头文件不存在'
} else {
    foreach ($line in (Read-Utf8Lines $headerPath)) {
        $m = [regex]::Match($line, '^\s*#define\s+(DES_\w+)\s+0x([0-9A-Fa-f]+)u?\b')
        if (-not $m.Success) { continue }
        $name = $m.Groups[1].Value
        $val = [Convert]::ToInt32($m.Groups[2].Value, 16)
        if (($val -lt 0) -or ($val -gt 0xFFF)) { continue }
        if ($name -match '^DES_IO_ST_') { continue }
        $headerAddrs[$name] = $val
    }
    $script:stats.Registers = $headerAddrs.Count
}

if (-not (Test-Path -LiteralPath $docPath -PathType Leaf)) {
    Add-Finding 'error' $docRel 0 'PL 设计文档不存在'
} else {
    $docLines = Read-Utf8Lines $docPath
    $docTokens = New-Object System.Collections.ArrayList
    $ranges = New-Object System.Collections.ArrayList
    for ($i = 0; $i -lt $docLines.Length; $i++) {
        $ln = $docLines[$i]
        foreach ($rm in [regex]::Matches($ln, '0x([0-9A-Fa-f]{3})\s*~\s*0x([0-9A-Fa-f]{3})')) {
            [void]$ranges.Add([pscustomobject]@{ Start = [Convert]::ToInt32($rm.Groups[1].Value, 16); End = [Convert]::ToInt32($rm.Groups[2].Value, 16) })
        }
        foreach ($tm in [regex]::Matches($ln, '0x([0-9A-Fa-f]{3})\b')) {
            $idx = $tm.Index
            $len = $tm.Length
            $before = ''
            if ($idx -gt 0) { $before = $ln.Substring(0, $idx).TrimEnd() }
            $after = ''
            if (($idx + $len) -lt $ln.Length) { $after = $ln.Substring($idx + $len) }
            if ($before.EndsWith('~')) { continue }
            if ($after.StartsWith('~')) { continue }
            [void]$docTokens.Add([pscustomobject]@{
                Token = ('0x' + $tm.Groups[1].Value.ToUpper())
                Addr  = [Convert]::ToInt32($tm.Groups[1].Value, 16)
                Line  = ($i + 1)
            })
        }
    }

    $docSet = @{}
    foreach ($t in $docTokens) { $docSet[$t.Token] = $true }

    # 头文件 → 文档：每个寄存器都必须能在 PL 设计里查到（或落在已记载的地址区间内）
    foreach ($kv in $headerAddrs.GetEnumerator()) {
        $token = ('0x{0:X3}' -f $kv.Value)
        if ($docSet.ContainsKey($token)) { continue }
        if (Test-InRanges ([int]$kv.Value) $ranges) { continue }
        if (@($reg.headerOnlyAllowed) -contains $kv.Key) { continue }
        Add-Finding 'error' $docRel 0 "寄存器未在文档记载：$($kv.Key) $token（头文件有、PL设计没有；补表或登记例外的接入点）"
    }

    # 文档 → 头文件：文档里每个偏移都必须有对应 define（范围端点已跳过）
    $seen = @{}
    foreach ($t in $docTokens) {
        if ($seen.ContainsKey($t.Token)) { continue }
        $seen[$t.Token] = $true
        if ($docSet.ContainsKey($t.Token)) { continue }
        if (@($reg.docOnlyAllowed) -contains $t.Token) { continue }
        Add-Finding 'error' $docRel $t.Line "文档有、头文件没有的偏移：$($t.Token)（补 des_regs.h 或删文档行）"
    }

    # core 寄存器读地址表（RTL）↔ 头文件 0x000~0x03F
    $rtlRel = [string]$reg.rtlCore
    if (-not [string]::IsNullOrWhiteSpace($rtlRel)) {
        $rtlPath = Join-Path $repoRoot $rtlRel
        if (Test-Path -LiteralPath $rtlPath -PathType Leaf) {
            $rtlAddrs = @{}
            foreach ($line in (Read-Utf8Lines $rtlPath)) {
                foreach ($m in [regex]::Matches($line, "10'h([0-9A-Fa-f]{3})\s*:")) {
                    $word = [Convert]::ToInt32($m.Groups[1].Value, 16)
                    $addr = $word * 4
                    if ($addr -le 0xFFF) { $rtlAddrs[('0x{0:X3}' -f $addr)] = $true }
                }
            }
            $coreHeader = @{}
            foreach ($kv in $headerAddrs.GetEnumerator()) {
                if ([int]$kv.Value -le 0x03F) { $coreHeader[('0x{0:X3}' -f $kv.Value)] = $true }
            }
            foreach ($t in $coreHeader.Keys) {
                if (-not $rtlAddrs.ContainsKey($t)) {
                    Add-Finding 'error' $rtlRel 0 "RTL 读地址表缺 $t（头文件有、des_regs.v 的 case 没有）"
                }
            }
            foreach ($t in $rtlAddrs.Keys) {
                if (-not $coreHeader.ContainsKey($t)) {
                    Add-Finding 'error' $rtlRel 0 "RTL 读地址表多出 $t（des_regs.v 有、头文件没有）"
                }
            }
            if ($Verbose) { Write-Host "  寄存器：头文件 $($headerAddrs.Count) 个 / 文档 $($seen.Count) 个偏移 / RTL core $($rtlAddrs.Count) 个" -ForegroundColor DarkGray }
        } else {
            Add-Finding 'error' $rtlRel 0 'RTL 文件不存在（登记在 source_check.json 的 register.rtlCore）'
        }
    }
}

# ================= 2) CSV 列数 =================
$csv = $config.csv
$csvSourceRel = [string]$csv.source
$csvSourcePath = Join-Path $repoRoot $csvSourceRel
$csvCols = -1
if (-not (Test-Path -LiteralPath $csvSourcePath -PathType Leaf)) {
    Add-Finding 'error' $csvSourceRel 0 'CSV 头文件不存在'
} else {
    $lines = Read-Utf8Lines $csvSourcePath
    $start = -1
    for ($i = 0; $i -lt $lines.Length; $i++) {
        if ($lines[$i] -match 'csv_header\s*\(') { $start = $i; break }
    }
    if ($start -lt 0) {
        Add-Finding 'error' $csvSourceRel 0 '找不到 csv_header()'
    } else {
        $text = ''
        $sawReturn = $false
        for ($i = $start; $i -lt $lines.Length; $i++) {
            $ln = $lines[$i]
            if (-not $sawReturn) {
                if ($ln -match 'return\s+"') { $sawReturn = $true } else { continue }
            }
            foreach ($qm in [regex]::Matches($ln, '"([^"]*)"')) { $text += $qm.Groups[1].Value }
            if (($i -gt $start) -and ($ln -match '^\s*\}\s*$')) { break }
        }
        if ($text.Length -eq 0) {
            Add-Finding 'error' $csvSourceRel 0 'csv_header() 解析失败（找不到列名字符串）'
        } else {
            $csvCols = (($text.ToCharArray() | Where-Object { $_ -eq ',' }).Count) + 1
            if ($Verbose) { Write-Host "  CSV：part_log.cpp csv_header() = $csvCols 列" -ForegroundColor DarkGray }
            $mdFiles = @(Get-ChildItem -LiteralPath $repoRoot -Recurse -File -Filter *.md | Where-Object { $_.FullName -notmatch '\\\.git\\' })
            foreach ($f in $mdFiles) {
                $rel = Get-RelPath $f.FullName
                if (Test-AnyGlob $rel $csv.excludeGlobs) { continue }
                $fileLines = Read-Utf8Lines $f.FullName
                $inFence = $false
                for ($i = 0; $i -lt $fileLines.Length; $i++) {
                    $ln = $fileLines[$i]
                    if ($inFence) {
                        if ($ln -match '^\s*(`{3,}|~{3,})\s*$') { $inFence = $false }
                        continue
                    }
                    if ($ln -match '^\s*(`{3,}|~{3,})') { $inFence = $true; continue }
                    # 只看与 CSV / 记录格式相关的行，避免把 manifest「5 列」之类误报
                    if ($ln -notmatch 'CSV|记录|parts_') { continue }
                    foreach ($cm in [regex]::Matches($ln, '(?:(\d+)\s*列\s*CSV|(\d+)\s*列\s*[）)])')) {
                        $val = $null
                        if ($cm.Groups[1].Success) { $val = $cm.Groups[1].Value } elseif ($cm.Groups[2].Success) { $val = $cm.Groups[2].Value }
                        if ($null -eq $val) { continue }
                        $script:stats.CsvClaims++
                        if ([int]$val -ne $csvCols) {
                            Add-Finding 'error' $rel ($i + 1) "CSV 列数口径过期：写的是 $val 列，part_log.cpp 的 csv_header() 是 $csvCols 列"
                        }
                    }
                }
            }
        }
    }
}

# ================= 3) 类别表 =================
$lbl = $config.labels
$jsonRel = [string]$lbl.json
$docRel2 = [string]$lbl.doc
$jsonPath = Join-Path $repoRoot $jsonRel
$docPath2 = Join-Path $repoRoot $docRel2
if (-not (Test-Path -LiteralPath $jsonPath -PathType Leaf)) {
    Add-Finding 'error' $jsonRel 0 '类别表 JSON 不存在'
} elseif (-not (Test-Path -LiteralPath $docPath2 -PathType Leaf)) {
    Add-Finding 'error' $docRel2 0 '视觉与检测文档不存在'
} else {
    $json = Get-Content -Raw -LiteralPath $jsonPath -Encoding UTF8 | ConvertFrom-Json
    $labels = @($json.labels)
    $script:stats.Labels = $labels.Count
    for ($i = 0; $i -lt $labels.Count; $i++) {
        if ([int]$labels[$i].id -ne $i) {
            Add-Finding 'error' $jsonRel 0 "类别 id 不连续：第 $i 项的 id = $($labels[$i].id)（应与数组下标一致）"
        }
    }
    $docMap = @{}
    $docLines2 = Read-Utf8Lines $docPath2
    for ($i = 0; $i -lt $docLines2.Length; $i++) {
        foreach ($m in [regex]::Matches($docLines2[$i], '^\|\s*(\d+)\s*\|\s*`([^`]+)`\s*\|')) {
            $id = [int]$m.Groups[1].Value
            if ($docMap.ContainsKey($id)) {
                Add-Finding 'error' $docRel2 ($i + 1) "类别表 id $id 重复"
            } else {
                $docMap[$id] = $m.Groups[2].Value
            }
        }
    }
    foreach ($l in $labels) {
        $id = [int]$l.id
        if (-not $docMap.ContainsKey($id)) {
            Add-Finding 'error' $docRel2 0 "类别表缺 id $id（$($l.label)）"
        } elseif ($docMap[$id] -ne [string]$l.label) {
            Add-Finding 'error' $docRel2 0 "类别名不一致：文档 id $id = $($docMap[$id])，label_list.json = $($l.label)"
        }
    }
    $allowedExtra = @()
    foreach ($x in @($lbl.allowedExtraDocIds)) { if ($null -ne $x) { $allowedExtra += [int]$x } }
    foreach ($id in $docMap.Keys) {
        if ($id -ge $labels.Count) {
            if (-not ($allowedExtra -contains $id)) {
                Add-Finding 'error' $docRel2 0 "文档多出类别 id $id（$($docMap[$id])）：label_list.json 只有 $($labels.Count) 类，且未登记为待加类别"
            }
        }
    }
    if ($Verbose) { Write-Host "  类别：label_list.json $($labels.Count) 类 / 文档表 $($docMap.Count) 行" -ForegroundColor DarkGray }
}

# ================= 4) 部署清单 =================
$dep = $config.deploy
$manifestRel = [string]$dep.manifest
$outDirRel = [string]$dep.outDir
$manifestPath = Join-Path $repoRoot $manifestRel
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    Add-Finding 'error' $manifestRel 0 '部署清单不存在'
} else {
    $manifestSources = @{}
    $lines = Read-Utf8Lines $manifestPath
    for ($i = 0; $i -lt $lines.Length; $i++) {
        $ln = $lines[$i]
        if ([string]::IsNullOrWhiteSpace($ln)) { continue }
        if ($ln.TrimStart().StartsWith('#')) { continue }
        $fields = @($ln -split "`t")
        if ($fields.Count -lt 5) {
            Add-Finding 'error' $manifestRel ($i + 1) "manifest 应为 5 列 TAB 分隔，实际 $($fields.Count) 列"
            continue
        }
        $src = $fields[0].Trim()
        if ($src -eq '') { continue }
        $script:stats.ManifestRows++
        $manifestSources[$src] = $true
        $abs = Join-Path $repoRoot $src
        if (($src -match '^(scripts/deploy/out/|ps/src/)')) {
            if (-not (Test-Path -LiteralPath $abs -PathType Leaf)) {
                Add-Finding 'error' $manifestRel ($i + 1) "manifest 源文件不存在：$src"
            }
        } elseif (Test-AnyGlob $src $dep.missingAllowedGlobs) {
            # 编译产物 / 模型，不入库，仓库里没有是正常的
        } else {
            Add-Finding 'error' $manifestRel ($i + 1) "manifest 源路径未登记规则：$src（更新 scripts/check/source_check.json）"
        }
    }

    $tracked = @()
    try { $tracked = @(& git -C $repoRoot ls-files -- 'scripts/deploy/out' 2>$null) } catch { $tracked = @() }
    if ($tracked.Count -eq 0) {
        Add-Finding 'warning' $outDirRel 0 'git ls-files 查不到 out/ 入库文件，跳过「未登记」检查'
    } else {
        $allow = @{}
        foreach ($a in @($dep.outAllowlist)) {
            if ($null -ne $a) { $allow[[string]$a.file] = [string]$a.reason }
        }
        foreach ($rel in $tracked) {
            $r = ([string]$rel).Trim().Replace('\', '/')
            if ($r -eq '') { continue }
            $script:stats.OutFiles++
            if ($manifestSources.ContainsKey($r)) { continue }
            $leaf = Split-Path $r -Leaf
            if ($allow.ContainsKey($leaf)) { continue }
            Add-Finding 'error' $r 0 "out/ 文件未登记：既不在 manifest.tsv，也不在 source_check.json 的 outAllowlist（要么进产品清单，要么登记为手动/暂存件）"
        }
    }
    if ($Verbose) { Write-Host "  部署：manifest $($manifestSources.Count) 条源 / out 入库 $($script:stats.OutFiles) 个" -ForegroundColor DarkGray }
}

# ================= 输出 =================
Write-Host ''
Write-Host ("唯一源检查：寄存器 {0} 个 / CSV 口径 {1} 处 / 类别 {2} 类 / 部署清单 {3} 行" -f $script:stats.Registers, $script:stats.CsvClaims, $script:stats.Labels, $script:stats.ManifestRows) -ForegroundColor Cyan

if ($script:errors.Count -gt 0) {
    Write-Host ("`n错误 {0} 项：" -f $script:errors.Count) -ForegroundColor Red
    foreach ($e in ($script:errors | Sort-Object File, Line)) {
        Write-Host ("  {0}:{1}  {2}" -f $e.File, $e.Line, $e.Message) -ForegroundColor Red
    }
}
if ($script:warnings.Count -gt 0) {
    Write-Host ("`n告警 {0} 项：" -f $script:warnings.Count) -ForegroundColor Yellow
    foreach ($w in ($script:warnings | Sort-Object File, Line)) {
        Write-Host ("  {0}:{1}  {2}" -f $w.File, $w.Line, $w.Message) -ForegroundColor Yellow
    }
}
if (($script:errors.Count -eq 0) -and ($script:warnings.Count -eq 0)) {
    Write-Host '✓ 全部通过' -ForegroundColor Green
}

if (($script:errors.Count -gt 0) -and (-not $WarnOnly)) { exit 1 }
exit 0
