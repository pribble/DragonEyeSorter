#!/usr/bin/env python3
"""top_demosaic.v 结构旁证：golden（注释清洗后）vs 新版 wrapper 的
模块参数表 / 顶层端口表 / 两个子模块实例（参数映射 + 端口映射/连线拓扑）逐项比对。

wrapper 无逻辑，且已被 tests/demosaic.f 的 trace 级等价覆盖；本脚本再给一份结构级证据。
内部连线名允许重命名（如 demosaic_tlast -> d_tlast），按「首次出现顺序」规范化后比较拓扑：
同样的内部线是否仍同时连到同样的实例端口，顶层的同名端口是否仍一一对应。
用法：python3 check_demosaic_wrapper.py [--repo DIR] [--rev REV]
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

PATH = "pl/soc_base/src/ui_isp/demosaic_4x_2_0/top_demosaic.v"


def default_repo():
    p = Path(__file__).resolve()
    if p.parent.name == "audit" and p.parent.parent.name == "equiv":
        return p.parents[4]
    raise SystemExit("cannot locate repo root: run from pl/sim/equiv/audit/ or pass --repo")
INSTS = ["raw_matrix_3x3_buffer", "bilinear_interpolation"]


def norm(s):
    return re.sub(r"\s+", " ", s.strip())


def strip_comments(repo: Path, data: bytes) -> str:
    return subprocess.run(
        ["python3", str(repo / "pl/sim/equiv/strip_comments.py")],
        input=data, check=True, capture_output=True).stdout.decode("utf-8", errors="replace")


def strip_line_comments(txt: str) -> str:
    return re.sub(r"//[^\n]*", "", txt)


def parse(txt):
    head = txt[txt.index("module demosaic"): txt.index(");")]
    params = [(norm(m.group(1)), norm(m.group(2)))
              for m in re.finditer(r"parameter\s+(\w+)\s*=\s*([^,\n)]+)", head)]
    ports = [(m.group(1), norm(m.group(2) or ""), norm(m.group(3) or ""))
             for m in re.finditer(r"(input|output|inout)\s+(\[[^\]]*\])?\s*(\w+)\s*[,)]", head)]
    inst = {}
    for mod in INSTS:
        m = re.search(re.escape(mod) + r"\s*#\s*\((.*?)\)\s*(\w+)\s*\((.*?)\);", txt, re.S)
        assert m, f"instance {mod} not found"
        pmap = [(norm(a), norm(b)) for a, b in re.findall(
            r"\.(\w+)\s*\(\s*([^)]*?)\s*\)", m.group(1))]
        portmap = [(norm(a), norm(b)) for a, b in re.findall(
            r"\.(\w+)\s*\(\s*([^)]*?)\s*\)", m.group(3))]
        inst[mod] = (pmap, portmap)
    return params, ports, inst


def canonical(inst, top_ports):
    """内部连线按首次出现顺序编号（允许改名）；顶层端口按名字保留。"""
    tops = {p[2] for p in top_ports}

    def cv(net):
        if net in tops:
            return "PORT:" + net
        if net not in seen:
            seen[net] = "INT#%d" % len(seen)
        return seen[net]

    seen = {}
    out = {}
    for mod in INSTS:
        pmap, portmap = inst[mod]
        out[mod] = (pmap, [(p, cv(net)) for p, net in portmap])
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=str(default_repo()))
    ap.add_argument("--rev", default="")
    args = ap.parse_args()
    repo = Path(args.repo)
    rev = args.rev
    if not rev:
        for line in (repo / "pl/sim/equiv/baseline.rev").read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                rev = line
                break
    gold_raw = subprocess.run(["git", "-C", str(repo), "show", f"{rev}:{PATH}"],
                              check=True, capture_output=True).stdout
    gp, gports, ginst = parse(strip_comments(repo, gold_raw))
    np_, nports, ninst = parse(strip_line_comments((repo / PATH).read_text(encoding="utf-8")))
    bad = 0
    if gp == np_:
        print(f"PASS 模块参数表: {len(gp)} 项一致 ({', '.join(p[0] for p in gp)})")
    else:
        bad += 1
        print("FAIL 模块参数表", gp, np_)
    if gports == nports:
        print(f"PASS 顶层端口表: {len(gports)} 项一致（名/方向/位宽/顺序不变）")
    else:
        bad += 1
        print("FAIL 顶层端口表")
        print("  golden:", gports)
        print("  new   :", nports)
    gc, nc = canonical(ginst, gports), canonical(ninst, nports)
    if gc == nc:
        nports = sum(len(v[1]) for v in gc.values())
        nparams = sum(len(v[0]) for v in gc.values())
        print(f"PASS 子模块实例映射: 2 个实例, {nparams} 个参数映射 / {nports} 个端口连线拓扑一致")
    else:
        bad += 1
        print("FAIL 子模块实例映射")
        print("  golden:", gc)
        print("  new   :", nc)
    print("PASS wrapper 结构一致（参数/顶层端口/子模块连线拓扑）" if bad == 0
          else f"FAIL {bad} 项不一致")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
