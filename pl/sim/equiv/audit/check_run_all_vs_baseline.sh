#!/bin/bash
# des 域 8 项功能仿真（pl/sim/run_sim.sh）与基线日志逐字节对比 —— 本轮重构验收口径。
#
# 用法（仓库根目录）：bash pl/sim/equiv/audit/check_run_all_vs_baseline.sh [baseline_dir]
# 基线目录优先级：参数 > 环境变量 SIM_BASELINE_DIR > 本轮默认
#   pl/soc_base/soc_prj_Runs/_round3/sim_baseline（git 忽略，由重构开始前冻结的 8 个 <target>.log 组成；
#   run_all.sh 的逐项日志就是 run_sim.sh 的 stdout，命名 <target>.log）。
# 找不到默认目录时退路：取 pl/soc_base/soc_prj_Runs/*/sim_baseline 中最新的一份。
# 输出：逐项 BYTE-IDENTICAL / DIFF / MISS，任一不同或 run_sim 非 0 则退出码 1。
set -u
cd "$(git rev-parse --show-toplevel)" || exit 1
BASE=${1:-${SIM_BASELINE_DIR:-pl/soc_base/soc_prj_Runs/_round3/sim_baseline}}
if [ ! -d "$BASE" ]; then
  BASE=$(ls -dt pl/soc_base/soc_prj_Runs/*/sim_baseline 2>/dev/null | head -n1)
fi
if [ ! -d "$BASE" ]; then
  echo "FAIL: 找不到功能仿真基线目录（默认 _round3/sim_baseline）"
  echo "      基线应由重构开始前的 bash pl/sim/run_all.sh 逐项日志留存；"
  echo "      或用参数 / SIM_BASELINE_DIR 指定。"
  exit 2
fi
OUT=$(mktemp -d /tmp/des-sim-8-vs-baseline.XXXXXX)
trap 'rm -rf "$OUT"' EXIT
FAILED=0
for t in des_axi servo_bus stepper io pwm vision vision2 awb; do
  bash pl/sim/run_sim.sh "$t" > "$OUT/$t.log" 2>&1
  rc=$?
  if [ ! -f "$BASE/$t.log" ]; then
    echo "MISS $t : no baseline log in $BASE"; FAILED=$((FAILED + 1)); continue
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
echo "run_all_8_vs_baseline: FAILED=$FAILED (baseline=$BASE)"
exit $(( FAILED > 0 ))
