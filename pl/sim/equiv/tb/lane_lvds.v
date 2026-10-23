`timescale 1ns/1ps
// lane_lvds_10_1 等价回归（工程唯一配置 DEVICE="DR1"）：
// 10:1 并串转换 / load_en 产生 / 移位加载逻辑，DR1 ODDR 原语用行为桩。
module tb_lane_lvds;
    `include "equiv_tb_common.vh"

    reg         pixel_clk  = 0;
    reg         serial_clk = 0;
    reg         rst        = 1;
    reg [9:0]   data_in = 10'h000;

    wire serial_out;

    lane_lvds_10_1 #(
        .DEVICE ("DR1")
    ) dut (
        .I_pixel_clk  (pixel_clk),
        .I_serial_clk (serial_clk),
        .I_rst        (rst),
        .I_data_in    (data_in),
        .O_serial_out (serial_out)
    );

    always #5 pixel_clk  = ~pixel_clk;
    always #1 serial_clk = ~serial_clk;

    initial equiv_trace_open;

    always @(posedge serial_clk) begin
        #1;
        cyc = cyc + 1;
        $fdisplay(fd, "%0d %b", cyc, serial_out);
    end

    integer pn = 0;
    always @(negedge pixel_clk) begin
        pn = pn + 1;
        if (pn == 40)       data_in <= 10'h3FF;
        else if (pn == 80)  data_in <= 10'h000;
        else if (pn == 120) data_in <= 10'h155;
        else                data_in <= data_in + 10'h0A5;
    end

    initial begin
        rst = 1'b1;
        repeat (6) @(negedge pixel_clk);
        rst = 1'b0;
        repeat (120) @(negedge pixel_clk);
        rst = 1'b1;
        repeat (2) @(negedge pixel_clk);
        rst = 1'b0;
        repeat (180) @(negedge pixel_clk);
        equiv_finish(cyc);
    end
endmodule
