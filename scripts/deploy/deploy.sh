#!/bin/sh
# scripts/deploy/deploy.sh —— 按 manifest.tsv 把"板端产品运行时"送上板（或只对比 / 攒一份）
#
#   bash scripts/deploy/deploy.sh --dry-run       只列板上与本地/仓库的差异，一块字节都不传
#   bash scripts/deploy/deploy.sh                 部署：刷新 out/ → 对比 → 传输 → 校验
#   bash scripts/deploy/deploy.sh --stage         只刷新 out/（要上板的那一份），不碰板子
#   bash scripts/deploy/deploy.sh --only des_app  只处理源/板端路径里含 des_app 的条目
#   bash scripts/deploy/deploy.sh --with-service  传 des_app 前自动停自启服务，校验完再起来
#   bash scripts/deploy/deploy.sh --dest /root/des/test   整份落到这个目录（文件名不变），不碰生产件
#
# 清单里只有"项目跑通后板上必须存在"的东西（主程序 + 它读的配置 + 开机自启链脚本，判据见 manifest.tsv 表头）。
# 测试 / 调试件（desreg、canplc、test_*、pl_check.sh、soak.sh、display_check.sh）不在清单里：要用时手动 scp
# （三个测试脚本与产品脚本一起住在 out/，只是不进清单、部署器不传）。
# 依赖：ssh / scp 免密（.env 的 BOARD_HOST）、awk、md5sum、sort、uniq。宿主机侧运行（Git Bash / WSL）。
# --stage 完全不联网（板子不在线也能攒），部署与 dry-run 才需要板子。
# 本脚本**不编译、不提交二进制**：主程序来自 ps/build/；`out/` 里平铺的脚本、des.env 与配置**就是发给板子
# 的那一份**（板端脚本与 des.env 的唯一源就在那儿：改那一份、提交那一份），只有编出来的二进制被 .gitignore
# 挡住（见 manifest.tsv 与同目录 README.md）。
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
manifest="$here/manifest.tsv"
out="$here/out"
stage="$out"      # 要上板的那一份：清单条目（产品运行时）平铺在这里；文本件入库，编出来的二进制不入库
                  # 板端脚本与 des.env 的**唯一源**也在这里 —— 刷新只拷"源在别处"的文件，不重建目录
TAB=$(printf '\t')

MODE=deploy
ONLY=""
WITH_SERVICE=0
DEST=""           # --dest：整份落到这个板端目录下（文件名不变），用来在板上试新版而不覆盖生产件

usage() {
    cat <<'EOF'
用法（在仓库根目录）：
  bash scripts/deploy/deploy.sh --dry-run       只列差异，不传文件
  bash scripts/deploy/deploy.sh                 部署（差异 → 传输 → 校验）
  bash scripts/deploy/deploy.sh --stage         刷 out/（主程序与配置从仓库别处拷进来；脚本与 des.env 就地不动）
  bash scripts/deploy/deploy.sh --only <子串>   只处理匹配的条目（如 des_app）
  bash scripts/deploy/deploy.sh --with-service  传 des_app 前停自启服务，校验完再起
  bash scripts/deploy/deploy.sh --dest <目录>   整份落到该目录（文件名不变），试新版而不覆盖生产件
  bash scripts/deploy/deploy.sh -h              本帮助
Windows 侧用同目录的 deploy.ps1（参数同名，它只是转调本脚本）。
清单 = 板端产品运行时（主程序 + 配置 + 自启链脚本）；测试 / 调试件手动 scp，见 manifest.tsv 末尾。
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        -n|--dry-run)   MODE=dry ;;
        -s|--stage)     MODE=stage ;;
        -o|--only)      [ $# -ge 2 ] || { echo "--only 需要参数" >&2; exit 2; }; ONLY=$2; shift ;;
        -d|--dest)      [ $# -ge 2 ] || { echo "--dest 需要参数" >&2; exit 2; }; DEST=$2; shift ;;
        --with-service) WITH_SERVICE=1 ;;
        -h|--help)      usage; exit 0 ;;
        *) echo "未知参数：$1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

