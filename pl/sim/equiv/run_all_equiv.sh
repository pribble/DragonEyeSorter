#!/bin/bash
# 跑 pl/sim/equiv/tests/ 下全部等价回归（逐个 run_equiv.sh），任一失败返回非 0
# 用法：bash pl/sim/equiv/run_all_equiv.sh [golden_rev]
set -u
HERE=$(CDPATH= cd -- "$(dirname -- "$(readlink -f -- "$0")")" && pwd)
REV=${1:-}
PASSED=0
FAILED=0
shopt -s nullglob
for f in "$HERE"/tests/*.f; do
  name=$(basename "$f" .f)
  [ -f "$HERE/tb/$name.v" ] || { echo "SKIP $name (no tb)"; continue; }
  if [ -n "$REV" ]; then
    out=$(bash "$HERE/run_equiv.sh" "$name" "$REV" 2>&1)
  else
    out=$(bash "$HERE/run_equiv.sh" "$name" 2>&1)
  fi
  rc=$?
  echo "$out" | grep -E '^(PASS|FAIL)' | sed 's/^/  /'
  if [ $rc -eq 0 ]; then PASSED=$((PASSED + 1)); else FAILED=$((FAILED + 1)); fi
done
echo "equiv: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ] || exit 1
