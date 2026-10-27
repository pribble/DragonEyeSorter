# 环境配置测试脚本 - 验证所有必要路径和工具是否正确配置

param(
    [switch]$Verbose
)

Write-Host "`n🔍 DragonEyeSorter 环境配置测试`n" -ForegroundColor Cyan

# 加载环境（env_loader 自己会定位仓库根的 .env；已加载过就不重复）
if (-not $DES_ENV_LOADED) {
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

# 打包 BOOT.bin 的零件：都在仓库内（scripts/boot/parts/），不依赖仓库外的厂家 demo 目录
$parts = "$PSScriptRoot\..\boot\parts"
$tests += Test-Item "BOOT.bin 零件目录" $parts "Directory" "Critical"
$tests += Test-Item "FSBL 零件" "$parts\fsbl.elf" "File" "Critical"
$tests += Test-Item "U-Boot 零件" "$parts\u-boot.elf" "File" "Critical"
$tests += Test-Item "零件台账" "$parts\MD5SUMS.txt" "File" "Important"

# u-boot.elf 的 MD5 是硬约束：它进 BOOT.bin 第 3 分区，静默损坏要到开机失败才发现
if (Test-Path "$parts\u-boot.elf") {
    $ubHash = (Get-FileHash "$parts\u-boot.elf" -Algorithm MD5).Hash
    $ubOk   = $ubHash -eq 'E844648EC910C87EA8A036AD4D3D3E45'
    $tests += @{
        Name    = "U-Boot 零件的 MD5"
        Path    = "$parts\u-boot.elf"
        Type    = "Hash"
        Level   = "Critical"
        Status  = if ($ubOk) { "Pass" } else { "Fail" }
        Message = if ($ubOk) { $ubHash } else { "$ubHash ≠ E844648E…（零件被改/损坏；删掉后用 -VendorDemo 重建）" }
    }
}

# vendor.bit 与「原样重打包验证」已下线（2026-10-15）：BOOT.bin 的第 2 分区一律用我们自己的
# 比特流，-Bit 是必填项；因此这里不再检查任何默认比特流，缺失时 make_bootbin.ps1 会直接报错。

# ===== PS 开发依赖测试 =====
Write-Host "`n🛠️ PS 开发依赖" -ForegroundColor Yellow

# 两个第三方库各自一个变量、各自一个根（名字与板端 /opt 下同名目录一致）
$tests += Test-Item "ArmNN 库目录（ARMNN_LIB）" $env:ARMNN_LIB "Directory" "Important"
$tests += Test-Item "OpenCV+FFmpeg 库目录（OPENCV_LIB）" $env:OPENCV_LIB "Directory" "Important"
# 库文件本身也查：目录在而 .so 软链接没修（sdk_fix_sonames.sh 没跑）时，链接期才会报错
$tests += Test-Item "ArmNN 的链接名 libarmnn.so" "$env:ARMNN_LIB\lib\libarmnn.so" "File" "Important"
$tests += Test-Item "OpenCV 的链接名 libopencv_core.so" "$env:OPENCV_LIB\lib\libopencv_core.so" "File" "Important"
$tests += Test-Item "FreeType 头文件（编 des_app 必需）" $env:FREETYPE_INC "Directory" "Important"
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
Write-Host ("`n" + ("=" * 60)) -ForegroundColor Gray
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
    Write-Host "  3. 重新复制 .env.example 为 .env 并填入路径后重跑本脚本"
    Write-Host ""
    exit 1
}
