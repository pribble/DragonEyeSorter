#!/bin/bash
# 等价回归 TB 重构验收：证明「TB 只抽公共样板、行为与 trace 完全没变」。
#
# 用法（仓库根目录）：
#   bash pl/sim/equiv/audit/check_tb_trace.sh capture [tb_rev]
#       用 tb_rev（默认 HEAD）的旧 TB + 当前 golden rev（baseline.rev）的 golden RTL
#       生成 36 项 golden trace 基线，写 manifest（sha256/行数/EQUIV_CYCLES）。
#   bash pl/sim/equiv/audit/check_tb_trace.sh verify
#       用工作区当前 TB 重跑 36 项，逐项与基线比 sha256 + 行数 + cycles；
#       完全一致=OK，任一不同=FAIL 非 0。
#
# 基线目录默认 pl/soc_base/soc_prj_Runs/_round3/equiv_traces_baseline（git 忽略），
# 可用环境变量 TB_TRACE_DIR 覆盖。旧 TB 从 git 取（不依赖任何未跟踪文件），
# 中间 harness 目录 pl/sim/equiv_head 用完即删。
# 注意：pwm 旧基线比新 trace 少最后 1 行（旧 TB 的 $finish 与 trace $fdisplay 竞争丢行，
# 旧 TB 本身不确定；新 TB 稳定复现完整 trace），故 pwm 行数会显示 DIFF，其余一致。
set -u
cd "$(git rev-parse --show-toplevel)" || exit 1
MODE=${1:-}
TB_REV=${2:-HEAD}
BASE=${TB_TRACE_DIR:-pl/soc_base/soc_prj_Runs/_round3/equiv_traces_baseline}
H=pl/sim/equiv_head
REV=$(grep -v '^[[:space:]]*#' pl/sim/equiv/baseline.rev |
      grep -v '^[[:space:]]*$' | head -n1 | tr -d '[:space:]')
die() { echo "FAIL: $*"; exit 2; }
[ -n "$REV" ] || die "no golden rev in pl/sim/equiv/baseline.rev"
shopt -s nullglob

capture() {
  rm -rf "$BASE" "$H"; mkdir -p "$BASE" "$H/tb"
  cp pl/sim/equiv/run_equiv.sh pl/sim/equiv/run_all_equiv.sh \
     pl/sim/equiv/strip_comments.py pl/sim/equiv/baseline.rev "$H/"
  cp -r pl/sim/equiv/tests pl/sim/equiv/stubs "$H/"
  git archive "$TB_REV" pl/sim/equiv/tb | tar -x -C "$H/tb" --strip-components=4 ||
    die "git archive $TB_REV pl/sim/equiv/tb"
  local n
  n=$(ls "$H/tb"/*.v 2>/dev/null | wc -l)
  [ "$n" -eq 36 ] || die "expect 36 old TBs in $TB_REV, got $n"
  echo "capture: tb_rev=$TB_REV golden=$REV base=$BASE"
  : > "$BASE/manifest.tsv"
  local f name out rc kept cycles lines sha
  for f in "$H"/tests/*.f; do
    name=$(basename "$f" .f)
    out=$(EQUIV_KEEP=1 bash "$H/run_equiv.sh" "$name" 2>&1); rc=$?
    kept=$(printf '%s\n' "$out" | sed -n 's/^kept: //p' | tail -n1)
    if [ -n "$kept" ] && [ -f "$kept/gold.trace" ]; then
      cp "$kept/gold.trace" "$BASE/$name.gold.trace"
      cycles=$(grep -o 'EQUIV_CYCLES [0-9]*' "$kept/gold.log" | tail -n1 | awk '{print $2}')
      lines=$(wc -l < "$kept/gold.trace")
      sha=$(sha256sum "$BASE/$name.gold.trace" | cut -d' ' -f1)
      printf '%s\t%s\t%s\t%s\t%s\n' "$name" "$rc" "${cycles:--}" "$lines" "$sha" \
        >> "$BASE/manifest.tsv"
      echo "  $([ "$rc" -eq 0 ] && echo OK || echo "FAIL(rc=$rc)") $name cycles=$cycles"
    else
      printf '%s\t%s\t-\t-\t-\n' "$name" "$rc" >> "$BASE/manifest.tsv"
      echo "  FAIL $name rc=$rc（无 golden trace）"
      printf '%s\n' "$out" | tail -n 5 | sed 's/^/       /'
    fi
    [ -n "$kept" ] && rm -rf "$kept"
  done
  rm -rf "$H"
  echo "capture done -> $BASE/manifest.tsv"
}

verify() {
  [ -f "$BASE/manifest.tsv" ] || die "no baseline: run capture first ($BASE/manifest.tsv)"
  local fail=0 ident=0 total=0 name brc cycles lines sha out rc kept nsha nl ncyc
  while IFS=$'\t' read -r name brc cycles lines sha; do
    [ "$sha" = "-" ] && { echo "SKIP $name (基线无 trace)"; continue; }
    total=$((total + 1))
    out=$(EQUIV_KEEP=1 bash pl/sim/equiv/run_equiv.sh "$name" 2>&1); rc=$?
    kept=$(printf '%s\n' "$out" | sed -n 's/^kept: //p' | tail -n1)
    if [ -n "$kept" ] && [ -f "$kept/gold.trace" ]; then
      nsha=$(sha256sum "$kept/gold.trace" | cut -d' ' -f1)
      nl=$(wc -l < "$kept/gold.trace")
      ncyc=$(grep -o 'EQUIV_CYCLES [0-9]*' "$kept/gold.log" |
             tail -n1 | awk '{print $2}')
      if [ "$rc" -eq 0 ] && [ "$nsha" = "$sha" ] && [ "$nl" = "$lines" ] &&
         [ "$ncyc" = "$cycles" ]; then
        echo "IDENTICAL $name cycles=$ncyc trace_lines=$nl"
        ident=$((ident + 1))
      elif [ "$ncyc" = "$cycles" ] && [ "$nl" = "$((lines + 1))" ] &&
           [ "$rc" -eq 0 ] &&
           cmp -s "$BASE/$name.gold.trace" <(head -n "$lines" "$kept/gold.trace"); then
        echo "SAME+1LINE $name cycles=$ncyc trace_lines=$nl/$lines" \
             "（旧 TB 尾行竞争丢行，新 TB 完整；pwm 预期如此）"
        ident=$((ident + 1))
      else
        echo "DIFF $name rc=$rc cycles=$ncyc/$cycles lines=$nl/$lines sha=$nsha/$sha"
        fail=$((fail + 1))
      fi
    else
      echo "NO-TRACE $name rc=$rc"
      printf '%s\n' "$out" | tail -n 6 | sed 's/^/     /'
      fail=$((fail + 1))
    fi
    [ -n "$kept" ] && rm -rf "$kept"
  done < "$BASE/manifest.tsv"
  echo "tb-trace-verify: identical=$ident diff=$fail total=$total" \
       "（SAME+1LINE 记 identical，见文件头说明）"
  [ "$fail" -eq 0 ]
}

case "$MODE" in
  capture) capture ;;
  verify)  verify ;;
  *) echo "usage: bash pl/sim/equiv/audit/check_tb_trace.sh capture|verify [tb_rev]"; exit 2 ;;
esac
