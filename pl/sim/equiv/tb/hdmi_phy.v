`timescale 1ns/1ps
// hdmi_tx_phy_wrapper 等价回归（工程唯一配置 DEVICE="DR1"）：
// 验证 4 路 TMDS 通道的位反序映射与 4 个 lane 例化，ODDR 原语用行为桩。
module tb_hdmi_phy;
    `include "equiv_tb_common.vh"

    reg         pixel_clk  = 0;
    reg         serial_clk = 0;
    reg         rst        = 1;
    reg [9:0]   tmds0 = 10'h000;
    reg [9:0]   tmds1 = 10'h000;
    reg [9:0]   tmds2 = 10'h000;
    reg [9:0]   tmds_clk = 10'h000;

    wire tmds_ch0_p;
    wire tmds_ch1_p;
    wire tmds_ch2_p;
    wire tmds_clk_p;

    hdmi_tx_phy_wrapper #(
        .DEVICE ("DR1")
    ) dut (
        .I_pixel_clk        (pixel_clk),
        .I_serial_clk       (serial_clk),
        .I_rst              (rst),
        .I_tmds_channel_0   (tmds0),
        .I_tmds_channel_1   (tmds1),
        .I_tmds_channel_2   (tmds2),
        .I_tmds_channel_clk (tmds_clk),
        .O_tmds_ch0_p       (tmds_ch0_p),
        .O_tmds_ch1_p       (tmds_ch1_p),
        .O_tmds_ch2_p       (tmds_ch2_p),
        .O_tmds_clk_p       (tmds_clk_p)
    );

    always #5 pixel_clk  = ~pixel_clk;
    always #1 serial_clk = ~serial_clk;

    initial equiv_trace_open;

    always @(posedge serial_clk) begin
        #1;
        cyc = cyc + 1;
        $fdisplay(fd, "%0d %b %b %b %b",
                  cyc, tmds_ch0_p, tmds_ch1_p, tmds_ch2_p, tmds_clk_p);
    end

    integer pn = 0;
    always @(negedge pixel_clk) begin
        pn = pn + 1;
        tmds0   <= tmds0   + 10'h001;
        tmds1   <= tmds1   + 10'h007;
        tmds2   <= tmds2   + 10'h033;
        tmds_clk <= tmds_clk + 10'h155;
        if (pn == 60) begin
            tmds0   <= 10'h3FF;
            tmds1   <= 10'h000;
            tmds2   <= 10'h2AA;
            tmds_clk <= 10'h155;
        end
    end

    initial begin
        rst = 1'b1;
        repeat (6) @(negedge pixel_clk);
        rst = 1'b0;
        repeat (140) @(negedge pixel_clk);
        rst = 1'b1;
        repeat (2) @(negedge pixel_clk);
        rst = 1'b0;
        repeat (240) @(negedge pixel_clk);
        equiv_finish(cyc);
    end
endmodule
