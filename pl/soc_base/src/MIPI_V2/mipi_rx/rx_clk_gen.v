// MIPI D-PHY RX clock generation for DR1: DR1_PHY_LCLK divides the input HS clock
// into the x4 / x2 / x1 clocks used by hs_rx_wrapper. Only DEVICE="DR1" is
// supported (the single configuration used by design_top_wrapper); the parameter
// is kept for interface compatibility.
/* verilator lint_off UNUSEDPARAM */ // DEVICE is retained for interface compatibility only
module rx_clk_gen #(
    parameter DEVICE = "EG"   // kept for interface compatibility; only DR1 is supported
)(
    input  wire I_mipi_clk_p,
    input  wire I_rst,
    output wire O_clk,
    output wire O_clk_x2,
    output wire O_clk_x4
);
/* verilator lint_on UNUSEDPARAM */
    // DIV=2: clkout -> x4, clkdivout -> x2; DIV=4: clkdivout -> x1
    DR1_PHY_LCLK #(.CEMD("SYNC"), .CLKSEL("ORG"), .DIV(2), .LCLK("BYPASS")) U_io_clk_div_0 (
        .clkin(I_mipi_clk_p), .rst(I_rst), .ce(1'b1),
        .clkout(O_clk_x4), .clkdivout(O_clk_x2)
    );
    DR1_PHY_LCLK #(.CEMD("SYNC"), .CLKSEL("ORG"), .DIV(4), .LCLK("BYPASS")) U_io_clk_div_1 (
        .clkin(I_mipi_clk_p), .rst(I_rst), .ce(1'b1),
        .clkout(), .clkdivout(O_clk)
    );
endmodule
