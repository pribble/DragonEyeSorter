#!/usr/bin/env python3
"""golden vs 工作区 verilator lint 对比（verifier-platform 交付物，只读源码）。

对平台域每个目标：把 golden rev 的全部 pl/soc_base/src 导出到临时目录，用同一组
verilator --lint-only -Wall -Wno-fatal 参数分别 lint golden 快照与工作区，收集告警，
归一化（去掉文件路径/行/列）后做多重集 diff，输出「新增 / 消失」告警类别。

用法：
  python3 pl/sim/equiv/audit/lint_compare.py [--rev 9a905cc] [--target NAME ...] [--json]
目标：pwm / system_top / mipi_chain / hdmi_chain / uidbuf_write / uiFDMA / fs_cap
退出码：0 无新增告警且两侧 lint 无新增 error；1 有新增告警/新增 error。
"""
import argparse
import json
import re
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

from audit_paths import find_repo_root

REPO_DEFAULT = str(find_repo_root())
SRC = "pl/soc_base/src"

# name -> (top module, [RTL 相对 pl/soc_base/src 的路径], [stub 相对仓库根路径], 额外 verilator 参数)
TARGETS = {
    "pwm": ("pwm", ["pwm.v"], [], []),
    "system_top": (
        "system_top",
        ["system_top.v",
         "des/des_regs.v", "des/servo_bus.v", "des/stepper_ctrl.v", "des/des_vision.v",
         "des/des_pwm.v", "des/des_io.v", "des/awb_seq_div.v",
         "write_fdma/fs_cap.v", "write_fdma/uiFDMA.v", "write_fdma/uidbuf_write.v",
         "write_fdma/wfifo.v"],
        ["pl/sim/equiv/audit/lint_sys_stubs.v"],
        ["--language", "1364-2005", "-I{tree}/pl/soc_base/src/des"],
    ),
    "mipi_chain": (
        "design_top_wrapper",
        ["MIPI_V2/src/design_top_wrapper.v", "MIPI_V2/src/csi_unpacket.v",
         "MIPI_V2/mipi_rx/mipi_dphy_rx_over_lvds_wrapper.v",
         "MIPI_V2/mipi_rx/hs_rx_wrapper.sv", "MIPI_V2/mipi_rx/rx_clk_gen.v"],
        ["pl/sim/equiv/stubs/x_mipi_enc_stubs.v",
         "pl/sim/equiv/stubs/x_mipi_chain_stubs.v",
         "pl/sim/equiv/stubs/x_vendor_prim_stubs.v"],
        [],
    ),
    "hdmi_chain": (
        "uihdmi_tx",
        ["hdmi_tx/uihdmi_tx.v", "hdmi_tx/hdmi_phy_warpper.v", "hdmi_tx/lane_lvds_10_1.v"],
        ["pl/sim/equiv/stubs/x_hdmi_stubs.v", "pl/sim/equiv/stubs/x_vendor_prim_stubs.v"],
        [],
    ),
    "uidbuf_write": (
        "uidbuf_write",
        ["write_fdma/uidbuf_write.v", "write_fdma/fs_cap.v", "write_fdma/wfifo.v"],
        [],
        [],
    ),
    "uiFDMA": ("uiFDMA", ["write_fdma/uiFDMA.v"], [], []),
    "fs_cap": ("fs_cap", ["write_fdma/fs_cap.v"], [], []),
}

WARN_RE = re.compile(r"^(%Warning-[A-Z0-9_]+): (.+?)(?::(\d+):(\d+))?: (.*)$")
ERR_RE = re.compile(r"^(%Error(?:-[A-Z0-9_]+)?): (.+?)(?::(\d+):(\d+))?: (.*)$")

# 已审阅并解释的「预期新增告警」：{target: {warning code: 理由}}。
# 只有落在白名单里的新增告警不判 FAIL；每条都会在输出中以 EXPECTED 打印，便于人复核。
ALLOWED_ADDED = {
    "system_top": {
        "PINMISSING": "删除了原本空连接/只连死网的端口连接——等价清理（端口变悬空，原信号无观察者/无驱动）",
        "WIDTHTRUNC": "继承自 uiFDMA 内部位宽按参数收紧（取值域内无截断；x_uiFDMA 12000 拍逐字节等价佐证）",
        "WIDTHEXPAND": "同上（uiFDMA 内部位宽收紧）",
    },
    "uiFDMA": {
        "WIDTHTRUNC": "burst 长度/字节数改成按参数计算的位宽（BURST_LEN_W/BURST_BYTES_W），参数取值域内无截断；x_uiFDMA 12000 拍等价",
        "WIDTHEXPAND": "同上（位宽计算表达式扩宽，无行为影响）",
    },
    "mipi_chain": {
        "UNUSEDPARAM": "DEVICE 单配置特化后参数不再被引用（Lead 已批准；保留了参数名/默认值）",
    },
    "hdmi_chain": {
        "UNUSEDPARAM": "DEVICE 单配置特化后参数不再被引用（Lead 已批准；源码已加 lint waiver）",
    },
}


def warn_code(key):
    m = re.match(r"%Warning-([A-Z0-9_]+):", key)
    return m.group(1) if m else "?"


