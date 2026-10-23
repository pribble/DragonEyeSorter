#!/usr/bin/env python3
"""system_top.v 结构等价审计（verifier-platform 交付物，只读）。

对 golden 版（git rev，默认 baseline.rev）与工作区版 system_top.v 提取结构化事实并比对：
  (a) 顶层模块端口：名 / 方向 / 位宽 / 顺序
  (b) 参数声明：名 / 默认值
  (c) 例化：模块名 / 实例名 / 参数覆盖 / 每个端口连接表达式
  (d) 内部逻辑：assign 语句、always 块（敏感表 + 体，空白/注释归一化）
  (e) 声明：wire/reg 逐信号（名/类型/位宽/初值）

分级：
  HARD（硬差异，必须人审）= 端口/参数不同；例化增删；端口连接表达式变化/新增；
         assign/always 文本变化；声明位宽/类型/初值变化；删掉的声明信号仍被引用。
  SOFT（等价清理，记 INFO）= 例化端口连接被删除，且原表达式为
        - 空连接（()），或
        - 一个 bit 数常量且该端口是输入 → 视为「悬空/未用清理」，仍报告供人确认；
        - 一个裸信号名，该信号在 golden 中除声明与该连接外无其它引用（死网）。
        端口连接被**改成常量/空连接**时，若 golden 侧连接的是同样的死网（判据同上），
        也记 SOFT-DEADNET（死网从未驱动/观察，等价清理）；新版用参数组成的常量表达式
        （如 {AXI_ADDR_WIDTH{1'b0}}）按常量处理。
  其余删除/变化按 HARD 处理。
退出码：0 = 仅 SOFT 差异或无差异；1 = 有 HARD 差异。

用法：
  python3 pl/sim/equiv/audit/audit_system_top.py [--rev REV] [--json out.json] [--file path]
"""
import argparse
import json
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

from audit_paths import find_repo_root

REPO_DEFAULT = str(find_repo_root())
TARGET = "pl/soc_base/src/system_top.v"

TOKEN_RE = re.compile(
    r"""(?P<str>"(?:\\.|[^"\\])*")               # 字符串
      | (?P<num>\d*'[sS]?[bBoOdDhH][0-9a-fA-F_]+ | \d[\d_]*)   # 数字（含 10'b1 等）
      | (?P<id>[A-Za-z_][A-Za-z0-9_$]*)          # 标识符
      | (?P<op>\[|\]|\(|\)|\{|\}|\.|,|;|:|@|\#|=|<>|<=|>=|==|!=|&&|\|\||[+\-*/%&|^~!<>?])
      | (?P<ws>\s+)
      | (?P<other>.)
    """, re.VERBOSE)


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


def tokenize(text: str):
    text = strip_comments(text.replace("\r\n", "\n").replace("\r", "\n"))
    toks = []
    for m in TOKEN_RE.finditer(text):
        if m.lastgroup == "ws":
            continue
        toks.append((m.lastgroup, m.group()))
    return toks


def tok_text(toks):
    out, prev = [], None
    for kind, val in toks:
        if kind == "op" and val not in (".",):
            if out and out[-1] not in ("(", "[", "{", "") and out[-1][-1:] not in "([{.":
                if val not in (")", "]", "}", ",", ";"):
                    out.append(" ")
            out.append(val)
            if val not in ("(", "[", "{"):
                out.append(" ")
        elif kind == "op" and val == ".":
            out.append(". ")
        else:
            if out and out[-1] and (out[-1][-1].isalnum() or out[-1][-1] in "_'\")"):
                out.append(" ")
            out.append(val)
    s = re.sub(r"\s+", " ", "".join(out)).strip()
    s = re.sub(r"\s+([,;\)\]\}])", r"\1", s)
    s = re.sub(r"([\(\[\{])\s+", r"\1", s)
    return s


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
    raise ValueError("unbalanced brackets")


