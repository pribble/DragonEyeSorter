# check_docs.ps1 — 文档机械一致性检查（DragonEyeSorter P0）
#
# 把「文档互相对不上 / 索引漏登记 / 正文混入主机路径 / 行内命令路径过期 / 口径过期」变成一条命令 + 退出码，
# 等价于 DSH 仓库的 `pnpm run doc-sync`（只保留本项目用得上的检查）。
#
# 检查项：链接（内联 / 图片 / 引用式 [text][ref] / 图片嵌链接外层）与锚点（GitHub slug：
# 重复标题生成 -1/-2 后缀、连续空格逐个转 -）、索引完整性（docs/ 递归 + log/ + 各目录 README
# 必须挂进文档索引）、路径卫生、行数预算、结构卫生、行内代码与围栏代码块里的仓内路径存在性
# （默认只查 scripts/，规则在 doc_check.json 的 inlinePaths）、关键数字「同段」来源、版本口径。
#
# 用法：
#   pwsh -File scripts/check/check_docs.ps1              # 有 error 时退出 1
#   pwsh -File scripts/check/check_docs.ps1 -WarnOnly    # 只报告、总是退出 0（排查用）
#   pwsh -File scripts/check/check_docs.ps1 -Verbose     # 额外打印通过项统计
#
# 规则与例外全部在同目录 doc_check.json：改规则先改配置，例外必须写 reason。
# 只读：不会修改任何文件。

