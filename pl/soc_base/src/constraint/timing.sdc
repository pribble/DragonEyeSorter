
derive_clocks
# DragonEyeSorter 2026-10-11: the vendor line put clk_mipi on the hard MIPI PHY pin
#   design_top_wrapper_u/u_mipi_dphy_rx_dr1_mipiio_wrapper/u_DR1_mipiio_wrapper/u_DR1_PHY_MIPIIO.hsrx_byteclk_to_fabric
# which does not exist in this design (MIPI over LVDS IO): TD made clk_mipi a virtual clock and never timed the
# pixel domain (ISP, des_vision, frame writer). The byte clock is the /4 output of the LVDS clock divider.
# IMX415 runs 891 Mbps/lane (INCK 27 MHz, SYS_MODE 5) -> byte clock 111.4 MHz; constrained at 125 MHz (8.0 ns).
create_clock -name clk_mipi -period 8.0 -waveform {0 4.0} [get_pins {design_top_wrapper_u/u_mipi_dphy_rx_over_lvds_wrapper/u_rx_clk_gen/U_io_clk_div_1.clkdivout}]

# 约束命令，将从ps p2f_rst0_n开始的路径都设置为false path
set_false_path -from [get_nets {design_0_inst/design_0_ARM_Processor_System_0_Inst/p2f_rst0_n}]


set_clock_groups -exclusive  \
    -group [get_clocks { clk_mipi } ] \
    -group [get_clocks {design_0_inst/design_0_PLL_npu_Inst/dr1_phy_pll_dcc52a480158_Inst/u_DR1_PHY_PLL.clkc[0]}] \
    -group [get_clocks {design_0_inst/design_0_pll_75m_Inst/dr1_phy_pll_1f386b1db2c3_Inst/u_DR1_PHY_PLL.clkc[0]}]  \
    -group [get_clocks {design_top_wrapper_u/U_PLL/dr1_phy_pll_59fc11d80b0d_Inst/u_DR1_PHY_PLL.clkc[2]}]

    
set_clock_groups -exclusive  \
    -group [get_clocks { clk_mipi } ] \
    -group [get_clocks {design_0_inst/design_0_pll_150m_Inst/dr1_phy_pll_bdb046a15d6b_Inst/u_DR1_PHY_PLL.clkc[0]}]

# ---------------------------------------------------------------------------
# DragonEyeSorter 2026-10-28 round-6: video timing generator PLL clock is asynchronous
# to the NPU PLL / 150 MHz PLL.  The VTC pixel clock is its own PLL inside the
# (encrypted) video timing generator: 148.5 MHz = 2.9697 x p2f_clk0, phase 262 deg;
# NPU PLL and the 150 MHz PLL are both 150.015 MHz = 3 x p2f_clk0.  They are not
# frequency-related, and the design does not cross data between them synchronously:
#   * VTG PLL "locked" -> vendor pwm_0_1 asynchronous reset (recovery)  [place: 14 EP]
#   * proc_sys_reset (150M) -> AXI4S-to-video async-FIFO reset (recovery) [place: 6 EP]
#   * AXI4S async-FIFO address crosser -> first synchroniser stage (setup) [place: 5 EP]
# STA used to time those CDC structures as synchronous setup/recovery: place SWNS
# -0.954 ns / TNS -17.4 ns / 25 setup EP, none of them fixable by P&R.  The router's
# setup optimisation chased them and never converged on 2026-10-28 (route Phase 2
# 1667 overflow / Phase 3 1296 overflow, >2.5 h).  Declare the domains asynchronous;
# the actual synchronous VTG -> TMDS (PLL_3 = 5 x VTC) relation stays analysed.
set_clock_groups -asynchronous \
    -group [get_clocks {design_0_inst/design_0_video_timing_generator_v01_0_Inst/video_timing_generator_v01_e2dadf6740ca_Inst/dr1_pll_vtc_Inst/u_DR1_PHY_PLL.clkc[0]}] \
    -group [get_clocks {design_0_inst/design_0_PLL_npu_Inst/dr1_phy_pll_dcc52a480158_Inst/u_DR1_PHY_PLL.clkc[0]}]

set_clock_groups -asynchronous \
    -group [get_clocks {design_0_inst/design_0_video_timing_generator_v01_0_Inst/video_timing_generator_v01_e2dadf6740ca_Inst/dr1_pll_vtc_Inst/u_DR1_PHY_PLL.clkc[0]}] \
    -group [get_clocks {design_0_inst/design_0_pll_150m_Inst/dr1_phy_pll_bdb046a15d6b_Inst/u_DR1_PHY_PLL.clkc[0]}]
