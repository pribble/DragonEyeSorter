#!/usr/bin/env python3
"""demosaic_4x_2_0 六个模块的接口不变性审计：golden（注释清洗后）vs 新版
逐文件比对 模块名 / 参数（名·默认值·位宽） / 端口（方向·位宽·名·顺序）。
用法：python3 check_demosaic_ports.py [--repo DIR] [--rev REV]
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

D = "pl/soc_base/src/ui_isp/demosaic_4x_2_0"
FILES = ["top_demosaic", "raw_matrix_3x3_buffer", "bilinear_interpolation",
         "zhenghe", "line_buffer_demosaic", "mipi_to_raw_converter"]


def norm(s):
    return re.sub(r"\s+", "", s)


def strip_comments(repo, data: bytes) -> str:
    return subprocess.run(
        ["python3", str(repo / "pl/sim/equiv/strip_comments.py")],
        input=data, check=True, capture_output=True).stdout.decode("utf-8", errors="replace")


def strip_line_comments(txt: str) -> str:
    return re.sub(r"//[^\n]*", "", txt)


def parse(txt):
    m = re.search(r"module\s+(\w+)\s*(#\s*\()?", txt)
    name = m.group(1)
    # 头部：module 名之后到第一个端口表结束的 ');'（参数表里的 ')' 不干扰：取最后一个 ');'）
    start = m.end()
    end = txt.index(");", start)
    head = txt[start:end]
    params = [(norm(w or ""), p, norm(d))
              for w, p, d in re.findall(r"parameter\s+(\[[^\]]*\])?\s*(\w+)\s*=\s*([^,\n)]+)", head)]
    # 端口表：参数表之后（第一个 ')' 之后）到 end
    ppos = head.find(") (")
    if ppos < 0:
        ppos = head.find(")(")
    ptext = head[ppos:] if ppos >= 0 else head
    ports = [(d, norm(w or ""), p)
             for d, w, p in re.findall(
                 r"(input|output|inout)\s+(?:wire\s+|reg\s+|logic\s+)?(\[[^\]]*\])?\s*(\w+)", ptext)]
    return name, params, ports


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default="")
    ap.add_argument("--rev", default="")
    args = ap.parse_args()
    if args.repo:
        repo = Path(args.repo)
    else:
        p = Path(__file__).resolve()
        if p.parent.name != "audit":
            raise SystemExit("cannot locate repo root: run from pl/sim/equiv/audit/ or pass --repo")
        repo = p.parents[4]
    rev = args.rev
    if not rev:
        for line in (repo / "pl/sim/equiv/baseline.rev").read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                rev = line
                break
    bad = 0
    for f in FILES:
        raw = subprocess.run(["git", "-C", str(repo), "show", f"{rev}:{D}/{f}.v"],
                             check=True, capture_output=True).stdout
        g = parse(strip_comments(repo, raw))
        n = parse(strip_line_comments((repo / D / f"{f}.v").read_text(encoding="utf-8")))
        ok = g == n
        if ok:
            print(f"PASS {f}.v: 模块名/参数 {len(g[1])} 项/端口 {len(g[2])} 项完全一致")
        else:
            bad += 1
            print(f"FAIL {f}.v")
            for i, label in enumerate(("模块名", "参数", "端口")):
                if g[i] != n[i]:
                    print(f"  {label} golden:", g[i])
                    print(f"  {label} new   :", n[i])
    print("PASS 六个模块接口（名/参数默认值/端口名·方向·位宽·顺序）全部不变" if bad == 0
          else f"FAIL {bad} 个文件接口不一致")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