def canon_width(w):
    """位宽规范化：能算成常数的 [hi:lo] 统一成整数形式（[128-1:0] == [127:0]）。"""
    if w is None:
        return None
    s = w.strip()
    m = re.fullmatch(r"\[\s*(.+?)\s*:\s*(.+?)\s*\]", s)
    if not m:
        return re.sub(r"\s+", "", s)

    def ev(expr):
        e = re.sub(r"\s+", "", expr)
        if not re.fullmatch(r"[0-9+\-*/()]+", e):
            return None
        try:
            v = eval(e, {"__builtins__": {}}, {})  # 仅数字与四则运算
            return int(v)
        except Exception:
            return None

    hi, lo = ev(m.group(1)), ev(m.group(2))
    if hi is not None and lo is not None:
        return f"[{hi}:{lo}]"
    return re.sub(r"\s+", "", s)


def parse_ports(toks):
    ports = []
    for item in split_top(toks):
        direction, i = None, 0
        if i < len(item) and item[i][0] == "id" and item[i][1] in ("input", "output", "inout"):
            direction = item[i][1]
            i += 1
        attr = []
        while i < len(item) and item[i][0] == "op" and item[i][1] == "(":  # (* attr *)
            pass
        decl = []
        while i < len(item) and item[i][0] == "id" and item[i][1] in (
                "wire", "reg", "logic", "signed", "unsigned", "tri"):
            decl.append(item[i][1])
            i += 1
        width = None
        if i < len(item) and item[i][0] == "op" and item[i][1] == "[":
            j = find_matching(item, i)
            width = canon_width(tok_text(item[i:j + 1]))
            i = j + 1
        name = item[i][1] if i < len(item) and item[i][0] == "id" else None
        if name is not None:
            i += 1
        rest = tok_text(item[i:])
        ports.append({"dir": direction, "decl": " ".join(decl), "width": width,
                      "name": name, "rest": rest})
    return ports


def parse_params(toks):
    params = []
    for item in split_top(toks):
        kw, i = None, 0
        if item and item[0][0] == "id" and item[0][1] in ("parameter", "localparam"):
            kw = item[0][1]
            i = 1
        while i < len(item) and item[i][0] == "id" and item[i][1] in (
                "integer", "int", "signed", "unsigned"):
            i += 1
        width = None
        if i < len(item) and item[i][0] == "op" and item[i][1] == "[":
            j = find_matching(item, i)
            width = canon_width(tok_text(item[i:j + 1]))
            i = j + 1
        name = item[i][1] if i < len(item) and item[i][0] == "id" else None
        i += 1
        expr = None
        if i < len(item) and item[i][0] == "op" and item[i][1] == "=":
            expr = tok_text(item[i + 1:])
        params.append({"kw": kw, "name": name, "width": width, "expr": expr,
                       "text": tok_text(item)})
    return params


def parse_conns(toks):
    conns, positional = [], False
    for item in split_top(toks):
        if item and item[0][0] == "op" and item[0][1] == ".":
            name = item[1][1] if len(item) > 1 and item[1][0] == "id" else "?"
            if len(item) > 2 and item[2][0] == "op" and item[2][1] == "(":
                j = find_matching(item, 2)
                expr = tok_text(item[3:j])
            else:
                expr = ""
            conns.append((name, expr))
        else:
            positional = True
            conns.append((None, tok_text(item)))
    return conns, positional


def parse_module(toks):
    i, n = 0, len(toks)
    while i < n and not (toks[i][0] == "id" and toks[i][1] == "module"):
        i += 1
    if i >= n:
        raise ValueError("no module found")
    i += 1
    mod_name = toks[i][1]
    i += 1
    params = []
    if toks[i][0] == "op" and toks[i][1] == "#":
        i += 1
        j = find_matching(toks, i)
        params = parse_params(toks[i + 1:j])
        i = j + 1
    ports = []
    if toks[i][0] == "op" and toks[i][1] == "(":
        j = find_matching(toks, i)
        ports = parse_ports(toks[i + 1:j])
        i = j + 1
    while i < n and not (toks[i][0] == "op" and toks[i][1] == ";"):
        i += 1
    i += 1
    body = toks[i:]
    for k in range(len(body) - 1, -1, -1):
        if body[k][0] == "id" and body[k][1] == "endmodule":
            body = body[:k]
            break
    return {"name": mod_name, "params": params, "ports": ports, "body": body}


