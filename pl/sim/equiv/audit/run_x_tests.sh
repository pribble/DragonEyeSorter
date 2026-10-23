#!/bin/bash
# verifier-platform：批量跑指定的 x_* 等价 test（默认全部 x_*）
# 用法：bash pl/sim/equiv/audit/run_x_tests.sh [test ...]
set -u
cd "$(git rev-parse --show-toplevel)" || exit 1
if [ "$#" -gt 0 ]; then
    TESTS="$*"
else
    TESTS=$(cd pl/sim/equiv/tests && ls x_*.f 2>/dev/null | sed 's/\.f$//')
fi
FAILED=0
for t in $TESTS; do
    start=$(date +%s)
    if out=$(bash pl/sim/equiv/run_equiv.sh "$t" 2>&1); then
        rc=0
    else
        rc=$?
    fi
    dur=$(( $(date +%s) - start ))
    line=$(printf '%s\n' "$out" | grep -E '^(PASS|FAIL)' | head -n 3)
    printf '[%3ss] %s\n' "$dur" "${line:-FAIL $t: no verdict}"
    if [ $rc -ne 0 ]; then
        printf '%s\n' "$out" | head -n 25 | sed 's/^/      /'
        FAILED=$((FAILED + 1))
    fi
done
echo "x-tests: FAILED=$FAILED"
exit $(( FAILED > 0 ))
