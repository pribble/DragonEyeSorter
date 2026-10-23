#!/usr/bin/env python3
"""生成 lint_sys_stubs.v：从真实 design_0.v / design_top_wrapper.v 复制模块头（端口方向/位宽），
避免手抄出错。只用于 verilator --lint-only。"""
import re
from pathlib import Path

from audit_paths import find_repo_root

REPO_DEFAULT = str(find_repo_root())
SRC = REPO / "pl/soc_base"

def extract_module_header(path: Path) -> str:
    text = path.read_text(encoding="utf-8", errors="replace")
    m = re.search(r"^module\s+\w+.*?\)\s*;", text, re.S | re.M)
    if not m:
        raise SystemExit(f"cannot find module header in {path}")
    return m.group(0)

design0 = extract_module_header(SRC / "design_0/hdl/design_0.v")
dtop = extract_module_header(SRC / "src/MIPI_V2/src/design_top_wrapper.v")

out = f"""// verifier-platform lint 专用桩（仅 verilator --lint-only，不参与仿真/综合）。
// 本文件由 pl/sim/equiv/audit/gen_lint_stubs.py 从 golden 的 design_0.v / design_top_wrapper.v
// 模块头自动生成，端口方向/位宽与真实工程一致；两侧共用。
// 目的：让 system_top 单独 lint 时不被 PS 块设计与 MIPI 顶层内部实现打断。

{design0}
endmodule

{dtop}
endmodule
"""
out_path = REPO / "pl/sim/equiv/audit/lint_sys_stubs.v"
out_path.write_text(out, encoding="utf-8")
print(f"wrote {out_path} ({len(out)} bytes)")