def parse_decl_stmt(toks):
    """wire/reg/logic/integer 声明 -> {name: (kind, width, init)}；localparam/parameter 不算信号。"""
    stmt = toks
    kind = None
    i = 0
    if stmt and stmt[0][0] == "id" and stmt[0][1] in ("wire", "reg", "logic", "integer", "genvar"):
        kind = stmt[0][1]
        i = 1
    # 去掉 signed/unsigned
    while i < len(stmt) and stmt[i][0] == "op" and stmt[i][1] == "(":  # 属性
        break
    while i < len(stmt) and stmt[i][0] == "id" and stmt[i][1] in ("signed", "unsigned"):
        i += 1
    width = None
    if i < len(stmt) and stmt[i][0] == "op" and stmt[i][1] == "[":
        try:
            j = find_matching(stmt, i)
        except ValueError:
            return {}
        width = canon_width(tok_text(stmt[i:j + 1]))
        i = j + 1
    # 剩余为 name [= init] , name [= init]
    rest = stmt[i:]
    # 去掉末尾 ;
    if rest and rest[-1][0] == "op" and rest[-1][1] == ";":
        rest = rest[:-1]
    out = {}
    for item in split_top(rest):
        name = item[0][1] if item and item[0][0] == "id" else None
        if not name:
            continue
        init = None
        if len(item) > 1 and item[1][0] == "op" and item[1][1] == "=":
            init = tok_text(item[2:])
        out[name] = {"kind": kind, "width": width, "init": init}
    return out


def collect_body(toks):
    items = []
    i, n = 0, len(toks)
    while i < n:
        kind, val = toks[i]
        if kind == "id" and val in ("always", "initial"):
            kw = val
            j = i + 1
            sens = []
            if j < n and toks[j][0] == "op" and toks[j][1] == "@":
                j += 1
                if j < n and toks[j][0] == "op" and toks[j][1] == "(":
                    k = find_matching(toks, j)
                    sens = toks[j:k + 1]
                    j = k + 1
                elif j < n and toks[j][0] == "op" and toks[j][1] == "*":
                    sens = [(None, "*")]
                    j += 1
            if j < n and toks[j][0] == "id" and toks[j][1] == "begin":
                depth, k = 0, j
                while k < n:
                    if toks[k][0] == "id" and toks[k][1] == "begin":
                        depth += 1
                    elif toks[k][0] == "id" and toks[k][1] == "end":
                        depth -= 1
                        if depth == 0:
                            k += 1
                            break
                    k += 1
                body_toks = toks[j:k]
                i = k
            else:
                k = j
                while k < n and not (toks[k][0] == "op" and toks[k][1] == ";"):
                    k += 1
                body_toks = toks[j:k + 1]
                i = k + 1
            items.append(("always", kw, tok_text(sens), tok_text(body_toks)))
        elif kind == "id" and val == "assign":
            k = i + 1
            while k < n and not (toks[k][0] == "op" and toks[k][1] == ";"):
                k += 1
            items.append(("assign", tok_text(toks[i + 1:k])))
            i = k + 1
        elif kind == "id" and val in ("localparam", "parameter"):
            k = i
            while k < n and not (toks[k][0] == "op" and toks[k][1] == ";"):
                k += 1
            items.append(("param", tok_text(toks[i:k + 1])))
            i = k + 1
        elif kind == "id" and val in ("genvar", "integer", "reg", "wire", "logic"):
            k = i
            while k < n and not (toks[k][0] == "op" and toks[k][1] == ";"):
                k += 1
            items.append(("decl", parse_decl_stmt(toks[i:k + 1]), tok_text(toks[i:k + 1])))
            i = k + 1
        elif kind == "id" and val == "generate":
            depth, k = 0, i
            while k < n:
                if toks[k][0] == "id" and toks[k][1] == "generate":
                    depth += 1
                elif toks[k][0] == "id" and toks[k][1] == "endgenerate":
                    depth -= 1
                    if depth == 0:
                        k += 1
                        break
                k += 1
            items.append(("generate", tok_text(toks[i:k])))
            i = k
        elif kind == "id" and i + 1 < n and (toks[i + 1][0] == "id" or
                                            (toks[i + 1][0] == "op" and toks[i + 1][1] == "#")):
            k = i + 1
            mod_name = val
            params = []
            if toks[k][0] == "op" and toks[k][1] == "#":
                k += 1
                j = find_matching(toks, k)
                params = parse_params(toks[k + 1:j])
                k = j + 1
            insts = []
            while True:
                inst_name = toks[k][1] if toks[k][0] == "id" else None
                k += 1
                if not (k < n and toks[k][0] == "op" and toks[k][1] == "("):
                    break
                j = find_matching(toks, k)
                conns, positional = parse_conns(toks[k + 1:j])
                insts.append({"module": mod_name, "inst": inst_name,
                              "params": params, "conns": conns, "positional": positional})
                k = j + 1
                if k < n and toks[k][0] == "op" and toks[k][1] == ",":
                    k += 1
                    continue
                break
            if k < n and toks[k][0] == "op" and toks[k][1] == ";":
                k += 1
            for inst in insts:
                items.append(("inst", inst))
            i = k
        else:
            k = i
            while k < n and not (toks[k][0] == "op" and toks[k][1] == ";"):
                k += 1
            items.append(("other", tok_text(toks[i:k + 1])))
            i = k + 1
    return items


