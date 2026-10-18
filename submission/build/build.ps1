<#
.SYNOPSIS
    作品报告一键生成：docs/技术文档.md → submission/out/龙瞳分拣_作品报告.docx → 同名 PDF。

.DESCRIPTION
    两步串起来跑，最后打印产物路径 / 页数 / 大小，并检查 PDF 文件头与目录页数：
      ① md2docx.py     Markdown → DOCX（封面 / 目录 / 正文排版，python-docx）
      ② word_to_pdf.ps1  DOCX → PDF（Word COM，导出前更新页脚与目录域）
    任何一步失败即停，退出码非 0。中间产物（_md2docx.json）放在 out/ 下，可随时重建。

    依赖：本机 Microsoft Word（转 PDF 用）+ python-docx（生成 docx 用）。
    Python 解释器按 `python` → `py -3` 顺序在 PATH 上找，脚本里不写死解释器路径；
    要指定解释器就直接调：python submission/build/md2docx.py。

    脚本内路径一律由 $PSScriptRoot 推导，不写主机绝对路径。

.PARAMETER NoPdf
    只生成 docx，不调 Word（本机没装 Word 时用）。

.EXAMPLE
    pwsh -File submission/build/build.ps1
#>
[CmdletBinding()]
param(
    [switch] $NoPdf
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8   # 中文 + µ / ⚠ 输出

$BuildDir = $PSScriptRoot                                   # submission/build/
$Submission = Split-Path -Parent $BuildDir                  # submission/
$Repo = Split-Path -Parent $Submission                      # 仓库根
$DocsDir = Join-Path $Repo 'docs'
$Source = Join-Path $DocsDir '技术文档.md'
$OutDir = Join-Path $Submission 'out'
$Stem = '龙瞳分拣_作品报告'
$Docx = Join-Path $OutDir "$Stem.docx"
$Pdf = Join-Path $OutDir "$Stem.pdf"
$Stats = Join-Path $OutDir '_md2docx.json'

if (-not (Test-Path -LiteralPath $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
}

function Get-PythonExe {
    foreach ($cand in @(
            @{ Exe = 'python'; Args = @() },
            @{ Exe = 'py';     Args = @('-3') })) {
        $cmd = Get-Command $cand.Exe -ErrorAction SilentlyContinue
        if ($cmd) {
            try {
                & $cmd.Source @($cand.Args + @('-c', 'import docx')) 2>$null | Out-Null
                if ($LASTEXITCODE -eq 0) {
                    return @{ Exe = $cmd.Source; Args = $cand.Args }
                }
            } catch { }
        }
    }
    throw "找不到带 python-docx 的 Python：请先 `python -m pip install python-docx`（PATH 上要有 python 或 py -3）"
}

function Get-RelPath([string] $Path) {
    $full = [System.IO.Path]::GetFullPath($Path)
    $root = [System.IO.Path]::GetFullPath($Repo)
    if ($full.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $full.Substring($root.Length).TrimStart('\', '/')
    }
    return $full
}

function Show-Step([string] $Name) {
    Write-Host ''
    Write-Host ("=== {0} ===" -f $Name) -ForegroundColor Cyan
}

$sw = [System.Diagnostics.Stopwatch]::StartNew()

# ---------------------------------------------------------------- ① md → docx
Show-Step "1/2  Markdown → DOCX"
if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
    throw "找不到内容源：$Source"
}
Write-Host ("源文件：{0}（{1} KB）" -f (Get-RelPath $Source),
            [math]::Round((Get-Item -LiteralPath $Source).Length / 1KB, 1))
$py = Get-PythonExe
Write-Host ("解释器：{0}" -f $py.Exe)
& $py.Exe @($py.Args + @((Join-Path $BuildDir 'md2docx.py'),
                         '-Source', $Source, '-Out', $Docx, '-Stats', $Stats))
if ($LASTEXITCODE -ne 0) { throw "md2docx.py 失败（退出码 $LASTEXITCODE）" }
if (-not (Test-Path -LiteralPath $Docx -PathType Leaf)) { throw "没有生成 $Docx" }

# ---------------------------------------------------------------- ② docx → pdf
$pages = $null
$pdfBytes = $null
if ($NoPdf) {
    Write-Host ''
    Write-Host "[skip] -NoPdf：跳过 Word 转 PDF" -ForegroundColor Yellow
} else {
    Show-Step "2/2  DOCX → PDF（Microsoft Word COM）"
    if (-not (Get-Command pwsh -ErrorAction SilentlyContinue)) {
        throw "找不到 pwsh，无法调用 word_to_pdf.ps1"
    }
    $pdfStats = Join-Path $OutDir '_word_to_pdf.json'
    & pwsh -NoProfile -File (Join-Path $BuildDir 'word_to_pdf.ps1') `
        -InputPath $Docx -OutputPath $Pdf -Stats $pdfStats
    if ($LASTEXITCODE -ne 0) { throw "word_to_pdf.ps1 失败（退出码 $LASTEXITCODE）" }
    $pdfInfo = Get-Item -LiteralPath $Pdf
    $pdfBytes = $pdfInfo.Length
    $head = [System.Text.Encoding]::ASCII.GetString(
        [System.IO.File]::ReadAllBytes($Pdf), 0, 5)
    if ($head -cne '%PDF-') { throw "PDF 文件头异常：'$head'" }
    if (Test-Path -LiteralPath $pdfStats) {
        $pages = (Get-Content -LiteralPath $pdfStats -Raw -Encoding UTF8 |
                  ConvertFrom-Json).Pages
    }
}

$sw.Stop()

# ---------------------------------------------------------------- 汇总
Write-Host ''
Write-Host '=== 产物 ===' -ForegroundColor Cyan
$docxInfo = Get-Item -LiteralPath $Docx
Write-Host ("DOCX : {0}" -f (Get-RelPath $docxInfo.FullName))
Write-Host ("       {0:N0} 字节（{1:N2} MB）" -f $docxInfo.Length, ($docxInfo.Length / 1MB))
if ($pdfBytes) {
    Write-Host ("PDF  : {0}" -f (Get-RelPath $Pdf))
    Write-Host ("       {0:N0} 字节（{1:N2} MB）" -f $pdfBytes, ($pdfBytes / 1MB))
    Write-Host ("       页数：{0} 页（Word ComputeStatistics(wdStatisticPages)）" -f $pages)
    Write-Host ("       文件头：%PDF- ✓  目录域更新：{0} 个" -f (
        (Get-Content -LiteralPath (Join-Path $OutDir '_word_to_pdf.json') -Raw `
            -Encoding UTF8 | ConvertFrom-Json).TocsUpdated))
} else {
    Write-Host 'PDF  : （未生成）'
}
if (Test-Path -LiteralPath $Stats) {
    $s = Get-Content -LiteralPath $Stats -Raw -Encoding UTF8 | ConvertFrom-Json
    Write-Host ("段落 {0} · 表格 {1} · 插图 {2} · 章 {3} · 节 {4} · 页眉页脚节 {5}" -f `
        $s.paragraphs, $s.tables, $s.images, $s.headings.h1, $s.headings.h2, $s.sections)
    if ($s.forbidden_hits -and $s.forbidden_hits.Count -gt 0) {
        Write-Host ("[!] 禁项命中：{0}" -f ($s.forbidden_hits -join ',')) -ForegroundColor Red
        exit 2
    }
}
Write-Host ("耗时：{0:N1} 秒" -f $sw.Elapsed.TotalSeconds)
Write-Host '完成。' -ForegroundColor Green
exit 0
