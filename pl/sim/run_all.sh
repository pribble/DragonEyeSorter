#!/bin/bash
# 一次跑完全部 PL 仿真目标（逐个调用 run_sim.sh）
# 用法：bash pl/sim/run_all.sh [target ...]（不给参数 = 8 个全跑；可从任意目录调用）
set -u
SIM=$(CDPATH= cd -- "$(dirname -- "$(readlink -f -- "$0")")" && pwd)
cd "$SIM" || exit 1
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
[ $# -gt 0 ] && TARGETS="$*" || TARGETS="des_axi servo_bus stepper io pwm vision vision2 awb"
PASSED=0
FAILED=0
for t in $TARGETS; do
  L="$T/$t.log"
  bash ./run_sim.sh "$t" > "$L" 2>&1
  RC=$?
  OK=0
  if grep -q "ALL TESTS PASSED" "$L"; then
    OK=1
  elif grep -Eq "[0-9]+ passed, 0 failed" "$L"; then
    OK=1
  fi
  # 失败标志优先：即使日志里同时出现通过行也不算过（注意 "-> TIMEOUT = 1" 是正常断言行，只认行首）
  grep -Eq "[1-9][0-9]* TESTS FAILED|^TIMEOUT|error: |unknown test" "$L" && OK=0
  if [ $RC -eq 0 ] && [ $OK -eq 1 ]; then
    echo "PASS $t"
    PASSED=$((PASSED + 1))
  else
    echo "FAIL $t (exit $RC)"
    # 失败摘要：失败 / 超时 / 编译错误行；没有就取日志尾部
    S=$(grep -E "TESTS FAILED|^TIMEOUT|error: |unknown test" "$L" | head -n 5)
    [ -n "$S" ] && echo "$S" | sed 's/^/     /' || tail -n 5 "$L" | sed 's/^/     /'
    FAILED=$((FAILED + 1))
  fi
done
echo "run_all: $PASSED passed, $FAILED failed"
[ $FAILED -eq 0 ] || exit 1
