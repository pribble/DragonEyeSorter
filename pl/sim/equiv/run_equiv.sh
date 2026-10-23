#!/bin/bash
# 行为等价回归：golden RTL（固定 git rev）vs 工作区 RTL，逐拍 trace 比对
#
# 用法：
#   bash pl/sim/equiv/run_equiv.sh <test> [golden_rev]
#   bash pl/sim/equiv/run_all_equiv.sh [golden_rev]
#
# 每个 test 由两个文件定义：
#   pl/sim/equiv/tb/<test>.v      testbench，顶层模块必须叫 tb_<test>
#   pl/sim/equiv/tests/<test>.f   依赖清单（每行一个，repo 相对路径）：
#       别的前缀           RTL 依赖：golden 版从 git rev 取，新版取工作区
#       @ <path>           桩文件（加密模块等），两种构建都用工作区这一份
#       % <path>           仅 golden 侧依赖（本轮被删除/特化的模块）：从 git rev 取、只编进 golden；
#                          新版源码不引用它。典型：gamma 特化后删除的 8 个未用 LUT。
#       * <path>           仅新版侧依赖（本轮新增的文件）：只编进新版、不从 golden 抽；
#                          工作区文件必须存在。golden 版没有该文件时用它，避免 FAIL golden file not in rev。
#       ~ <path>           仅 golden 侧依赖，且编译 golden 前用 strip_comments.py 去掉注释：
#                          厂家文件有 iverilog 不认的注释病（如 top_demosaic.v 的 `/*//input`），
#                          注释不影响行为，去掉后 golden 仍语义等价。
#       + <iverilog 参数>  原样传给 iverilog（如 -I<dir>）
#   `#` 起注释，空行忽略。
#
# 约定（testbench 必须遵守）：
#   - 用 $value$plusargs("TRACE=%s", name) 取输出文件名，逐拍用 $fdisplay 写 trace；
#     trace 里必须带自增 cycle 计数，并逐拍写出被测模块的**全部输出**（含 x/z 态）。
#   - 结束前打印 "EQUIV_CYCLES <n>" 与 "EQUIV_DONE"，然后 $finish。
#   - 激励必须确定（固定 $random 种子或固定向量），不得依赖绝对时间。
# golden 版与新版各自编译、各自运行，trace 必须逐字节相同，否则 FAIL。
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$(readlink -f -- "$0")")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
NAME=${1:-}
if [ -z "$NAME" ]; then echo "usage: bash pl/sim/equiv/run_equiv.sh <test> [golden_rev]"; exit 2; fi
REV=${2:-}
if [ -z "$REV" ] && [ -f "$HERE/baseline.rev" ]; then
  REV=$(grep -v '^[[:space:]]*#' "$HERE/baseline.rev" | grep -v '^[[:space:]]*$' | head -n1 | tr -d '[:space:]')
fi
if [ -z "$REV" ]; then echo "FAIL $NAME: no golden rev (arg or pl/sim/equiv/baseline.rev)"; exit 2; fi

MAN="$HERE/tests/$NAME.f"
TB="$HERE/tb/$NAME.v"
[ -f "$MAN" ] || { echo "FAIL $NAME: missing $MAN"; exit 2; }
[ -f "$TB" ]  || { echo "FAIL $NAME: missing $TB"; exit 2; }

TMP=$(mktemp -d /tmp/des-equiv.XXXXXX)
KEEP=${EQUIV_KEEP:-0}
cleanup() { if [ "$KEEP" = "1" ]; then echo "kept: $TMP"; else rm -rf "$TMP"; fi; }
trap cleanup EXIT
mkdir -p "$TMP/gold"

