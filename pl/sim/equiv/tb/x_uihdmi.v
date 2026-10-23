`timescale 1ns/1ps
// x_uihdmi — uihdmi_tx.v 对抗性等价回归（verifier-platform 独立验证）
// 链路：uihdmi_tx（DUT）+ 真实 hdmi_phy_warpper/lane_lvds_10_1 + 加密 TMDS 控制器桩 + 原语桩。
// 覆盖：像素数据随机/边界码（0x000000/0xFFFFFF/单 bit 游走）、DE/VS/HS 各种组合
//       （含消隐期、全 0/全 1 长时间、中途复位、复位时输入仍在变）。
module tb_x_uihdmi;
    `include "equiv_tb_common.vh"

    reg pixel_clk = 0;
    always #10 pixel_clk = ~pixel_clk;   // 50MHz
    reg serial_clk = 0;
    always #2 serial_clk = ~serial_clk;  // 250MHz

    reg rst = 0;
    reg [23:0] video_data = 24'd0;
    reg video_de = 0, video_vs = 0, video_hs = 0;
    wire o_clk_p;
    wire [2:0] o_tx_p;

    uihdmi_tx #(.DEVICE("DR1")) dut (
        .I_pixel_clk(pixel_clk), .I_serial_clk(serial_clk), .I_rst(rst),
        .I_video_in_data(video_data), .I_video_in_de(video_de),
        .I_video_in_vs(video_vs), .I_video_in_hs(video_hs),
        .O_hdmi_clk_p(o_clk_p), .O_hdmi_tx_p(o_tx_p)
    );

    reg [31:0] lfsr = 32'h1234_ABCD;
    reg [5:0]  pix_cnt = 6'd0;

    initial equiv_trace_open;

    always @(negedge pixel_clk) begin
        cyc <= cyc + 1;
        $fdisplay(fd, "%0d %b %b %h", cyc, o_clk_p, o_tx_p, video_data);
        lfsr <= {lfsr[30:0], lfsr[31] ^ lfsr[21] ^ lfsr[1] ^ lfsr[0]};
        pix_cnt <= pix_cnt + 1'b1;

        if (cyc < 3) begin
            rst <= 1'b1;
        end else if (cyc == 120 || cyc == 121) begin
            rst <= 1'b1;                        // 中途复位（复位期间数据继续变）
            video_data <= lfsr[23:0];
            video_de <= lfsr[0];
        end else if (cyc == 122) begin
            rst <= 1'b0;
        end else begin
            rst <= 1'b0;
            // 视频时序：DE 占空比约 3/4，HS/VS 按行/帧节奏
            video_de <= (pix_cnt[1:0] != 2'b11);
            video_hs <= (pix_cnt[5:0] == 6'd0);
            video_vs <= (pix_cnt == 6'd0);
            case (pix_cnt % 8)
                0: video_data <= 24'h000000;
                1: video_data <= 24'hFFFFFF;
                2: video_data <= {8'h00, 8'h00, 8'h01} << (pix_cnt[2:0]);
                3: video_data <= lfsr[23:0];
                default: video_data <= lfsr[31:8];
            endcase
        end
        if (cyc == 5000) begin
            equiv_finish(cyc);
        end
    end
endmodule
