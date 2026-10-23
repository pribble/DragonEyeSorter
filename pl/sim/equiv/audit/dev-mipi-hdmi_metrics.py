#!/usr/bin/env python3
"""Before/after size metrics for the 8 refactored MIPI_V2 / hdmi_tx plaintext RTL files.

Before = golden rev (as stored in git, normalised to LF by .gitattributes `* text=auto`),
after  = worktree.  lines = wc -l, bytes = tr -d '\r' | wc -c  (i.e. CR-free content bytes).
Read-only audit.
"""
import subprocess, sys

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

def stat(data):
    data = data.replace(b"\r", b"")
    return data.count(b"\n"), len(data)

tot = [0, 0, 0, 0]
print(f"{'file':72s} {'lines':>13s} {'bytes':>15s}")
for f in FILES:
    g = subprocess.run(["git", "show", f"{REV}:{f}"], capture_output=True, check=True).stdout
    n = open(f, "rb").read()
    gl, gb = stat(g)
    nl, nb = stat(n)
    tot = [tot[0] + gl, tot[1] + nl, tot[2] + gb, tot[3] + nb]
    print(f"{f.split('/')[-1]:72s} {gl:5d}->{nl:<5d} {gb:7d}->{nb:<7d}")
gl, nl, gb, nb = tot
print(f"{'TOTAL':72s} {gl:5d}->{nl:<5d} {gb:7d}->{nb:<7d}")
print(f"lines {gl}->{nl} ({(nl-gl)*100.0/gl:+.1f}%), bytes {gb}->{nb} ({(nb-gb)*100.0/gb:+.1f}%)")
