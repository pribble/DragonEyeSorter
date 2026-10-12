#!/bin/bash
# 环境变量加载器 - 从项目根目录的 .env 文件加载配置
# 用法: source scripts/env_loader.sh

# 找到项目根目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ENV_FILE="$PROJECT_ROOT/.env"

if [ ! -f "$ENV_FILE" ]; then
    echo "❌ 错误: 找不到 .env 文件"
    echo "请从 .env.example 复制并填入你的本机路径："
    echo "  cp .env.example .env"
    echo "  然后编辑 .env 文件"
    return 1 2>/dev/null || exit 1
fi

# 加载 .env 文件（忽略注释和空行；剥掉可能的 UTF-8 BOM 与 CRLF）
set -a
source <(grep -v '^#' "$ENV_FILE" | grep -v '^[[:space:]]*$' | sed -e 's/^\xEF\xBB\xBF//' -e 's/\r$//')
set +a

echo "✅ 环境配置已加载: $ENV_FILE"

# 验证关键路径是否存在
check_path() {
    local var_name=$1
    local path=${!var_name}
    if [ -n "$path" ]; then
        # 处理 Windows 路径格式
        if [[ "$path" =~ ^[A-Za-z]:[/\\] ]]; then
            # 转换为 WSL 路径
            local drive=$(echo "${path:0:1}" | tr '[:upper:]' '[:lower:]')
            local rest=$(echo "${path:2}" | tr '\\' '/')
            path="/mnt/$drive$rest"
        fi

        if [ ! -e "$path" ] && [ ! -d "$path" ]; then
            echo "⚠️  警告: $var_name 指向的路径不存在: ${!var_name}"
        fi
    fi
}

# 检查关键路径
check_path "FD_ROOT"
check_path "NPU_DEMO"
check_path "SDK_LIBS"

# 导出常用的派生变量
export CROSS_COMPILE="${FD_ROOT}/toolchain/gcc/aarch64_linux/bin/aarch64-linux-gnu-"
export BOOTGEN="${FD_ROOT}/tools/bootgen/albg1.exe"

echo "📌 关键路径:"
echo "   FD_ROOT    = $FD_ROOT"
echo "   NPU_DEMO   = $NPU_DEMO"
echo "   SDK_LIBS   = $SDK_LIBS"
