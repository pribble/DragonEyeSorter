#!/usr/bin/env python3
"""verifier-platform 审计脚本公共路径工具。

仓库根从脚本自身位置推导（scripts 位于 `<repo>/pl/sim/equiv/audit/`），
**不在代码里写任何主机路径 / 盘符 / 用户名**；调用方仍可用 `--repo <dir>` 覆盖。
"""
from pathlib import Path


def find_repo_root() -> Path:
    """向上找含 .git 或 pl/sim/equiv/baseline.rev 的目录作为仓库根。"""
    here = Path(__file__).resolve()
    for cand in here.parents:
        if (cand / ".git").exists() or (cand / "pl/sim/equiv" / "baseline.rev").is_file():
            return cand
    # 兜底：pl/sim/equiv/audit/x.py -> parents[4] 是仓库根
    return here.parents[4]
