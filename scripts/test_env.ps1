# 环境配置测试脚本 - 验证所有必要路径和工具是否正确配置

param(
    [switch]$Verbose
)

Write-Host "`n🔍 DragonEyeSorter 环境配置测试`n" -ForegroundColor Cyan

# 加载环境
if (-not (Test-Path "$PSScriptRoot\..\env")) {
    . "$PSScriptRoot\env_loader.ps1"
}

$tests = @()
$passed = 0
$failed = 0
$warnings = 0

function Test-Item {
    param(
        [string]$Name,
        [string]$Path,
        [string]$Type = "Directory",  # Directory, File, Executable
        [string]$Level = "Critical"   # Critical, Important, Optional
    )

    $result = @{
        Name = $Name
        Path = $Path
        Type = $Type
        Level = $Level
        Status = "Unknown"
        Message = ""
    }

    if ([string]::IsNullOrEmpty($Path)) {
        $result.Status = "NotSet"
        $result.Message = "未设置"
    } elseif ($Type -eq "Directory") {
        if (Test-Path $Path -PathType Container) {
            $result.Status = "Pass"
            $result.Message = "✓ 存在"
        } else {
            $result.Status = "Fail"
            $result.Message = "✗ 目录不存在"
        }
    } elseif ($Type -eq "File" -or $Type -eq "Executable") {
        if (Test-Path $Path -PathType Leaf) {
            $result.Status = "Pass"
            $result.Message = "✓ 存在"
        } else {
            $result.Status = "Fail"
            $result.Message = "✗ 文件不存在"
        }
    }

    return $result
}

# ===== 工具链测试 =====
Write-Host "📦 工具链" -ForegroundColor Yellow

$tests += Test-Item "FD 安装目录" $env:FD_ROOT "Directory" "Critical"
$tests += Test-Item "FD BootGen" "$env:FD_ROOT\tools\bootgen\albg1.exe" "Executable" "Critical"
$tests += Test-Item "FD 交叉编译器" "$env:FD_ROOT\toolchain\gcc\aarch64_linux\bin\aarch64-linux-gnu-gcc.exe" "Executable" "Critical"
$tests += Test-Item "TD 安装目录" $env:TD_ROOT "Directory" "Important"
$tests += Test-Item "TD 主程序" "$env:TD_ROOT\bin\td.exe" "Executable" "Important"

# 检查 TD 版本
if ($env:TD_ROOT -match '6\.2\.1') {
    $tdVersion = "✓ 版本 6.2.1 (正确)"
    $tests += @{Name="TD 版本"; Path=$env:TD_ROOT; Type="Version"; Level="Critical"; Status="Pass"; Message=$tdVersion}
} elseif ($env:TD_ROOT -match '2026\.1') {
    $tdVersion = "⚠ 版本 2026.1 (需要 6.2.1)"
    $tests += @{Name="TD 版本"; Path=$env:TD_ROOT; Type="Version"; Level="Critical"; Status="Warning"; Message=$tdVersion}
}

# ===== 厂家资源测试 =====
Write-Host "`n📚 厂家资源" -ForegroundColor Yellow

$tests += Test-Item "NPU Demo 目录" $env:NPU_DEMO "Directory" "Critical"
$tests += Test-Item "NPU Demo BOOT.bin" "$env:NPU_DEMO\boot\BOOT.bin" "File" "Critical"
$tests += Test-Item "NPU Demo uInitrd" "$env:NPU_DEMO\boot\uInitrd.lz4" "File" "Important"
$tests += Test-Item "NPU Demo SDK" "$env:SDK_LIBS" "Directory" "Important"

# ===== PS 开发依赖测试 =====
Write-Host "`n🛠️ PS 开发依赖" -ForegroundColor Yellow

$tests += Test-Item "SDK armnn_lib" "$env:SDK_LIBS\armnn_lib" "Directory" "Important"
$tests += Test-Item "SDK OpenCV" "$env:SDK_LIBS\ffmpeg_opencv4.7.0_aarch64" "Directory" "Important"
$tests += Test-Item "FreeType 头文件" $env:FREETYPE_INC "Directory" "Optional"
$tests += Test-Item "板上库文件" $env:BOARD_LIBS "Directory" "Optional"

# ===== 显示结果 =====
Write-Host "`n📊 测试结果`n" -ForegroundColor Cyan

foreach ($test in $tests) {
    $icon = switch ($test.Status) {
        "Pass" { "✓"; $script:passed++ }
        "Fail" { "✗"; $script:failed++ }
        "Warning" { "⚠"; $script:warnings++ }
        "NotSet" { "?"; $script:warnings++ }
        default { "-" }
    }

    $color = switch ($test.Status) {
        "Pass" { "Green" }
        "Fail" { if ($test.Level -eq "Critical") { "Red" } else { "Yellow" } }
        "Warning" { "Yellow" }
        "NotSet" { "Gray" }
        default { "White" }
    }

    $levelTag = switch ($test.Level) {
        "Critical" { "[必需]" }
        "Important" { "[重要]" }
        "Optional" { "[可选]" }
        default { "" }
    }

    Write-Host "  $icon " -NoNewline -ForegroundColor $color
    Write-Host "$($test.Name) " -NoNewline
    Write-Host "$levelTag" -NoNewline -ForegroundColor Gray
    Write-Host " - $($test.Message)" -ForegroundColor $color

    if ($Verbose -and $test.Path) {
        Write-Host "     路径: $($test.Path)" -ForegroundColor Gray
    }
}

# ===== 总结 =====
Write-Host "`n" + ("=" * 60) -ForegroundColor Gray
Write-Host "总计: " -NoNewline
Write-Host "$passed 通过  " -NoNewline -ForegroundColor Green
Write-Host "$warnings 警告  " -NoNewline -ForegroundColor Yellow
Write-Host "$failed 失败" -ForegroundColor Red

$criticalFailed = ($tests | Where-Object { $_.Level -eq "Critical" -and $_.Status -eq "Fail" }).Count

if ($criticalFailed -eq 0) {
    Write-Host "`n✅ 核心环境配置正确，可以开始开发！`n" -ForegroundColor Green
    exit 0
} else {
    Write-Host "`n❌ 有 $criticalFailed 个必需项配置错误，请修复后再试`n" -ForegroundColor Red
    Write-Host "建议操作:" -ForegroundColor Yellow
    Write-Host "  1. 检查 .env 文件中的路径是否正确"
    Write-Host "  2. 确保所有工具已正确安装"
    Write-Host "  3. 运行 .\setup_env.ps1 重新配置"
    Write-Host ""
    exit 1
}
