`timescale 1ns/1ps
// mipi_dphy_rx_over_lvds_wrapper 等价回归（工程唯一配置 DEVICE="DR1" / LANE_NUM=4）：
// rx_clk_gen 的 DR1 时钟分频原语用行为桩，加密 data_lane / channel_aligner 用行为桩，
// 验证 wrapper 的时钟分发、LP lane0 交换与 hs_rx 端口直连。
module tb_mipi_dphy;
    integer fd;
    integer cyc = 0;
    reg [1023:0] tf;

    reg        mipi_clk = 0;
    reg        lp_clk   = 0;
    reg        rst      = 1;
    reg [3:0]  hs_p     = 4'h0;
    reg [3:0]  lp_p     = 4'h0;
    reg [3:0]  lp_n     = 4'h0;
    reg [3:0]  hs_inv   = 4'h0;
    reg [3:0]  lp_inv   = 4'h0;

    wire        hs_rx_clk;
    wire        hs_rx_valid;
    wire [31:0] hs_rx_data;
    wire        lp_lane0_p;
    wire        lp_lane0_n;
    wire [3:0]  lane_error;

    mipi_dphy_rx_over_lvds_wrapper #(
        .DEVICE   ("DR1"),
        .LANE_NUM (4)
    ) dut (
        .I_lp_clk         (lp_clk),
        .I_rst            (rst),
        .I_mipi_clk_p     (mipi_clk),
        .I_mipi_data_hs_p (hs_p),
        .I_mipi_data_lp_p (lp_p),
        .I_mipi_data_lp_n (lp_n),
        .I_lane_hs_invert (hs_inv),
        .I_lane_lp_invert (lp_inv),
        .O_hs_rx_clk      (hs_rx_clk),
        .O_hs_rx_valid    (hs_rx_valid),
        .O_hs_rx_data     (hs_rx_data),
        .O_lp_rx_lane0_p  (lp_lane0_p),
        .O_lp_rx_lane0_n  (lp_lane0_n),
        .O_lane_error     (lane_error)
    );

    always #2 mipi_clk = ~mipi_clk;
    always #9 lp_clk   = ~lp_clk;

    initial begin
        if (!$value$plusargs("TRACE=%s", tf)) tf = "trace.txt";
        fd = $fopen(tf, "w");
        if (fd == 0) begin
            $display("cannot open trace");
            $finish;
        end
    end

    always @(posedge mipi_clk) begin
        #1;
        cyc = cyc + 1;
        $fdisplay(fd, "%0d %b %b %h %b %b %b",
                  cyc, hs_rx_clk, hs_rx_valid, hs_rx_data,
                  lp_lane0_p, lp_lane0_n, lane_error);
    end

    integer n = 0;
    always @(negedge mipi_clk) begin
        n = n + 1;
        hs_p <= hs_p + 4'h1;
        lp_p <= lp_p + 4'h2;
        lp_n <= lp_n + 4'h3;
        if (n % 13 == 0) hs_inv <= hs_inv + 4'h1;
        if (n % 19 == 0) lp_inv <= ~lp_inv;
    end

    initial begin
        rst = 1'b1;
        repeat (6) @(negedge mipi_clk);
        rst = 1'b0;
        repeat (190) @(negedge mipi_clk);
        rst = 1'b1;
        repeat (3) @(negedge mipi_clk);
        rst = 1'b0;
        repeat (400) @(negedge mipi_clk);
        $display("EQUIV_CYCLES %0d", cyc);
        $display("EQUIV_DONE");
        $fclose(fd);
        $finish;
    end
endmodule
