#!/usr/bin/env python3
"""Compare module interfaces (module name / parameter names+defaults / port
direction+width+name+order) between golden rev and worktree for the 8 refactored files.

Handles ANSI grouped port declarations (`input wire a, b, c,`) and GBK bytes in the
golden uihdmi_tx.v (decoded latin-1, only ASCII syntax is compared).
Read-only audit.
"""
import re, subprocess, sys

REV = "9a905cc"
FILES = [
    "pl/soc_base/src/MIPI_V2/mipi_rx/mipi_dphy_rx_over_lvds_wrapper.v",
    "pl/soc_base/src/MIPI_V2/mipi_rx/hs_rx_wrapper.sv",
    "pl/soc_base/src/MIPI_V2/mipi_rx/rx_clk_gen.v",
    "pl/soc_base/src/MIPI_V2/src/csi_unpacket.v",
    "pl/soc_base/src/MIPI_V2/src/design_top_wrapper.v",
    "pl/soc_base/src/hdmi_tx/uihdmi_tx.v",
    "pl/soc_base/src/hdmi_tx/hdmi_phy_warpper.v",
    "pl/soc_base/src/hdmi_tx/lane_lvds_10_1.v",
]

def golden(path):
    out = subprocess.run(["git", "show", f"{REV}:{path}"], capture_output=True, check=True).stdout
    return out.decode("latin-1")

def worktree(path):
    return open(path, "rb").read().decode("latin-1")

def strip_comments(t):
    t = re.sub(r"/\*.*?\*/", "", t, flags=re.S)
    return re.sub(r"//[^\n]*", "", t)

def split_top(body):
    parts, depth, cur = [], 0, ""
    for ch in body:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        parts.append(cur)
    return [" ".join(p.split()) for p in parts if p.strip()]

def find_paren(t, start):
    depth, i = 0, start
    while i < len(t):
        if t[i] == "(":
            depth += 1
        elif t[i] == ")":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise AssertionError("unbalanced paren")

def parse_ports(body):
    """Return [(direction, width, name)] preserving order, from ANSI port list."""
    ports = []
    direction = width = None
    for chunk in split_top(body):
        m = re.match(r"(input|output|inout)\b", chunk)
        if m:
            direction = m.group(1)
            w = re.search(r"\[([^\]]*)\]", chunk)
            width = "".join(w.group(1).split()) if w else "1"
            rest = chunk[m.end():]
            rest = re.sub(r"^\s*(wire|reg)\b", "", rest).strip()
            name = rest.split()[-1] if rest.split() else ""
        else:
            name = chunk.split()[-1]
        ports.append((direction, width, name))
    return ports

def module_interface(text):
    t = strip_comments(text)
    m = re.search(r"\bmodule\s+(\w+)", t)
    assert m, "no module"
    name = m.group(1)
    rest = t[m.end():]
    params = []
    if re.match(r"\s*#\s*\(", rest):
        pstart = rest.index("(")
        pend = find_paren(rest, pstart)
        for chunk in split_top(rest[pstart+1:pend]):
            mm = re.search(r"\bparameter\s+(?:\w+\s+)?(\w+)\s*=\s*(.*)", chunk, re.S)
            if mm:
                params.append((mm.group(1), "".join(mm.group(2).split())))
        rest = rest[pend+1:]
    pstart = rest.index("(")
    pend = find_paren(rest, pstart)
    return name, params, parse_ports(rest[pstart+1:pend])

ok = True
for f in FILES:
    gi = module_interface(golden(f))
    ni = module_interface(worktree(f))
    same = gi == ni
    print(("OK   " if same else "DIFF ") + f)
    if not same:
        ok = False
        if gi[0] != ni[0]:
            print("  module name:", gi[0], "->", ni[0])
        if gi[1] != ni[1]:
            print("  params golden:", gi[1])
            print("  params new   :", ni[1])
        if gi[2] != ni[2]:
            print("  ports golden:", gi[2])
            print("  ports new   :", ni[2])
    else:
        print(f"     params={[p[0] for p in gi[1]]} ports={[(d, w, n) for d, w, n in gi[2]]}" if "--verbose" in sys.argv else
              f"     {len(gi[2])} ports, {len(gi[1])} params identical")
print("ALL_INTERFACES_IDENTICAL" if ok else "INTERFACE_DIFF_FOUND")
sys.exit(0 if ok else 1)
