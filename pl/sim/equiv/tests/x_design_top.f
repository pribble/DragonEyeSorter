# x_design_top：design_top_wrapper + MIPI 明文链（dphy/hs_rx/rx_clk_gen/csi_unpacket）对抗性等价；
# 加密子模块 / raw10 / image_correction / isp_top / PLL 用桩（两侧共用），隔离 ISP 域重构
pl/soc_base/src/MIPI_V2/src/design_top_wrapper.v
pl/soc_base/src/MIPI_V2/src/csi_unpacket.v
pl/soc_base/src/MIPI_V2/mipi_rx/mipi_dphy_rx_over_lvds_wrapper.v
pl/soc_base/src/MIPI_V2/mipi_rx/hs_rx_wrapper.sv
pl/soc_base/src/MIPI_V2/mipi_rx/rx_clk_gen.v
@ pl/sim/equiv/stubs/x_mipi_enc_stubs.v
@ pl/sim/equiv/stubs/x_mipi_chain_stubs.v
@ pl/sim/equiv/stubs/x_vendor_prim_stubs.v
