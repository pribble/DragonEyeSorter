# 环境变量加载器 (PowerShell 版本) - 从项目根目录的 .env 文件加载配置
# 用法: . .\scripts\env_loader.ps1

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$EnvFile = Join-Path $ProjectRoot ".env"

if (-not (Test-Path $EnvFile)) {
    Write-Host "❌ 错误: 找不到 .env 文件" -ForegroundColor Red
    Write-Host "请从 .env.example 复制并填入你的本机路径："
    Write-Host "  copy .env.example .env"
    Write-Host "  然后编辑 .env 文件"
    return
}

Write-Host "✅ 正在加载环境配置: $EnvFile" -ForegroundColor Green

# 读取并解析 .env 文件
Get-Content $EnvFile | ForEach-Object {
    $line = $_.Trim()
    # 跳过注释和空行
    if ($line -match '^#' -or $line -eq '') {
        return
    }

    # 解析 KEY=VALUE
    if ($line -match '^([^=]+)=(.*)$') {
        $key = $matches[1].Trim()
        $value = $matches[2].Trim()

        # 处理环境变量引用 (如 $HOME)
        $value = $ExecutionContext.InvokeCommand.ExpandString($value)

        # 设置环境变量
        Set-Item -Path "env:$key" -Value $value

        # 同时设置为脚本变量，方便使用
        Set-Variable -Name $key -Value $value -Scope Global
    }
}

Write-Host "`n📌 关键路径:" -ForegroundColor Cyan

# 验证并显示关键路径
$criticalPaths = @(
    @{Name="FD_ROOT"; Path=$env:FD_ROOT},
    @{Name="TD_ROOT"; Path=$env:TD_ROOT},
    @{Name="NPU_DEMO"; Path=$env:NPU_DEMO},
    @{Name="SDK_LIBS"; Path=$env:SDK_LIBS}
)

foreach ($item in $criticalPaths) {
    $name = $item.Name
    $path = $item.Path

    if ([string]::IsNullOrEmpty($path)) {
        Write-Host "   $name = (未设置)" -ForegroundColor Yellow
    } elseif (Test-Path $path) {
        Write-Host "   $name = $path" -ForegroundColor Green
    } else {
        Write-Host "   $name = $path ⚠️ 路径不存在" -ForegroundColor Red
    }
}

Write-Host "`n✨ 环境变量已加载到当前会话" -ForegroundColor Green