def normalize_line(line):
    """把 verilator 诊断行归一化成 (kind, key)。去掉绝对路径与行列号。"""
    m = WARN_RE.match(line)
    if m:
        code, path, msg = m.group(1), m.group(2), m.group(5)
        # 去掉消息里出现的文件名（相对/绝对）与行列
        msg = re.sub(r"/[^\s:]+\.(?:v|sv|vh)", "<file>", msg)
        msg = re.sub(r"\.\d+:\d+", "", msg)
        # 位置相关的信号/表达式保留；纯行号项不会出现
        return ("warning", f"{code}: {msg}")
    m = ERR_RE.match(line)
    if m:
        code, msg = m.group(1), m.group(5)
        msg = re.sub(r"/[^\s:]+\.(?:v|sv|vh)", "<file>", msg)
        msg = re.sub(r"\.\d+:\d+", "", msg)
        return ("error", f"{code}: {msg}")
    return None


def run_lint(tree, top, files, stubs, extra, repo):
    cmd = ["verilator", "--lint-only", "-Wall", "-Wno-fatal", "--top-module", top]
    ex = [a.format(tree=tree) for a in extra]
    cmd += ex
    for f in files:
        cmd.append(str(Path(tree) / SRC / f))
    for s in stubs:
        cmd.append(str(Path(repo) / s))
    p = subprocess.run(cmd, capture_output=True, text=True)
    diags = []
    for line in (p.stdout + "\n" + p.stderr).splitlines():
        n = normalize_line(line)
        if n:
            diags.append(n)
    return p.returncode, diags, p.stdout + p.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("repo", nargs="?", default=REPO_DEFAULT)
    ap.add_argument("--rev", default=None)
    ap.add_argument("--target", nargs="*", default=None)
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()
    repo = Path(args.repo)
    rev = args.rev
    if not rev:
        base = repo / "pl/sim/equiv/baseline.rev"
        rev = [l.strip() for l in base.read_text().splitlines()
               if l.strip() and not l.strip().startswith("#")][0]
    names = args.target or list(TARGETS)
    tmp = tempfile.mkdtemp(prefix="des-lint-gold.")
    # 导出 golden 全部 src
    arch = subprocess.run(["git", "-C", str(repo), "archive", rev, SRC],
                          capture_output=True)
    if arch.returncode != 0:
        print("FAIL: git archive golden", arch.stderr.decode()[:300])
        sys.exit(2)
    subprocess.run(["tar", "-x", "-C", tmp], input=arch.stdout, check=True)

    report = {"rev": rev, "targets": {}}
    total_added = 0
    for name in names:
        top, files, stubs, extra = TARGETS[name]
        rc_g, dg, log_g = run_lint(tmp, top, files, stubs, extra, repo)
        rc_n, dn, log_n = run_lint(str(repo), top, files, stubs, extra, repo)
        cg, cn = Counter(dg), Counter(dn)
        removed = list((cg - cn).elements())
        added = list((cn - cg).elements())
        print(f"===== lint target: {name} (top={top}) =====")
        print(f"  golden rc={rc_g} 诊断={len(dg)} | new rc={rc_n} 诊断={len(dn)}"
              f" | 新增={len(added)} 消失={len(removed)}")
        allow = ALLOWED_ADDED.get(name, {})
        unexplained = []
        for kind, key in added:
            code = warn_code(key)
            if kind == "warning" and code in allow:
                print(f"  = EXPECTED {kind}: {key}   # {allow[code]}")
            else:
                print(f"  + NEW {kind}: {key}")
                unexplained.append((kind, key))
        for kind, key in removed:
            print(f"  - OLD {kind}: {key}")
        # new 侧 error（lint 失败）单独列出
        new_errors = [z for z in dn if z[0] == "error"]
        gold_errors = [z for z in dg if z[0] == "error"]
        added_errors = Counter(new_errors) - Counter(gold_errors)
        if added_errors:
            print("  !! NEW LINT ERRORS:")
            for z in list(added_errors.elements())[:10]:
                print("     " + z[1])
        report["targets"][name] = {
            "top": top, "golden_returncode": rc_g, "new_returncode": rc_n,
            "golden_diag": len(dg), "new_diag": len(dn),
            "added": added, "removed": removed,
            "expected_added": len(added) - len(unexplained),
            "unexplained_added": len(unexplained),
            "golden_errors": len(gold_errors), "new_errors": len(new_errors),
        }
        total_added += len(unexplained) + len(added_errors)

    print()
    if total_added == 0:
        print("LINT RESULT: PASS（无新增告警；已解释/已批准的预期项见上）")
    else:
        print(f"LINT RESULT: DIFF（未解释新增 {total_added} 项）")
    if args.json:
        out = repo / "pl/sim/equiv/audit/last_lint_compare.json"
        out.write_text(json.dumps(report, ensure_ascii=False, indent=1), encoding="utf-8")
        print(f"(json -> {out})")
    subprocess.run(["rm", "-rf", tmp])
    sys.exit(0 if total_added == 0 else 1)


if __name__ == "__main__":
    main()
