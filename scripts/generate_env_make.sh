#!/bin/bash
# 从 .env 生成 .env.make 供 Makefile include 使用

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ENV_FILE="$PROJECT_ROOT/.env"
OUTPUT_FILE="$PROJECT_ROOT/.env.make"

if [ ! -f "$ENV_FILE" ]; then
    echo "⚠️  警告: 找不到 .env 文件，跳过生成 .env.make"
    exit 0
fi

echo "# 自动生成的 Makefile 配置 - 不要手动编辑" > "$OUTPUT_FILE"
echo "# 由 scripts/generate_env_make.sh 从 .env 生成" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"

# 转换 .env 为 Makefile 格式
grep -v '^#' "$ENV_FILE" | grep -v '^[[:space:]]*$' | while IFS='=' read -r key value; do
    # 移除首尾空格（含 CRLF 的 \r）和成对引号。不用 xargs：它把反斜杠当转义吃掉，D:\ANLU\FD 会变成 D:ANLUFD
    key=$(printf '%s' "$key" | sed -E 's/^[[:space:]]+//; s/[[:space:]]+$//')
    value=$(printf '%s' "$value" | sed -E "s/^[[:space:]]+//; s/[[:space:]]+\$//; s/^\"(.*)\"\$/\\1/; s/^'(.*)'\$/\\1/")

    # 处理 Windows 路径（替换反斜杠为正斜杠）
    value=$(echo "$value" | sed 's|\\|/|g')
    # shell 写法 $HOME / ${HOME} → make 的 $(HOME)（make 里 $HOME 会被读成 $H + "OME"）
    value=$(echo "$value" | sed -E 's/\$\{?([A-Za-z_][A-Za-z0-9_]*)\}?/$(\1)/g')

    # make 把值里的 # 当注释开头：转义成 \#（make 读回时还原成 #），否则 BOARD_HOST 一类的值会被悄悄截断
    case $value in
        *'#'*) value=$(printf '%s' "$value" | sed 's/#/\\#/g'); echo "   （$key 的值含 #，已转义成 \\#）" ;;
    esac

    echo "export $key := $value" >> "$OUTPUT_FILE"
done

echo "" >> "$OUTPUT_FILE"
echo "✅ 已生成 $OUTPUT_FILE"
