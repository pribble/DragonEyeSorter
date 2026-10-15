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
    # 移除首尾空格
    key=$(echo "$key" | xargs)
    value=$(echo "$value" | xargs)

    # 处理 Windows 路径（替换反斜杠为正斜杠）
    value=$(echo "$value" | sed 's|\\|/|g')
    # shell 写法 $HOME / ${HOME} → make 的 $(HOME)（make 里 $HOME 会被读成 $H + "OME"）
    value=$(echo "$value" | sed -E 's/\$\{?([A-Za-z_][A-Za-z0-9_]*)\}?/$(\1)/g')

    echo "export $key := $value" >> "$OUTPUT_FILE"
done

echo "" >> "$OUTPUT_FILE"
echo "✅ 已生成 $OUTPUT_FILE"
