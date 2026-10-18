<#
.SYNOPSIS
    用本机 Microsoft Word（COM 自动化）把作品报告 docx 导出为 PDF。

.DESCRIPTION
    提交件是 PDF，而 Word 是无头服务器上唯一可靠的 docx 排版 / 分页引擎（本机没有
    LibreOffice / pandoc）。脚本对源 docx 只读打开，全程隐藏窗口，导出后不改动源文件。

    导出前先更新域，这一步不能省：
      ① $doc.Fields.Update()          —— 页脚 PAGE / NUMPAGES
      ② 目录域的 Update()             —— TOC 条目与页码
    否则 PDF 里的目录页码会停在「未更新」的占位文本上。Word 打开文档时也会按
    settings.xml 的 w:updateFields 自动更新一次，但导出是脚本驱动的，仍显式调一遍。

    再用 ComputeStatistics(2)（wdStatisticPages）报页数，并检查文件头是不是 %PDF-。
    输入输出一律解析成绝对路径：Word COM 不认相对路径（它按自己的进程目录解析，
    往往直接报"命令失败"）。

    注意：本脚本不写主机绝对路径，输入输出默认位置全部由 $PSScriptRoot 推导。

.PARAMETER InputPath
    源 docx，缺省 = ../out/龙瞳分拣_作品报告.docx（相对本脚本位置）。

.PARAMETER OutputPath
    目标 PDF，缺省 = ../out/龙瞳分拣_作品报告.pdf。

.PARAMETER Visible
    调试用：让 Word 窗口可见（默认全程隐藏）。

.PARAMETER Stats
    可选：把结果（页数 / 字节 / 域更新情况）写成 JSON，给 build.ps1 汇总用。

.EXAMPLE
    pwsh -File submission/build/word_to_pdf.ps1

.EXAMPLE
    pwsh -File submission/build/word_to_pdf.ps1 -InputPath x.docx -OutputPath y.pdf
#>
[CmdletBinding()]
param(
    [string] $InputPath,
    [string] $OutputPath,
    [string] $Stats,
    [switch] $Visible
)

$ErrorActionPreference = 'Stop'

# Word 常量
$wdFormatPDF         = 17   # wdExportFormatPDF
$wdStatisticPages    = 2    # wdStatisticPages
$wdAlertsNone        = 0    # wdAlertsNone
$wdDoNotSaveChanges  = 0    # wdDoNotSaveChanges
$wdExportOptimizePrint = 1  # wdExportOptimizeForPrint
$wdExportAllDocument = 0    # wdExportAllDocument
$wdExportDocumentWithMarkup = 7
$wdExportCreateHeadingBookmarks = 1
$wdExportCreateNoBookmarks = 0

$OutDir = Join-Path (Split-Path -Parent $PSScriptRoot) 'out'   # submission/out/
if (-not $InputPath)  { $InputPath  = Join-Path $OutDir '龙瞳分拣_作品报告.docx' }
if (-not $OutputPath) { $OutputPath = Join-Path $OutDir '龙瞳分拣_作品报告.pdf' }

# Word COM 不认相对路径：先解析成绝对路径（不要求文件已存在）
$InputPath  = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($InputPath)
$OutputPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)

if (-not (Test-Path -LiteralPath $InputPath -PathType Leaf)) {
    throw "找不到源 docx：$InputPath —— 先跑 build.ps1（或 md2docx.py）生成它"
}
if ([System.IO.Path]::GetExtension($InputPath) -ine '.docx') {
    throw "只支持 .docx 输入：$InputPath"
}
$outDirPath = Split-Path -Parent $OutputPath
if (-not (Test-Path -LiteralPath $outDirPath)) {
    New-Item -ItemType Directory -Path $outDirPath -Force | Out-Null
}

$pidBefore = @(Get-Process WINWORD -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id)
$ownPids = @()
$word = $null
$docs = $null
$doc = $null
$fieldsUpdated = $false
$tocUpdated = $false
$tocCount = 0
$pages = $null

