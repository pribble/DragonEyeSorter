`timescale 1ns/1ps
// design_top_wrapper 等价回归：MIPI→ISP 顶层胶水。
// PLL.v 用真文件（其 TD 门级 PLL / BUFG 原语用行为桩），MIPI 加密子模块与
// raw10_unpacket_4lane / image_correction / isp_top 用行为桩（isp 域并发重构，隔离）。
// 验证：PLL 时钟/锁定→复位链、hs_rx 时钟域 CSI 解析、RAW10/图像修正/ISP 各级连线。
module tb_design_top_wrapper;
    `include "equiv_tb_common.vh"

    reg         sys_clk   = 0;   // 156M 参考（行为上按 50MHz 周期驱动）
    reg         clk_hs    = 0;
    reg         rst_n     = 0;
    reg [3:0]   data_hs_p = 4'h0;
    reg [3:0]   data_lp_p = 4'h0;
    reg [3:0]   data_lp_n = 4'h0;

    wire        pll_lock;
    wire [3:0]  lane_error;
    wire        hs_rx_clk;
    wire [127:0] isp_tdata;
    wire        isp_tlast;
    wire        isp_tuser;
    wire        isp_tvalid;
    wire        imx334_clk;

    design_top_wrapper dut (
        .I_sys_clk_156m  (sys_clk),
        .O_imx334_clk_24m(imx334_clk),
        .I_clk_hs_p      (clk_hs),
        .I_data_hs_p     (data_hs_p),
        .I_data_lp_p     (data_lp_p),
        .I_data_lp_n     (data_lp_n),
        .S_pll_lock      (pll_lock),
        .S_lane_error    (lane_error),
        .S_hs_rx_clk     (hs_rx_clk),
        .O_ISP_tdata     (isp_tdata),
        .O_ISP_tlast     (isp_tlast),
        .O_ISP_tuser     (isp_tuser),
        .O_ISP_tvalid    (isp_tvalid)
    );

    always #10 sys_clk = ~sys_clk;   // 20ns
    always #2  clk_hs  = ~clk_hs;    // 4ns

    initial equiv_trace_open;

    always @(posedge clk_hs) begin
        #1;
        cyc = cyc + 1;
        $fdisplay(fd, "%0d %b %b %b %h %b %b %b %b",
                  cyc, pll_lock, lane_error, hs_rx_clk, isp_tdata,
                  isp_tlast, isp_tuser, isp_tvalid, imx334_clk);
    end

    integer n = 0;
    always @(negedge clk_hs) begin
        n = n + 1;
        data_hs_p <= data_hs_p + 4'h1;
        data_lp_p <= data_lp_p + 4'h2;
        data_lp_n <= data_lp_n + 4'h7;
    end

    initial begin
        rst_n     = 1'b0;
        data_hs_p = 4'h0;
        data_lp_p = 4'h0;
        data_lp_n = 4'h0;
        repeat (3000) @(posedge clk_hs);
        equiv_finish(cyc);
    end
endmodule