case "$DEST" in
    "")  ;;
    /*)  ;;
    *)   echo "--dest 要写板端绝对路径（如 /root/des/test）" >&2; exit 2 ;;
esac
[ -z "$DEST" ] || [ "$WITH_SERVICE" = 0 ] || { echo "--dest 与 --with-service 互斥：测试目录不动自启服务" >&2; exit 2; }

tmpd=$(mktemp -d) || exit 1
trap 'rm -rf "$tmpd"' EXIT INT TERM

# ---------- .env（BOARD_HOST）：只有要连板子的模式才必须（--stage 不联网） ----------
if [ -f "$repo/.env" ]; then
    # 只认 KEY=值 行，值剥掉成对引号后整体再加单引号 source：带空格的路径不拆词，值里的 shell 元字符不展开
    # （与 generate_env_make.sh / env_loader.ps1 同一语义：值原样进变量，不做转义展开）
    tr -d '\r' < "$repo/.env" | sed -e 's/^\xEF\xBB\xBF//' | awk '
        BEGIN { q = sprintf("%c", 39) }
        /^[[:space:]]*#/ || /^[[:space:]]*$/ { next }
        {
            i = index($0, "=")
            if (i < 2 || substr($0, 1, i - 1) !~ /^[A-Za-z_][A-Za-z0-9_]*$/) {
                printf ".env 里这行不合 KEY=值 格式，已跳过：%s\n", $0 > "/dev/stderr"; next
            }
            k = substr($0, 1, i - 1); v = substr($0, i + 1)
            c1 = substr(v, 1, 1)
            if ((c1 == "\"" || c1 == q) && length(v) >= 2 && substr(v, length(v), 1) == c1) v = substr(v, 2, length(v) - 2)
            # 受限展开：值里的 $NAME / ${NAME} 只替换 shell 环境里已有的名字（单遍；找不到原样保留）。
            # $() / 反引号不执行——不再 source 原文件的原因就在这。
            out = ""
            while (match(v, /\$\{?[A-Za-z_][A-Za-z0-9_]*\}?/)) {
                m = substr(v, RSTART, RLENGTH); name = m
                gsub(/\$\{?|\}?/, "", name)
                out = out substr(v, 1, RSTART - 1) ((name in ENVIRON) ? ENVIRON[name] : m)
                v = substr(v, RSTART + RLENGTH)
            }
            v = out v
            # 值里的每个单引号写成 '\''（逐字符拼，不用 gsub：gsub 替换串的 \\ 处理各 awk 有歧义）
            r = ""
            n = length(v)
            for (j = 1; j <= n; j++) { ch = substr(v, j, 1); if (ch == q) r = r q "\\" q q; else r = r ch }
            print k "=" q r q
        }
    ' > "$tmpd/env"
    set -a; . "$tmpd/env"; set +a
fi
if [ "$MODE" != stage ]; then
    [ -f "$repo/.env" ] || { echo "找不到 $repo/.env —— 先 cp .env.example .env 填好（见 docs/环境与上手.md §2）" >&2; exit 1; }
    [ -n "${BOARD_HOST:-}" ] || { echo ".env 里没有 BOARD_HOST（ssh/scp 的目标，如 des）" >&2; exit 1; }
fi

rsh() { ssh -o BatchMode=yes -o ConnectTimeout=8 "$BOARD_HOST" "$@"; }
rscp() { scp -o BatchMode=yes -q "$@"; }

# ---------- manifest 条目 ----------
grep -v '^[[:space:]]*#' "$manifest" | grep -v '^[[:space:]]*$' > "$tmpd/raw"
if [ -n "$ONLY" ]; then
    # 只看"源"与"板端路径"两列：备注里出现过的词（如 desreg 备注提到 des_app）不该把条目带进来
    awk -F'\t' -v pat="$ONLY" 'index($1,pat) || index($2,pat)' "$tmpd/raw" > "$tmpd/entries"
    [ -s "$tmpd/entries" ] || { echo "manifest.tsv 里没有源/板端路径含 '$ONLY' 的条目" >&2; exit 1; }
else
    cp "$tmpd/raw" "$tmpd/entries"
fi

# --dest：只改"落到哪"这一列（文件名不变，全平铺在该目录下），后面所有环节（对比 / 传输 / 安装 / 校验）都用改写后的路径。
# 板端脚本与 des_app 的路径全部来自同目录的 des.env（按自己所在目录派生），所以拷到别处部署就能管那一份：
# /root/des/test/des_app.sh 跑测试那份、/root/des/test/des_service.sh 也只管测试那份（日志 / PID 文件都在里面）。
if [ -n "$DEST" ] && [ "$MODE" != stage ]; then
    [ -s "$tmpd/entries" ] || { echo "--dest 下没有要处理的条目（--only '$ONLY' 没匹配到）"; exit 0; }
    awk -F'\t' -v OFS='\t' -v d="$DEST" '{n=$2; sub(/.*\//,"",n); $2 = d "/" n; print}' "$tmpd/entries" > "$tmpd/entries.dest"
    mv "$tmpd/entries.dest" "$tmpd/entries"
fi

# 文本类去 CR 再算哈希：仓库工作区是 CRLF、板上是 LF，直接比 md5 会全是"不同"（见 README）
# 按**文件类型**判文本，不按 manifest 的 kind：kind 只管装完的权限（.onnx 是二进制，不能被去 CR）
is_text() {
    case "$1" in
        *.sh|*.env|*.json|*.tsv|*.conf|*.txt) return 0 ;;
        *) return 1 ;;
    esac
}
hash_file() {
    if is_text "$1"; then tr -d '\r' < "$1" | md5sum | cut -d' ' -f1
    else md5sum "$1" | cut -d' ' -f1; fi
}
remote_hash() {   # 参数：交替的 T|B 与板端路径；输出 "哈希  路径"（缺失为 MISSING）
    cat > "$tmpd/remote.sh" <<'REMOTE'
while [ $# -ge 2 ]; do
    k=$1; p=$2; shift 2
    if [ ! -f "$p" ]; then printf 'MISSING\t%s\n' "$p"; continue; fi
    if [ "$k" = T ]; then h=$(tr -d '\r' < "$p" | md5sum | cut -d' ' -f1); else h=$(md5sum "$p" | cut -d' ' -f1); fi
    printf '%s\t%s\n' "$h" "$p"
done
REMOTE
    rsh sh -s "$@" < "$tmpd/remote.sh"
}

# ---------- 攒"要上板的那一份"（out/，文件平铺；脚本与配置入库，编出来的二进制不入库） ----------
# out/ **既是暂存也是源**：板端脚本与 des.env 就住在这里（改这里、提交这里，不另存副本）。所以这里**不再
# rm -rf 重建**（那会把源删掉），改成"刷 + 清"：
#   刷：清单里源在 out/ 之外的那些 —— ps/build/des_app、ps/src 的配置、models/ 导出的模型；
#   清：不属于清单、也不在手动清单里的文件（旧版本留下的垃圾），以及本地已缺的旧产物，
#       免得 U 盘 / 打包里混进"上一次的那一份"。
# 手动清单：住在 out/、但不进清单、部署器不传的板端测试脚本（要用时 scp，见 manifest.tsv 末尾）。
# 新增板端脚本要么进 manifest.tsv（产品件），要么加进这一行（测试件），否则刷新时会被清掉。
# 用**全量**清单（$tmpd/raw）而不是 --only 过滤后的，免得不带参数的过滤把暂存刷成残缺的一份。
# 平铺是因为"哪个文件去哪"在清单与 MD5SUMS.txt 里已经写清楚了，再按板端目录分层只是多层嵌套。
manual_files="pl_check.sh soak.sh display_check.sh des_tool.sh"
stage_payload() {
    dup=$(awk -F'\t' '{n=$1; sub(/.*\//,"",n); print n}' "$tmpd/raw" | sort | uniq -d)
    [ -z "$dup" ] || { echo "manifest 里重名文件，平铺暂存会互相覆盖：$dup" >&2; exit 1; }
    mkdir -p "$stage"
    awk -F'\t' '{n=$1; sub(/.*\//,"",n); print n}' "$tmpd/raw" > "$tmpd/keep"
    keep="MD5SUMS.txt"
    while IFS= read -r b; do
        if [ -n "$b" ]; then keep="$keep $b"; fi
    done < "$tmpd/keep"
    for b in $manual_files; do keep="$keep $b"; done
    {
        echo "# 要上板的那一份 = 板端产品运行时（scripts/deploy/out/，文件平铺；脚本与配置入库，只有编出来的二进制不入库）"
        echo "# 板端脚本与 des.env 的**唯一源就在这里**（直接改这一份并提交）；其余文件由 deploy.sh 从仓库别处刷进来："
        echo "#   主程序 ps/build/des_app、配置 ps/src/*.json、模型 models/part_cls*（label_list.json 就住在本目录）"
        echo "# 左列 = 哈希（脚本/配置/数据类去掉 CR 后算），右列 = 这个文件该去的板端路径"
        echo "# 清单指纹：scripts/deploy/manifest.tsv 的 MD5 = $(hash_file "$manifest")"
    } > "$stage/MD5SUMS.txt"
    n=0
    stale=""
    while IFS="$TAB" read -r src dst kind restart note; do
        [ -n "$src" ] || continue
        if [ ! -f "$repo/$src" ]; then
            case "$src" in
                scripts/deploy/out/*)   # 源就在 out/ 里却不在 = 手写的那一份被人删了，说一声继续
                    echo "  清单里的板端文件不见了：$src" >&2 ;;
                *)                      # 源在别处又不在本地 = 没编 / 没导出（des_app、模型）
                    echo "  暂存跳过（本地缺）：$src"
                    if [ "$kind" = model ]; then echo "      —— 模型没导出时板端 NPU 分类是关的（日志里 npu: cannot open …）"; fi
                    stale="$stale $(basename "$src")" ;;
            esac
            continue
        fi
        case "$src" in
            scripts/deploy/out/*) ;;   # 源就在暂存目录里：本来就是它，不用拷
            *)
                cp "$repo/$src" "$stage/$(basename "$src")"
                n=$((n + 1))
                ;;
        esac
        printf '%s  %s\n' "$(hash_file "$repo/$src")" "$dst" >> "$stage/MD5SUMS.txt"
    done < "$tmpd/raw"
    {
        echo
        echo "# ---- 手动上板：测试 / 回归脚本（**不进清单**，部署器不传；scp 到 /root/des/，用法见 README.md）----"
        for b in $manual_files; do
            if [ -f "$stage/$b" ]; then printf '%s  %s\n' "$(hash_file "$stage/$b")" "/root/des/$b"; fi
        done
    } >> "$stage/MD5SUMS.txt"
    removed=0
    for f in "$stage"/*; do
        [ -e "$f" ] || continue
        b=${f##*/}
        case " $keep " in *" $b "*) ;; *)
            echo "  清掉不在清单里的：$b（新增板端脚本请写进 manifest.tsv 或 deploy.sh 的 manual_files）"
            rm -rf "$f"; removed=$((removed + 1)); continue ;;
        esac
        case " $stale " in *" $b "*)
            echo "  清掉本地已缺的旧产物：$b"
            rm -f "$f"; removed=$((removed + 1)) ;;
        esac
    done
    echo "== 已刷新 out：从仓库别处刷入 $n 个文件，板端脚本与 des.env 就地不动，清掉 $removed 个"
}

