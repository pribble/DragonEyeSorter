`timescale 1ns/1ps
// x_lane_lvds — lane_lvds_10_1.v 对抗性等价回归（verifier-platform 独立验证）
// 覆盖：pixel/serial 双时钟频率关系、10bit 数据移位/ODDR 相位、中途复位（不同相位）、
//       连续数据突发与长时间空闲、DEVICE 实际值 DR1（ODDR 原语用桩，两侧共用）。
module tb_x_lane_lvds;
    integer fd;
    integer cyc = 0;
    reg [1023:0] tf;

    reg pixel_clk = 0;
    always #10 pixel_clk = ~pixel_clk;   // 50MHz 像素时钟
    reg serial_clk = 0;
    always #2 serial_clk = ~serial_clk;  // 250MHz 串行时钟

    reg rst = 0;
    reg [9:0] data_in = 10'd0;
    wire serial_out;

    lane_lvds_10_1 #(.DEVICE("DR1")) dut (
        .I_pixel_clk(pixel_clk), .I_serial_clk(serial_clk), .I_rst(rst),
        .I_data_in(data_in), .O_serial_out(serial_out)
    );

    reg [31:0] lfsr = 32'h1BAD_D00D;

    initial begin
        if (!$value$plusargs("TRACE=%s", tf)) tf = "trace.txt";
        fd = $fopen(tf, "w");
        if (fd == 0) begin $display("cannot open trace"); $finish; end
    end

    always @(negedge serial_clk) begin
        cyc <= cyc + 1;
        $fdisplay(fd, "%0d %b %b", cyc, serial_out, data_in[0]);
        lfsr <= {lfsr[30:0], lfsr[31] ^ lfsr[21] ^ lfsr[1] ^ lfsr[0]};
        if (cyc < 3) rst <= 1'b1;
        else if (cyc == 1000 || cyc == 1001) rst <= 1'b1;
        else rst <= 1'b0;
        if (cyc == 1987) rst <= 1'b1;         // 另一次复位
        if (cyc > 2000 && cyc < 2600) data_in <= lfsr[9:0];   // 数据突发
        if (cyc == 2600) data_in <= 10'b1010_1010_10;
        if (cyc == 3000) data_in <= 10'b0101_0101_01;
        if (cyc == 3500) data_in <= 10'h3FF;
        if (cyc == 3800) data_in <= 10'h000;
        if (cyc == 3999) begin
            $display("EQUIV_CYCLES %0d", cyc);
            $display("EQUIV_DONE");
            $fclose(fd);
            $finish;
        end
    end
endmodule
