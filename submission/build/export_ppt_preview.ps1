# 用 PowerPoint COM 把答辩 PPT 导出 PDF 与逐页 PNG 预览（现场兜底件 + 排版自查）。
# 用法：pwsh -File submission/build/export_ppt_preview.ps1
# 产物：submission/out/龙瞳分拣_答辩.pdf、submission/out/ppt_preview/slide_XX.png
# 注意：与 make_ppt.py 同属 PPT 生成链；本脚本只读 pptx、只写 out/。
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$pptx = Join-Path $repoRoot 'submission\out\龙瞳分拣_答辩.pptx'
$pdf  = Join-Path $repoRoot 'submission\out\龙瞳分拣_答辩.pdf'
$prev = Join-Path $repoRoot 'submission\out\ppt_preview'

if (-not (Test-Path -LiteralPath $pptx)) {
    throw "找不到 $pptx，先跑：python submission/build/make_ppt.py"
}
New-Item -ItemType Directory -Force -Path $prev | Out-Null

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

    $prop = $pres.BuiltInDocumentProperties
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
