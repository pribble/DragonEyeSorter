# check_sources.ps1 — 文档 ↔ 源码唯一源一致性检查（DragonEyeSorter P0）
#
# 检查「文档说的事实」和「源码里的事实」是否同一份，专治：
#   1) 寄存器映射：docs/PL设计.md ↔ ps/src/hw/des_regs.h 双向（头文件有 → 文档；文档有 → 头文件），
#      外加 core 读地址表 RTL des_regs.v ↔ 头文件 0x000~0x03F。头文件缺失时只报 1 条错并跳过这些交叉检查。
#      >0xFFF 的 #define 先按 register.headerNonRegister 例外静默跳过（基址 / ID 常量），
#      没登记的才报 warning 列出名字，避免漏掉新寄存器。
#   2) 类别表：docs/视觉与检测.md 的类别表 ↔ scripts/deploy/out/label_list.json；
#      只解析「表头含 label / 类别」的表格块，普通表格里的 | N | xxx | 行不算类别表。
#   3) CSV 列数：文档声明（「N 列 CSV」「N 列，」「N 列）」等）↔ part_log.cpp csv_header() 的
#      return 表达式里拼起来的字符串字面量（注释与其他语句不参与计数）。
#   4) 部署清单：scripts/deploy/manifest.tsv ↔ scripts/deploy/out/ 实存文件
#      （已入库 + 未跟踪未忽略的都算，新建还没 git add 的部署件同样会被抓出来）。
#
# 用法：
#   pwsh -File scripts/check/check_sources.ps1              # 有 error 时退出 1
#   pwsh -File scripts/check/check_sources.ps1 -WarnOnly    # 只报告、总是退出 0
#   pwsh -File scripts/check/check_sources.ps1 -Verbose     # 打印通过项统计
#
# 路径与例外在同目录 source_check.json：例外（docOnlyAllowed / headerOnlyAllowed / headerNonRegister /
# allowedExtraDocIds / missingAllowedGlobs / outAllowlist）一律写成带 reason 的对象；旧的纯字符串写法仍能跑但会告警。
# 只读：不会修改任何文件。

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
        if ($null -eq $g) { continue }
        $pat = $g
        if ($g -isnot [string]) { $pat = $g.glob }
        if ([string]::IsNullOrWhiteSpace([string]$pat)) { continue }
        if (Test-Glob $Path ([string]$pat)) { return $true }
    }
    return $false
}

function ConvertTo-ExceptionList {
    # 把例外数组统一成 @{ Value; Reason } 列表。兼容旧写法：纯字符串（Value=字符串，Reason 为空）。
    param($Items)
    $out = @()
    foreach ($it in @($Items)) {
        if ($null -eq $it) { continue }
        # 旧写法：纯字符串，或数字（如 allowedExtraDocIds: [5, 6]）
        if (($it -is [string]) -or ($it -is [ValueType])) {
            $txt = [string]$it
            if ([string]::IsNullOrWhiteSpace($txt)) { continue }
            $out += [pscustomobject]@{ Value = $txt; Reason = '' }
            continue
        }
        $val = ''
        $reason = ''
        foreach ($p in $it.PSObject.Properties) {
            if ($p.Name -eq 'reason') { $reason = [string]$p.Value }
            elseif ([string]::IsNullOrWhiteSpace($val)) { $val = [string]$p.Value }
        }
        if ([string]::IsNullOrWhiteSpace($val)) { continue }
        $out += [pscustomobject]@{ Value = $val; Reason = $reason }
    }
    return $out
}

function Assert-ExceptionReasons {
    # 配置纪律：例外必须写 reason。旧写法（纯字符串 / 数字）给 warning（仍可用），对象缺 reason 给 error。
    param([string]$Name, $Items)
    foreach ($it in @($Items)) {
        if ($null -eq $it) { continue }
        if (($it -is [string]) -or ($it -is [ValueType])) {
            $txt = [string]$it
            if ([string]::IsNullOrWhiteSpace($txt)) { continue }
            Add-Finding 'warning' 'scripts/check/source_check.json' 0 "例外 $Name 用的是旧写法（$txt，没有 reason），建议改成带 reason 的对象"
            continue
        }
        $val = ''
        $reason = ''
        foreach ($p in $it.PSObject.Properties) {
            if ($p.Name -eq 'reason') { $reason = [string]$p.Value }
            elseif ([string]::IsNullOrWhiteSpace($val)) { $val = [string]$p.Value }
        }
        if ([string]::IsNullOrWhiteSpace($reason)) {
            Add-Finding 'error' 'scripts/check/source_check.json' 0 "例外 $Name 的条目「$val」缺少 reason（配置纪律：例外必须写原因）"
        }
    }
}

