#!/usr/bin/env python3
"""bilinear_interpolation 抽头表审计 + 新版 RTL 生成器（dev-demosaic）。

1) --check：从 golden rev（pl/sim/equiv/baseline.rev）取厂家 bilinear_interpolation.v，
   解析 4 个 generate 分支里 (模式, bayer_ypos 分支, R/G/B 通道, 像素位 0..3, sum_part0/1)
   的抽头集合（matrixXY -> (行, 窗口列)）、matrix_plus 真正相加的 part、均值移位；
   用独立的「统一双线性公式」推导期望值逐项比对，任何不一致 -> 退出码 1。
2) --emit <file>：按同一公式生成新版 RTL（角色和 + Bayer 模式色表），实现与校验同源。

统一公式：像素位 j（列奇偶 q=j&1）、行奇偶 p=bayer_ypos，取 2x2 Bayer 单元四位置颜色
  C=L(p,q) 中心、H=L(p,1-q) 水平、V=L(1-p,q) 垂直、D=L(1-p,1-q) 对角；
颜色 T 的取值：T==C -> 中心 1 抽头；T==H==V -> 水平+垂直 4 抽头；T==H -> 水平 2；
  T==V -> 垂直 2；T==D -> 对角 4；均值移位 = log2(抽头数)（1->0, 2->>>1, 4->>>2）。
新版 RTL：与模式无关地先算 4 个「角色和」（中心/水平/垂直/对角，几何固定），
  再用模式色表把角色和映射到 R/G/B（按 bayer_ypos 选择表项），省掉 4 份重复通道逻辑。

用法：
  python3 check_demosaic_taps.py --check [--repo DIR] [--rev REV]
  python3 check_demosaic_taps.py --emit pl/soc_base/src/ui_isp/demosaic_4x_2_0/bilinear_interpolation.v
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

IMAGE = "pl/soc_base/src/ui_isp/demosaic_4x_2_0/bilinear_interpolation.v"
MODES = ["RGGB", "BGGR", "GBRG", "GRBG"]

# 2x2 Bayer 单元 (行奇偶 p, 列奇偶 q) -> 颜色；0=R 1=G 2=B
BAYER = {
    "RGGB": {(0, 0): 0, (0, 1): 1, (1, 0): 1, (1, 1): 2},
    "BGGR": {(0, 0): 2, (0, 1): 1, (1, 0): 1, (1, 1): 0},
    "GBRG": {(0, 0): 1, (0, 1): 2, (1, 0): 0, (1, 1): 1},
    "GRBG": {(0, 0): 1, (0, 1): 0, (1, 0): 2, (1, 1): 1},
}
CNAME = {0: "R", 1: "G", 2: "B"}
ROLE_SHIFT = {"C": 0, "H": 1, "V": 1, "D": 2, "HV": 2}


def default_repo():
    """本脚本若放在 pl/sim/equiv/audit/ 下，自动定位仓库根；否则用本机路径。"""
    p = Path(__file__).resolve()
    if p.parent.name == "audit" and p.parent.parent.name == "equiv":
        return p.parents[4]
    raise SystemExit("cannot locate repo root: run from pl/sim/equiv/audit/ or pass --repo")


def golden_text(repo, rev):
    out = subprocess.run(
        ["git", "-C", str(repo), "show", f"{rev}:{IMAGE}"],
        check=True, capture_output=True)
    return out.stdout.decode("gbk", errors="replace")   # 代码 ASCII，注释 GBK


# --------------------------------------------------------------------------
# golden 解析
# --------------------------------------------------------------------------
def split_modes(txt):
    hits = list(re.finditer(r"BAYER_MODE\s*==\s*(\w+)", txt))
    golden_order = ["BGGR", "GBRG", "RGGB", "GRBG"]   # 与源码出现顺序一致
    assert [m.group(1) for m in hits] == golden_order, [m.group(1) for m in hits]
    out = {}
    for i, m in enumerate(hits):
        end = hits[i + 1].start() if i + 1 < len(hits) else txt.rindex("endgenerate")
        out[m.group(1)] = txt[m.end():end]
    return out


def split_channels(slice_txt):
    hits = list(re.finditer(r"//\s*([RGB])\s*Channel", slice_txt))
    assert [m.group(1) for m in hits] == ["R", "G", "B"], [m.group(1) for m in hits]
    out = {}
    for i, m in enumerate(hits):
        end = hits[i + 1].start() if i + 1 < len(hits) else len(slice_txt)
        out[m.group(1)] = slice_txt[m.end():end]
    return out


def split_ypos(channel_txt, mode, ch):
    m = re.search(r"if\s*\(\s*bayer_ypos\s*\)", channel_txt)
    assert m, (mode, ch)
    tail = channel_txt[m.end():]
    m2 = re.search(r"\bend\s*\n\s*else\s+begin", tail)
    assert m2, (mode, ch)
    return tail[:m2.start()], tail[m2.end():]


def token_taps(expr):
    toks = re.findall(r"matrix(\d)(\d)", expr)
    rest = re.sub(r"matrix\d\d", "", expr)
    rest = re.sub(r"\d*'[dbh][0-9a-fA-FxXzZ_]+", "", rest)
    rest = re.sub(r"[^0-9a-zA-Z+]", "", rest)
    assert rest in ("", "+"), (expr, rest)
    return [(int(a), int(b) - 3) for a, b in toks]


def parse_golden(txt):
    parts, plus, mean = {}, {}, {}
    for mode, mtxt in split_modes(txt).items():
        for ch, ctxt in split_channels(mtxt).items():
            color = {"R": 0, "G": 1, "B": 2}[ch]
            ytxt, ntxt = split_ypos(ctxt, mode, ch)
            for p, seg in ((1, ytxt), (0, ntxt)):
                for m in re.finditer(r"([rgb])([0-3])_sum_part([01])\s*<=\s*([^;]+);", seg):
                    assert m.group(1).lower() == ch.lower(), (mode, ch, m.group(0))
                    parts[(mode, p, color, int(m.group(2)), int(m.group(3)))] = token_taps(m.group(4))
        for m in re.finditer(r"([rgb])([0-3])_matrix_plus\s*<=\s*([^;]+);", mtxt):
            color = {"r": 0, "g": 1, "b": 2}[m.group(1)]
            plus[(mode, color, int(m.group(2)))] = \
                [int(x) for x in re.findall(r"_sum_part([01])", m.group(3))]
        for m in re.finditer(r"([rgb])([0-3])_mean\s*<=\s*\(?\s*(!?)\s*bayer_ypos_r1\s*\)?"
                             r"\s*\?\s*([^:;]+?)\s*:\s*([^;]+);", mtxt):
            color = {"r": 0, "g": 1, "b": 2}[m.group(1)]
            j, neg, a, b = int(m.group(2)), m.group(3) == "!", m.group(4), m.group(5)

            def sh(e):
                mm = re.search(r">>\s*(\d+)", e)
                return int(mm.group(1)) if mm else 0

            # cond ? A : B；cond=!(p) 时 p=1 取 B
            mean[(mode, 0 if neg else 1, color, j)] = sh(a)
            mean[(mode, 1 if neg else 0, color, j)] = sh(b)
    return parts, plus, mean


# --------------------------------------------------------------------------
# 统一公式
# --------------------------------------------------------------------------
def sites(mode, p, j):
    """像素位 j 的颜色 -> 抽头角色（C/H/V/HV/D）映射。"""
    q = j & 1
    cm = BAYER[mode]
    C, H = cm[(p, q)], cm[(p, 1 - q)]
    V, D = cm[(1 - p, q)], cm[(1 - p, 1 - q)]
    roles = {C: "C"}
    if H == V:
        roles[H] = "HV"
    else:
        roles[H] = "H"
        roles[V] = "V"
    if D not in roles:
        roles[D] = "D"
    assert len(roles) == 3, (mode, p, j, C, H, V, D)
    return roles


def expected(mode, p, j, color):
    role = sites(mode, p, j)[color]
    return role, ROLE_SHIFT[role]


# --------------------------------------------------------------------------
# 校验
# --------------------------------------------------------------------------
def do_check(repo, rev):
    parts, plus, mean = parse_golden(golden_text(repo, rev))
    n_cmp = n_part = n_plus = n_shift = 0
    bad = []
    for mode in MODES:
        for p in (0, 1):
            for color in (0, 1, 2):
                for j in range(4):
                    role, exp_shift = expected(mode, p, j, color)
                    used = plus[(mode, color, j)]
                    got = []
                    for part in used:
                        taps = parts.get((mode, p, color, j, part), [])
                        n_part += 1
                        if len(taps) > 2:
                            bad.append(f"{mode} p={p} {CNAME[color]}{j} part{part} 抽头 {len(taps)}>2")
                        got += taps
                    n_cmp += 1
                    if role == "C":
                        exp_taps = [(1, j + 1)]
                    elif role == "H":
                        exp_taps = [(1, j), (1, j + 2)]
                    elif role == "V":
                        exp_taps = [(0, j + 1), (2, j + 1)]
                    elif role == "HV":
                        exp_taps = [(1, j), (1, j + 2), (0, j + 1), (2, j + 1)]
                    else:
                        exp_taps = [(0, j), (0, j + 2), (2, j), (2, j + 2)]
                    if sorted(got) != sorted(exp_taps):
                        bad.append(f"{mode} p={p} {CNAME[color]}{j} 抽头 {sorted(got)} != 期望 {sorted(exp_taps)}")
                    for part in (0, 1):
                        if part not in used and parts.get((mode, p, color, j, part), []):
                            bad.append(f"{mode} p={p} {CNAME[color]}{j} matrix_plus 漏加 part{part}")
                    n_plus += 1
                    if mean[(mode, p, color, j)] != exp_shift:
                        bad.append(f"{mode} p={p} {CNAME[color]}{j} 移位 "
                                   f"{mean[(mode,p,color,j)]} != >>{exp_shift}")
                    n_shift += 1
    print(f"golden rev        : {rev}")
    print(f"抽头组合比对      : {n_cmp} 组 (4 模式 x 2 行奇偶 x 3 通道 x 4 像素位)")
    print(f"sum_part 条目     : {n_part}")
    print(f"matrix_plus 引用  : {n_plus} 组")
    print(f"均值移位比对      : {n_shift} 组")
    for b in bad:
        print("  MISMATCH:", b)
    if bad:
        print(f"FAIL: {len(bad)} 处不一致")
        return 1
    print("PASS: golden 抽头表 / matrix_plus 引用 / 均值移位与统一双线性公式完全一致")
    return 0


# --------------------------------------------------------------------------
# 生成新版 RTL
# --------------------------------------------------------------------------
def role_expr(role, j):
    base = j * 40
    if role == "C":
        return f"q[{base} +: 10]"
    if role == "H":
        return f"q[{base + 10} +: 10]"
    if role == "V":
        return f"q[{base + 20} +: 10]"
    if role == "D":
        return f"q[{base + 30} +: 10]"
    return f"(q[{base + 10} +: 10] + q[{base + 20} +: 10])"   # HV


def emit(path):
    L = []
    w = L.append
    w("// 出处：Milianke uisrc ISP demo（ui_isp/demosaic_4x_2_0/bilinear_interpolation.v,")
    w("//       XiaoQingquan 2025/08, v2.0）。本版按统一双线性公式 + Bayer 模式色表折叠重写，逐拍行为等价：")
    w("// 先用与模式无关的 3x3 窗口几何算 4 个「角色和」(中心/水平/垂直/对角)，再按 BAYER_MODE + bayer_ypos")
    w("// 的色表映射到 R/G/B，替代原码 4 份重复通道逻辑。")
    w("module bilinear_interpolation #(")
    w("    parameter BAYER_MODE = \"BGGR\"   // Bayer 排列：RGGB/BGGR/GBRG/GRBG")
    w(")(")
    w("    input               I_clk           ,")
    w("    input               I_rst_n         ,")
    w("")
    w("    input               I_tlast         ,")
    w("    input               I_tuser         ,")
    w("    input               I_tvalid        ,")
    w("    output              I_tready        ,")
    w("")
    w("    input               bayer_ypos      ,   // 窗口中心行行奇偶")
    w("    input      [95:0]   matrix_last_line,")
    w("    input      [95:0]   matrix_cur_line ,")
    w("    input      [95:0]   matrix_next_line,")
    w("")
    w("    output              O_tlast         ,")
    w("    output              O_tuser         ,")
    w("    output     [127:0]  O_tdata         ,")
    w("    output              O_tvalid        ,")
    w("    input               O_tready")
    w(");")
    w("")
    w("    // 3x3 窗口字节：行 0/1/2 = 上一行/当前行/下一行；窗口列 0..5 = 相对当前 4 像素组的 -1..+4")
    w("    wire [95:0] line [0:2];")
    w("    wire [7:0]  w    [0:2][0:5];")
    w("    wire [9:0]  role [0:3][0:3];   // [角色 C/H/V/D][像素位 0..3]，10bit（4 抽头最大 1020）")
    w("")
    w("    assign line[0] = matrix_last_line;")
    w("    assign line[1] = matrix_cur_line;")
    w("    assign line[2] = matrix_next_line;")
    w("")
    w("    genvar gl, gw, gj;")
    w("    generate")
    w("        for (gl = 0; gl < 3; gl = gl + 1)")
    w("            for (gw = 0; gw < 6; gw = gw + 1)")
    w("                assign w[gl][gw] = line[gl][71 - 8*gw -: 8];")
    w("        for (gj = 0; gj < 4; gj = gj + 1) begin : g_role")
    w("            // 8bit 相加先零扩展到 9bit，避免截断；对角 4 抽头扩到 10bit")
    w("            assign role[0][gj] = {2'b0, w[1][gj+1]};")
    w("            assign role[1][gj] = {1'b0, w[1][gj]} + {1'b0, w[1][gj+2]};")
    w("            assign role[2][gj] = {1'b0, w[0][gj+1]} + {1'b0, w[2][gj+1]};")
    w("            assign role[3][gj] = ({1'b0, w[0][gj]} + {1'b0, w[0][gj+2]})")
    w("                              + ({1'b0, w[2][gj]} + {1'b0, w[2][gj+2]});")
    w("        end")
    w("    endgenerate")
    w("")
    w("    // 第 1 级：角色和打拍（q 布局：每个像素位 40bit = {D, V, H, C}，各 10bit）")
    w("    reg [159:0] q;")
    w("    always @(posedge I_clk or negedge I_rst_n) begin")
    w("        if (!I_rst_n)")
    w("            q <= 160'd0;")
    w("        else if (I_tvalid)")
    w("            q <= {role[3][3], role[2][3], role[1][3], role[0][3],")
    w("                  role[3][2], role[2][2], role[1][2], role[0][2],")
    w("                  role[3][1], role[2][1], role[1][1], role[0][1],")
    w("                  role[3][0], role[2][0], role[1][0], role[0][0]};")
    w("    end")
    w("")
    w("    // bayer_ypos 打拍：第 2 级用 r0 选色表，第 3 级用 r1 选均值移位")
    w("    reg bayer_ypos_r0, bayer_ypos_r1;")
    w("    always @(posedge I_clk or negedge I_rst_n) begin")
    w("        if (!I_rst_n) begin")
    w("            bayer_ypos_r0 <= 1'b0;")
    w("            bayer_ypos_r1 <= 1'b0;")
    w("        end")
    w("        else begin")
    w("            bayer_ypos_r0 <= bayer_ypos;")
    w("            bayer_ypos_r1 <= bayer_ypos_r0;")
    w("        end")
    w("    end")
    w("")
    w("    // 流控制信号同步：tlast/tuser/tvalid 延迟 5 拍与数据对齐")
    w("    reg [4:0] I_tlast_r, I_tuser_r, I_tvalid_r;")
    w("    always @(posedge I_clk or negedge I_rst_n) begin")
    w("        if (!I_rst_n) begin")
    w("            I_tlast_r  <= 5'd0;")
    w("            I_tuser_r  <= 5'd0;")
    w("            I_tvalid_r <= 5'd0;")
    w("        end")
    w("        else begin")
    w("            I_tlast_r  <= {I_tlast_r[3:0], I_tlast};")
    w("            I_tuser_r  <= {I_tuser_r[3:0], I_tuser};")
    w("            I_tvalid_r <= {I_tvalid_r[3:0], I_tvalid};")
    w("        end")
    w("    end")
    w("")
    w("    // 第 2/3 级：按 Bayer 模式取色表（角色和 -> R/G/B），再按抽头数移位求均值。")
    w("    // 未列出的 BAYER_MODE（非法参数）不生成任何逻辑 -> 与厂家原码一致输出 x。")
    w("    reg [119:0] mp;     // 每像素位 30bit = {B, G, R}，各 10bit")
    w("    reg [119:0] mean;   // 每像素位 30bit = {B, G, R}，各 10bit（值域 <=255，与原码 9bit 均值等效）")
    w("    generate")
    for mi, mode in enumerate(MODES):
        kw = "if" if mi == 0 else "else if"
        w(f"        {kw} (BAYER_MODE == \"{mode}\") begin : g_{mode.lower()}")
        for p in (0, 1):
            exprs = []
            for j in range(4):
                roles = sites(mode, p, j)
                for color in (0, 1, 2):
                    exprs.append(role_expr(roles[color], j))
            exprs = list(reversed(exprs))     # 拼接左起为 MSB
            w(f"            // bayer_ypos={p} 的色表")
            w(f"            wire [119:0] mp_p{p} = {{")
            for i in range(0, 12, 4):
                chunk = exprs[i:i + 4]
                w("                " + ", ".join(chunk) + ("," if i + 4 < 12 else ""))
            w("            };")
        w("            always @(posedge I_clk or negedge I_rst_n) begin")
        w("                if (!I_rst_n)")
        w("                    mp <= 120'd0;")
        w("                else if (I_tvalid_r[0])")
        w("                    mp <= bayer_ypos_r0 ? mp_p1 : mp_p0;")
        w("            end")
        w("")
        w("            always @(posedge I_clk or negedge I_rst_n) begin")
        w("                if (!I_rst_n)")
        w("                    mean <= 120'd0;")
        w("                else if (I_tvalid_r[1])")
        w("                    mean <= bayer_ypos_r1 ? {")
        for p in (1, 0):
            items = []
            for j in range(4):
                roles = sites(mode, p, j)
                for color in (0, 1, 2):
                    e = j * 3 + color
                    items.append(f"mp[{e * 10} +: 10] >> {ROLE_SHIFT[roles[color]]}")
            items = list(reversed(items))
            for i in range(0, 12, 3):
                chunk = items[i:i + 3]
                w("                        " + ", ".join(chunk) + ("," if i + 3 < 12 else ""))
            w("                    } : {" if p == 1 else "                    };")
        w("            end")
        w("        end")
    w("    endgenerate")
    w("")
    w("    // 第 4 级：饱和到 8bit，并按输出格式拼 128bit（每个 32bit 像素 = {8'b0, R, G, B}）")
    w("    reg [127:0] rgb_d;")
    w("    always @(posedge I_clk or negedge I_rst_n) begin")
    w("        if (!I_rst_n)")
    w("            rgb_d <= 128'd0;")
    w("        else if (I_tvalid_r[2])")
    w("            rgb_d <= {")
    items = []
    for j in (3, 2, 1, 0):
        items.append("8'd0")
        for color in (0, 1, 2):
            e = j * 3 + color
            items.append(f"(mean[{e * 10 + 8}] ? 8'd255 : mean[{e * 10 + 7} -: 8])")
    for i in range(0, 16, 4):
        chunk = items[i:i + 4]
        w("                " + ", ".join(chunk) + ("," if i + 4 < 16 else ""))
    w("            };")
    w("    end")
    w("")
    w("    // 第 5 级：输出数据打拍")
    w("    reg [127:0] rgb_q;")
    w("    always @(posedge I_clk or negedge I_rst_n) begin")
    w("        if (!I_rst_n)")
    w("            rgb_q <= 128'd0;")
    w("        else if (I_tvalid_r[3])")
    w("            rgb_q <= rgb_d;")
    w("    end")
    w("")
    w("    assign O_tlast  = I_tlast_r[4];")
    w("    assign O_tuser  = I_tuser_r[4];")
    w("    assign O_tvalid = I_tvalid_r[4];")
    w("    assign O_tdata  = rgb_q;")
    w("    assign I_tready = O_tready;")
    w("")
    w("endmodule")
    Path(path).write_text("\n".join(L) + "\n", encoding="utf-8", newline="\n")
    print(f"wrote {path} ({len(L)} lines)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=str(default_repo()))
    ap.add_argument("--rev", default="")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--emit", default="")
    args = ap.parse_args()
    if args.check:
        repo = Path(args.repo)
        rev = args.rev
        if not rev:
            for line in (repo / "pl/sim/equiv/baseline.rev").read_text(encoding="utf-8").splitlines():
                line = line.strip()
                if line and not line.startswith("#"):
                    rev = line
                    break
        return do_check(repo, rev)
    if args.emit:
        emit(args.emit)
        return 0
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
