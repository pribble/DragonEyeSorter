#!/bin/bash
# verifier-platform 终验总脚本：一次性生成「平台域（MIPI_V2 / hdmi_tx / write_fdma / 顶层 / pwm）」
# 的等价证据包到 pl/sim/equiv/audit/evidence/（纯文本，可复跑、可逐字节对照）。
#
# 用法（必须在仓库根目录）：bash pl/sim/equiv/audit/run_final_verify.sh
# 前置：WSL + iverilog 12 + verilator 5.x + python3；golden rev 取 pl/sim/equiv/baseline.rev。
set -u
cd "$(git rev-parse --show-toplevel)" || exit 1
OUT=pl/sim/equiv/audit/evidence
rm -rf "$OUT"; mkdir -p "$OUT"

PLATFORM_FILES="pl/soc_base/src/system_top.v
pl/soc_base/src/pwm.v
pl/soc_base/src/MIPI_V2/src/design_top_wrapper.v
pl/soc_base/src/MIPI_V2/src/csi_unpacket.v
pl/soc_base/src/MIPI_V2/mipi_rx/mipi_dphy_rx_over_lvds_wrapper.v
pl/soc_base/src/MIPI_V2/mipi_rx/hs_rx_wrapper.sv
pl/soc_base/src/MIPI_V2/mipi_rx/rx_clk_gen.v
pl/soc_base/src/hdmi_tx/uihdmi_tx.v
pl/soc_base/src/hdmi_tx/hdmi_phy_warpper.v
pl/soc_base/src/hdmi_tx/lane_lvds_10_1.v
pl/soc_base/src/write_fdma/fs_cap.v
pl/soc_base/src/write_fdma/uiFDMA.v
pl/soc_base/src/write_fdma/uidbuf_write.v
pl/soc_base/src/write_fdma/wfifo.v"

{
  echo "# 平台域终验证据（verifier-platform）"
  echo "date: $(date '+%F %T %z')"
  echo "host: $(uname -srm)"
  echo "repo: $(pwd)"
  echo "branch: $(git rev-parse --abbrev-ref HEAD)"
  echo "HEAD: $(git rev-parse HEAD)"
  echo "golden: $(grep -v '^[[:space:]]*#' pl/sim/equiv/baseline.rev | grep -v '^[[:space:]]*$' | head -n1)"
  echo "iverilog: $(iverilog -V 2>/dev/null | head -n1)"
  echo "verilator: $(verilator --version 2>/dev/null)"
} > "$OUT/env.txt"
cat "$OUT/env.txt"
git status --short > "$OUT/git_status.txt"
echo "$PLATFORM_FILES" | xargs sha256sum > "$OUT/sha256_platform_rtl.txt"
sha256sum pl/soc_base/soc_prj.al pl/soc_base/soc_prj_Runs/syn_1/soc_prj.prj \
          pl/soc_base/soc_prj_Runs/phy_1/soc_prj.prj \
          pl/sim/equiv/run_equiv.sh pl/sim/equiv/baseline.rev > "$OUT/sha256_project_and_fw.txt"

FAIL=0
run() { # $1=name $2..=command
  local name=$1; shift
  echo "=== $name: $(date '+%T') ==="
  if "$@" > "$OUT/$name.log" 2>&1; then
    echo "PASS $name"
  else
    echo "FAIL $name"
    FAIL=$((FAIL + 1))
  fi
}

run equiv_all_36 bash pl/sim/equiv/run_all_equiv.sh
run audit_system_top python3 pl/sim/equiv/audit/audit_system_top.py --json pl/sim/equiv/audit/last_audit_system_top.json
run contracts_platform python3 pl/sim/equiv/audit/check_contracts_platform.py --json
run lint_compare python3 pl/sim/equiv/audit/lint_compare.py --json
run encoding python3 pl/sim/equiv/audit/check_encoding.py --rev 9a905cc
run prj_files python3 pl/sim/equiv/audit/check_prj_files.py --rev 9a905cc --json
run run_all_8_vs_baseline bash pl/sim/equiv/audit/check_run_all_vs_baseline.sh
run selftest_audit bash pl/sim/equiv/audit/selftest_audit.sh
mv pl/sim/equiv/audit/last_*.json "$OUT"/ 2>/dev/null || true

echo
echo "================ 汇总 ================"
grep -E "^(PASS|FAIL|BYTE-IDENTICAL|DIFF|MISS) " "$OUT"/equiv_all_36.log "$OUT"/run_all_8_vs_baseline.log \
  | sed 's|.*/||' | sort | uniq -c | sort -rn | head -5
echo "equiv_all_36: $(grep -c '^  PASS' "$OUT/equiv_all_36.log") passed, $(grep -c '^  FAIL' "$OUT/equiv_all_36.log") failed"
echo "audit:      $(grep 'STRUCT_AUDIT' "$OUT/audit_system_top.log" | tail -n1) hard=$(grep -o 'hard_diff_items=[0-9]*' "$OUT/audit_system_top.log")"
echo "contracts:  $(grep 'CONTRACT RESULT' "$OUT/contracts_platform.log")"
echo "prj:        $(grep 'PRJ RESULT' "$OUT/prj_files.log")"
echo "encoding:   $(grep -E '文件数=' "$OUT/encoding.log" | tail -n1)"
echo "lint:       $(grep 'LINT RESULT' "$OUT/lint_compare.log")"
echo "selftest:   $(grep 'audit-selftest' "$OUT/selftest_audit.log")"
echo "evidence:   $OUT/"
echo "TOTAL_FAIL=$FAIL"
exit $(( FAIL > 0 ))
