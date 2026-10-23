`timescale 1ns/1ps
// uihdmi_tx 等价回归（工程唯一配置 DEVICE="DR1"）：加密 TMDS 控制器用行为桩，
// 明文 hdmi_tx_phy_wrapper + lane_lvds_10_1（DR1 ODDR 原语用桩）走真实链路。
// 激励：pixel/serial 双时钟域、de/vs/hs 模式化、数据极值、中途复位。
module tb_uihdmi_tx;
    `include "equiv_tb_common.vh"

    reg         pixel_clk = 0;
    reg         serial_clk = 0;
    reg         rst = 1;
    reg [23:0]  video_data = 24'h000000;
    reg         video_de = 0;
    reg         video_vs = 0;
    reg         video_hs = 0;

    wire        hdmi_clk_p;
    wire [2:0]  hdmi_tx_p;

    uihdmi_tx #(
        .DEVICE ("DR1")
    ) dut (
        .I_pixel_clk     (pixel_clk),
        .I_serial_clk    (serial_clk),
        .I_rst           (rst),
        .I_video_in_data (video_data),
        .I_video_in_de   (video_de),
        .I_video_in_vs   (video_vs),
        .I_video_in_hs   (video_hs),
        .O_hdmi_clk_p    (hdmi_clk_p),
        .O_hdmi_tx_p     (hdmi_tx_p)
    );

    always #5 pixel_clk  = ~pixel_clk;   // 100MHz
    always #1 serial_clk = ~serial_clk;  // 500MHz（5x）

    initial equiv_trace_open;

    always @(posedge serial_clk) begin
        #1;
        cyc = cyc + 1;
        $fdisplay(fd, "%0d %b %b", cyc, hdmi_clk_p, hdmi_tx_p);
    end

    integer pn = 0;
    always @(negedge pixel_clk) begin
        pn = pn + 1;
        video_de <= ((pn % 8) != 0);        // 7/8 有效
        video_hs <= ((pn % 4) == 0);
        video_vs <= ((pn % 32) < 2);
        if (pn == 100)      video_data <= 24'hFFFFFF;   // 极值
        else if (pn == 110) video_data <= 24'h000000;
        else if (pn == 120) video_data <= 24'h800001;
        else                video_data <= video_data + 24'h010203;
    end

    initial begin
        rst = 1'b1;
        video_data = 24'h000000;
        video_de   = 1'b0;
        video_vs   = 1'b0;
        video_hs   = 1'b0;
        repeat (8) @(negedge pixel_clk);
        rst = 1'b0;
        repeat (180) @(negedge pixel_clk);
        rst = 1'b1;
        repeat (3) @(negedge pixel_clk);
        rst = 1'b0;
        repeat (420) @(negedge pixel_clk);
        equiv_finish(cyc);
    end
endmodule
