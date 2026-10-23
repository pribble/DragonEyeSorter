`timescale 1ns/1ps
// hs_rx_wrapper 等价回归：4 lane 加密 data_lane / channel_aligner 用行为桩，
// 验证 valid 归约（&）、lane 数据反序合并、invert 连线与 lane_error 直通。
// 激励：多时钟域、lane 引脚 / invert 位变化、复位中途拉低。
module tb_hs_rx;
    `include "equiv_tb_common.vh"

    reg        clk        = 0;
    reg        clk_x2     = 0;
    reg        clk_x4     = 0;
    reg        lp_clk     = 0;
    reg        rst        = 1;
    reg [3:0]  lane_hs_inv = 4'h0;
    reg [3:0]  lane_lp_inv = 4'h0;
    reg [3:0]  hs_p        = 4'h0;
    reg [3:0]  lp_p        = 4'h0;
    reg [3:0]  lp_n        = 4'h0;

    wire        hs_valid;
    wire [31:0] hs_data;
    wire [3:0]  lane_error;

    hs_rx_wrapper #(
        .LANE_NUM (4),
        .DEVICE   ("DR1")
    ) dut (
        .I_hs_clk         (clk),
        .I_hs_clk_x2      (clk_x2),
        .I_hs_clk_x4      (clk_x4),
        .I_lp_clk         (lp_clk),
        .I_rst            (rst),
        .I_lane_hs_invert (lane_hs_inv),
        .I_lane_lp_invert (lane_lp_inv),
        .I_mipi_hs_p      (hs_p),
        .I_mipi_lp_p      (lp_p),
        .I_mipi_lp_n      (lp_n),
        .O_hs_valid       (hs_valid),
        .O_hs_data        (hs_data),
        .O_lane_error     (lane_error)
    );

    always #5    clk    = ~clk;
    always #2.5  clk_x2 = ~clk_x2;
    always #1.25 clk_x4 = ~clk_x4;
    always #7    lp_clk = ~lp_clk;

    initial equiv_trace_open;

    always @(posedge clk) begin
        #1;
        cyc = cyc + 1;
        $fdisplay(fd, "%0d %b %h %b", cyc, hs_valid, hs_data, lane_error);
    end

    integer n = 0;
    always @(negedge clk) begin
        n = n + 1;
        hs_p <= hs_p + 4'h1;
        lp_p <= lp_p + 4'h3;
        lp_n <= lp_n + 4'h5;
        if (n % 17 == 0) lane_hs_inv <= lane_hs_inv + 4'h1;
        if (n % 23 == 0) lane_lp_inv <= ~lane_lp_inv;
    end

    initial begin
        rst = 1'b1;
        repeat (6) @(negedge clk);
        rst = 1'b0;
        repeat (144) @(negedge clk);
        rst = 1'b1;
        repeat (3) @(negedge clk);
        rst = 1'b0;
        repeat (180) @(negedge clk);
        equiv_finish(cyc);
    end
endmodule
