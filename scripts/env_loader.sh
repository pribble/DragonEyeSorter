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

# 加载 .env 文件：逐行只认 KEY=值（剥 BOM / CRLF / 注释 / 空行，值剥成对引号），export 进环境。
# 不再 source 原文件：值里带空格不拆词，$、反引号等元字符不展开（与 deploy.sh / generate_env_make.sh 同一语义）。
bad=0
while IFS= read -r line || [ -n "$line" ]; do
    line=${line#$'\xEF\xBB\xBF'}
    line=${line%$'\r'}
    line=${line#"${line%%[![:space:]]*}"}   # 前导空白剥掉再判注释（缩进注释与旧加载器一样静默跳过）
    case $line in ''|\#*) continue ;; esac
    key=${line%%=*}
    val=${line#*=}
    if [ "$key" = "$line" ] || ! [[ $key =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]]; then
        echo "⚠️  跳过不合 KEY=值 格式的行: $line"
        bad=1
        continue
    fi
    val=${val#"${val%%[![:space:]]*}"}; val=${val%"${val##*[![:space:]]}"}
    case $val in
        \"*\") val=${val#\"}; val=${val%\"} ;;
        \'*)   val=${val#\'}; val=${val%\'} ;;
    esac
    # 文档承诺的 $HOME 跨用户兼容：只展开 shell 里**已有**变量名的 $NAME / ${NAME}（单遍、不执行任何东西；
    # 找不到的名字原样保留）。$() 、反引号不会被执行——这与旧的 source 行为的区别就在这。
    out=""
    while [[ $val =~ \$\{?([A-Za-z_][A-Za-z0-9_]*)\}? ]]; do
        out+=${val%%"${BASH_REMATCH[0]}"*}
        name=${BASH_REMATCH[1]}
        if declare -p "$name" > /dev/null 2>&1; then out+=${!name}; else out+=$BASH_REMATCH; fi
        val=${val#*"${BASH_REMATCH[0]}"}
    done
    val=$out$val
    export "$key=$val"
done < "$ENV_FILE"
[ $bad -eq 0 ] || echo "（有行被跳过：改成 KEY=值，值含空格也不用加引号）"

echo "✅ 环境配置已加载: $ENV_FILE"

# WSL 下把 Windows 盘符路径换成 /mnt/<盘>/...（否则下面派生的 CROSS_COMPILE / BOOTGEN 在 WSL 里不可用）
if grep -qi microsoft /proc/sys/kernel/osrelease 2>/dev/null; then
    to_wsl() {
        local p=$1
        if [[ $p =~ ^[A-Za-z]:[/\\] ]]; then
            local drive
            drive=$(printf %s "${p:0:1}" | tr '[:upper:]' '[:lower:]')
            p="/mnt/$drive$(printf %s "${p:2}" | tr '\\' '/')"
        fi
        printf %s "$p"
    }
    for v in FD_ROOT ARMNN_LIB OPENCV_LIB FREETYPE_INC BOARD_LIBS; do
        if [ -n "${!v:-}" ]; then
            declare -g "$v=$(to_wsl "${!v}")"
        fi
    done
fi

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
check_path "ARMNN_LIB"
check_path "OPENCV_LIB"

# 导出常用的派生变量
export CROSS_COMPILE="${FD_ROOT}/toolchain/gcc/aarch64_linux/bin/aarch64-linux-gnu-"
export BOOTGEN="${FD_ROOT}/tools/bootgen/albg1.exe"

echo "📌 关键路径:"
echo "   FD_ROOT    = $FD_ROOT"
echo "   ARMNN_LIB  = $ARMNN_LIB"
echo "   OPENCV_LIB = $OPENCV_LIB"
