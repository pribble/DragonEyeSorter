<#
.SYNOPSIS
    作品报告一键生成：submission/figures/make_figures.py → submission/技术文档.md → submission/out/龙瞳分拣_作品报告.docx → 同名 PDF → 插图高清化。

.DESCRIPTION
    四步串起来跑，最后打印产物路径 / 页数 / 大小（含插图等效 dpi）：
      ⓪ make_figures.py       报告插图按脚本重生成（SVG / PNG / fig_meta.json；生成物不入库）
      ① md2docx.py            Markdown → DOCX（论文单节版式：A4 / 页边距 2.4cm / 无封面目录页眉，python-docx）
      ② word_to_pdf.ps1       DOCX → PDF（Word COM，导出前更新页脚页码域）
      ③ pdf_images_hires.py   PDF 里的插图换回源图分辨率（Word 导出会压到 96 dpi）
    任何一步失败即停，退出码非 0。中间产物（_md2docx.json / _word_to_pdf.json）放在
    out/ 下，可随时重建。报告插图生成物不入库（submission/figures/.gitignore），所以每次
    出报告先按当前脚本重画，保证图与脚本一致。

    依赖：本机 Microsoft Word（转 PDF 用）+ python-docx（生成 docx 用）
    + matplotlib（第 ⓪ 步重画插图用）+ PyMuPDF（第 ③ 步换图用，import 名 pymupdf）。Python 解释器按
    `python` → `py -3` 顺序在 PATH 上找，按「该解释器有没有这个模块」分别挑，
    脚本里不写死解释器路径；要指定解释器就直接调对应的 .py。
    缺 PyMuPDF 时第 ③ 步失败并给出安装 / 换解释器提示（这是**故意**的硬失败：
    没有高清化的 PDF 图内文字是糊的，不该当成品交出去）。要临时跳过用 -NoHires。

    脚本内路径一律由 $PSScriptRoot 推导，不写主机绝对路径。

.PARAMETER NoPdf
    只生成 docx，不调 Word，也不做插图高清化（本机没装 Word 时用）。

.PARAMETER NoHires
    出 PDF 但不做插图高清化（调试排版时用；交件前必须跑完整链）。

.EXAMPLE
    pwsh -File submission/build/build.ps1
