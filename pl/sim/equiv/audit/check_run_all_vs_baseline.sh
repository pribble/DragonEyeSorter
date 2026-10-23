#!/bin/bash
# verifier-platform：des 域 8 项回归（run_sim.sh）与 _vendor_refactor/sim_baseline 逐字节对比。
# 说明：des 域不在本次重构范围，此检查作为「平台域改动不扰动 des 回归」的旁证；
#       主验证人 verifier-isp 另有 pl/sim/equiv 的 ISP 域分工。
set -u
cd "$(git rev-parse --show-toplevel)" || exit 1
BASE=pl/soc_base/soc_prj_Runs/_vendor_refactor/sim_baseline
OUT=/tmp/des_sim_after_verify
rm -rf "$OUT"; mkdir -p "$OUT"
FAILED=0
for t in des_axi servo_bus stepper io pwm vision vision2 awb; do
  bash pl/sim/run_sim.sh "$t" > "$OUT/$t.log" 2>&1
  rc=$?
  if [ ! -f "$BASE/$t.log" ]; then
    echo "MISS $t : no baseline log"; FAILED=$((FAILED + 1)); continue
  fi
  if cmp -s "$BASE/$t.log" "$OUT/$t.log"; then
    echo "BYTE-IDENTICAL $t"
  else
    echo "DIFF $t"
    diff "$BASE/$t.log" "$OUT/$t.log" | head -5 | sed 's/^/     /'
    FAILED=$((FAILED + 1))
  fi
  [ $rc -eq 0 ] || { echo "  (run_sim rc=$rc)"; FAILED=$((FAILED + 1)); }
done
echo "run_all_8_vs_baseline: FAILED=$FAILED (logs in $OUT)"
exit $(( FAILED > 0 ))
