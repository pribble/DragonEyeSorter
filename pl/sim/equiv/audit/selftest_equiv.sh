#!/bin/bash
# run_equiv.sh 依赖前缀语义自测（变异测试）：证明 `*`=仅新版侧、`%`=仅 golden 侧、普通行=两侧。
# 用法（仓库根目录）：bash pl/sim/equiv/audit/selftest_equiv.sh [golden_rev]
# 临时文件用 _selftest_* 名字创建、退出时删除；只读真实 RTL（% 用例期间临时移走
# stubs/td_prim_models.v 再放回，<2s），失败数 >0 时退出码 1。
set -u
cd "$(git rev-parse --show-toplevel)" || exit 1
REV=${1:-}
if [ -z "$REV" ]; then
  REV=$(grep -v '^[[:space:]]*#' pl/sim/equiv/baseline.rev |
        grep -v '^[[:space:]]*$' | head -n1 | tr -d '[:space:]')
fi
T=pl/sim/equiv/tests/_selftest_star.f
STAR=pl/sim/equiv/stubs/_selftest_star_mod.v
KEEP_STUB=pl/sim/equiv/stubs/td_prim_models.v
TMP_STUB=/tmp/_selftest_keep_stub.v
TB=pl/sim/equiv/tb/_selftest_star.v
FAIL=0
cleanup() {
  rm -f "$T" "$STAR" "$TB"
  [ -f "$TMP_STUB" ] && mv "$TMP_STUB" "$KEEP_STUB"
}
trap cleanup EXIT

cat > "$TB" <<'V'
`timescale 1ns/1ps
module tb__selftest_star;
    integer fd;
    integer cyc = 0;
    reg [1023:0] tf;
    reg clk = 0;
    wire out;
    pwm dut (.clk(clk), .rst_n(1'b1), .pwm_out(out));
    always #5 clk = ~clk;
    initial begin
        if (!$value$plusargs("TRACE=%s", tf)) tf = "trace.txt";
        fd = $fopen(tf, "w");
        if (fd == 0) begin $display("cannot open trace"); $finish; end
    end
    always @(posedge clk) begin
        cyc <= cyc + 1;
        $fdisplay(fd, "%0d %b", cyc, out);
    end
    initial begin
        repeat (200) @(posedge clk);
        $display("EQUIV_CYCLES %0d", cyc);
        $display("EQUIV_DONE");
        $fclose(fd);
        $finish;
    end
endmodule
V

check() { # $1=rc $2=期望(0=通过/1=失败) $3=输出里必须出现的片段 $4=用例名 $5=输出
  local rc=$1 want=$2 pat=$3
  if [ "$want" = "0" ] && [ "$rc" -eq 0 ]; then echo "OK   $4"; return; fi
  if [ "$want" != "0" ] && [ "$rc" -ne 0 ] && printf '%s' "$5" | grep -q -- "$pat"; then
    echo "OK   $4"; return
  fi
  echo "FAIL $4（rc=$rc，期望模式: $pat）"
  printf '%s\n' "$5" | tail -n 8 | sed 's/^/     /'
  FAIL=$((FAIL + 1))
}

# T1 *) 仅新版侧：新版多编一个未用模块，两侧都应 PASS
echo "module _selftest_star_mod; endmodule" > "$STAR"
printf '# 自测：* 前缀\n* pl/sim/equiv/stubs/_selftest_star_mod.v\npl/soc_base/src/pwm.v\n' > "$T"
out=$(bash pl/sim/equiv/run_equiv.sh _selftest_star "$REV" 2>&1); rc=$?
check "$rc" 0 "PASS _selftest_star" "T1 * 仅新版侧（新版多一个未用模块，两侧 PASS）" "$out"

# T2 *) 负向：新侧文件重复定义 module pwm -> 新版编译失败证明 * 进了新版；golden 不报错证明未进 golden
echo "module pwm; endmodule" > "$STAR"
out=$(bash pl/sim/equiv/run_equiv.sh _selftest_star "$REV" 2>&1); rc=$?
check "$rc" 1 "new compile error" "T2 * 只进新版（新版重复定义报错、golden 正常）" "$out"
printf '%s' "$out" | grep -q "golden compile error" &&
  { echo "FAIL T2 golden 侧不应报错"; FAIL=$((FAIL + 1)); }

# T3 对照：去掉 * 行后同样内容 PASS，证明 T2 失败由 * 引入
printf 'pl/soc_base/src/pwm.v\n' > "$T"
out=$(bash pl/sim/equiv/run_equiv.sh _selftest_star "$REV" 2>&1); rc=$?
check "$rc" 0 "PASS _selftest_star" "T3 对照（无 * 行 -> PASS）" "$out"

# T4 普通行对 untracked 新文件必须报 golden 缺失（证明普通行 ≠ *）
printf 'pl/sim/equiv/stubs/_selftest_star_mod.v\npl/soc_base/src/pwm.v\n' > "$T"
out=$(bash pl/sim/equiv/run_equiv.sh _selftest_star "$REV" 2>&1); rc=$?
check "$rc" 1 "golden file not in" "T4 普通行要求 golden 存在" "$out"

# T5 %) 仅 golden 侧、不要求工作区存在：临时移走 td_prim_models.v，仍应 PASS
printf '# 自测：%% 前缀\n%% pl/sim/equiv/stubs/td_prim_models.v\npl/soc_base/src/pwm.v\n' > "$T"
mv "$KEEP_STUB" "$TMP_STUB"
out=$(bash pl/sim/equiv/run_equiv.sh _selftest_star "$REV" 2>&1); rc=$?
mv "$TMP_STUB" "$KEEP_STUB"
check "$rc" 0 "PASS _selftest_star" "T5 % 只从 golden 取（工作区缺文件也能过）" "$out"

# T6 对照：同一缺失文件写成 * 必须报工作区缺失（证明 * 与 % 互补）
printf '# 自测：* 缺文件\n* pl/sim/equiv/stubs/td_prim_models.v\npl/soc_base/src/pwm.v\n' > "$T"
mv "$KEEP_STUB" "$TMP_STUB"
out=$(bash pl/sim/equiv/run_equiv.sh _selftest_star "$REV" 2>&1); rc=$?
mv "$TMP_STUB" "$KEEP_STUB"
check "$rc" 1 "new-side file missing" "T6 * 要求工作区存在" "$out"

echo "equiv-prefix-selftest: FAILED=$FAIL"
exit $((FAIL > 0))
