#!/bin/bash
# 跑 pl/sim/equiv/tests/ 下全部等价回归（逐个 run_equiv.sh），任一失败返回非 0
# 用法：bash pl/sim/equiv/run_all_equiv.sh [golden_rev]
#
# 环境变量：
#   EQUIV_JOBS=N    并行度，默认 = nproc；1 = 串行（输出顺序与 PASS/FAIL 判定不变）
#   EQUIV_ONLY=glob 只跑匹配的 test，例如 EQUIV_ONLY='x_*'（shell 通配，可空格分隔多个模式）
#   EQUIV_KEEP=1    透传给 run_equiv.sh，保留每个 test 的临时目录
#
# 并行时每个 test 的原始输出先落到临时文件，全部结束后按 tests 字典序逐项打印，
# 因此日志行与串行版逐行一致（顺序也一致），适合与基线日志直接 diff。
set -u
HERE=$(CDPATH= cd -- "$(dirname -- "$(readlink -f -- "$0")")" && pwd)
REV=${1:-}
JOBS=${EQUIV_JOBS:-}
if [ -z "$JOBS" ]; then
  JOBS=$(nproc 2>/dev/null || echo 1)
fi
if ! [ "$JOBS" -ge 1 ] 2>/dev/null; then
  echo "warn: EQUIV_JOBS='$JOBS' invalid, fallback to 1" >&2
  JOBS=1
fi
# wait -n 需要 bash >= 4.3；更老的 bash 退化串行，保证行为正确
if [ "${BASH_VERSINFO[0]}" -lt 4 ] ||
   { [ "${BASH_VERSINFO[0]}" -eq 4 ] && [ "${BASH_VERSINFO[1]}" -lt 3 ]; }; then
  JOBS=1
fi

TMP=$(mktemp -d /tmp/des-equiv-all.XXXXXX)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/out"

NAMES=()
shopt -s nullglob
for f in "$HERE"/tests/*.f; do
  name=$(basename "$f" .f)
  [ -f "$HERE/tb/$name.v" ] || { echo "SKIP $name (no tb)"; continue; }
  if [ -n "${EQUIV_ONLY:-}" ]; then
    matched=0
    for pat in $EQUIV_ONLY; do
      # shellcheck disable=SC2254  # 故意用未加引号模式做 glob 匹配
      case "$name" in $pat) matched=1; break ;; esac
    done
    [ "$matched" -eq 1 ] || continue
  fi
  NAMES+=("$name")
done
if [ ${#NAMES[@]} -eq 0 ]; then
  echo "equiv: no test to run"; exit 2
fi

# 并行启动顺序：按实测耗时降序先跑最重的 test，缩短并行尾延迟（只影响启动时机；
# 打印/计数顺序仍是 tests 字典序，与串行日志逐行一致）。未列出的 test 按字典序跟在后面。
PRIORITY="x_system_top x_isp_top isp_front x_uidbuf_write x_awb awb wfifo uidbuf_write x_demosaic demosaic pwm"
LAUNCH=()
for p in $PRIORITY; do
  for n in "${NAMES[@]}"; do [ "$n" = "$p" ] && LAUNCH+=("$n"); done
done
for n in "${NAMES[@]}"; do
  case " $PRIORITY " in
    *" $n "*) ;;
    *) LAUNCH+=("$n") ;;
  esac
done

run_one() { # $1 = test name；rc 写文件，stdout/stderr 写文件
  local name=$1
  if [ -n "$REV" ]; then
    bash "$HERE/run_equiv.sh" "$name" "$REV" > "$TMP/out/$name.raw" 2>&1
  else
    bash "$HERE/run_equiv.sh" "$name" > "$TMP/out/$name.raw" 2>&1
  fi
  echo $? > "$TMP/out/$name.rc"
}

started=0
for name in "${LAUNCH[@]}"; do
  run_one "$name" &
  started=$((started + 1))
  if [ "$started" -ge "$JOBS" ]; then
    wait -n || true          # 任意一个完成即回收，保持最多 JOBS 个并发
    started=$((started - 1))
  fi
done
wait

PASSED=0
FAILED=0
for name in "${NAMES[@]}"; do
  rc=$(cat "$TMP/out/$name.rc" 2>/dev/null || echo 1)
  grep -E '^(PASS|FAIL)' "$TMP/out/$name.raw" | sed 's/^/  /'
  if [ "$rc" -eq 0 ]; then PASSED=$((PASSED + 1)); else FAILED=$((FAILED + 1)); fi
done
echo "equiv: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ] || exit 1