class Facts:
    def __init__(self, mod):
        self.ports = [f"PORT {i:02d} {p['dir']} {p['decl']} {p['width']} {p['name']} {p['rest']}"
                      for i, p in enumerate(mod["ports"])]
        self.params = [f"PARAM {p['kw']} {p['width']} {p['name']} = {p['expr']}"
                       for p in mod["params"]]
        self.assigns, self.always, self.others, self.gen = [], [], [], []
        self.insts = []
        self.signals = {}
        self.decl_lines = []
        for item in collect_body(mod["body"]):
            if item[0] == "inst":
                self.insts.append(item[1])
            elif item[0] == "assign":
                self.assigns.append(item[1])
            elif item[0] == "always":
                _, kw, sens, body = item
                self.always.append(f"{kw} @ {sens} :: {body}")
            elif item[0] == "decl":
                _, sigmap, text = item
                self.decl_lines.append(text)
                self.signals.update(sigmap)
            elif item[0] == "generate":
                self.gen.append(item[1])
            elif item[0] == "param":
                self.params.append(item[1])
            else:
                self.others.append(item[1])


def diff_mset(name, gold, new):
    cg, cn = Counter(gold), Counter(new)
    removed = list((cg - cn).elements())
    added = list((cn - cg).elements())
    if removed or added:
        print(f"[{name}] 差异: golden_only={len(removed)} new_only={len(added)}")
        for r in removed:
            print(f"  GOLDEN ONLY: {r}")
        for a in added:
            print(f"  NEW ONLY:    {a}")
    return removed, added


def is_const_expr(expr):
    return bool(expr) and re.fullmatch(r"[0-9]+'[sS]?[bBoOdDhH][0-9a-fA-F_]+|[0-9]+", expr) is not None


def is_dead_net_expr(expr, use_count):
    """golden 侧裸信号名，且全文件出现 <= 3 次（声明 1 + 端口名 1 + 连接 1）→ 死网。"""
    return (bool(re.fullmatch(r"[A-Za-z_][A-Za-z0-9_$]*", expr))
            and use_count.get(expr, 0) <= 3)


def is_const_conn(expr, param_ids):
    """新版连接表达式是否只由常量与参数组成（没有真实信号引用）；空连接也算常量。"""
    if expr == "":
        return True
    return {v for k, v in tokenize(expr) if k == "id"} <= param_ids