trim() { printf '%s' "$1" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//'; }
paths=(); gonly=(); gstrip=(); newonly=(); stubs=(); opts=()
while IFS= read -r raw || [ -n "$raw" ]; do
  line=$(printf '%s' "$raw" | sed -e '1s/^\xEF\xBB\xBF//' | tr -d '\r')
  line=${line%%#*}
  line=$(trim "$line")
  [ -z "$line" ] && continue
  case "$line" in
    +*) opts+=("$(trim "${line#+}")") ;;
    @*) stubs+=("$REPO/$(trim "${line#@}")") ;;
    %*) gonly+=("$(trim "${line#%}")") ;;
    \**) newonly+=("$(trim "${line#\*}")") ;;
    ~*) gstrip+=("$(trim "${line#"~"}")") ;;
    *)  paths+=("$line") ;;
  esac
done < "$MAN"

if [ ${#paths[@]} -eq 0 ] && [ ${#gonly[@]} -eq 0 ] && [ ${#gstrip[@]} -eq 0 ] && [ ${#newonly[@]} -eq 0 ]; then
  echo "FAIL $NAME: manifest has no RTL deps"; exit 2
fi

gfiles=(); nfiles=(); i=0
declare -A seen=()
declare -A gdst=()
extract_golden() { # $1 = repo-relative path; sets seen/gdst, appends to gfiles
  local p=$1 b dst
  b=$(basename "$p")
  if [ -n "${seen[$b]:-}" ]; then echo "FAIL $NAME: duplicate basename $b ($p vs ${seen[$b]})"; exit 2; fi
  seen[$b]="$p"
  i=$((i + 1))
  dst="$TMP/gold/$(printf '%03d' "$i")_$b"
  if ! git -C "$REPO" show "$REV:$p" > "$dst" 2>/dev/null; then
    echo "FAIL $NAME: golden file not in $REV: $p"; exit 2
  fi
  gdst[$p]="$dst"
  gfiles+=("$dst")
}
for p in "${paths[@]}"; do
  extract_golden "$p"
  [ -f "$REPO/$p" ] || { echo "FAIL $NAME: working-tree file missing: $p"; exit 2; }
  nfiles+=("$REPO/$p")
done
for p in ${gonly[@]+"${gonly[@]}"}; do
  extract_golden "$p"
done
for p in ${newonly[@]+"${newonly[@]}"}; do  # 仅新版侧：不抽 golden，只放进新版构建
  [ -f "$REPO/$p" ] || { echo "FAIL $NAME: new-side file missing: $p"; exit 2; }
  nfiles+=("$REPO/$p")
done
for p in ${gstrip[@]+"${gstrip[@]}"}; do
  if [ -n "${gdst[$p]:-}" ]; then
    dst="${gdst[$p]}"          # 同一文件已作为普通依赖抽过 golden：直接清洗那个副本
  else
    extract_golden "$p"
    dst="${gfiles[${#gfiles[@]}-1]}"
  fi
  command -v python3 >/dev/null 2>&1 || { echo "FAIL $NAME: python3 required for '~' deps ($p)"; exit 2; }
  if ! python3 "$HERE/strip_comments.py" < "$dst" > "$TMP/strip.tmp"; then
    echo "FAIL $NAME: comment strip failed: $p"; exit 2
  fi
  mv "$TMP/strip.tmp" "$dst"
done

compile() { # $1=out $2=log, shift=sources；-I tb 目录供各 TB `include "equiv_tb_common.vh"`
  local out=$1 log=$2; shift 2
  if [ ${#opts[@]} -gt 0 ]; then
    iverilog -g2012 -I"$HERE/tb" -o "$out" "${opts[@]}" "$TB" "$@" > "$log" 2>&1
  else
    iverilog -g2012 -I"$HERE/tb" -o "$out" "$TB" "$@" > "$log" 2>&1
  fi
}
# 两侧编译互不依赖，并行跑（单 test 墙钟约减半）；跨 test 并行度由 run_all_equiv.sh 的 EQUIV_JOBS 控制
compile "$TMP/gold.out" "$TMP/compile.gold.log" "${gfiles[@]}" ${stubs[@]+"${stubs[@]}"} &
CGPID=$!
compile "$TMP/new.out" "$TMP/compile.new.log" "${nfiles[@]}" ${stubs[@]+"${stubs[@]}"} &
CNPID=$!
wait "$CGPID"; GRC=$?
wait "$CNPID";  NRC=$?
if [ $GRC -ne 0 ]; then
  echo "FAIL $NAME: golden compile error"; sed -n '1,25p' "$TMP/compile.gold.log"; exit 1
fi
if [ $NRC -ne 0 ]; then
  echo "FAIL $NAME: new compile error"; sed -n '1,25p' "$TMP/compile.new.log"; exit 1
fi

run() { # $1=exe, $2=trace, $3=log
  vvp -n "$1" +TRACE="$2" > "$3" 2>&1
}
# golden / 新版各自独立进程、独立 trace，同样并行跑
run "$TMP/gold.out" "$TMP/gold.trace" "$TMP/gold.log" &
RGPID=$!
run "$TMP/new.out"  "$TMP/new.trace"  "$TMP/new.log" &
RNPID=$!
wait "$RGPID"; GRRC=$?
wait "$RNPID"; NRRC=$?
if [ $GRRC -ne 0 ]; then
  echo "FAIL $NAME: golden run error"; tail -n 20 "$TMP/gold.log"; exit 1
fi
if [ $NRRC -ne 0 ]; then
  echo "FAIL $NAME: new run error"; tail -n 20 "$TMP/new.log"; exit 1
fi
grep -q "EQUIV_DONE" "$TMP/gold.log" || {
  echo "FAIL $NAME: golden tb did not finish"; tail -n 20 "$TMP/gold.log"; exit 1; }
grep -q "EQUIV_DONE" "$TMP/new.log" || {
  echo "FAIL $NAME: new tb did not finish"; tail -n 20 "$TMP/new.log"; exit 1; }

CYCLES=$(grep -o 'EQUIV_CYCLES [0-9]*' "$TMP/new.log" | tail -n1 | awk '{print $2}')
CGOLD=$(grep -o 'EQUIV_CYCLES [0-9]*' "$TMP/gold.log" | tail -n1 | awk '{print $2}')
if [ "$CGOLD" != "$CYCLES" ]; then echo "FAIL $NAME: cycle count golden=$CGOLD new=$CYCLES"; exit 1; fi
if ! cmp -s "$TMP/gold.trace" "$TMP/new.trace"; then
  echo "FAIL $NAME: trace mismatch (golden vs new)"
  diff "$TMP/gold.trace" "$TMP/new.trace" | head -n 20
  exit 1
fi
LINES=$(wc -l < "$TMP/new.trace")
echo "PASS $NAME: $CYCLES cycles, $LINES trace lines identical"