# --stage：只攒本地这一份，不连板子（板子不在线、WSL 里也能跑）
if [ "$MODE" = stage ]; then
    stage_payload
    echo "上板推荐直接跑部署：powershell -NoProfile -File scripts/deploy/deploy.ps1"
    echo "手工拷贝（U 盘 / 给队友）整份拷走，按 MD5SUMS.txt 右列的板端路径落位"
    exit 0
fi
# 部署前自动刷新那一份：编出来的二进制不用手工攒，板端脚本与 des.env 就地不动（dry-run 只读，不写盘）
[ "$MODE" = dry ] || stage_payload

# ---------- 本地扫描 + 板上扫描 → 状态表 ----------
: > "$tmpd/local"
: > "$tmpd/missing"
while IFS="$TAB" read -r src dst kind restart note; do
    [ -n "$src" ] || continue
    if [ -f "$repo/$src" ]; then
        printf '%s\t%s\t%s\t%s\t%s\n' "$dst" "$kind" "$restart" "$(hash_file "$repo/$src")" "$src" >> "$tmpd/local"
    else
        printf '%s\t%s\n' "$src" "$dst" >> "$tmpd/missing"
    fi
done < "$tmpd/entries"

set --
while IFS="$TAB" read -r dst kind restart h src; do
    if is_text "$repo/$src"; then set -- "$@" T "$dst"; else set -- "$@" B "$dst"; fi
