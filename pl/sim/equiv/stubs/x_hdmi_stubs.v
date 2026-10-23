// verifier-platform 独立桩：HDMI 加密 TMDS 控制器（uihdmi_tx 内 hdmi_tx_controller_wrapper）
// golden 与工作区两侧共用；输出对每个输入都敏感（像素数据/DE/VS/HS/复位），
// 用确定性组合+打拍函数产生 4 路 10bit TMDS 码，使 uihdmi_tx 的连线/寄存器改动可在 trace 中暴露。
module hdmi_tx_controller_wrapper (
    input  wire        I_pixel_clk,
    input  wire        I_rst,
    input  wire [23:0] I_video_in_data,
    input  wire        I_video_in_vs,
    input  wire        I_video_in_hs,
    input  wire        I_video_in_de,
    output reg  [9:0]  O_ch0_tmds_code_data,
    output reg  [9:0]  O_ch1_tmds_code_data,
    output reg  [9:0]  O_ch2_tmds_code_data,
    output reg  [9:0]  O_clk_tmds_code_data
);
    reg [7:0] cnt = 8'd0;
    always @(posedge I_pixel_clk) begin
        if (I_rst) begin
            cnt <= 8'd0;
            O_ch0_tmds_code_data  <= 10'd0;
            O_ch1_tmds_code_data  <= 10'd0;
            O_ch2_tmds_code_data  <= 10'd0;
            O_clk_tmds_code_data  <= 10'd0;
        end else begin
            cnt <= cnt + 1'b1;
            O_ch0_tmds_code_data  <= {2'b10, I_video_in_data[7:0]}  ^ {8'h00, cnt[1:0]};
            O_ch1_tmds_code_data  <= {2'b01, I_video_in_data[15:8]} ^ {8'h00, I_video_in_data[9:8]};
            O_ch2_tmds_code_data  <= {2'b11, I_video_in_data[23:16]} ^ {I_video_in_de, I_video_in_vs, I_video_in_hs, 7'b000_0000};
            O_clk_tmds_code_data  <= {2'b00, cnt} ^ {8'h00, I_video_in_de, I_video_in_vs};
        end
    end
endmodule
