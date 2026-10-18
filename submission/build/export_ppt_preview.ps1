# 用 PowerPoint COM 把答辩 PPT 导出 PDF 与逐页 PNG 预览（现场兜底件 + 排版自查）。
#
# 用法：pwsh -File submission/build/export_ppt_preview.ps1
#
# 产物（都在 submission/out/，已 gitignore）：
#   * 龙瞳分拣_答辩.pdf          现场兜底件；插图按源图原生分辨率回填，PDF 内 ≥300dpi
#   * ppt_preview/slide_XX.png   1600x900 逐页预览（自查 / 互审）
#   * _ppt_pdf_hires.json        每张插图的 dpi 前后对比 + 逐页版面度量（原始证据）
#
# 三步：
#   1) PowerPoint COM：pptx → pdf（SaveAs 32）+ 逐页 PNG（1600x900）
#   2) PDF 插图高清化 + 清晰度体检：调 submission/build/ppt_pdf_hires.py
#      （PowerPoint 导 PDF 会把插图降采样到 200dpi：实测源图 3200px / 7.42in 只剩 1483px；
#        PPTX 本身嵌的是全分辨率，问题只在 PDF 兜底件 —— 该脚本按
#        submission/out/_ppt_images.json 的「页号 + 放置矩形」逐张回填源图像素，门禁 ≥300dpi）
#   3) 逐页版面度量：同一脚本从预览 PNG 量墨迹 bbox（顶档 / 头档 / 底档 / 填充率），
#      与 make_ppt.py 的目标（顶档≤1.20in、头档≤0.60in、底档≤1.20in、填充率 65%~85%）对照
#
# 与同事工具的关系：报告链的 submission/build/pdf_images_hires.py 支持 --order 模式
#   （按文档顺序一一对应）。PPT 链的插图顺序里夹着图标 / 项目符号，所以本脚本用
#   ppt_pdf_hires.py 的 --manifest 模式对位；若 pdf_images_hires.py 以后支持 --map，
#   本脚本会自动改用它做替换、只留 ppt_pdf_hires.py 做体检。
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$pptx     = Join-Path $repoRoot 'submission\out\龙瞳分拣_答辩.pptx'
$pdf      = Join-Path $repoRoot 'submission\out\龙瞳分拣_答辩.pdf'
$prev     = Join-Path $repoRoot 'submission\out\ppt_preview'
$manifest = Join-Path $repoRoot 'submission\out\_ppt_images.json'
$hiresPy  = Join-Path $repoRoot 'submission\build\ppt_pdf_hires.py'
$sharedPy = Join-Path $repoRoot 'submission\build\pdf_images_hires.py'
$evidence = Join-Path $repoRoot 'submission\out\_ppt_pdf_hires.json'

if (-not (Test-Path -LiteralPath $pptx)) {
    throw "找不到 $pptx，先跑：python submission/build/make_ppt.py"
}
if (-not (Test-Path -LiteralPath $hiresPy)) {
    throw "找不到 $hiresPy（PPT 链的 PDF 高清化脚本）"
}
if (-not (Test-Path -LiteralPath $manifest)) {
    throw "找不到插图清单 $manifest，先跑：python submission/build/make_ppt.py"
}
New-Item -ItemType Directory -Force -Path $prev | Out-Null

# 中文输出：让 Python 子进程与控制台都用 UTF-8
$env:PYTHONIOENCODING = 'utf-8'
try { [Console]::OutputEncoding = New-Object Text.UTF8Encoding($false) } catch { }

# --------------------------------------------------------------------------- #
# 1) PowerPoint COM：pptx → pdf + 逐页 PNG
# --------------------------------------------------------------------------- #
$app = $null
$pres = $null
try {
    $app = New-Object -ComObject PowerPoint.Application
    $pres = $app.Presentations.Open($pptx, $true, $false, $false)   # ReadOnly, Untitled, WithWindow=false

    # 32 = ppSaveAsPDF
    $pres.SaveAs($pdf, 32)
    Write-Host "PDF  : $pdf"

    $n = $pres.Slides.Count
    for ($i = 1; $i -le $n; $i++) {
        $out = Join-Path $prev ("slide_{0:D2}.png" -f $i)
        # 1600x900 对应 16:9
        $pres.Slides.Item($i).Export($out, 'PNG', 1600, 900)
    }
    Write-Host "PNG  : $n 张 -> $prev"
    Write-Host "页数 : $n"
    Write-Host "幻灯片尺寸: $($pres.PageSetup.SlideWidth) x $($pres.PageSetup.SlideHeight) pt"
}
finally {
    if ($pres) { $pres.Close() | Out-Null }
    if ($app)  { $app.Quit() | Out-Null }
    if ($pres) { [void][Runtime.InteropServices.Marshal]::ReleaseComObject($pres) }
    if ($app)  { [void][Runtime.InteropServices.Marshal]::ReleaseComObject($app) }
    [GC]::Collect()
}

# --------------------------------------------------------------------------- #
# 2) PDF 插图高清化（优先同事的共用工具，需支持 --map）+ 3) 清晰度 / 版面体检
# --------------------------------------------------------------------------- #
$sharedDidReplace = $false
if (Test-Path -LiteralPath $sharedPy) {
    $helpText = (& python $sharedPy --help 2>&1 | Out-String)
    if ($helpText -match '--map') {
        Write-Host "PDF  : 用共用工具 pdf_images_hires.py --map 替换插图（同事维护）"
        & python $sharedPy --pdf $pdf --map $manifest --min-dpi 300 --inplace
        if ($LASTEXITCODE -eq 0) {
            $sharedDidReplace = $true
        } else {
            Write-Warning "pdf_images_hires.py 退出码 $LASTEXITCODE，回退到 ppt_pdf_hires.py"
        }
    } else {
        Write-Host "PDF  : pdf_images_hires.py 暂无 --map 模式（PPT 链插图里夹着图标），用 ppt_pdf_hires.py"
    }
}

if ($sharedDidReplace) {
    & python $hiresPy --pdf $pdf --manifest $manifest --report --preview $prev --json $evidence
} else {
    & python $hiresPy --pdf $pdf --manifest $manifest --inplace --min-dpi 300 `
        --preview $prev --json $evidence
}
$postExit = $LASTEXITCODE

if ($postExit -ne 0) { throw "PPT 导出后处理失败（退出码 $postExit）" }
Write-Host "完成：PDF $pdf"
