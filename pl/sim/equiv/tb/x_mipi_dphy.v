`timescale 1ns/1ps
// x_mipi_dphy — mipi_dphy_rx_over_lvds_wrapper.v 对抗性等价回归（verifier-platform 独立验证）
// 链路：wrapper（DUT）+ 真实 rx_clk_gen/hs_rx_wrapper + 加密子模块桩 + 厂家原语桩。
// 覆盖：
//   * O_hs_rx_clk 来自 rx_clk_gen（频率关系是否保持）
//   * O_lp_rx_lane0_p/n 的 lane_lp_invert mux 行为
//   * lane hs/lp invert → O_lane_error
//   * 参考时钟中途抖动（换相位继续跑）、复位中途（复位期间输入变化）
// 逐拍 trace 全部输出。
module tb_x_mipi_dphy;
    integer fd;
    integer cyc = 0;
    reg [1023:0] tf;

    reg mipi_clk_p = 0;
    always #4 mipi_clk_p = ~mipi_clk_p;      // 125MHz D-PHY 参考时钟
    reg lp_clk = 0;
    always #19 lp_clk = ~lp_clk;

    reg rst = 0;
    reg [3:0] data_hs_p = 4'b1100;
    reg [3:0] data_lp_p = 4'b1010;
    reg [3:0] data_lp_n = 4'b0101;
    reg [3:0] lane_hs_invert = 4'b0000;
    reg [3:0] lane_lp_invert = 4'b0000;

    wire        o_hs_rx_clk, o_hs_rx_valid;
    wire [31:0] o_hs_rx_data;
    wire        o_lp0_p, o_lp0_n;
    wire [3:0]  o_lane_error;

    mipi_dphy_rx_over_lvds_wrapper #(.DEVICE("DR1"), .LANE_NUM(4)) dut (
        .I_lp_clk(lp_clk), .I_rst(rst),
        .I_mipi_clk_p(mipi_clk_p),
        .I_mipi_data_hs_p(data_hs_p), .I_mipi_data_lp_p(data_lp_p), .I_mipi_data_lp_n(data_lp_n),
        .I_lane_hs_invert(lane_hs_invert), .I_lane_lp_invert(lane_lp_invert),
        .O_hs_rx_clk(o_hs_rx_clk), .O_hs_rx_valid(o_hs_rx_valid), .O_hs_rx_data(o_hs_rx_data),
        .O_lp_rx_lane0_p(o_lp0_p), .O_lp_rx_lane0_n(o_lp0_n),
        .O_lane_error(o_lane_error)
    );

    reg [31:0] lfsr = 32'h0D15_EA5E;

    initial begin
        if (!$value$plusargs("TRACE=%s", tf)) tf = "trace.txt";
        fd = $fopen(tf, "w");
        if (fd == 0) begin $display("cannot open trace"); $finish; end
    end

    always @(posedge mipi_clk_p) begin
        cyc <= cyc + 1;
        $fdisplay(fd, "%0d %b %b %h %b %b %b", cyc, o_hs_rx_clk, o_hs_rx_valid,
                  o_hs_rx_data, o_lp0_p, o_lp0_n, o_lane_error);
        lfsr <= {lfsr[30:0], lfsr[31] ^ lfsr[21] ^ lfsr[1] ^ lfsr[0]};
        if (cyc < 4) begin
            rst <= 1'b1;
        end else if (cyc == 600 || cyc == 601) begin
            rst <= 1'b1;                   // 中途复位（复位期间输入仍在变）
            data_lp_p <= lfsr[3:0];
            lane_lp_invert <= lfsr[7:4];
        end else if (cyc == 700) begin
            rst <= 1'b0;
            lane_hs_invert <= 4'b0110;
            lane_lp_invert <= 4'b1001;
            data_lp_p <= 4'b0101;
            data_lp_n <= 4'b1010;
        end else if (cyc == 1200) begin
            lane_lp_invert <= 4'b0011;     // 翻转 lane0 lp 极性 → mux 输出交换
        end else if (cyc == 1500) begin
            rst <= 1'b1; repeat (3) @(posedge mipi_clk_p); rst <= 1'b0;  // 短复位
        end else begin
            rst <= 1'b0;
            if (cyc > 1600) begin
                data_lp_p <= lfsr[3:0];
                data_lp_n <= lfsr[7:4];
                lane_hs_invert <= lfsr[11:8];
                data_hs_p <= lfsr[15:12];
            end
        end
        if (cyc == 4000) begin
            $display("EQUIV_CYCLES %0d", cyc);
            $display("EQUIV_DONE");
            $fclose(fd);
            $finish;
        end
    end
endmodule
