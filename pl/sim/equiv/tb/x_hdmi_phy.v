`timescale 1ns/1ps
// x_hdmi_phy — hdmi_phy_warpper.v 对抗性等价回归（verifier-platform 独立验证）
// 覆盖：4 条 lane 的 bit 反转连线（I_tmds_channel_x[0..9] 逐位重排）、
//       随机码字 + 固定边界码字（全 0/全 1/单 bit 游走）、中途复位、双时钟相位。
module tb_x_hdmi_phy;
    integer fd;
    integer cyc = 0;
    reg [1023:0] tf;

    reg pixel_clk = 0;
    always #10 pixel_clk = ~pixel_clk;
    reg serial_clk = 0;
    always #2 serial_clk = ~serial_clk;

    reg rst = 0;
    reg [9:0] ch0 = 0, ch1 = 0, ch2 = 0, chc = 0;
    wire o0, o1, o2, oc;

    hdmi_tx_phy_wrapper #(.DEVICE("DR1")) dut (
        .I_pixel_clk(pixel_clk), .I_serial_clk(serial_clk), .I_rst(rst),
        .I_tmds_channel_0(ch0), .I_tmds_channel_1(ch1),
        .I_tmds_channel_2(ch2), .I_tmds_channel_clk(chc),
        .O_tmds_ch0_p(o0), .O_tmds_ch1_p(o1), .O_tmds_ch2_p(o2), .O_tmds_clk_p(oc)
    );

    reg [31:0] lfsr = 32'hF00D_5EED;
    reg [3:0] bit_walk = 4'd0;

    initial begin
        if (!$value$plusargs("TRACE=%s", tf)) tf = "trace.txt";
        fd = $fopen(tf, "w");
        if (fd == 0) begin $display("cannot open trace"); $finish; end
    end

    always @(negedge serial_clk) begin
        cyc <= cyc + 1;
        $fdisplay(fd, "%0d %b %b %b %b", cyc, o0, o1, o2, oc);
        lfsr <= {lfsr[30:0], lfsr[31] ^ lfsr[21] ^ lfsr[1] ^ lfsr[0]};
        if (cyc < 3) rst <= 1'b1;
        else if (cyc == 1500 || cyc == 1501) rst <= 1'b1;
        else rst <= 1'b0;
        if (cyc == 1000) begin
            ch0 <= 10'h000; ch1 <= 10'h3FF; ch2 <= 10'h2AA; chc <= 10'h155;
        end else if (cyc == 2000) begin
            ch0 <= 10'h3FF; ch1 <= 10'h000; ch2 <= 10'h155; chc <= 10'h2AA;
        end else if (cyc == 2400) begin
            ch0 <= (10'b1 << bit_walk); ch1 <= ~(10'b1 << bit_walk);
            ch2 <= 10'h001 << bit_walk; chc <= 10'h200 >> bit_walk;
            bit_walk <= bit_walk + 1'b1;
        end else if (cyc > 3000) begin
            ch0 <= lfsr[9:0]; ch1 <= lfsr[19:10]; ch2 <= lfsr[29:20]; chc <= lfsr[9:0] ^ lfsr[19:10];
        end
        if (cyc == 3999) begin
            $display("EQUIV_CYCLES %0d", cyc);
            $display("EQUIV_DONE");
            $fclose(fd);
            $finish;
        end
    end
endmodule
