// hdmi_tx_phy_wrapper: bit-reverses the 4 TMDS words and serializes them with
// 4 lane_lvds_10_1 instances. Only DEVICE="DR1" is supported (the single
// configuration used by uihdmi_tx); the parameter is kept for interface compatibility.
module hdmi_tx_phy_wrapper #(
    parameter DEVICE = "PH1P"   // kept for interface compatibility; only DR1 is supported
)(
    input wire      I_pixel_clk,
    input wire      I_serial_clk,
    input wire      I_rst,

    input wire[9:0] I_tmds_channel_0,
    input wire[9:0] I_tmds_channel_1,
    input wire[9:0] I_tmds_channel_2,
    input wire[9:0] I_tmds_channel_clk,

    output wire     O_tmds_ch0_p,
    output wire     O_tmds_ch1_p,
    output wire     O_tmds_ch2_p,
    output wire     O_tmds_clk_p
);
    wire[9:0] S_tmds_data_ch0;
    wire[9:0] S_tmds_data_ch1;
    wire[9:0] S_tmds_data_ch2;
    wire[9:0] S_tmds_data_clk;

    // the serializer consumes the TMDS word LSB-first, so reverse the bit order
    genvar j;
    generate
        for (j = 0; j < 10; j = j + 1) begin : TMDS_BIT_REV
            assign S_tmds_data_ch0[j] = I_tmds_channel_0[9-j];
            assign S_tmds_data_ch1[j] = I_tmds_channel_1[9-j];
            assign S_tmds_data_ch2[j] = I_tmds_channel_2[9-j];
            assign S_tmds_data_clk[j] = I_tmds_channel_clk[9-j];
        end
    endgenerate

    lane_lvds_10_1 #(
        .DEVICE (DEVICE)
    ) u0_lane_lvds_10_1 (
        .I_pixel_clk  (I_pixel_clk),
        .I_serial_clk (I_serial_clk),
        .I_rst        (I_rst),

        .I_data_in    (S_tmds_data_ch0),
        .O_serial_out (O_tmds_ch0_p)
    );

    lane_lvds_10_1 #(
        .DEVICE (DEVICE)
    ) u1_lane_lvds_10_1 (
        .I_pixel_clk  (I_pixel_clk),
        .I_serial_clk (I_serial_clk),
        .I_rst        (I_rst),

        .I_data_in    (S_tmds_data_ch1),
        .O_serial_out (O_tmds_ch1_p)
    );

    lane_lvds_10_1 #(
        .DEVICE (DEVICE)
    ) u2_lane_lvds_10_1 (
        .I_pixel_clk  (I_pixel_clk),
        .I_serial_clk (I_serial_clk),
        .I_rst        (I_rst),

        .I_data_in    (S_tmds_data_ch2),
        .O_serial_out (O_tmds_ch2_p)
    );

    lane_lvds_10_1 #(
        .DEVICE (DEVICE)
    ) u3_lane_lvds_10_1 (
        .I_pixel_clk  (I_pixel_clk),
        .I_serial_clk (I_serial_clk),
        .I_rst        (I_rst),

        .I_data_in    (S_tmds_data_clk),
        .O_serial_out (O_tmds_clk_p)
    );
endmodule
