# check_docs.ps1 — 文档机械一致性检查（DragonEyeSorter P0）
#
# 把「文档互相对不上 / 索引漏登记 / 正文混入主机路径 / 口径过期」变成一条命令 + 退出码，
# 等价于 DSH 仓库的 `pnpm run doc-sync`（只保留本项目用得上的检查）。
#
# 用法：
#   pwsh -File scripts/check_docs.ps1              # 有 error 时退出 1
#   pwsh -File scripts/check_docs.ps1 -WarnOnly    # 只报告、总是退出 0（排查用）
#   pwsh -File scripts/check_docs.ps1 -Verbose     # 额外打印通过项统计
#
# 规则与例外全部在 scripts/doc_check.json：改规则先改配置，例外必须写 reason。
# 只读：不会修改任何文件。

param(
    [switch]$WarnOnly,
    [switch]$Verbose
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$configPath = Join-Path $PSScriptRoot 'doc_check.json'
if (-not (Test-Path -LiteralPath $configPath)) {
    Write-Host "缺少配置: $configPath" -ForegroundColor Red
    exit 2
}
$config = Get-Content -Raw -LiteralPath $configPath -Encoding UTF8 | ConvertFrom-Json

$script:errors = New-Object System.Collections.ArrayList
$script:warnings = New-Object System.Collections.ArrayList
$script:stats = @{ Files = 0; Links = 0; Images = 0; Anchors = 0; LinksSkipped = 0 }
$script:pathRules = @(
    @{ Name = '主机盘符路径'; Regex = '\b[A-Za-z]:[\\/]' },
    @{ Name = 'UNC 路径'; Regex = '\\\\[A-Za-z0-9_.\-]+\\' },
    @{ Name = 'IPv4'; Regex = '\b(?:(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)\.){3}(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)\b' },
    @{ Name = '私钥路径'; Regex = '(?:id_(?:rsa|dsa|ecdsa|ed25519)|\.pem\b|\.ppk\b)' }
)

function Add-Finding {
    param([string]$Level, [string]$File, [int]$Line, [string]$Message)
    $item = [pscustomobject]@{ Level = $Level; File = $File; Line = $Line; Message = $Message }
    if ($Level -eq 'error') { [void]$script:errors.Add($item) } else { [void]$script:warnings.Add($item) }
}

function Get-RepoRel {
    param([string]$Path)
    if ($Path.Length -le $script:repoRoot.Length) { return $Path.Replace('\', '/') }
    return $Path.Substring($script:repoRoot.Length + 1).Replace('\', '/')
}

# glob：按 / 分段匹配（* 不跨目录）；** 结尾表示整个目录
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

function Remove-InlineCode {
    param([string]$Text)
    $prev = $null
    $t = $Text
    while ($prev -ne $t) {
        $prev = $t
        $t = [regex]::Replace($t, '`[^`]*`', '')
    }
    return $t
}

# 标题 slug（近似 GitHub）：小写、去标点、空格换 -、保留 CJK 字母 / 数字 / - _
function Get-GithubSlug {
    param([string]$Text)
    $t = $Text.Trim()
    $t = [regex]::Replace($t, '[`*\[\]()#!.，。：:；;、]', '')
    $t = $t.ToLowerInvariant()
    $sb = New-Object System.Text.StringBuilder
    foreach ($ch in $t.ToCharArray()) {
        if ([char]::IsLetterOrDigit($ch) -or $ch -eq ' ' -or $ch -eq '-' -or $ch -eq '_') {
            [void]$sb.Append($ch)
        }
    }
    return ($sb.ToString() -replace '\s+', '-')
}

function Test-PathHygieneException {
    param([string]$Rel, [string]$Value)
    foreach ($e in @($config.pathHygiene.exceptions)) {
        if ($null -eq $e) { continue }
        if ((Test-Glob $Rel ([string]$e.file)) -and ($Value -match [string]$e.regex)) { return $true }
    }
    return $false
}

function Read-Utf8Lines {
    param([string]$Path)
    return [System.IO.File]::ReadAllLines($Path, [System.Text.Encoding]::UTF8)
}

# ---------- 枚举全部 md ----------
$mdFiles = @(Get-ChildItem -LiteralPath $repoRoot -Recurse -File -Filter *.md |
    Where-Object { $_.FullName -notmatch '\\\.git\\' } | Sort-Object FullName)
$script:stats.Files = $mdFiles.Count

$docs = New-Object System.Collections.ArrayList
$docInfo = @{}
$claimHits = @{}
foreach ($c in @($config.claims)) {
    if ($null -ne $c) { $claimHits[[string]$c.name] = New-Object System.Collections.ArrayList }
}

# ---------- 第一遍：结构 / 路径卫生 / 关键数字 / 口径 ----------
foreach ($f in $mdFiles) {
    $rel = Get-RepoRel $f.FullName
    $lines = Read-Utf8Lines $f.FullName
    $raw = [System.IO.File]::ReadAllText($f.FullName, [System.Text.Encoding]::UTF8)
    $clean = [string[]]::new($lines.Length)
    $fenceFlags = [bool[]]::new($lines.Length)
    $slugs = @{}
    $ids = @{}
    $inFence = $false
    $fenceChar = ''
    $fenceLen = 0
    $lastLevel = 0

    for ($i = 0; $i -lt $lines.Length; $i++) {
        $ln = $lines[$i]
        if (-not $inFence) {
            $open = [regex]::Match($ln, '^\s*(`{3,}|~{3,})')
            if ($open.Success) {
                $inFence = $true
                $fenceChar = $open.Groups[1].Value.Substring(0, 1)
                $fenceLen = $open.Groups[1].Value.Length
                $fenceFlags[$i] = $true
                continue
            }
            $hm = [regex]::Match($ln, '^(#{1,6})\s+(.+?)\s*#*\s*$')
            if ($hm.Success) {
                $level = $hm.Groups[1].Value.Length
                $text = $hm.Groups[2].Value
                if (($lastLevel -gt 0) -and ($level -gt ($lastLevel + 1))) {
                    Add-Finding 'error' $rel ($i + 1) "标题跳级：H$lastLevel → H$level"
                }
                $lastLevel = $level
                $slug = Get-GithubSlug $text
                if ($slugs.ContainsKey($slug)) {
                    Add-Finding 'warning' $rel ($i + 1) "重复标题（slug 相同）：$text"
                } else {
                    $slugs[$slug] = $true
                }
            }
            foreach ($im in [regex]::Matches($ln, '<a\s+(?:id|name)="([^"]+)"')) {
                $ids[$im.Groups[1].Value] = $true
            }
            $clean[$i] = Remove-InlineCode $ln
        } else {
            $fenceFlags[$i] = $true
            $close = [regex]::Match($ln, '^\s*(`{3,}|~{3,})\s*$')
            if ($close.Success -and ($close.Groups[1].Value.Substring(0, 1) -eq $fenceChar) -and ($close.Groups[1].Value.Length -ge $fenceLen)) {
                $inFence = $false
            }
        }
    }
    if ($inFence) { Add-Finding 'error' $rel $lines.Length '代码块未闭合' }

    if ($raw.Length -gt 0) {
        $norm = $raw.Replace("`r`n", "`n")
        if (-not $norm.EndsWith("`n")) {
            Add-Finding 'error' $rel $lines.Length '文件末尾缺少换行'
        } elseif ($norm.EndsWith("`n`n")) {
            Add-Finding 'warning' $rel $lines.Length '文件末尾有多余空行（约定恰好一个换行）'
        }
    }

    # 路径卫生：非 log 文档正文（含代码块）禁止主机盘符 / UNC / IPv4 / 私钥路径
    if (-not (Test-AnyGlob $rel $config.pathHygiene.excludeGlobs)) {
        for ($i = 0; $i -lt $lines.Length; $i++) {
            foreach ($rule in $script:pathRules) {
                foreach ($m in [regex]::Matches($lines[$i], $rule.Regex)) {
                    if (Test-PathHygieneException $rel $m.Value) { continue }
                    Add-Finding 'error' $rel ($i + 1) ("$($rule.Name): " + $m.Value)
                }
            }
        }
    }

    # 关键实测数字：出现在「测试与验证」之外、且同段没有唯一源链接 → 告警
    if (-not (Test-AnyGlob $rel $config.keyNumbers.exemptGlobs)) {
        foreach ($pat in @($config.keyNumbers.patterns)) {
            for ($i = 0; $i -lt $lines.Length; $i++) {
                if ($fenceFlags[$i]) { continue }
                if ($lines[$i] -notmatch [string]$pat) { continue }
                if ($lines[$i] -notmatch '测试与验证|log/') {
                    Add-Finding 'warning' $rel ($i + 1) "关键数字「$pat」出现但同段没有唯一源链接（测试与验证 / log）"
                }
            }
        }
    }

    # 口径 claims
    foreach ($c in @($config.claims)) {
        if ($null -eq $c) { continue }
        if (Test-AnyGlob $rel $c.excludeGlobs) { continue }
        $name = [string]$c.name
        for ($i = 0; $i -lt $lines.Length; $i++) {
            if ($fenceFlags[$i]) { continue }
            $m = [regex]::Match($lines[$i], [string]$c.regex)
            if (-not $m.Success) { continue }
            $val = $null
            for ($g = 1; $g -lt $m.Groups.Count; $g++) {
                if ($m.Groups[$g].Success -and ($m.Groups[$g].Value -ne '')) { $val = $m.Groups[$g].Value; break }
            }
            if ($null -ne $val) {
                [void]$claimHits[$name].Add([pscustomobject]@{ Value = $val; File = $rel; Line = ($i + 1) })
            }
        }
    }

    $doc = [pscustomobject]@{
        Full        = $f.FullName
        Rel         = $rel
        Lines       = $lines
        Clean       = $clean
        InFence     = $fenceFlags
        Slugs       = $slugs
        Ids         = $ids
        LinkTargets = New-Object System.Collections.ArrayList
    }
    [void]$docs.Add($doc)
    $docInfo[$f.FullName.ToLowerInvariant()] = $doc
}

# ---------- 第二遍：链接 / 图片 / 锚点 ----------
foreach ($doc in $docs) {
    $parent = Split-Path -Parent $doc.Full
    for ($i = 0; $i -lt $doc.Lines.Length; $i++) {
        if ($null -eq $doc.Clean[$i]) { continue }
        if ($doc.Clean[$i].IndexOf('](') -lt 0) { continue }
        foreach ($m in [regex]::Matches($doc.Clean[$i], '(!?)\[([^\]]*)\]\(([^)]*)\)')) {
            $isImage = ($m.Groups[1].Value -eq '!')
            $target = $m.Groups[3].Value.Trim()
            if ($target.Length -eq 0) { continue }
            if ($target -match '^(?:https?|mailto|tel|ftp|data):' -or $target.StartsWith('//')) { continue }
            if ($isImage) { $script:stats.Images++ } else { $script:stats.Links++ }

            $skip = $false
            foreach ($e in @($config.linkExceptions)) {
                if ($null -eq $e) { continue }
                if ((Test-Glob $doc.Rel ([string]$e.file)) -and ($target -match [string]$e.target)) { $skip = $true; break }
            }
            if ($skip) { $script:stats.LinksSkipped++; continue }

            $orig = $target
            if ($target.StartsWith('<')) {
                $gt = $target.IndexOf('>')
                if ($gt -gt 0) { $target = $target.Substring(1, $gt - 1) }
            } else {
                $sm = [regex]::Match($target, '^(\S+)\s')
                if ($sm.Success) { $target = $sm.Groups[1].Value }
            }

            $frag = ''
            $hash = $target.IndexOf('#')
            if ($hash -ge 0) {
                $frag = $target.Substring($hash + 1)
                $target = $target.Substring(0, $hash)
            }
            $q = $target.IndexOf('?')
            if ($q -ge 0) { $target = $target.Substring(0, $q) }

            $targetPath = $target
            try { $targetPath = [System.Uri]::UnescapeDataString($target) } catch { }

            $resolved = $null
            $targetRel = $null
            if ($targetPath -eq '') {
                $resolved = $doc.Full
                $targetRel = $doc.Rel
            } else {
                try {
                    if ($targetPath.StartsWith('/')) { $resolved = Join-Path $script:repoRoot $targetPath.TrimStart('/') }
                    else { $resolved = Join-Path $parent $targetPath }
                    $resolved = [System.IO.Path]::GetFullPath($resolved)
                } catch {
                    Add-Finding 'error' $doc.Rel ($i + 1) "链接目标非法：$orig"
                    continue
                }
                if ($resolved.StartsWith($script:repoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
                    $targetRel = $resolved.Substring($script:repoRoot.Length + 1).Replace('\', '/')
                }
            }
            if ($null -ne $targetRel) { [void]$doc.LinkTargets.Add($targetRel.ToLowerInvariant()) }

            $exists = Test-Path -LiteralPath $resolved
            if ($isImage -or ($targetPath -match '\.(?:png|jpe?g|gif|svg|webp)$')) {
                if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
                    Add-Finding 'error' $doc.Rel ($i + 1) "图片不存在：$orig"
                    continue
                }
            } elseif (-not $exists) {
                Add-Finding 'error' $doc.Rel ($i + 1) "链接目标不存在：$orig"
                continue
            }

            if (($frag -ne '') -and $targetRel -and ($targetRel -match '\.md$')) {
                $script:stats.Anchors++
                $targetFull = $resolved.ToLowerInvariant()
                if ($docInfo.ContainsKey($targetFull)) {
                    $targetDoc = $docInfo[$targetFull]
                    $fragDecoded = $frag
                    try { $fragDecoded = [System.Uri]::UnescapeDataString($frag) } catch { }
                    $fragSlug = $fragDecoded.ToLowerInvariant()
                    if (-not ($targetDoc.Slugs.ContainsKey($fragSlug) -or $targetDoc.Ids.ContainsKey($fragDecoded) -or $targetDoc.Ids.ContainsKey($fragSlug))) {
                        Add-Finding 'error' $doc.Rel ($i + 1) "锚点不存在：#$frag → $targetRel"
                    }
                }
            }
        }
    }
}

# ---------- 索引完整性 ----------
function Test-IndexCompleteness {
    param([string]$IndexRel, [string]$DirRel, [string]$Label)
    $indexFull = Join-Path $script:repoRoot $IndexRel
    $indexKey = $indexFull.ToLowerInvariant()
    if (-not $docInfo.ContainsKey($indexKey)) {
        Add-Finding 'error' $IndexRel 0 '索引文件不存在'
        return
    }
    $seen = @{}
    foreach ($t in $docInfo[$indexKey].LinkTargets) { $seen[[string]$t] = $true }
    $dirFull = Join-Path $script:repoRoot $DirRel
    foreach ($e in @(Get-ChildItem -LiteralPath $dirFull -File -Filter *.md | Where-Object { $_.Name -ne 'README.md' })) {
        $erel = (Get-RepoRel $e.FullName).ToLowerInvariant()
        if (-not $seen.ContainsKey($erel)) {
            Add-Finding 'error' $IndexRel 0 "未索引：$($e.Name)（$Label 的每个文档都要在索引里登记）"
        }
    }
}
Test-IndexCompleteness ([string]$config.index.docsRoot.index) ([string]$config.index.docsRoot.dir) 'docs/ 根目录'
Test-IndexCompleteness ([string]$config.index.log.index) ([string]$config.index.log.dir) 'log/'

foreach ($d in @($config.index.requiredTopDirs)) {
    if ([string]::IsNullOrWhiteSpace([string]$d)) { continue }
    $rp = Join-Path $script:repoRoot ((Join-Path ([string]$d) 'README.md'))
    if (-not (Test-Path -LiteralPath $rp -PathType Leaf)) {
        Add-Finding 'error' (([string]$d) + '/README.md') 0 '缺少目录 README'
    }
}

# ---------- 行数预算 / AGENTS 红线 ----------
foreach ($doc in $docs) {
    foreach ($b in @($config.lineLimits)) {
        if ($null -eq $b) { continue }
        if (-not (Test-Glob $doc.Rel ([string]$b.pattern))) { continue }
        $n = $doc.Lines.Length
        $lim = [int]$b.limit
        if ($n -gt $lim) { Add-Finding 'error' $doc.Rel $n "超出预算：$n 行 > $lim 行" }
        break
    }
}

$agentsDoc = $null
foreach ($d in $docs) { if ($d.Rel -eq 'AGENTS.md') { $agentsDoc = $d; break } }
if ($null -ne $agentsDoc) {
    $start = -1
    for ($i = 0; $i -lt $agentsDoc.Lines.Length; $i++) {
        if ($agentsDoc.Lines[$i] -match '^##\s*红线') { $start = $i; break }
    }
    if ($start -lt 0) {
        Add-Finding 'warning' 'AGENTS.md' 0 '未找到「## 红线」小节'
    } else {
        $end = $agentsDoc.Lines.Length
        for ($i = $start + 1; $i -lt $agentsDoc.Lines.Length; $i++) {
            if ($agentsDoc.Lines[$i] -match '^##\s') { $end = $i; break }
        }
        $cnt = 0
        for ($i = $start + 1; $i -lt $end; $i++) {
            if ($agentsDoc.Lines[$i] -match '^\s*\d+\.\s+\*\*') { $cnt++ }
        }
        $max = [int]$config.redLineMax
        if ($cnt -gt $max) { Add-Finding 'error' 'AGENTS.md' ($end + 1) "红线条目 $cnt 条 > 上限 $max 条" }
        elseif ($Verbose) { Write-Host "  AGENTS.md 红线 $cnt 条（上限 $max）" -ForegroundColor DarkGray }
    }
}

# ---------- 口径一致性 ----------
foreach ($c in @($config.claims)) {
    if ($null -eq $c) { continue }
    $name = [string]$c.name
    $hits = $claimHits[$name]
    if ($null -eq $hits -or $hits.Count -eq 0) {
        Add-Finding 'warning' '<全仓>' 0 "未找到口径：$name"
        continue
    }
    $vals = @($hits | ForEach-Object { [string]$_.Value } | Sort-Object -Unique)
    if ($vals.Count -gt 1) {
        $detail = ($hits | ForEach-Object { "$($_.Value)@$($_.File):$($_.Line)" }) -join '，'
        Add-Finding 'error' '<全仓>' 0 "口径不一致（$name）：$($vals -join ' / ')；出现处：$detail"
    }
    if ($c.PSObject.Properties.Name -contains 'expect') {
        $exp = [string]$c.expect
        if ($vals -notcontains $exp) {
            Add-Finding 'error' '<全仓>' 0 "口径不是当前值（$name）：期望 $exp，实际 $($vals -join ' / ')"
        }
    }
}

# ---------- 输出 ----------
Write-Host ''
Write-Host ("文档检查：{0} 篇 md / {1} 条链接（图 {2}，锚点 {3}）" -f $script:stats.Files, $script:stats.Links, $script:stats.Images, $script:stats.Anchors) -ForegroundColor Cyan
if ($Verbose) { Write-Host ("  跳过例外链接 {0} 条" -f $script:stats.LinksSkipped) -ForegroundColor DarkGray }

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