try {
    try {
        $word = New-Object -ComObject Word.Application
    } catch {
        throw "本机没有可用的 Microsoft Word（Word.Application COM 组件），无法导出 PDF：$($_.Exception.Message)"
    }
    $word.Visible = [bool]$Visible
    try { $word.DisplayAlerts = $wdAlertsNone } catch { }
    try { $word.Options.UpdateFieldsAtPrint = $true } catch { }
    $docs = $word.Documents
    $ownPids = @(Get-Process WINWORD -ErrorAction SilentlyContinue |
                 Where-Object { $_.Id -notin $pidBefore } |
                 Select-Object -ExpandProperty Id)

    Write-Host ("[word_to_pdf] 打开  : {0}" -f $InputPath)
    # Open(FileName, ConfirmConversions, ReadOnly, AddToRecentFiles)
    $doc = $docs.Open($InputPath, $false, $true, $false)

    # ① 全部域：页脚 PAGE / NUMPAGES 等
    try {
        [void]$doc.Fields.Update()
        $fieldsUpdated = $true
    } catch {
        Write-Warning "Fields.Update() 失败（PDF 里页码可能是旧值）：$($_.Exception.Message)"
    }

    # ② 目录：逐个 TOC 域单独更新（Fields.Update() 对 TOC 的条目重建不总是生效）
    try {
        foreach ($toc in @($doc.TablesOfContents)) {
            $tocCount++
            [void]$toc.Update()
            $tocUpdated = $true
        }
        if ($tocCount -eq 0) {
            Write-Warning "文档里没有 TOC 域 —— 目录页只有占位文本，请检查 md2docx.py 的目录段"
        } else {
            Write-Host ("[word_to_pdf] 目录域: {0} 个，已更新" -f $tocCount)
        }
    } catch {
        Write-Warning "目录 Update() 失败：$($_.Exception.Message)"
    }

    # ③ 页数（分页统计）
    try { $pages = [int]$doc.ComputeStatistics($wdStatisticPages) } catch {
        Write-Warning "ComputeStatistics(wdStatisticPages) 失败：$($_.Exception.Message)"
    }

    # ④ 导出 PDF
    Write-Host ("[word_to_pdf] 导出  : {0}" -f $OutputPath)
    try {
        $doc.ExportAsFixedFormat(
            $OutputPath,
            $wdFormatPDF,
            $false,                          # OpenAfterExport
            $wdExportOptimizePrint,          # OptimizeFor
            $wdExportAllDocument,            # Range
            1, 1,                            # From / To
            $wdExportAllDocument,            # Item
            $true,                           # IncludeDocProps
            $true,                           # KeepIRM
            $wdExportCreateHeadingBookmarks, # CreateBookmarks（标题进 PDF 书签）
            $true,                           # DocStructureTags（无障碍结构）
            $true,                           # BitmapMissingFonts
            $true)                           # UseISO19005_1（PDF/A 兼容输出）
    } catch {
        Write-Warning "ExportAsFixedFormat 失败，回退 SaveAs2：$($_.Exception.Message)"
        $doc.SaveAs2($OutputPath, $wdFormatPDF)
    }
} finally {
    if ($doc) {
        try { $doc.Close($wdDoNotSaveChanges) } catch { }   # 只读打开，绝不回写源文件
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($doc)
    }
    if ($word) {
        if ($docs) { [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($docs) }
        try { $word.Quit($wdDoNotSaveChanges) } catch { }
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($word)
        [GC]::Collect(); [GC]::WaitForPendingFinalizers()
        # Word 退出慢，最多等 20 秒；只盯本次启动的进程，不动用户已开的 Word
        $alive = @(Get-Process WINWORD -ErrorAction SilentlyContinue |
                   Where-Object { $_.Id -in $ownPids }).Count
        for ($i = 0; $i -lt 40 -and $alive -gt 0; $i++) {
            Start-Sleep -Milliseconds 500
            $alive = @(Get-Process WINWORD -ErrorAction SilentlyContinue |
                       Where-Object { $_.Id -in $ownPids }).Count
        }
        if ($ownPids.Count -gt 0 -and $alive -gt 0) {
            Write-Warning "Word 进程未自行退出：$($ownPids -join ', ')"
        }
    }
}

if (-not (Test-Path -LiteralPath $OutputPath -PathType Leaf)) {
    throw "导出失败：没有生成 $OutputPath"
}
$fi = Get-Item -LiteralPath $OutputPath
$head = [System.Text.Encoding]::ASCII.GetString(
    [System.IO.File]::ReadAllBytes($OutputPath), 0, 5)
if ($head -cne '%PDF-') {
    throw "输出文件头不是 %PDF-（实际 '$head'）：$OutputPath"
}

$result = [pscustomobject]@{
    Docx         = $InputPath
    Pdf          = $fi.FullName
    Pages        = $pages
    Bytes        = $fi.Length
    MB           = [math]::Round($fi.Length / 1MB, 2)
    ValidPdf     = $true
    FieldsUpdate = $fieldsUpdated
    TocsUpdated  = $tocCount
}
$result | Format-List

if ($Stats) {
    $Stats = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Stats)
    $statsDir = Split-Path -Parent $Stats
    if ($statsDir -and -not (Test-Path -LiteralPath $statsDir)) {
        New-Item -ItemType Directory -Path $statsDir -Force | Out-Null
    }
    $result | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $Stats -Encoding UTF8
}
exit 0