#>
[CmdletBinding()]
param(
    [switch] $NoPdf,
    [switch] $NoHires
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8   # 中文 + µ / ⚠ 输出

$BuildDir = $PSScriptRoot                                   # submission/build/
$Submission = Split-Path -Parent $BuildDir                  # submission/
$Repo = Split-Path -Parent $Submission                      # 仓库根
$Source = Join-Path $Submission '技术文档.md'
$OutDir = Join-Path $Submission 'out'
$Stem = '龙瞳分拣_作品报告'
$Docx = Join-Path $OutDir "$Stem.docx"
$Pdf = Join-Path $OutDir "$Stem.pdf"
$Stats = Join-Path $OutDir '_md2docx.json'
$MinDpi = 300

if (-not (Test-Path -LiteralPath $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
}

function Get-PythonExe {
    param([string] $Module = 'docx', [string] $Hint = 'python -m pip install python-docx')
    foreach ($cand in @(
            @{ Exe = 'python'; Args = @() },
            @{ Exe = 'py';     Args = @('-3') })) {
        $cmd = Get-Command $cand.Exe -ErrorAction SilentlyContinue
        if ($cmd) {
            try {
                & $cmd.Source @($cand.Args + @('-c', "import $Module")) 2>$null | Out-Null
                if ($LASTEXITCODE -eq 0) {
                    return @{ Exe = $cmd.Source; Args = $cand.Args }
                }
            } catch { }
        }
    }
    throw "找不到带 $Module 的 Python：请先 `$Hint`（PATH 上要有 python 或 py -3）"
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

# ---------------------------------------------------------------- ⓪ 报告插图重生成
# 生成物（SVG / PNG / fig_meta.json）不入库，见 submission/figures/.gitignore：每次出报告
# 先按当前脚本重画，保证 ① 有图可嵌、③ 有源图可换、verify_report.py 第 12 项有 meta 可复核。
Show-Step "0/3  报告插图重生成（make_figures.py）"
$FigDir = Join-Path $Submission 'figures'
$pyFig = Get-PythonExe -Module 'matplotlib' -Hint 'python -m pip install matplotlib'
Write-Host ("解释器：{0}" -f $pyFig.Exe)
& $pyFig.Exe @($pyFig.Args + @((Join-Path $FigDir 'make_figures.py')))
if ($LASTEXITCODE -ne 0) { throw "make_figures.py 失败（退出码 $LASTEXITCODE）" }

# ---------------------------------------------------------------- ① md → docx
Show-Step "1/3  Markdown → DOCX"
if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
    throw "找不到内容源：$Source"
}
Write-Host ("源文件：{0}（{1} KB）" -f (Get-RelPath $Source),
            [math]::Round((Get-Item -LiteralPath $Source).Length / 1KB, 1))
$py = Get-PythonExe -Module 'docx' -Hint 'python -m pip install python-docx'
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
    Write-Host "[skip] -NoPdf：跳过 Word 转 PDF 与插图高清化" -ForegroundColor Yellow
} else {
    Show-Step "2/3  DOCX → PDF（Microsoft Word COM）"
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

    # ------------------------------------------------------- ③ PDF 插图高清化
    # Word 导出会把插图重采样到 96 dpi（实测 3200px 原图只剩 547px）。这一步把每张
    # 插图换回源图分辨率（图框不变），等效 dpi 从 96 提到 ~500。插图顺序取自
    # _md2docx.json 的 figures（= 正文里 ![](...) 的出现顺序），与 PDF 内插图一一对应。
    if ($NoHires) {
        Write-Host ''
        Write-Host "[skip] -NoHires：跳过插图高清化（PDF 里插图仍是 96 dpi）" -ForegroundColor Yellow
    } else {
        Show-Step "3/3  PDF 插图高清化（PyMuPDF，Word 只给 96 dpi）"
        $pyHi = Get-PythonExe -Module 'pymupdf' -Hint 'python -m pip install pymupdf'
        Write-Host ("解释器：{0}" -f $pyHi.Exe)
        $figures = @()
        if (Test-Path -LiteralPath $Stats) {
            $figures = @((Get-Content -LiteralPath $Stats -Raw -Encoding UTF8 |
                          ConvertFrom-Json).figures)
        }
        if ($figures.Count -eq 0) {
            throw "没从 $Stats 读到插图列表（figures）—— 先跑第 ① 步，或检查 md2docx.py"
        }
        # 页眉 logo（官方模板「芯辰大海」）也是 PDF 里的图，但各页共用一个 xref、
        # 不在正文插图清单里 —— pdf_images_hires.py 对它走 --header 原地换源像素
        # （Word 导出把 logo 压到 96 dpi，不换回源像素页眉就是糊的）。
        $logo = Join-Path $Repo 'submission/figures/logo_header.png'
        if (-not (Test-Path -LiteralPath $logo -PathType Leaf)) {
            throw "页眉 logo 不存在：$logo"
        }
        $order = @($figures | ForEach-Object { Join-Path $Repo $_ })
        foreach ($f in $order) {
            if (-not (Test-Path -LiteralPath $f -PathType Leaf)) {
                throw "源图不存在：$f（内容源里的插图路径与仓库里的图对不上）"
            }
        }
        Write-Host ("按出现顺序替换 {0} 张：{1}" -f $order.Count,
                    (($order | ForEach-Object { Split-Path -Leaf $_ }) -join ' '))
        & $pyHi.Exe @($pyHi.Args + @((Join-Path $BuildDir 'pdf_images_hires.py'),
                                     '--pdf', $Pdf, '--order') + $order +
                                     @('--header', $logo, '--inplace', '--min-dpi', "$MinDpi",
                                       '--photo-jpeg', '88'))
        if ($LASTEXITCODE -ne 0) {
            throw "pdf_images_hires.py 失败（退出码 $LASTEXITCODE）：插图等效 dpi 未达 $MinDpi 或数量对不上"
        }
        $hiBytes = (Get-Item -LiteralPath $Pdf).Length
        Write-Host ("PDF 大小：{0:N2} MB → {1:N2} MB（换图后涨的是插图本来的像素）" -f `
            ($pdfBytes / 1MB), ($hiBytes / 1MB))
        $pdfBytes = $hiBytes
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
    Write-Host "       文件头：%PDF- ✓（页脚 PAGE 页码域已更新）"
} else {
    Write-Host 'PDF  : （未生成）'
}
if (Test-Path -LiteralPath $Stats) {
    $s = Get-Content -LiteralPath $Stats -Raw -Encoding UTF8 | ConvertFrom-Json
    Write-Host ("段落 {0} · 表格 {1} · 插图 {2} · 一级标题 {3} · 二级标题 {4} · 节 {5}" -f `
        $s.paragraphs, $s.tables, $s.images, $s.headings.h1, $s.headings.h2, $s.sections)
    if ($s.forbidden_hits -and $s.forbidden_hits.Count -gt 0) {
        Write-Host ("[!] 禁项命中：{0}" -f ($s.forbidden_hits -join ',')) -ForegroundColor Red
        exit 2
    }
}
Write-Host ("耗时：{0:N1} 秒" -f $sw.Elapsed.TotalSeconds)
Write-Host '完成。' -ForegroundColor Green
exit 0
