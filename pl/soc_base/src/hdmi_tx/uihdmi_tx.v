/*******************************MILIANKE*******************************
* 米联客 HDMI TX 顶层（安路 IP 封装），RGB 输入 + TMDS 控制器 + LVDS 并串。
* 本工程只用 DEVICE="DR1"（1080p60，pixel_clk 148.5MHz / serial_clk 5x）。
*********************************************************************/
`timescale 1ns / 1ns
module uihdmi_tx #(
    parameter DEVICE = "DR1"  //"EF2","EF3","EF4","SF1","EG","PH1A","PH1P","DR1","PH2A"
)(
    input  wire        I_pixel_clk,      //像素时钟
    input  wire        I_serial_clk,     //串行发送时钟（5x 像素时钟）
    input  wire        I_rst,            //异步复位信号，高电平有效
    input  wire [23:0] I_video_in_data,  //视频输入数据
    input  wire        I_video_in_de, I_video_in_vs, I_video_in_hs,
    output wire        O_hdmi_clk_p,     //HDMI 输出时钟 P 端
    output wire [2:0]  O_hdmi_tx_p       //HDMI 输出数据 P 端
);
    // 输入数据先打一拍再送 TMDS 控制器
    reg [23:0] r_video_in_data;
    always @(posedge I_pixel_clk) r_video_in_data <= I_video_in_data;

    wire [9:0] S_ch0_tmds_code_data, S_ch1_tmds_code_data, S_ch2_tmds_code_data, S_clk_tmds_code_data;

    hdmi_tx_controller_wrapper u_hdmi_tx_controller_wrapper (
        .I_pixel_clk(I_pixel_clk), .I_rst(I_rst),
        .I_video_in_data(r_video_in_data),
        .I_video_in_vs(I_video_in_vs), .I_video_in_hs(I_video_in_hs), .I_video_in_de(I_video_in_de),
        .O_ch0_tmds_code_data(S_ch0_tmds_code_data),
        .O_ch1_tmds_code_data(S_ch1_tmds_code_data),
        .O_ch2_tmds_code_data(S_ch2_tmds_code_data),
        .O_clk_tmds_code_data(S_clk_tmds_code_data)
    );

    hdmi_tx_phy_wrapper #(.DEVICE(DEVICE)) u_hdmi_tx_phy_wrapper (
        .I_pixel_clk(I_pixel_clk), .I_serial_clk(I_serial_clk), .I_rst(I_rst),
        .I_tmds_channel_0(S_ch0_tmds_code_data),
        .I_tmds_channel_1(S_ch1_tmds_code_data),
        .I_tmds_channel_2(S_ch2_tmds_code_data),
        .I_tmds_channel_clk(S_clk_tmds_code_data),
        .O_tmds_ch0_p(O_hdmi_tx_p[0]), .O_tmds_ch1_p(O_hdmi_tx_p[1]),
        .O_tmds_ch2_p(O_hdmi_tx_p[2]), .O_tmds_clk_p(O_hdmi_clk_p)
    );
endmodule
