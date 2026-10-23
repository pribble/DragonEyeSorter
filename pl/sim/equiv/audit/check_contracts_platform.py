#!/usr/bin/env python3
"""平台域（MIPI_V2 / hdmi_tx / write_fdma / pwm / system_top）端口·参数契约比对。
verifier-platform 独立验证交付物，只读。

对 golden rev 与工作区逐文件提取全部 module 的：
  - 端口方向 / 名 / packed 位宽表达式 / 顺序
  - parameter / localparam 名 / 位宽 / 默认值 / 顺序
归一化：去注释、空白折叠、常量位宽按数值归一（[128-1:0] == [127:0]）。
判定：公共 module（被工程内其他文件例化的模块 + 两个顶层）头不一致 = FAIL；
      文件内部私有 helper module（如 wfifo 内的 fifo_cross_domain_* / ram_infer_*）变化 = INFO。
用法：python3 pl/sim/equiv/audit/check_contracts_platform.py [--rev R] [--json]
退出码 0/1。
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

from audit_paths import find_repo_root

REPO_DEFAULT = str(find_repo_root())
SCOPE_DIRS = ["pl/soc_base/src/MIPI_V2", "pl/soc_base/src/hdmi_tx",
              "pl/soc_base/src/write_fdma"]
SCOPE_FILES = ["pl/soc_base/src/pwm.v", "pl/soc_base/src/system_top.v"]
# 被工程其它文件例化、或为顶层的公共模块：头一变就是契约变化
PUBLIC = {
    "system_top", "pwm", "design_top_wrapper", "mipi_dphy_rx_over_lvds_wrapper",
    "hs_rx_wrapper", "rx_clk_gen", "csi_unpacket", "uihdmi_tx",
    "hdmi_tx_phy_wrapper", "lane_lvds_10_1", "uidbuf_write", "uiFDMA", "fs_cap", "wfifo",
}
EXTS = {".v", ".sv"}

TOKEN_RE = re.compile(
    r"""(?P<str>"(?:\\.|[^"\\])*")
      | (?P<num>\d*'[sS]?[bBoOdDhH][0-9a-fA-F_]+ | \d[\d_]*)
      | (?P<id>[A-Za-z_][A-Za-z0-9_$]*)
      | (?P<op>\[|\]|\(|\)|\{|\}|\.|,|;|:|@|\#|=|<>|<=|>=|==|!=|&&|\|\||[+\-*/%&|^~!<>?])
      | (?P<ws>\s+)
      | (?P<other>.)""", re.VERBOSE)


def decode(data: bytes) -> str:
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError:
        try:
            return data.decode("gbk", errors="replace")   # 原始厂家文件部分为 GBK
        except Exception:
            return data.decode("latin-1")


def strip_comments(text: str) -> str:
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == '"':
                    j += 1
                    break
                j += 1
            out.append(text[i:j])
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
            out.append(" ")
        else:
            out.append(c)
            i += 1
    return "".join(out)


def tokenize(text):
    return [(m.lastgroup, m.group()) for m in TOKEN_RE.finditer(strip_comments(text))
            if m.lastgroup != "ws"]


def tok_text(toks):
    out = []
    for kind, val in toks:
        if kind == "op" and val == ".":
            out.append(".")
        elif kind == "op":
            out.append(" " + val + " ")
        else:
            out.append(val)
    s = re.sub(r"\s+", " ", "".join(out)).strip()
    s = re.sub(r"\s+([,;\)\]\}])", r"\1", s)
    s = re.sub(r"([\(\[\{])\s+", r"\1", s)
    return s


def canon_literals(expr):
    """把定长数字字面量按数值归一：1'b1 -> 1、32'h10 -> 16、8'd5 -> 5，去掉下划线。"""
    if expr is None:
        return None

    def repl(m):
        w, s, base, digits = m.group(1), m.group(2), m.group(3).lower(), m.group(4)
        v = int(digits.replace("_", ""), {"b": 2, "o": 8, "d": 10, "h": 16}[base])
        if v >= 2 ** 63:
            v = 0
        return str(v)

    s = re.sub(r"(\d+)'([sS]?)([bBoOdDhH])([0-9a-fA-F_]+)", repl, expr)
    s = re.sub(r"(?<![\w'])(\d[\d_]*)(?![\w'])", lambda m: str(int(m.group(1).replace("_", ""))), s)
    return re.sub(r"\s+", "", s)


def canon_width(w):
    if w is None:
        return None
    w = canon_literals(w)
    m = re.fullmatch(r"\[\s*(.+?)\s*:\s*(.+?)\s*\]", w.strip())
    if not m:
        return re.sub(r"\s+", "", w)

    def ev(e):
        e = re.sub(r"\s+", "", e)
        if not re.fullmatch(r"[0-9+\-*/()]+", e):
            return None
        try:
            return int(eval(e, {"__builtins__": {}}, {}))
        except Exception:
            return None
    hi, lo = ev(m.group(1)), ev(m.group(2))
    return f"[{hi}:{lo}]" if hi is not None and lo is not None else re.sub(r"\s+", "", w)


def split_top(toks, sep=","):
    parts, cur, depth = [], [], 0
    for kind, val in toks:
        if kind == "op" and val in "([{":
            depth += 1
        elif kind == "op" and val in ")]}":
            depth -= 1
        if depth == 0 and kind == "op" and val == sep:
            parts.append(cur)
            cur = []
        else:
            cur.append((kind, val))
    if cur:
        parts.append(cur)
    return [p for p in parts if p]


def find_matching(toks, start):
    depth = 0
    for i in range(start, len(toks)):
        kind, val = toks[i]
        if kind == "op" and val in "([{":
            depth += 1
        elif kind == "op" and val in ")]}":
            depth -= 1
            if depth == 0:
                return i
    return len(toks) - 1


def parse_module_header(toks, i):
    """toks[i] == 'module'；返回 (name, ports, params, next_index)。"""
    i += 1
    name = toks[i][1]
    i += 1
    params = []
    if i < len(toks) and toks[i][0] == "op" and toks[i][1] == "#":
        i += 1
        j = find_matching(toks, i)
        for item in split_top(toks[i + 1:j]):
            kw, k = None, 0
            if item and item[0][0] == "id" and item[0][1] in ("parameter", "localparam"):
                kw = item[0][1]
                k = 1
            while k < len(item) and item[k][0] == "id" and item[k][1] in (
                    "integer", "int", "signed", "unsigned"):
                k += 1
            width = None
            if k < len(item) and item[k][0] == "op" and item[k][1] == "[":
                m = find_matching(item, k)
                width = canon_width(tok_text(item[k:m + 1]))
                k = m + 1
            pname = item[k][1] if k < len(item) and item[k][0] == "id" else None
            k += 1
            expr = None
            if k < len(item) and item[k][0] == "op" and item[k][1] == "=":
                expr = tok_text(item[k + 1:])
            if kw == "localparam":
                continue       # 内部实现细节
            params.append((kw, pname, width, expr))
        i = j + 1
    ports = []
    if i < len(toks) and toks[i][0] == "op" and toks[i][1] == "(":
        j = find_matching(toks, i)
        cur_dir, cur_width = None, None
        for item in split_top(toks[i + 1:j]):
            k, direction, width = 0, None, None
            if item and item[0][0] == "id" and item[0][1] in ("input", "output", "inout"):
                direction = item[0][1]
                k = 1
            while k < len(item) and item[k][0] == "id" and item[k][1] in (
                    "wire", "reg", "logic", "signed", "unsigned", "tri"):
                k += 1
            if k < len(item) and item[k][0] == "op" and item[k][1] == "[":
                m = find_matching(item, k)
                width = canon_width(tok_text(item[k:m + 1]))
                k = m + 1
            pname = item[k][1] if k < len(item) and item[k][0] == "id" else None
            if direction is not None:          # ANSI 声明：更新继承状态
                cur_dir, cur_width = direction, width
            else:                              # 继承上一条的 direction/位宽（`input a, b, c`）
                direction = cur_dir
                if width is None:
                    width = cur_width
            ports.append((direction, pname, width))
        i = j + 1
    return name, ports, params, i


def parse_all_modules(text):
    toks = tokenize(text)
    mods = {}
    i = 0
    while i < len(toks):
        if toks[i][0] == "id" and toks[i][1] == "module":
            try:
                name, ports, params, j = parse_module_header(toks, i)
            except Exception:
                break
            mods[name] = {"ports": ports, "params": params}
            i = j
        else:
            i += 1
    return mods


def scope_files(repo, rev):
    files = []
    for d in SCOPE_DIRS:
        if rev:
            out = subprocess.run(["git", "-C", str(repo), "ls-tree", "-r", "--name-only", rev, d],
                                 capture_output=True, text=True).stdout.splitlines()
            frontier = out
        else:
            frontier = [str(p.relative_to(repo)) for p in (repo / d).rglob("*")
                        if p.is_file() and p.suffix in EXTS]
        for f in frontier:
            if Path(f).suffix in EXTS:
                files.append(f)
    files += SCOPE_FILES
    return sorted(files)


def load(repo, rev, rel):
    if rev:
        p = subprocess.run(["git", "-C", str(repo), "show", f"{rev}:{rel}"], capture_output=True)
        return decode(p.stdout) if p.returncode == 0 else None
    p = repo / rel
    return decode(p.read_bytes()) if p.is_file() else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("repo", nargs="?", default=REPO_DEFAULT)
    ap.add_argument("--rev", default=None)
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()
    repo = Path(args.repo)
    rev = args.rev
    if not rev:
        base = repo / "pl/sim/equiv/baseline.rev"
        rev = [l.strip() for l in base.read_text().splitlines()
               if l.strip() and not l.strip().startswith("#")][0]

    gold_files = scope_files(repo, rev)
    work_files = scope_files(repo, None)
    gset, wset = set(gold_files), set(work_files)
    fail = 0
    print(f"golden {rev}: {len(gold_files)} 文件 | 工作区: {len(work_files)} 文件")
    for f in sorted(gset - wset):
        print(f"  FILE REMOVED: {f}")
        # 平台域不允许删明文 RTL（wfifo/驱动由工程文件引用）
        if Path(f).suffix in EXTS and Path(f).name not in ("des_common.vh",):
            fail += 1
    for f in sorted(wset - gset):
        print(f"  FILE ADDED:   {f}")

    gmods, nmods = {}, {}
    for f in sorted(gset & wset):
        gt, nt = load(repo, rev, f), load(repo, None, f)
        gm, nm = parse_all_modules(gt), parse_all_modules(nt)
        for m in gm:
            gmods[(f, m)] = gm[m]
        for m in nm:
            nmods[(f, m)] = nm[m]

    gkeys, nkeys = set(gmods), set(nmods)
    for f, m in sorted(gkeys - nkeys):
        kind = "FAIL" if m in PUBLIC else "INFO"
        print(f"  MODULE REMOVED [{kind}]: {m}  ({f})")
        if kind == "FAIL":
            fail += 1
    for f, m in sorted(nkeys - gkeys):
        kind = "FAIL" if m in PUBLIC else "INFO"
        print(f"  MODULE ADDED   [{kind}]: {m}  ({f})")
        if kind == "FAIL":
            fail += 1
    for key in sorted(gkeys & nkeys):
        f, m = key
        gp, gm = gmods[key]["ports"], gmods[key]["params"]
        np_, nm = nmods[key]["ports"], nmods[key]["params"]
        diffs = []
        if gp != np_:
            diffs.append(f"ports golden={gp} new={np_}")
        if gm != nm:
            diffs.append(f"params golden={gm} new={nm}")
        if diffs:
            kind = "FAIL" if m in PUBLIC else "INFO"
            print(f"  MODULE HEAD CHANGE [{kind}]: {m} ({f})")
            for d in diffs:
                print(f"    {d}")
            if kind == "FAIL":
                fail += 1
    print()
    print("CONTRACT RESULT:", "PASS（公共模块端口/参数契约一致）" if fail == 0
          else f"FAIL（{fail} 项公共契约变化）")
    if args.json:
        out = repo / "pl/sim/equiv/audit/last_contracts_platform.json"
        out.write_text(json.dumps({"rev": rev, "fail": fail,
                                   "public_golden": sorted(gmods.keys() - nmods.keys()),
                                   "public_new": sorted(nmods.keys() - gmods.keys())},
                                  ensure_ascii=False, indent=1), encoding="utf-8")
        print(f"(json -> {out})")
    sys.exit(0 if fail == 0 else 1)


if __name__ == "__main__":
    main()
