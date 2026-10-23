#!/bin/bash
# verifier-platform：结构审计脚本的「能失败」自检（变异测试）。
# 对 golden system_top.v 做 4 个已知变异，audit_system_top.py 必须判 DIFF；
# 原样文件、纯注释/空白改动必须判 EQUIVALENT（证明既不漏报也不误报）。
set -u
cd "$(git rev-parse --show-toplevel)" || exit 1
T=$(mktemp -d /tmp/audit-selftest.XXXXXX)
mkdir -p "$T/pl/sim/equiv"
echo "9a905cc343fd25003ee5a7188d31c52ce5955ac3" > "$T/pl/sim/equiv/baseline.rev"
git show 9a905cc:pl/soc_base/src/system_top.v > "$T/gold.v"
fail=0

check() { # $1=file $2=expect(EQUIVALENT|DIFF) $3=label
  out=$(python3 pl/sim/equiv/audit/audit_system_top.py "$T" --file "$1" --golden-file "$T/gold.v" 2>&1)
  got=$(printf '%s\n' "$out" | grep -o 'STRUCT_AUDIT [A-Z]*' | tail -n1)
  if [ "$got" = "STRUCT_AUDIT $2" ]; then
    echo "OK   $3 -> $got"
  else
    echo "FAIL $3 -> [$got] (expect STRUCT_AUDIT $2)"
    fail=$((fail + 1))
  fi
}

# 1) 原样：必须 EQUIVALENT
check "$T/gold.v" EQUIVALENT "identical"

# 2) 变异：assign led[0] 的 des_hb_cnt[27] -> [26]（行为变化）
sed 's/des_hb_cnt\[27\]/des_hb_cnt[26]/' "$T/gold.v" > "$T/mut_led.v"
check "$T/mut_led.v" DIFF "assign 变更"

# 3) 变异：design_0 的 .can0_tx(CAN0_TXD) -> .can0_tx(f2p_irq)（连线变化）
sed 's/\.can0_tx(CAN0_TXD)/.can0_tx(f2p_irq)/' "$T/gold.v" > "$T/mut_conn.v"
check "$T/mut_conn.v" DIFF "端口连线变更"

# 4) 变异：顶层端口顺序交换（CAN0_TXD / CAN0_RXD 声明互换）
python3 - "$T/gold.v" "$T/mut_port.v" <<'PY'
import sys
src = open(sys.argv[1]).read()
a = "    output         CAN0_TXD,     // F20\n    input          CAN0_RXD,     // F19"
b = "    input          CAN0_RXD,     // F19\n    output         CAN0_TXD,     // F20"
assert a in src, "port decl pattern not found"
open(sys.argv[2], "w").write(src.replace(a, b))
PY
check "$T/mut_port.v" DIFF "顶层端口顺序变化"

# 5) 纯注释/空白变化：必须仍 EQUIVALENT
sed 's/endmodule/endmodule   \/\/ reformatted/' "$T/gold.v" > "$T/reformat.v"
check "$T/reformat.v" EQUIVALENT "纯注释/空白变化"

echo "audit-selftest: FAILED=$fail"
rm -rf "$T"
exit $(( fail > 0 ))
