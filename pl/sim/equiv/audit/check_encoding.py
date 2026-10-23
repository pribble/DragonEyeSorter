#!/usr/bin/env python3
"""全仓 RTL 文本编码检查（verifier-platform 交付物，只读）。

检查范围：仓库内全部 *.v / *.sv / *.vh 文件（跳过 .git）。
判定：
  - 必须能被 strict UTF-8 解码（errors='strict'）
  - 不得有 UTF-8 BOM（EF BB BF）
  - 允许 CRLF（不告警；.sh 才要求 LF）
输出：逐文件异常清单 + 汇总；有任何非 UTF-8 或 BOM 时退出码 1。

用法：
  python3 pl/sim/equiv/audit/check_encoding.py [repo_root]
  （golden 对比模式：python3 ... --rev 9a905cc  额外把 golden 版本同样检查一遍并汇总）
"""
import argparse
import subprocess
import sys
from pathlib import Path

from audit_paths import find_repo_root

REPO_DEFAULT = str(find_repo_root())
EXTS = {".v", ".sv", ".vh"}


def scan(paths, label):
    bad_utf8, bad_bom, ok = [], [], 0
    for p, data in paths:
        if data.startswith(b"\xef\xbb\xbf"):
            bad_bom.append((p, "UTF-8 BOM"))
        try:
            data.decode("utf-8", errors="strict")
            ok += 1
        except UnicodeDecodeError as e:
            bad_utf8.append((p, f"非 UTF-8（首个坏字节 offset {e.start}）"))
    print(f"[{label}] 文件数={len(paths)} OK={ok} 非UTF8={len(bad_utf8)} BOM={len(bad_bom)}")
    for p, why in bad_utf8:
        print(f"  NONUTF8 {p}  {why}")
    for p, why in bad_bom:
        print(f"  BOM     {p}  {why}")
    return len(bad_utf8) + len(bad_bom)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("repo", nargs="?", default=str(REPO_DEFAULT))
    ap.add_argument("--rev", default=None, help="额外检查该 git rev 下的同批文件")
    args = ap.parse_args()
    repo = Path(args.repo)
    paths = []
    for p in sorted(repo.rglob("*")):
        if not p.is_file():
            continue
        if ".git" in p.relative_to(repo).parts:
            continue
        if p.suffix in EXTS:
            paths.append((str(p.relative_to(repo)), p.read_bytes()))
    bad = scan(paths, f"worktree {repo}")
    worktree_bad = bad

    if args.rev:
        files = subprocess.run(
            ["git", "-C", str(repo), "ls-tree", "-r", "--name-only", args.rev],
            capture_output=True, text=True, check=True).stdout.splitlines()
        golden = []
        for f in files:
            if Path(f).suffix in EXTS:
                data = subprocess.run(["git", "-C", str(repo), "show", f"{args.rev}:{f}"],
                                      capture_output=True, check=True).stdout
                golden.append((f, data))
        # golden 侧的 GBK/BOM 是「重构前状态」，只做信息展示，不影响退出码
        golden_bad = scan(golden, f"golden {args.rev}")
        print(f"（信息）golden 侧非 UTF-8/BOM 文件数={golden_bad}（重构前固有，不算失败）")
    print("ENCODING RESULT:", "PASS（工作区全部 UTF-8 无 BOM）" if worktree_bad == 0
          else f"FAIL（工作区 {worktree_bad} 个文件）")
    sys.exit(1 if worktree_bad else 0)


if __name__ == "__main__":
    main()