done < "$tmpd/local"
if ! remote_hash "$@" > "$tmpd/remote" 2> "$tmpd/remote.err"; then
    echo "连不上板子（BOARD_HOST=$BOARD_HOST）—— 对比与部署都需要 ssh 通：" >&2
    sed 's/^/  /' "$tmpd/remote.err" >&2
    echo "  只想攒一份上板文件（不需要板子在线）：bash scripts/deploy/deploy.sh --stage" >&2
    exit 1
fi

# 状态：same（一致）/ update（不一致）/ new（板上没有）  字段：dst st kind restart 本地哈希 板上哈希 源
awk -F'\t' -v OFS='\t' '
    NR==FNR { rh[$2]=$1; next }
    {
        if (!($1 in rh) || rh[$1]=="MISSING") st="new";
        else if (rh[$1]==$4) st="same";
        else st="update";
        print $1, st, $2, $3, $4, rh[$1], $5
    }
' "$tmpd/remote" "$tmpd/local" > "$tmpd/status"

# ---------- 报告 ----------
echo "板子：$BOARD_HOST    清单：$(wc -l < "$tmpd/entries" | tr -d ' ') 条（manifest.tsv），本地有 $(wc -l < "$tmpd/local" | tr -d ' ') 个、缺 $(wc -l < "$tmpd/missing" | tr -d ' ') 个产物"
[ -z "$DEST" ] || echo "落点：$DEST（测试目录，文件名平铺；不碰 /root/des 下的生产件、不动自启服务）"
echo
echo "需要更新（板上与本地/仓库不一致）："
awk -F'\t' '$2=="update" {printf "  %-34s %s → %s   (%s)  %s\n", $1, substr($5,1,8), substr($6,1,8), $3, $7}' "$tmpd/status"
awk -F'\t' '$2=="update"' "$tmpd/status" | grep -q . || echo "  （无）"
echo "板上还没有："
awk -F'\t' '$2=="new" {printf "  %-34s (%s)  %s\n", $1, $3, $7}' "$tmpd/status"
awk -F'\t' '$2=="new"' "$tmpd/status" | grep -q . || echo "  （无）"
echo "一致：$(awk -F'\t' '$2=="same"' "$tmpd/status" | wc -l | tr -d ' ') 个"
if [ -s "$tmpd/missing" ]; then
    echo
    echo "本地缺产物（先在仓库里编出来）："
    awk -F'\t' '{printf "  %-28s → %s\n", $1, $2}' "$tmpd/missing"
    echo "  提示：主程序编 make -C ps all；NPU 模型由 train/export_npu.py 导出到 models/"
    echo "        测试 / 调试件不在本清单里（desreg、canplc、test_*、pl_check.sh…），要用时手动 scp"
