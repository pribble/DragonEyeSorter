#!/usr/bin/env python3
"""三份 TD 工程文件 vs 磁盘实际文件一致性核对（verifier-platform 交付物，只读）。

核对对象：
  pl/soc_base/soc_prj.al
  pl/soc_base/soc_prj_Runs/syn_1/soc_prj.prj
  pl/soc_base/soc_prj_Runs/phy_1/soc_prj.prj

判定：
  - 每个 <File Path="..."> 解析为工程文件所在目录的相对路径，必须存在且是普通文件
  - 同一工程文件内路径不得重复（规范化后）
  - 三份文件的工程内路径集合必须一致（顺序可以不同，但会报告顺序差异）
  - 与 golden rev 的工程文件对比，报告新增/删除条目（用于 Lead 删 gamma LUT 后复核）

用法：
  python3 pl/sim/equiv/audit/check_prj_files.py [--json] [--rev 9a905cc]
退出码：0 全部一致；1 有缺失/重复/跨文件不一致。
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

from audit_paths import find_repo_root

REPO_DEFAULT = str(find_repo_root())
PROJECTS = {
    "soc_prj.al": "pl/soc_base/soc_prj.al",
    "syn_1/soc_prj.prj": "pl/soc_base/soc_prj_Runs/syn_1/soc_prj.prj",
    "phy_1/soc_prj.prj": "pl/soc_base/soc_prj_Runs/phy_1/soc_prj.prj",
}
PATH_RE = re.compile(r'<File\s+Path="([^"]+)"')


def norm(project_rel: str, raw: str) -> str:
    """把工程文件里的路径规范化成相对仓库根的 posix 路径。"""
    p = Path(project_rel).parent / raw
    parts = []
    for part in p.as_posix().split("/"):
        if part in ("", "."):
            continue
        if part == "..":
            if parts:
                parts.pop()
            else:
                parts.append("..")
        else:
            parts.append(part)
    return "/".join(parts)


def parse_project(repo: Path, project_rel: str, text: str):
    entries = [norm(project_rel, m) for m in PATH_RE.findall(text)]
    return entries


def check_rev(repo: Path, rev: str):
    out = {}
    for name, project_rel in PROJECTS.items():
        data = subprocess.run(["git", "-C", str(repo), "show", f"{rev}:{project_rel}"],
                              capture_output=True, text=True)
        if data.returncode != 0:
            out[name] = None
        else:
            out[name] = [norm(project_rel, m) for m in PATH_RE.findall(data.stdout)]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("repo", nargs="?", default=REPO_DEFAULT)
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--rev", default=None, help="与 golden rev 的工程文件对比增删条目")
    args = ap.parse_args()
    repo = Path(args.repo)
    fail = 0
    report = {"projects": {}, "cross_file_set_equal": None, "cross_file_order_equal": None}

    parsed = {}
    for name, project_rel in PROJECTS.items():
        pf = repo / project_rel
        if not pf.is_file():
            print(f"FAIL {name}: 工程文件不存在 {project_rel}")
            fail += 1
            continue
        text = pf.read_text(encoding="utf-8", errors="replace")
        entries = parse_project(repo, project_rel, text)
        parsed[name] = entries
        missing = [e for e in entries if not (repo / e).is_file()]
        seen, dups = set(), []
        for e in entries:
            if e in seen:
                dups.append(e)
            seen.add(e)
        print(f"===== {project_rel}: {len(entries)} 条 =====")
        print(f"  唯一={len(seen)} 重复={len(dups)} 缺失={len(missing)}")
        for d in dups:
            print(f"  DUP   {d}")
        for m in missing:
            print(f"  MISS  {m} (resolve: {m})")
        report["projects"][name] = {
            "project_file": project_rel, "count": len(entries), "unique": len(seen),
            "duplicates": dups, "missing": missing, "entries": entries,
        }
        if missing or dups:
            fail += 1

    names = list(parsed)
    if len(names) >= 2:
        base = names[0]
        for other in names[1:]:
            a, b = parsed[base], parsed[other]
            if sorted(a) != sorted(b):
                print(f"FAIL 跨文件集合不一致: {base} vs {other}")
                report["cross_file_set_equal"] = False
                fail += 1
                only_a = sorted(set(a) - set(b))
                only_b = sorted(set(b) - set(a))
                for x in only_a[:20]:
                    print(f"  only-in {base}: {x}")
                for x in only_b[:20]:
                    print(f"  only-in {other}: {x}")
            elif a != b:
                print(f"WARN 跨文件集合相同但顺序不同: {base} vs {other}")
                report["cross_file_order_equal"] = False
            else:
                report["cross_file_set_equal"] = True

    if args.rev:
        gold = check_rev(repo, args.rev)
        for name, project_rel in PROJECTS.items():
            g = gold.get(name)
            c = report["projects"].get(name)
            if g is None or c is None:
                continue
            grel = [norm(project_rel, m) for m in PATH_RE.findall(
                subprocess.run(["git", "-C", str(repo), "show", f"{args.rev}:{project_rel}"],
                               capture_output=True, text=True).stdout)]
            cur = c["entries"]
            added = [e for e in cur if e not in set(g)]
            removed = [e for e in g if e not in set(cur)]
            print(f"----- {project_rel} vs {args.rev}: 新增={len(added)} 删除={len(removed)}")
            for e in added:
                print(f"  + {e}")
            for e in removed:
                print(f"  - {e}")
            report["projects"][name]["added_vs_golden"] = added
            report["projects"][name]["removed_vs_golden"] = removed

    if args.json:
        path = repo / "pl/sim/equiv/audit/last_prj_check.json"
        path.write_text(json.dumps(report, ensure_ascii=False, indent=1), encoding="utf-8")
        print(f"(json -> {path})")
    print("PRJ RESULT:", "PASS" if fail == 0 else f"FAIL ({fail})")
    sys.exit(0 if fail == 0 else 1)


if __name__ == "__main__":
    main()