# --- C++ 词法小工具：只用于 csv_header() 解析（字符串 / 注释 / 花括号配对） ---

function Skip-CppQuoted {
    # 从字符串字面量的起始引号往后跳，返回结束引号之后的下标
    param([string]$Text, [int]$Start)
    $q = $Text[$Start]
    $i = $Start + 1
    while ($i -lt $Text.Length) {
        if ($Text[$i] -eq '\') { $i += 2; continue }
        if ($Text[$i] -eq $q) { return ($i + 1) }
        $i++
    }
    return $i
}

function Skip-CppLineComment {
    param([string]$Text, [int]$Start)
    $i = $Start
    while (($i -lt $Text.Length) -and ($Text[$i] -ne "`n")) { $i++ }
    return $i
}

function Skip-CppBlockComment {
    param([string]$Text, [int]$Start)
    $i = $Start + 2
    while (($i + 1) -lt $Text.Length) {
        if (($Text[$i] -eq '*') -and ($Text[$i + 1] -eq '/')) { return ($i + 2) }
        $i++
    }
    return $Text.Length
}

function Remove-CppComments {
    param([string]$Text)
    $sb = New-Object System.Text.StringBuilder
    $i = 0
    while ($i -lt $Text.Length) {
        $c = $Text[$i]
        if (($c -eq '"') -or ($c -eq "'")) {
            $next = Skip-CppQuoted $Text $i
            [void]$sb.Append($Text.Substring($i, $next - $i))
            $i = $next
            continue
        }
        if (($c -eq '/') -and (($i + 1) -lt $Text.Length)) {
            if ($Text[$i + 1] -eq '/') { $i = Skip-CppLineComment $Text $i; continue }
            if ($Text[$i + 1] -eq '*') { $i = Skip-CppBlockComment $Text $i; continue }
        }
        [void]$sb.Append($c)
        $i++
    }
    return $sb.ToString()
}

function Get-CppFunctionBody {
    # 从 $OpenIndex 处的 '{' 做花括号配对（跳过字符串与注释），返回花括号内部的文本；失配返回 $null
    param([string]$Text, [int]$OpenIndex)
    $depth = 0
    $i = $OpenIndex
    while ($i -lt $Text.Length) {
        $c = $Text[$i]
        if (($c -eq '"') -or ($c -eq "'")) { $i = Skip-CppQuoted $Text $i; continue }
        if (($c -eq '/') -and (($i + 1) -lt $Text.Length)) {
            if ($Text[$i + 1] -eq '/') { $i = Skip-CppLineComment $Text $i; continue }
            if ($Text[$i + 1] -eq '*') { $i = Skip-CppBlockComment $Text $i; continue }
        }
        if ($c -eq '{') { $depth++ }
        elseif ($c -eq '}') {
            $depth--
            if ($depth -eq 0) { return $Text.Substring($OpenIndex + 1, $i - $OpenIndex - 1) }
        }
        $i++
    }
    return $null
}

function Get-CsvHeaderColumns {
    # 取 csv_header() 定义体 return 之后到第一个分号之间的字符串字面量，拼起来数逗号。
    # 注释先剥掉、return 之前的语句不参与 —— 函数体内的说明注释 / 别的字符串不会污染列数。
    param([string]$Path)
    $src = ((Read-Utf8Lines $Path) -join "`n")
    $from = 0
    $body = $null
    while ($from -lt $src.Length) {
        $m = [regex]::Match($src.Substring($from), 'csv_header\s*\([^)]*\)\s*\{')
        if (-not $m.Success) { return -1 }
        $openIdx = $from + $m.Index + $m.Length - 1
        $body = Get-CppFunctionBody $src $openIdx
        if ($null -ne $body) { break }
        $from = $openIdx + 1
    }
    if ($null -eq $body) { return -1 }
    $code = Remove-CppComments $body
    $rm = [regex]::Match($code, '\breturn\b')
    if (-not $rm.Success) { return -1 }
    $i = $rm.Index + $rm.Length
    $names = New-Object System.Text.StringBuilder
    while ($i -lt $code.Length) {
        $c = $code[$i]
        if ($c -eq ';') { break }
        if ($c -eq '"') {
            $j = $i + 1
            while ($j -lt $code.Length) {
                if (($code[$j] -eq '\') -and (($j + 1) -lt $code.Length)) { [void]$names.Append($code[$j + 1]); $j += 2; continue }
                if ($code[$j] -eq '"') { break }
                [void]$names.Append($code[$j])
                $j++
            }
            $i = $j + 1
            continue
        }
        $i++
    }
    $text = $names.ToString()
    if ($text.Length -eq 0) { return -1 }
    return (($text.ToCharArray() | Where-Object { $_ -eq ',' }).Count + 1)
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

# 配置纪律：所有例外都要有 reason（旧字符串写法只告警，保持向后兼容）
Assert-ExceptionReasons 'register.docOnlyAllowed' $config.register.docOnlyAllowed
Assert-ExceptionReasons 'register.headerOnlyAllowed' $config.register.headerOnlyAllowed
Assert-ExceptionReasons 'register.headerNonRegister' $config.register.headerNonRegister
Assert-ExceptionReasons 'csv.excludeGlobs' $config.csv.excludeGlobs
Assert-ExceptionReasons 'labels.allowedExtraDocIds' $config.labels.allowedExtraDocIds
Assert-ExceptionReasons 'deploy.missingAllowedGlobs' $config.deploy.missingAllowedGlobs
Assert-ExceptionReasons 'deploy.outAllowlist' $config.deploy.outAllowlist

# ================= 1) 寄存器映射 =================
$reg = $config.register
$headerRel = [string]$reg.header
$docRel = [string]$reg.doc
$headerPath = Join-Path $repoRoot $headerRel
$docPath = Join-Path $repoRoot $docRel
$headerAddrs = @{}   # name -> addr（只收 0x000~0xFFF 的寄存器）
$headerSet = @{}     # '0xNNN' -> name（「文档 → 头文件」方向用它比较，不能用文档自己建集合）
$docOnlyAllowed = @()
foreach ($ex in @(ConvertTo-ExceptionList $reg.docOnlyAllowed)) { $docOnlyAllowed += [string]$ex.Value }
$headerOnlyAllowed = @()
foreach ($ex in @(ConvertTo-ExceptionList $reg.headerOnlyAllowed)) { $headerOnlyAllowed += [string]$ex.Value }
# >0xFFF 的合法非寄存器 define（基址 / ID 常量）登记在这里，静默跳过；没登记的才告警
$headerNonRegister = @()
foreach ($ex in @(ConvertTo-ExceptionList $reg.headerNonRegister)) { $headerNonRegister += [string]$ex.Value }
$headerPresent = Test-Path -LiteralPath $headerPath -PathType Leaf
if (-not $headerPresent) {
    Add-Finding 'error' $headerRel 0 '寄存器头文件不存在（跳过文档 ↔ 头文件 / RTL 交叉检查，避免级联噪音）'
} else {
    $skipped = New-Object System.Collections.ArrayList
    foreach ($line in (Read-Utf8Lines $headerPath)) {
        $m = [regex]::Match($line, '^\s*#define\s+(DES_\w+)\s+0x([0-9A-Fa-f]+)u?\b')
        if (-not $m.Success) { continue }
        $name = $m.Groups[1].Value
        $val = [Convert]::ToInt64($m.Groups[2].Value, 16)
        if ($name -match '^DES_IO_ST_') { continue }
        if ($val -gt 0xFFF) {
            # 先匹配 headerNonRegister 例外（基址 / ID 常量）→ 静默跳过；没登记的才是疑似新寄存器 → 告警
            $isNonReg = $false
            foreach ($pat in $headerNonRegister) {
                if (($pat -ne '') -and ($name -match $pat)) { $isNonReg = $true; break }
            }
            if (-not $isNonReg) { [void]$skipped.Add($name) }
            continue
        }
        $headerAddrs[$name] = [int]$val
        $headerSet[('0x{0:X3}' -f [int]$val)] = $name
    }
    $script:stats.Registers = $headerAddrs.Count
    if ($skipped.Count -gt 0) {
        Add-Finding 'warning' $headerRel 0 ("跳过 {0} 个 >0xFFF 且未登记 register.headerNonRegister 的 #define（疑似新寄存器，请确认是基址 / ID 常量还是漏进寄存器表的偏移）：{1}" -f $skipped.Count, (@($skipped) -join ', '))
    }
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

    if (-not $headerPresent) {
        # 头文件缺失时上面已报 1 条 error；这里跳过全部交叉检查，避免每条文档偏移 / RTL 表都刷一条
        if ($Verbose) { Write-Host "  寄存器：头文件缺失，跳过文档 ↔ 头文件 / RTL 交叉检查（文档 $($docTokens.Count) 个偏移 token）" -ForegroundColor DarkGray }
    } else {
        # 头文件 → 文档：每个寄存器都必须能在 PL 设计里查到（或落在已记载的地址区间内）
        foreach ($kv in $headerAddrs.GetEnumerator()) {
            $token = ('0x{0:X3}' -f $kv.Value)
            if ($docSet.ContainsKey($token)) { continue }
            if (Test-InRanges ([int]$kv.Value) $ranges) { continue }
            if ($headerOnlyAllowed -contains $kv.Key) { continue }
            Add-Finding 'error' $docRel 0 "寄存器未在文档记载：$($kv.Key) $token（头文件有、PL设计没有；补表或登记例外的接入点）"
        }

        # 文档 → 头文件：文档里每个偏移都必须在头文件地址集合 $headerSet 里（范围端点已跳过）
        $seen = @{}
        foreach ($t in $docTokens) {
            if ($seen.ContainsKey($t.Token)) { continue }
            $seen[$t.Token] = $true
            if ($headerSet.ContainsKey($t.Token)) { continue }
            if ($docOnlyAllowed -contains $t.Token) { continue }
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
}

# ================= 2) CSV 列数 =================
$csv = $config.csv
$csvSourceRel = [string]$csv.source
$csvSourcePath = Join-Path $repoRoot $csvSourceRel
$csvCols = -1
if (-not (Test-Path -LiteralPath $csvSourcePath -PathType Leaf)) {
    Add-Finding 'error' $csvSourceRel 0 'CSV 头文件不存在'
} else {
    # 只认 csv_header() 定义体 return 之后到分号之间的字符串字面量：注释 / 其他语句不参与
    $csvCols = Get-CsvHeaderColumns $csvSourcePath
    if ($csvCols -lt 0) {
        Add-Finding 'error' $csvSourceRel 0 'csv_header() 解析失败（找不到 return 表达式里的列名字符串）'
    } else {
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
                # 口径写法：「N 列 CSV」「N 列，」「N 列）」「N 列；」等
                foreach ($cm in [regex]::Matches($ln, '(?:(\d+)\s*列\s*CSV|(\d+)\s*列\s*[，,；;）)])')) {
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

    # 只解析「表头含 label / 类别」的表格块：普通表格里的 | N | `xxx` | 行（如缺陷表）不算类别表
    $docMap = @{}
    $docLines2 = Read-Utf8Lines $docPath2
    $i = 0
    $inFence = $false
    while ($i -lt $docLines2.Length) {
        $ln = $docLines2[$i]
        if ($ln -match '^\s*(`{3,}|~{3,})') { $inFence = -not $inFence; $i++; continue }
        if ($inFence) { $i++; continue }
        if ($ln -notmatch '^\s*\|') { $i++; continue }
        $blockStart = $i
        while (($i -lt $docLines2.Length) -and ($docLines2[$i] -match '^\s*\|')) { $i++ }
        $header = $docLines2[$blockStart]
        if ($header -notmatch 'label|类别') { continue }   # 非类别表的表格块整块跳过
        $headerCells = @($header.Trim().Trim([char[]]'|') -split '\|')
        $idIdx = 0
        $labelIdx = -1
        for ($c = 0; $c -lt $headerCells.Count; $c++) {
            $h = $headerCells[$c].Trim()
            if ($h -match '^(id|编号)$') { $idIdx = $c }
            if (($labelIdx -lt 0) -and ($h -match 'label|类别')) { $labelIdx = $c }
        }
        if ($labelIdx -lt 0) { continue }
        for ($r = 1; $r -lt ($i - $blockStart); $r++) {
            $cells = @($docLines2[$blockStart + $r].Trim().Trim([char[]]'|') -split '\|')
            if ($cells.Count -le $labelIdx) { continue }
            if (($cells.Count -le $idIdx) -or ($cells[$idIdx].Trim() -notmatch '^\d+$')) { continue }
            $id = [int]$cells[$idIdx].Trim()
            $lab = $cells[$labelIdx].Trim()
            # 标签支持可选反引号：`gear_ok` 与 gear_ok 都认
            if (($lab.Length -ge 2) -and $lab.StartsWith('`') -and $lab.EndsWith('`')) { $lab = $lab.Substring(1, $lab.Length - 2).Trim() }
            $lineNo = $blockStart + $r + 1
            if ($docMap.ContainsKey($id)) {
                Add-Finding 'error' $docRel2 $lineNo "类别表 id $id 重复"
            } else {
                $docMap[$id] = $lab
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
    foreach ($x in @(ConvertTo-ExceptionList $lbl.allowedExtraDocIds)) { $allowedExtra += [int]$x.Value }
    foreach ($id in $docMap.Keys) {
        if ($id -ge $labels.Count) {
            if (-not ($allowedExtra -contains $id)) {
                Add-Finding 'error' $docRel2 0 "文档多出类别 id $id（$($docMap[$id])）：label_list.json 只有 $($labels.Count) 类，且未登记为待加类别"
            }
        }
    }
    if ($Verbose) { Write-Host "  类别：label_list.json $($labels.Count) 类 / 文档类别表 $($docMap.Count) 行" -ForegroundColor DarkGray }
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

    # 已入库 + 未跟踪未忽略：新建还没 git add 的部署件也要抓（否则 out/ 里多一个文件查不出来）
    $tracked = @()
    try { $tracked = @(& git -C $repoRoot ls-files -- 'scripts/deploy/out' 2>$null) } catch { $tracked = @() }
    $untracked = @()
    try { $untracked = @(& git -C $repoRoot ls-files --others --exclude-standard -- 'scripts/deploy/out' 2>$null) } catch { $untracked = @() }
    $outFiles = @{}
    foreach ($rel in (@($tracked) + @($untracked))) {
        $r = ([string]$rel).Trim().Replace('\', '/')
        if ($r -eq '') { continue }
        $outFiles[$r] = $true
    }
    if ($outFiles.Count -eq 0) {
        Add-Finding 'warning' $outDirRel 0 'git ls-files 查不到 out/ 文件（已入库 / 未跟踪都为空），跳过「未登记」检查'
    } else {
        $allow = @{}
        foreach ($a in @(ConvertTo-ExceptionList $dep.outAllowlist)) { $allow[[string]$a.Value] = [string]$a.Reason }
        foreach ($r in @($outFiles.Keys | Sort-Object)) {
            $script:stats.OutFiles++
            if ($manifestSources.ContainsKey($r)) { continue }
            if ($allow.ContainsKey($r)) { continue }
            $leaf = Split-Path $r -Leaf
            if ($allow.ContainsKey($leaf)) { continue }
            Add-Finding 'error' $r 0 "out/ 文件未登记：既不在 manifest.tsv，也不在 source_check.json 的 outAllowlist（要么进产品清单，要么登记为手动/暂存件）"
        }
    }
    if ($Verbose) { Write-Host "  部署：manifest $($manifestSources.Count) 条源 / out 文件 $($script:stats.OutFiles) 个（已入库 $($tracked.Count) + 未跟踪 $($untracked.Count)）" -ForegroundColor DarkGray }
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
