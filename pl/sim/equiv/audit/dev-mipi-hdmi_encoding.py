#!/usr/bin/env python3
"""Check the 8 refactored files are UTF-8 without BOM and LF-only."""
import sys

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

ok = True
for f in FILES:
    b = open(f, "rb").read()
    bom = b.startswith(b"\xef\xbb\xbf")
    cr = b.count(b"\r")
    try:
        b.decode("utf-8")
        utf8 = True
    except UnicodeDecodeError:
        utf8 = False
    ends_nl = b.endswith(b"\n")
    good = (not bom) and cr == 0 and utf8 and ends_nl
    ok = ok and good
    print(f"{f.split('/')[-1]:42s} bom={bom} cr={cr} utf8={utf8} ends_nl={ends_nl} {'OK' if good else 'BAD'}")
print("ENCODING_OK" if ok else "ENCODING_BAD")
sys.exit(0 if ok else 1)
