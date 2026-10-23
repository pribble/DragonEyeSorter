`timescale 1ns/1ps
// x_design_top — design_top_wrapper.v 对抗性等价回归（verifier-platform 独立验证）
// 整条 MIPI 明文链路：design_top_wrapper（DUT）+ 真实 dphy wrapper/hs_rx_wrapper/rx_clk_gen/csi_unpacket
// + 加密子模块桩 + raw10/image_correction/isp_top 桩（隔离 ISP 域重构）+ PLL/原语桩。
// 覆盖：PLL lock → S_rst 反相复位释放；lane_error 引出；ISP 输出通道位宽/连线；
//       O_imx334_clk_24m；CSI 包流（由 data_lane 桩按 CSI-2 包序列产生）。
// 逐拍 trace 全部输出；golden vs 新必须逐字节一致。
module tb_x_design_top;
    integer fd;
    integer cyc = 0;
    reg [1023:0] tf;

    reg sys_clk = 0;
    always #7 sys_clk = ~sys_clk;        // 156MHz 参考（周期 14ns）

    reg mipi_clk = 0;
    always #4 mipi_clk = ~mipi_clk;      // D-PHY 参考时钟

    // 用引脚编码 4 个 data lane 的 lane 号（0..3）：{hs_p, lp_p}
    reg [3:0] data_hs_p = 4'b1100;
    reg [3:0] data_lp_p = 4'b1010;
    reg [3:0] data_lp_n = 4'b0000;

    wire        s_pll_lock;
    wire [3:0]  s_lane_error;
    wire        s_hs_rx_clk;
    wire [127:0] o_isp_tdata;
    wire        o_isp_tlast, o_isp_tuser, o_isp_tvalid;
    wire        o_imx334_clk_24m;

    design_top_wrapper dut (
        .I_sys_clk_156m(sys_clk),
        .O_imx334_clk_24m(o_imx334_clk_24m),
        .I_clk_hs_p(mipi_clk),
        .I_data_hs_p(data_hs_p),
        .I_data_lp_p(data_lp_p),
        .I_data_lp_n(data_lp_n),
        .S_pll_lock(s_pll_lock),
        .S_lane_error(s_lane_error),
        .S_hs_rx_clk(s_hs_rx_clk),
        .O_ISP_tdata(o_isp_tdata),
        .O_ISP_tlast(o_isp_tlast),
        .O_ISP_tuser(o_isp_tuser),
        .O_ISP_tvalid(o_isp_tvalid)
    );

    reg [31:0] lfsr = 32'hBAD0_F00D;

    initial begin
        if (!$value$plusargs("TRACE=%s", tf)) tf = "trace.txt";
        fd = $fopen(tf, "w");
        if (fd == 0) begin $display("cannot open trace"); $finish; end
    end

    // 在 mipi_clk 下降沿采样（DUT 内部时钟由它分频产生，下降沿时全部同步输出已稳定）
    always @(negedge mipi_clk) begin
        cyc <= cyc + 1;
        $fdisplay(fd, "%0d %b %b %b %h %b %b %b %b %b", cyc,
                  s_pll_lock, s_lane_error, s_hs_rx_clk,
                  o_isp_tdata, o_isp_tlast, o_isp_tuser, o_isp_tvalid,
                  o_imx334_clk_24m, sys_clk);
        lfsr <= {lfsr[30:0], lfsr[31] ^ lfsr[21] ^ lfsr[1] ^ lfsr[0]};
        if (cyc > 3000) begin
            data_lp_p <= lfsr[3:0];
            data_lp_n <= lfsr[7:4];
            data_hs_p <= lfsr[11:8];
        end
        if (cyc == 30000) begin
            $display("EQUIV_CYCLES %0d", cyc);
            $display("EQUIV_DONE");
            $fclose(fd);
            $finish;
        end
    end
endmodule