param(
    [switch]$WarnOnly,
    [switch]$Verbose
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$configPath = Join-Path $PSScriptRoot 'doc_check.json'
if (-not (Test-Path -LiteralPath $configPath)) {
    Write-Host "缺少配置: $configPath" -ForegroundColor Red
    exit 2
}
$config = Get-Content -Raw -LiteralPath $configPath -Encoding UTF8 | ConvertFrom-Json

$script:errors = New-Object System.Collections.ArrayList
$script:warnings = New-Object System.Collections.ArrayList
$script:stats = @{ Files = 0; Links = 0; Images = 0; Anchors = 0; LinksSkipped = 0; InlinePaths = 0; InlinePathsSkipped = 0 }
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

# 例外必须写 reason：配置里任何例外条目缺 reason 都算 error（防止加了例外忘写理由）
function Test-ExceptionReasons {
    param($Entries, [string]$Where)
    $i = 0
    foreach ($e in @($Entries)) {
        $i++
        if ($null -eq $e) { continue }
        if ([string]::IsNullOrWhiteSpace([string]$e.reason)) {
            Add-Finding 'error' 'scripts/check/doc_check.json' 0 "例外缺少 reason：$Where 第 $i 条（$($e | ConvertTo-Json -Compress -Depth 3)）"
        }
    }
}
Test-ExceptionReasons $config.linkExceptions 'linkExceptions'
Test-ExceptionReasons $config.pathHygiene.exceptions 'pathHygiene.exceptions'
if ($null -ne $config.inlinePaths) {
    Test-ExceptionReasons $config.inlinePaths.allowExact 'inlinePaths.allowExact'
    Test-ExceptionReasons $config.inlinePaths.allowGlobs 'inlinePaths.allowGlobs'
}

function Get-RepoRel {
    param([string]$Path)
    $norm = $Path.Replace('\', '/')
    $root = $script:repoRoot.Replace('\', '/').TrimEnd('/')
    if ($norm.Equals($root, [System.StringComparison]::OrdinalIgnoreCase)) { return '' }
    if ($norm.StartsWith($root + '/', [System.StringComparison]::OrdinalIgnoreCase)) {
        return $norm.Substring($root.Length + 1)
    }
    # 不在仓库内：回退为规范化绝对路径（不能截断成看似相对的垃圾路径）
    return $norm
}

# 完整路径是否严格位于仓库内：按路径分隔符边界判断，避免 …/DragonEyeSorter-v16/ 这类同前缀兄弟目录误判
function Test-UnderRepo {
    param([string]$FullPath)
    foreach ($sep in @([string][System.IO.Path]::DirectorySeparatorChar, '/', '\')) {
        if ($FullPath.StartsWith($script:repoRoot + $sep, [System.StringComparison]::OrdinalIgnoreCase)) { return $true }
    }
    return $false
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

# 行内路径例外：allowExact 精确登记（忽略大小写、忽略末尾 /）或 allowGlobs（glob）命中即跳过
function Test-InlinePathAllowed {
    param([string]$Token)
    $cfg = $config.inlinePaths
    if ($null -eq $cfg) { return $false }
    $tok = $Token.Replace('\', '/').TrimEnd('/')
    foreach ($e in @($cfg.allowExact)) {
        if ($null -eq $e) { continue }
        $p = ([string]$e.path).Replace('\', '/').TrimEnd('/')
        if (($p -ne '') -and $p.Equals($tok, [System.StringComparison]::OrdinalIgnoreCase)) { return $true }
    }
    foreach ($e in @($cfg.allowGlobs)) {
        if ($null -eq $e) { continue }
        $g = [string]$e.glob
        if (([string]::IsNullOrWhiteSpace($g) -eq $false) -and (Test-Glob $tok $g)) { return $true }
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

# 标题 slug（近似 GitHub）：小写、去标点、每个空格变一个 -、保留 CJK 字母 / 数字 / - _
# 连续多个空格 → 连续多个 -（GitHub 语义，不再 \s+ 合并）；重复标题的 -1/-2 后缀在解析处处理
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
    return ($sb.ToString() -replace '\s', '-')
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
# .git 与 .zcode（ZCode CLI 本地工作目录，工具产物）都不是仓库文档，不入扫描
$mdFiles = @(Get-ChildItem -LiteralPath $repoRoot -Recurse -File -Filter *.md |
    Where-Object { $_.FullName -notmatch '[\\/]\.(git|zcode)[\\/]' } | Sort-Object FullName)
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
    $refDefs = @{}
    $indentedCode = [bool[]]::new($lines.Length)
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
            if ($ln -match '^(?: {4,}|\t)') { $indentedCode[$i] = $true }
            $hm = [regex]::Match($ln, '^(#{1,6})\s+(.+?)\s*#*\s*$')
            if ($hm.Success) {
                $level = $hm.Groups[1].Value.Length
                $text = $hm.Groups[2].Value
                if (($lastLevel -gt 0) -and ($level -gt ($lastLevel + 1))) {
                    Add-Finding 'error' $rel ($i + 1) "标题跳级：H$lastLevel → H$level"
                }
                $lastLevel = $level
                $slug = Get-GithubSlug $text
                if ($slug -ne '') {
                    if ($slugs.ContainsKey($slug)) {
                        Add-Finding 'warning' $rel ($i + 1) "重复标题（slug 相同）：$text"
                        # GitHub 对重复标题依次生成 -1 / -2 … 后缀锚点
                        $n = 1
                        while ($slugs.ContainsKey("$slug-$n")) { $n++ }
                        $slug = "$slug-$n"
                    }
                    $slugs[$slug] = $true
                }
            }
            if (-not $indentedCode[$i]) {
                # 引用式链接定义：[ref]: target（缩进 ≤3，定义行本身不是链接）
                $rd = [regex]::Match($ln, '^\s{0,3}\[([^\]]+)\]:\s*(\S+)')
                if ($rd.Success) {
                    $label = ($rd.Groups[1].Value.Trim().ToLowerInvariant() -replace '\s+', ' ')
                    if (-not $refDefs.ContainsKey($label)) { $refDefs[$label] = $rd.Groups[2].Value }
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

    # 行内代码 span / 围栏代码块里的仓内路径（默认 scripts/）：只查这类「命令写法」，
    # 防止目录迁移后 README 命令块里的旧路径漏检；$TD_ROOT/scripts/… 与 URL 用前置字符排除
    if ($null -ne $config.inlinePaths) {
        if (-not (Test-AnyGlob $rel $config.inlinePaths.excludeGlobs)) {
            $prefixes = @(@($config.inlinePaths.prefixes) | ForEach-Object { ([string]$_).Replace('\', '/').TrimEnd('/') } | Where-Object { $_ -ne '' })
            for ($i = 0; $i -lt $lines.Length; $i++) {
                if ($indentedCode[$i]) { continue }
                $texts = @()
                if ($fenceFlags[$i]) {
                    # 围栏标记行不算内容（内容行按整行扫）
                    if ($lines[$i] -notmatch '^\s*(`{3,}|~{3,})\s*$') { $texts = @($lines[$i]) }
                } else {
                    foreach ($m in [regex]::Matches($lines[$i], '`([^`]+)`')) { $texts += $m.Groups[1].Value }
                }
                foreach ($t in $texts) {
                    foreach ($pre in $prefixes) {
                        $re = '(?<![\w./-])' + [regex]::Escape($pre) + '[\\/][A-Za-z0-9_.\-/\\]*'
                        foreach ($m in [regex]::Matches($t, $re)) {
                            $tok = $m.Value.Replace('\', '/').TrimEnd('.', ',', ';', ':', '!', '?', ')', ']', '}', '，', '。', '；', '：', '、', '！', '？', '）')
                            if ($tok.Length -eq 0) { continue }
                            $script:stats.InlinePaths++
                            if (Test-InlinePathAllowed $tok) { $script:stats.InlinePathsSkipped++; continue }
                            $full = Join-Path $script:repoRoot ($tok.Replace('/', [string][System.IO.Path]::DirectorySeparatorChar))
                            if (-not (Test-Path -LiteralPath $full)) {
                                Add-Finding 'error' $rel ($i + 1) "行内路径不存在：$tok"
                            }
                        }
                    }
                }
            }
        }
    }

    # 段落划分：空行 / 标题行 / 围栏行是段落边界（关键数字按「同段」判断来源，不再只看同一行）
    $paraOf = New-Object 'int[]' $lines.Length
    $paraLines = New-Object System.Collections.ArrayList
    $curPara = -1
    for ($i = 0; $i -lt $lines.Length; $i++) {
        if ($fenceFlags[$i] -or ($lines[$i] -match '^\s*$') -or ($lines[$i] -match '^#{1,6}\s')) {
            $paraOf[$i] = -1
            $curPara = -1
            continue
        }
        if ($curPara -lt 0) {
            $curPara = $paraLines.Count
            [void]$paraLines.Add((New-Object System.Collections.ArrayList))
        }
        $paraOf[$i] = $curPara
        [void]$paraLines[$curPara].Add($i)
    }
    $paraHasSource = New-Object 'bool[]' $paraLines.Count
    for ($p = 0; $p -lt $paraLines.Count; $p++) {
        $pt = (@($paraLines[$p] | ForEach-Object { $clean[$_] }) -join "`n")
        $has = $false
        # 来源必须是 Markdown 链接，且目标含「测试与验证」或 /log/（catalog/ 之类不算）
        foreach ($m in [regex]::Matches($pt, '\]\(([^)]*)\)')) {
            if ($m.Groups[1].Value -match '测试与验证|(?:^|/)log/') { $has = $true; break }
        }
        if (-not $has) {
            foreach ($m in [regex]::Matches($pt, '\]\[([^\]]*)\]')) {
                $label = $m.Groups[1].Value.Trim().ToLowerInvariant() -replace '\s+', ' '
                if (($label -ne '') -and $refDefs.ContainsKey($label) -and ([string]$refDefs[$label] -match '测试与验证|(?:^|/)log/')) { $has = $true; break }
            }
        }
        $paraHasSource[$p] = $has
    }

    # 关键实测数字：出现在「测试与验证」之外、且同段没有唯一源链接 → 告警
    if (-not (Test-AnyGlob $rel $config.keyNumbers.exemptGlobs)) {
        foreach ($pat in @($config.keyNumbers.patterns)) {
            for ($i = 0; $i -lt $lines.Length; $i++) {
                if ($fenceFlags[$i] -or ($paraOf[$i] -lt 0)) { continue }
                if ($lines[$i] -notmatch [string]$pat) { continue }
                if (-not $paraHasSource[$paraOf[$i]]) {
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
        Full         = $f.FullName
        Rel          = $rel
        Lines        = $lines
        Clean        = $clean
        InFence      = $fenceFlags
        IndentedCode = $indentedCode
        Slugs        = $slugs
        Ids          = $ids
        RefDefs      = $refDefs
        LinkTargets  = New-Object System.Collections.ArrayList
    }
    [void]$docs.Add($doc)
    $docInfo[$f.FullName.ToLowerInvariant()] = $doc
}

# ---------- 第二遍：链接 / 图片 / 锚点 ----------
# 引用式链接 [text][ref] → [ref]: 的 target；定义缺失只告警（GitHub 会按普通文字渲染）
function Resolve-RefTarget {
    param($Doc, [int]$LineNo, [string]$Text, [string]$Ref)
    $label = $Ref
    if ([string]::IsNullOrWhiteSpace($label)) { $label = $Text }
    $key = $label.Trim().ToLowerInvariant() -replace '\s+', ' '
    if (($key -ne '') -and $Doc.RefDefs.ContainsKey($key)) { return [string]$Doc.RefDefs[$key] }
    Add-Finding 'warning' $Doc.Rel $LineNo "引用式链接缺少定义：[$($label.Trim())]"
    return $null
}

# 校验一条链接目标（内联 / 引用式 / 图片 / 图片嵌链接外层共用）：例外 → 解析 → 存在性 → 锚点
function Resolve-LinkTarget {
    param($Doc, [int]$LineNo, [string]$Orig, [bool]$IsImage)
    $target = $Orig.Trim()
    if ($target.Length -eq 0) { return }
    if ($target -match '^(?:https?|mailto|tel|ftp|data):' -or $target.StartsWith('//')) { return }
    if ($IsImage) { $script:stats.Images++ } else { $script:stats.Links++ }

    foreach ($e in @($config.linkExceptions)) {
        if ($null -eq $e) { continue }
        if ((Test-Glob $Doc.Rel ([string]$e.file)) -and ($target -match [string]$e.target)) {
            $script:stats.LinksSkipped++
            return
        }
    }

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
        $resolved = $Doc.Full
        $targetRel = $Doc.Rel
    } else {
        try {
            if ($targetPath.StartsWith('/')) { $resolved = Join-Path $script:repoRoot $targetPath.TrimStart('/') }
            else { $resolved = Join-Path (Split-Path -Parent $Doc.Full) $targetPath }
            $resolved = [System.IO.Path]::GetFullPath($resolved)
        } catch {
            Add-Finding 'error' $Doc.Rel $LineNo "链接目标非法：$orig"
            return
        }
        if (Test-UnderRepo $resolved) { $targetRel = Get-RepoRel $resolved }
    }
    if ($null -ne $targetRel) { [void]$Doc.LinkTargets.Add($targetRel.ToLowerInvariant()) }

    $exists = Test-Path -LiteralPath $resolved
    if ($IsImage -or ($targetPath -match '\.(?:png|jpe?g|gif|svg|webp)$')) {
        if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
            Add-Finding 'error' $Doc.Rel $LineNo "图片不存在：$orig"
            return
        }
    } elseif (-not $exists) {
        Add-Finding 'error' $Doc.Rel $LineNo "链接目标不存在：$orig"
        return
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
                Add-Finding 'error' $Doc.Rel $LineNo "锚点不存在：#$frag → $targetRel"
            }
        }
    }
}

foreach ($doc in $docs) {
    for ($i = 0; $i -lt $doc.Lines.Length; $i++) {
        if ($null -eq $doc.Clean[$i]) { continue }
        # 围栏 / 4 空格缩进代码块里的 [x](y) 是示例，不是链接；行内代码已在 Clean 里剥掉
        if ($doc.InFence[$i] -or $doc.IndentedCode[$i]) { continue }
        $line = $doc.Clean[$i]

        # 图片先取：剥掉内层图片后，[![img](a)](b) 的外层目标才不会被内层 ]( 截断
        if ($line.IndexOf('![') -ge 0) {
            foreach ($m in [regex]::Matches($line, '(?<!\\)!\[([^\]]*)\]\(([^)]*)\)')) {
                Resolve-LinkTarget $doc ($i + 1) $m.Groups[2].Value $true
            }
            $line = [regex]::Replace($line, '(?<!\\)!\[([^\]]*)\]\(([^)]*)\)', '')
        }
        # 内联链接；反斜杠转义的 \[x](y) 不是链接
        if ($line.IndexOf('](') -ge 0) {
            foreach ($m in [regex]::Matches($line, '(?<!\\)\[([^\]]*)\]\(([^)]*)\)')) {
                Resolve-LinkTarget $doc ($i + 1) $m.Groups[2].Value $false
            }
            $line = [regex]::Replace($line, '(?<!\\)\[([^\]]*)\]\(([^)]*)\)', '')
        }
        # 引用式图片 / 链接：[text][ref]，target 来自文末 [ref]: target
        if ($line.IndexOf('][') -ge 0) {
            foreach ($m in [regex]::Matches($line, '(?<!\\)!\[([^\]]*)\]\[([^\]]*)\]')) {
                $t = Resolve-RefTarget $doc ($i + 1) $m.Groups[1].Value $m.Groups[2].Value
                if ($null -ne $t) { Resolve-LinkTarget $doc ($i + 1) $t $true }
            }
            $line = [regex]::Replace($line, '(?<!\\)!\[([^\]]*)\]\[([^\]]*)\]', '')
            foreach ($m in [regex]::Matches($line, '(?<!\\)\[([^\]]*)\]\[([^\]]*)\]')) {
                $t = Resolve-RefTarget $doc ($i + 1) $m.Groups[1].Value $m.Groups[2].Value
                if ($null -ne $t) { Resolve-LinkTarget $doc ($i + 1) $t $false }
            }
        }
    }
}

# ---------- 索引完整性 ----------
# docs/ 递归（log/ 仍走 log 索引）、log/ 平铺：每个文档都必须被对应索引链接
function Test-IndexCompleteness {
    param([string]$IndexRel, [string]$DirRel, [string]$Label, [switch]$Recurse, [string[]]$SkipGlobs = @())
    $indexFull = Join-Path $script:repoRoot $IndexRel
    $indexKey = $indexFull.ToLowerInvariant()
    if (-not $docInfo.ContainsKey($indexKey)) {
        Add-Finding 'error' $IndexRel 0 '索引文件不存在'
        return
    }
    $seen = @{}
    foreach ($t in $docInfo[$indexKey].LinkTargets) { $seen[[string]$t] = $true }
    $indexRelNorm = (Get-RepoRel $indexFull).ToLowerInvariant()
    $dirFull = Join-Path $script:repoRoot $DirRel
    $entries = @(Get-ChildItem -LiteralPath $dirFull -File -Filter *.md -Recurse:$Recurse)
    foreach ($e in $entries) {
        $erel = (Get-RepoRel $e.FullName).ToLowerInvariant()
        if ($erel -eq $indexRelNorm) { continue }
        if (Test-AnyGlob $erel $SkipGlobs) { continue }
        if (-not $seen.ContainsKey($erel)) {
            Add-Finding 'error' $IndexRel 0 "未索引：$erel（$Label 的每个文档都要在索引里登记）"
        }
    }
}
Test-IndexCompleteness ([string]$config.index.docsRoot.index) ([string]$config.index.docsRoot.dir) 'docs/' -Recurse -SkipGlobs @('docs/log/**')
Test-IndexCompleteness ([string]$config.index.log.index) ([string]$config.index.log.dir) 'log/'

# requiredTopDirs 的 README 不只要求实存，还必须挂进文档索引（docs/README.md）里
$docsIndexKey = (Join-Path $script:repoRoot ([string]$config.index.docsRoot.index)).ToLowerInvariant()
$docsIndexSeen = @{}
if ($docInfo.ContainsKey($docsIndexKey)) {
    foreach ($t in $docInfo[$docsIndexKey].LinkTargets) { $docsIndexSeen[[string]$t] = $true }
}
foreach ($d in @($config.index.requiredTopDirs)) {
    if ([string]::IsNullOrWhiteSpace([string]$d)) { continue }
    $dirName = ([string]$d).Replace('\', '/').Trim('/')
    $rp = Join-Path $script:repoRoot ((Join-Path $dirName 'README.md'))
    if (-not (Test-Path -LiteralPath $rp -PathType Leaf)) {
        Add-Finding 'error' (($dirName) + '/README.md') 0 '缺少目录 README'
        continue
    }
    if ($dirName -eq 'docs') { continue }   # docs/README.md 即索引自身
    $relReadme = $dirName + '/README.md'
    if (-not $docsIndexSeen.ContainsKey($relReadme.ToLowerInvariant())) {
        Add-Finding 'error' ([string]$config.index.docsRoot.index) 0 "目录 README 未挂进文档索引：$relReadme"
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
Write-Host ("文档检查：{0} 篇 md / {1} 条链接（图 {2}，锚点 {3}）/ 行内代码路径 {4} 条" -f $script:stats.Files, $script:stats.Links, $script:stats.Images, $script:stats.Anchors, $script:stats.InlinePaths) -ForegroundColor Cyan
if ($Verbose) {
    Write-Host ("  跳过例外链接 {0} 条；行内路径例外 {1} 条" -f $script:stats.LinksSkipped, $script:stats.InlinePathsSkipped) -ForegroundColor DarkGray
}

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