fi
echo

awk -F'\t' -v OFS='\t' '$2=="update"||$2=="new" {print $7, $1, $3, $4}' "$tmpd/status" > "$tmpd/send"

case "$MODE" in
dry)
    echo "（--dry-run：没有传任何文件）"
    exit 0
    ;;
esac

# ---------- 部署 ----------
if [ ! -s "$tmpd/send" ]; then
    echo "没有要传的文件，板子已经是 manifest 里的这一份。"
    exit 0
fi

if [ "$WITH_SERVICE" = 0 ] && awk -F'\t' '$2=="/root/des/des_app"{f=1} END{exit !f}' "$tmpd/send"; then
    if rsh 'pidof des_app >/dev/null 2>&1'; then
        echo "/root/des/des_app 正被运行的 des_app 占用（覆盖会 Text file busy）。先停服务，或加 --with-service：" >&2
        echo "  ssh \$BOARD_HOST /root/des/des_service.sh stop" >&2
        exit 1
    fi
fi

# 覆盖**正在运行**的 des_service.sh 同样会读到半新半旧（ash 边读边执行），而且新版的 PID 文件
# 已从 /var/run 挪进数据目录、认不出旧进程 —— 正式部署遇到监督进程在跑就拦下，让人选 --with-service。
if [ "$WITH_SERVICE" = 0 ] && [ -z "$DEST" ] \
    && awk -F'\t' '$1 ~ /des_service\.sh$/' "$tmpd/send" | grep -q . \
    && rsh 'ps | grep -q "[_]supervise"'; then
    echo "自启服务的监督进程正在跑，直接覆盖 des_service.sh 会读到半新半旧（且新旧 PID 文件不通用）。" >&2
    echo "先停服务再传，或加 --with-service（脚本自己 stop → 传 → start）：" >&2
    echo "  ssh \$BOARD_HOST /root/des/des_service.sh stop" >&2
    exit 1
