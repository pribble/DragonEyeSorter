#!/usr/bin/env python3
"""verifier-isp 端口/参数契约比对：golden rev vs 工作区（只读，不做任何修改）。

比对 ui_isp 域每个 module 的：
  - parameter 名 / 类型或位宽 / 默认值 / 顺序
  - 端口方向 / 名 / packed 位宽表达式 / 顺序
  - module 名集合（检测删除/新增）
归一化：去注释、去多余空白、位宽表达式内去空白、数字字面量按值归一（9'd306 == 306 == 32'h13a）。
允许的唯一例外（Lead 批准）：gamma 的参数 GAMMA_10x 删除（isp_top 里对应连接也删，不属 module 头）。
另允许 8 个未用 LUT 模块被删除（lut_0_8/1_2/1_4/1_6/1_8/2_0/2_4/2_6），其余删除/改名/端口变化一律 FAIL。

用法：python3 pl/sim/equiv/check_contracts.py [--rev <git-rev>] [--quiet]
退出码：0 = 全部契约一致（允许例外记 INFO）；1 = 有违反。
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent.parent
GOLDEN_DIR = "pl/soc_base/src/ui_isp"

ALLOWED_REMOVED_MODULES = {
    "lut_0_8", "lut_1_2", "lut_1_4", "lut_1_6", "lut_1_8", "lut_2_0", "lut_2_4", "lut_2_6",
}
ALLOWED_REMOVED_PARAMS = {
    "gamma": {"GAMMA_10x"},
}


def git_show(rev, path):
    p = subprocess.run(["git", "-C", str(REPO), "show", f"{rev}:{path}"],
                       capture_output=True)
    return p.stdout if p.returncode == 0 else None


def git_ls(rev, dirpath):
    p = subprocess.run(["git", "-C", str(REPO), "ls-tree", "-r", "--name-only", rev, "--", dirpath],
                       capture_output=True, text=True)
    return [ln for ln in p.stdout.splitlines() if ln.endswith((".v", ".sv", ".vh"))]


def decode(data):
    for enc in ("utf-8", "gb18030"):
        try:
            return data.decode(enc)
        except UnicodeDecodeError:
            pass
    return data.decode("utf-8", errors="replace")


def strip_comments(text):
    """与 pl/sim/equiv/strip_comments.py 同口径（非嵌套块注释、保留换行）。"""
    out = []
    i, n = 0, len(text)
    state = 0
    while i < n:
        c = text[i]
        if state == 0:
            if text.startswith("//", i):
                state = 1
                i += 2
            elif text.startswith("/*", i):
                state = 2
                i += 2
            else:
                out.append(c)
                i += 1
        elif state == 1:
            if c == "\n":
                out.append(c)
                state = 0
            i += 1
        else:
            if text.startswith("*/", i):
                state = 0
                i += 2
            else:
                if c == "\n":
                    out.append(c)
                i += 1
    return "".join(out)


def find_matching_paren(s, start):
    """s[start] == '(' ; return index of matching ')' or -1."""
    depth = 0
    i = start
    while i < len(s):
        c = s[i]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def split_top(s):
    """split by top-level commas (ignoring (), [], {})."""
    parts, cur = [], []
    d = 0
    for c in s:
        if c in "([{":
            d += 1
            cur.append(c)
        elif c in ")]}":
            d -= 1
            cur.append(c)
        elif c == "," and d == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(c)
    if "".join(cur).strip():
        parts.append("".join(cur))
    return parts


WS_RE = re.compile(r"\s+")


def norm_expr(x):
    """normalise a width expression / literal: drop whitespace, canonicalise numeric literals."""
    x = WS_RE.sub("", x.strip())
    if not x:
        return x
    # sized/unsized numeric literal -> integer value when representable
    m = re.fullmatch(r"(?:\d+)?'([sS])?([hHdDoObB])([0-9a-fA-FxXzZ_]+)", x)
    if m:
        base = {"h": 16, "d": 10, "o": 8, "b": 2}[m.group(2).lower()]
        try:
            val = int(m.group(3).replace("_", ""), base)
            return str(val)
        except ValueError:
            return x.lower().replace("_", "")
    if re.fullmatch(r"[0-9_]+", x):
        return str(int(x.replace("_", "")))
    return x


def parse_modules(text, filename):
    """return list of dicts: name, params: [(type, name, default, raw)], ports: [(dir, name, width)]"""
    text = strip_comments(text)
    mods = []
    for m in re.finditer(r"\bmodule\s+([A-Za-z_]\w*)", text):
        name = m.group(1)
        i = m.end()
        params = []
        # optional parameter port list
        j = i
        while j < len(text) and text[j] in " \t\r\n":
            j += 1
        if j < len(text) and text[j] == "#":
            j += 1
            while j < len(text) and text[j] in " \t\r\n":
                j += 1
            if j < len(text) and text[j] == "(":
                k = find_matching_paren(text, j)
                if k < 0:
                    raise ValueError(f"{filename}: unbalanced parameter list in module {name}")
                for item in split_top(text[j + 1:k]):
                    it = " ".join(item.split())
                    it = re.sub(r"^parameter\s*", "", it).strip()
                    if not it:
                        continue
                    # type/range part before NAME = default
                    rhs = None
                    if "=" in it:
                        lhs, rhs = it.split("=", 1)
                    else:
                        lhs = it
                    lhs = lhs.strip()
                    # name = last identifier in lhs
                    nm = re.search(r"([A-Za-z_]\w*)\s*$", lhs)
                    if not nm:
                        continue
                    pname = nm.group(1)
                    ptype = lhs[:nm.start()].strip()
                    params.append((norm_expr(ptype), pname, norm_expr(rhs) if rhs is not None else ""))
                i = k + 1
        # optional port list
        j = i
        while j < len(text) and text[j] in " \t\r\n":
            j += 1
        ports = []
        ansi = False
        if j < len(text) and text[j] == "(":
            k = find_matching_paren(text, j)
            if k < 0:
                raise ValueError(f"{filename}: unbalanced port list in module {name}")
            items = split_top(text[j + 1:k])
            cur_dir = None
            for item in items:
                it = " ".join(item.split())
                if not it:
                    continue
                dm = re.match(r"^(input|output|inout)\b(.*)$", it)
                if dm:
                    ansi = True
                    cur_dir = dm.group(1)
                    rest = dm.group(2).strip()
                    rest = re.sub(r"^(wire|reg|logic|tri|signed|unsigned)\b\s*", "", rest).strip()
                    wm = re.match(r"^\[([^\]]*)\]\s*(.*)$", rest)
                    width = "[" + norm_expr(wm.group(1)) + "]" if wm else ""
                    if wm:
                        rest = wm.group(2).strip()
                    # rest may still be "name" or "name = default" (output reg init)
                    nm = re.match(r"^([A-Za-z_]\w*)", rest)
                    pname = nm.group(1) if nm else rest
                    ports.append((cur_dir, pname, width))
                else:
                    if cur_dir and re.fullmatch(r"[A-Za-z_]\w*", it):
                        ports.append((cur_dir, it, ""))
        mods.append({"name": name, "params": params, "ports": ports, "ansi": ansi, "file": filename})
    return mods


def collect(files, loader):
    mods = {}
    dup = []
    for f in files:
        data = loader(f)
        if data is None:
            continue
        try:
            for md in parse_modules(decode(data), f):
                if md["name"] in mods:
                    dup.append((md["name"], mods[md["name"]]["file"], f))
                mods[md["name"]] = md
        except ValueError as e:
            print(f"WARN parse {f}: {e}")
    return mods, dup


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rev", default=None)
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--json-out", default=None)
    ap.add_argument("--new-root", default=None,
                    help="比对用的『新版』根目录（默认=仓库；仅用于脚本自检/离线比对，如 /tmp/...）")
    args = ap.parse_args()

    rev = args.rev
    if not rev:
        br = (HERE / "baseline.rev").read_text(encoding="utf-8")
        rev = [ln.strip() for ln in br.splitlines() if ln.strip() and not ln.strip().startswith("#")][0]

    gold_files = git_ls(rev, GOLDEN_DIR)
    # worktree files under ui_isp (include untracked; deleted files are simply absent)
    if args.new_root:
        wt_dir = Path(args.new_root).resolve() / GOLDEN_DIR
        new_repo = Path(args.new_root).resolve()
    else:
        wt_dir = REPO / GOLDEN_DIR
        new_repo = REPO
    new_files = sorted(str(p.relative_to(new_repo)).replace("\\", "/")
                       for p in wt_dir.rglob("*") if p.suffix in (".v", ".sv", ".vh") and p.is_file())

    gold = {}
    for f in gold_files:
        d = git_show(rev, f)
        if d is not None:
            for md in parse_modules(decode(d), f):
                gold.setdefault(md["name"], md)
    new = {}
    for f in new_files:
        d = (new_repo / f).read_bytes()
        for md in parse_modules(decode(d), f):
            new.setdefault(md["name"], md)

    violations = []
    infos = []
    checked = 0

    # module set
    removed = sorted(set(gold) - set(new))
    added = sorted(set(new) - set(gold))
    for r in removed:
        if r in ALLOWED_REMOVED_MODULES:
            infos.append(f"ALLOWED module removed: {r} (unused LUT, 特化到 2.2)")
        else:
            violations.append(f"module removed (not allowed): {r}  [{gold[r]['file']}]")
    for a in added:
        infos.append(f"INFO module added: {a}  [{new[a]['file']}]")

    # per-module header contract
    for name in sorted(set(gold) & set(new)):
        g, n = gold[name], new[name]
        checked += 1
        # parameters
        gparams = [(t, pn, d) for (t, pn, d) in g["params"]]
        nparams = [(t, pn, d) for (t, pn, d) in n["params"]]
        allowed_drop = ALLOWED_REMOVED_PARAMS.get(name, set())
        gnames = [p[1] for p in gparams]
        nnames = [p[1] for p in nparams]
        for gp in gparams:
            if gp[1] not in nnames:
                if gp[1] in allowed_drop:
                    infos.append(f"ALLOWED param removed: {name}.{gp[1]} (特化)")
                else:
                    violations.append(f"param removed: {name}.{gp[1]} (golden default {gp[2]}) "
                                      f"[{g['file']} -> {n['file']}]")
        for np_ in nparams:
            if np_[1] not in gnames:
                violations.append(f"param added: {name}.{np_[1]} = {np_[2]}  [{n['file']}]")
        for gp in gparams:
            if gp[1] in nnames:
                np_ = nparams[nnames.index(gp[1])]
                if gp[0] != np_[0]:
                    violations.append(f"param type/width changed: {name}.{gp[1]} "
                                      f"{gp[0]!r} -> {np_[0]!r}")
                if gp[2] != np_[2]:
                    violations.append(f"param default changed: {name}.{gp[1]} "
                                      f"{gp[2]!r} -> {np_[2]!r}")
        # parameter ORDER among surviving params
        gsurv = [p[1] for p in gparams if p[1] in nnames or p[1] not in allowed_drop]
        nsurv = [p[1] for p in nparams if p[1] in gnames]
        if gsurv[:len(nsurv)] != nsurv:
            violations.append(f"param order changed: {name} golden {gsurv} -> new {nsurv}")
        # ports
        if len(g["ports"]) != len(n["ports"]):
            violations.append(f"port count changed: {name} {len(g['ports'])} -> {len(n['ports'])}")
        for idx, (gp, np_) in enumerate(zip(g["ports"], n["ports"])):
            if gp != np_:
                violations.append(f"port[{idx}] changed: {name} "
                                  f"{gp} -> {np_}  [{g['file']} -> {n['file']}]")
        if not n["ansi"] and g["ansi"]:
            infos.append(f"INFO {name}: port list style ANSI -> non-ANSI (ports compared by name/width)")

    if not args.quiet:
        print(f"== ui_isp port/parameter contract: golden {rev[:12]} vs worktree ==")
        print(f"files: golden {len(gold_files)}, worktree {len(new_files)}; "
              f"modules: golden {len(gold)}, worktree {len(new)}, compared {checked}")
        for i in infos:
            print("INFO", i)
        for v in violations:
            print("FAIL", v)
        print(f"contract: {checked} modules compared, {len(violations)} violation(s), "
              f"{len(infos)} allowed/info")
        print("CONTRACT_PASS" if not violations else "CONTRACT_FAIL")

    if args.json_out:
        dump = {
            "rev": rev,
            "date_utc": subprocess.run(["date", "-u", "+%Y-%m-%dT%H:%M:%SZ"],
                                       capture_output=True, text=True).stdout.strip(),
            "files_golden": len(gold_files),
            "files_worktree": len(new_files),
            "modules_golden": len(gold),
            "modules_worktree": len(new),
            "modules_compared": checked,
            "checked_modules": sorted(set(gold) & set(new)),
            "violations": violations,
            "info": infos,
            "pass": not violations,
        }
        Path(args.json_out).write_text(json.dumps(dump, ensure_ascii=False, indent=1), encoding="utf-8")
    return 0 if not violations else 1


if __name__ == "__main__":
    sys.exit(main())
