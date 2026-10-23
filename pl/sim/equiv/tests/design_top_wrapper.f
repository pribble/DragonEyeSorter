# design_top_wrapper：MIPI→ISP 顶层胶水。PLL.v 明文依赖用真文件；
# MIPI 加密子模块 + raw10_unpacket_4lane / image_correction / isp_top（isp 域并发重构）用桩。
pl/soc_base/src/MIPI_V2/src/design_top_wrapper.v
pl/soc_base/src/MIPI_V2/src/csi_unpacket.v
pl/soc_base/src/MIPI_V2/mipi_rx/mipi_dphy_rx_over_lvds_wrapper.v
pl/soc_base/src/MIPI_V2/mipi_rx/rx_clk_gen.v
pl/soc_base/src/MIPI_V2/mipi_rx/hs_rx_wrapper.sv
pl/soc_base/al_ip/PLL/PLL.v
@ pl/sim/equiv/stubs/mipi_dtop_chain_models.v
@ pl/sim/equiv/stubs/td_prim_models.v
@ pl/sim/equiv/stubs/pll_prim_models.v