def classify_dropped_conn(expr, use_count, is_input=True):
    if expr == "":
        return "SOFT-EMPTY", "空连接删除（原来就是 ()）"
    if is_const_expr(expr):
        return ("HARD", "常量输入连接被删除（引脚将悬空）") if is_input else \
               ("SOFT-CONST", "常量连接删除（输出无观察者）")
    if is_dead_net_expr(expr, use_count):
        return "SOFT-DEADNET", "悬空/未用网删除（golden 中仅声明+该连接出现）"
    return "HARD", "删除了仍在使用/有驱动的连接"


def instance_diff(gold_insts, new_insts, use_count, param_ids):
    hard = 0
    soft = []
    gmap = {(i["module"], i["inst"]): i for i in gold_insts}
    nmap = {(i["module"], i["inst"]): i for i in new_insts}
    for key in sorted(set(gmap) - set(nmap)):
        print(f"  INST REMOVED: {key}")
        hard += 1
    for key in sorted(set(nmap) - set(gmap)):
        print(f"  INST ADDED:   {key}")
        hard += 1
    for key in sorted(set(gmap) & set(nmap)):
        gi, ni = gmap[key], nmap[key]
        gp = " ".join(p["text"] for p in gi["params"])
        np_ = " ".join(p["text"] for p in ni["params"])
        if gp != np_:
            print(f"  INST {key[0]}/{key[1]} PARAMS: golden[{gp}] new[{np_}]")
            hard += 1
        gconn = dict(gi["conns"])
        nconn = dict(ni["conns"])
        for port in sorted(set(gconn) - set(nconn)):
            expr = gconn[port]
            kind, why = classify_dropped_conn(expr, use_count)
            print(f"  INST {key[0]}/{key[1]} PORT DROP: .{port}({expr}) -> {kind}: {why}")
            if kind.startswith("SOFT"):
                soft.append((f"{key[0]}/{key[1]}", port, expr, kind))
            else:
                hard += 1
        for port in sorted(set(nconn) - set(gconn)):
            print(f"  INST {key[0]}/{key[1]} PORT ADD:  .{port}({nconn[port]})")
            hard += 1
        for port in sorted(set(gconn) & set(nconn)):
            gexpr, nexpr = gconn[port], nconn[port]
            if gexpr == nexpr:
                continue
            if is_dead_net_expr(gexpr, use_count) and is_const_conn(nexpr, param_ids):
                why = "死网连接改为常量/空连接（golden 中仅声明+该连接）"
                print(f"  INST {key[0]}/{key[1]} PORT CHANGE: .{port} "
                      f"golden({gexpr}) new({nexpr}) -> SOFT-DEADNET: {why}")
                soft.append((f"{key[0]}/{key[1]}", port, gexpr, "SOFT-DEADNET"))
            else:
                print(f"  INST {key[0]}/{key[1]} PORT CHANGE: .{port} "
                      f"golden({gexpr}) new({nexpr})")
                hard += 1
    return hard, soft


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("repo", nargs="?", default=REPO_DEFAULT)
    ap.add_argument("--rev", default=None)
    ap.add_argument("--json", default=None)
    ap.add_argument("--file", default=TARGET)
    ap.add_argument("--golden-file", default=None,
                    help="直接用该文件当 golden（自检用：同一份文件应判 EQUIVALENT）")
    args = ap.parse_args()
    repo = Path(args.repo)
    rev = args.rev
    if not rev:
        base = repo / "pl/sim/equiv/baseline.rev"
        rev = [l.strip() for l in base.read_text().splitlines()
               if l.strip() and not l.strip().startswith("#")][0]
    target = args.file
    if args.golden_file:
        gtext = Path(args.golden_file).read_text(encoding="utf-8", errors="replace")
    else:
        gtext = subprocess.run(["git", "-C", str(repo), "show", f"{rev}:{target}"],
                               capture_output=True, check=True).stdout.decode("utf-8", "replace")
    ntext = (repo / target).read_text(encoding="utf-8", errors="replace")
    gmod, nmod = parse_module(tokenize(gtext)), parse_module(tokenize(ntext))
    gf, nf = Facts(gmod), Facts(nmod)

    # 端口/参数严格（含顺序）：先做多重集 diff 再检查顺序
    hard = 0
    gp_only, gn_only = diff_mset("ports", gf.ports, nf.ports)
    pp_only, pn_only = diff_mset("params", gf.params, nf.params)
    hard += len(gp_only) + len(gn_only) + len(pp_only) + len(pn_only)
    a_only, a_new = diff_mset("assigns", gf.assigns, nf.assigns)
    hard += len(a_only) + len(a_new)
    w_only, w_new = diff_mset("always", gf.always, nf.always)
    hard += len(w_only) + len(w_new)
    o_only, o_new = diff_mset("other", gf.others, nf.others)
    hard += len(o_only) + len(o_new)
    g_only, g_new = diff_mset("generate", gf.gen, nf.gen)
    hard += len(g_only) + len(g_new)

    # 声明：逐信号
    gs, ns = gf.signals, nf.signals
    removed = sorted(set(gs) - set(ns))
    added = sorted(set(ns) - set(gs))
    changed = []
    for nm in sorted(set(gs) & set(ns)):
        if gs[nm] != ns[nm]:
            changed.append((nm, gs[nm], ns[nm]))
    ntok_id = {v for k, v in tokenize(ntext) if k == "id"}
    decl_removed_used = [nm for nm in removed if nm in ntok_id]
    if removed or added or changed:
        print(f"[decls] 逐信号差异: removed={len(removed)} added={len(added)} changed={len(changed)}")
        for nm in removed:
            kind = "HARD（仍被引用）" if nm in ntok_id else "SOFT（死信号）"
            print(f"  DECL REMOVED {nm} golden={gs[nm]} -> {kind}")
        for nm in added:
            print(f"  DECL ADDED   {nm} = {ns[nm]}")
        for nm, g, n in changed:
            print(f"  DECL CHANGED {nm}: golden={g} new={n}")
    hard += len(decl_removed_used) + len(changed)

    # 例化细粒度：golden 里每个标识符的 token 出现次数（用于死网判定：声明+该连接=2）
    gtok = tokenize(gtext)
    gsig_uses = Counter(v for k, v in gtok if k == "id")
    gparam_ids = {p["name"] for p in gmod["params"]}
    # 模块体里也有 parameter/localparam（parse_module 只收模块头），常量连接判定要一并算参数
    gparam_ids |= set(re.findall(
        r"\b(?:parameter|localparam)\s+(?:integer\s+|signed\s+|\[[^\]]*\]\s*)*([A-Za-z_]\w*)\s*=",
        gtext))
    ihard, isoft = instance_diff(gf.insts, nf.insts, gsig_uses, gparam_ids)
    hard += ihard

    verdict = "STRUCT_AUDIT EQUIVALENT" if hard == 0 else "STRUCT_AUDIT DIFF"
    print()
    print(f"golden={rev}:{target}")
    print(f"ports={len(gf.ports)} params={len(gf.params)} insts={len(gf.insts)} "
          f"assigns={len(gf.assigns)} always={len(gf.always)} decl_signals={len(gs)}")
    print(f"hard_diff_items={hard} soft_items={len(isoft)}")
    print(verdict)
    if args.json:
        out = {"rev": rev, "file": target, "verdict": verdict, "hard_diff_items": hard,
               "ports_golden": gf.ports, "ports_new": nf.ports,
               "params_golden": gf.params, "params_new": nf.params,
               "assigns_golden": gf.assigns, "assigns_new": nf.assigns,
               "always_golden": gf.always, "always_new": nf.always,
               "insts_golden": [f"{i['module']} {i['inst']}" for i in gf.insts],
               "insts_new": [f"{i['module']} {i['inst']}" for i in nf.insts],
               "decls_removed": removed, "decls_added": added,
               "decls_changed": [list(c) for c in changed],
               "decls_removed_used": decl_removed_used,
               "soft_items": isoft}
        Path(args.json).write_text(json.dumps(out, ensure_ascii=False, indent=1), encoding="utf-8")
        print(f"(json -> {args.json})")
    sys.exit(0 if hard == 0 else 1)


if __name__ == "__main__":
    main()