fi

if [ "$WITH_SERVICE" = 1 ]; then
    echo "== 停自启服务"
    rsh '/root/des/des_service.sh stop'
fi

echo "== 传到板上中转目录 /tmp/desdeploy"
rsh 'rm -rf /tmp/desdeploy && mkdir -p /tmp/desdeploy'
set --
while IFS="$TAB" read -r src dst kind restart; do set -- "$@" "$repo/$src"; done < "$tmpd/send"
rscp "$@" "$BOARD_HOST:/tmp/desdeploy/"

echo "== 安装到位"
{
    echo 'set -e'
    # 在板上再挡一道：覆盖运行中的可执行文件会 Text file busy（上面本地那道只是提前给人提示）
    # --dest 是另装一份，不覆盖生产件，所以这条守卫只在正式部署时生成
    if [ -z "$DEST" ]; then
        echo 'if [ -f /tmp/desdeploy/des_app ] && pidof des_app >/dev/null 2>&1; then'
        echo '    echo "des_app 正在运行，拒绝覆盖 /root/des/des_app（先 des_service.sh stop，或用 --with-service）" >&2'
        echo '    exit 1'
        echo 'fi'
    fi
    while IFS="$TAB" read -r src dst kind restart; do
        case "$kind" in script|bin|tool) m=700 ;; *) m=600 ;; esac
        # mkdir -p：第一次上板（新卡 / eMMC 救生艇）时目标目录可能还不存在
        echo "mkdir -p \"\$(dirname '$dst')\" && cp -f \"/tmp/desdeploy/$(basename "$src")\" '$dst' && chmod $m '$dst'"
    done < "$tmpd/send"
    echo 'sync'
} > "$tmpd/install.sh"
rsh sh -s < "$tmpd/install.sh"
rsh 'rm -rf /tmp/desdeploy'

echo "== 校验"
sent=0
bad=0
set --
while IFS="$TAB" read -r src dst kind restart; do
    if is_text "$repo/$src"; then set -- "$@" T "$dst"; else set -- "$@" B "$dst"; fi
done < "$tmpd/send"
remote_hash "$@" > "$tmpd/verified"
while IFS="$TAB" read -r src dst kind restart; do
    sent=$((sent + 1))
    want=$(awk -F'\t' -v d="$dst" '$1==d {print $5}' "$tmpd/status")
    got=$(awk -v d="$dst" '$2==d {print $1}' "$tmpd/verified")
    [ "$want" = "$got" ] || { bad=$((bad + 1)); echo "  ✘ $dst  本地 $want 板上 $got"; }
done < "$tmpd/send"
[ "$bad" -eq 0 ] && echo "  ✔ $sent/$sent 与本地一致"

if [ "$WITH_SERVICE" = 1 ]; then
    echo "== 起自启服务"
    rsh '/root/des/des_service.sh start'
elif [ -z "$DEST" ] && [ -n "$(awk -F'\t' '$4=="yes"' "$tmpd/send")" ] && rsh 'pidof des_app >/dev/null 2>&1'; then
    echo "注意：des_app 正在跑，同步的程序/配置要 /root/des/des_service.sh restart 才生效"
fi

if [ -n "$DEST" ] && [ "$bad" -eq 0 ]; then
    echo
    echo "测试目录跑法：ssh \$BOARD_HOST $DEST/des_app.sh live -n 20    # 启动器 cd 到自己目录、-c 取自它自己的 des.env"
    echo "              ⚠ 别裸跑 $DEST/des_app（不带 -c）：缺省是**当前目录**的 des_app.json（环境 DES_CONFIG 优先），"
    echo "                换了工作目录就会用错配置 —— 一律用 ./des_app.sh"
    echo "              相机 / 舵机总线 / 步进只有一个主人：自启服务在跑就先 $DEST/des_service.sh stop，跑完 start"
    echo "              $DEST/des_service.sh 管的就是这一份：日志与 PID 文件都在 $DEST/data/ 下，不碰生产"
fi

[ "$bad" -eq 0 ] || exit 1
