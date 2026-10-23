`timescale 1ns/1ps
// x_hs_rx — hs_rx_wrapper.sv 对抗性等价回归（verifier-platform 独立验证）
// 数据通道用 x_mipi_enc_stubs.v（golden/新两侧共用）；覆盖：
//   * lane hs/lp invert 组合随机变化（O_lane_error 与数据通道极性）
//   * 4 个数据 lane 的字节拼接顺序（O_hs_data 每 lane 位置必须一致）
//   * 3 个时钟不同相位 + 中途复位（复位与时钟沿不同相位）
// 逐拍 trace O_hs_valid / O_hs_data / O_lane_error。
module tb_x_hs_rx;
    integer fd;
    integer cyc = 0;
    reg [1023:0] tf;

    reg hs_clk = 0, hs_clk_x2 = 0, hs_clk_x4 = 0, lp_clk = 0;
    always #10 hs_clk    = ~hs_clk;      // 50MHz
    always #5  hs_clk_x2 = ~hs_clk_x2;   // 100MHz
    always #2.5 hs_clk_x4 = ~hs_clk_x4;  // 200MHz
    always #17 lp_clk    = ~lp_clk;      // ~29MHz 独立低速时钟

    reg rst = 0;
    reg [3:0] lane_hs_invert = 4'b0000;
    reg [3:0] lane_lp_invert = 4'b0000;
    // 用引脚给 4 个 lane 编码 lane 号（data_lane 桩按 {hs_p,lp_p} 取字节）
    reg [3:0] mipi_hs_p = 4'b1100;
    reg [3:0] mipi_lp_p = 4'b1010;
    reg [3:0] mipi_lp_n = 4'b0101;

    wire       o_hs_valid;
    wire [31:0] o_hs_data;
    wire [3:0] o_lane_error;

    hs_rx_wrapper #(.LANE_NUM(4), .DEVICE("DR1")) dut (
        .I_hs_clk(hs_clk), .I_hs_clk_x2(hs_clk_x2), .I_hs_clk_x4(hs_clk_x4),
        .I_lp_clk(lp_clk), .I_rst(rst),
        .I_lane_hs_invert(lane_hs_invert), .I_lane_lp_invert(lane_lp_invert),
        .I_mipi_hs_p(mipi_hs_p), .I_mipi_lp_p(mipi_lp_p), .I_mipi_lp_n(mipi_lp_n),
        .O_hs_valid(o_hs_valid), .O_hs_data(o_hs_data), .O_lane_error(o_lane_error)
    );

    reg [31:0] lfsr = 32'h5EED_0BAD;

    initial begin
        if (!$value$plusargs("TRACE=%s", tf)) tf = "trace.txt";
        fd = $fopen(tf, "w");
        if (fd == 0) begin $display("cannot open trace"); $finish; end
    end

    always @(posedge hs_clk) begin
        cyc <= cyc + 1;
        $fdisplay(fd, "%0d %b %h %b", cyc, o_hs_valid, o_hs_data, o_lane_error);
        lfsr <= {lfsr[30:0], lfsr[31] ^ lfsr[21] ^ lfsr[1] ^ lfsr[0]};
        if (cyc < 3) begin
            rst <= 1'b0;
            lane_hs_invert <= 4'b0000;
            lane_lp_invert <= 4'b0000;
        end else if (cyc == 8000 || cyc == 8001 || cyc == 8002) begin
            rst <= 1'b0;
        end else if (cyc == 9000) begin
            // 极性组合 + 引脚编码变化（数据 lane 字节顺序随之改变）
            lane_hs_invert <= 4'b0101;
            lane_lp_invert <= 4'b0011;
            mipi_hs_p <= 4'b0101;
            mipi_lp_p <= 4'b1100;
            mipi_lp_n <= 4'b1010;
        end else if (cyc >= 12000) begin
            lane_hs_invert <= 4'b1111;
            lane_lp_invert <= 4'b0000;
        end else begin
            rst <= 1'b1;
            lane_hs_invert <= lfsr[3:0];
            lane_lp_invert <= lfsr[7:4];
        end
        if (cyc == 16000) begin
            $display("EQUIV_CYCLES %0d", cyc);
            $display("EQUIV_DONE");
            $fclose(fd);
            $finish;
        end
    end
endmodule
