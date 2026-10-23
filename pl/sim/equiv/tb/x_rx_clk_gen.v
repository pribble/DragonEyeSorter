`timescale 1ns/1ps
// x_rx_clk_gen — rx_clk_gen.v 对抗性等价回归（verifier-platform 独立验证）
// 注意：dev 把非 DEVICE="DR1" 的 generate 分支做了「单配置特化」（只保留 DR1，参数保留作接口兼容）。
// 本 test 因此只按工程实际配置 DEVICE="DR1" 做逐拍等价；特化本身在验证报告里单独记录为「参数语义变化」。
// 覆盖：三组 DR1 实例（不同参考时钟频率/相位、独立复位序列）：
//       复位中途（不同相位）、复位期间时钟继续、1 拍复位、长复位、稳定后的频率/相位关系。
module tb_x_rx_clk_gen;
    `include "equiv_tb_common.vh"

    reg mipi_clk_a = 0;   // 参考时钟 A：周期 10ns
    always #5 mipi_clk_a = ~mipi_clk_a;

    reg mipi_clk_b = 0;   // 参考时钟 B：周期 14ns（不同相位/频率）
    always #7 mipi_clk_b = ~mipi_clk_b;

    reg rst_a = 0, rst_b = 0, rst_c = 0;
    wire oa, oa2, oa4;
    wire ob, ob2, ob4;
    wire oc, oc2, oc4;

    rx_clk_gen #(.DEVICE("DR1")) u_a (.I_mipi_clk_p(mipi_clk_a), .I_rst(rst_a), .O_clk(oa),  .O_clk_x2(oa2), .O_clk_x4(oa4));
    rx_clk_gen #(.DEVICE("DR1")) u_b (.I_mipi_clk_p(mipi_clk_b), .I_rst(rst_b), .O_clk(ob),  .O_clk_x2(ob2), .O_clk_x4(ob4));
    rx_clk_gen #(.DEVICE("DR1")) u_c (.I_mipi_clk_p(mipi_clk_a), .I_rst(rst_c), .O_clk(oc),  .O_clk_x2(oc2), .O_clk_x4(oc4));

    initial equiv_trace_open;

    // 在 a/b 的下降沿采样（覆盖两个时钟域）
    always @(negedge mipi_clk_a or negedge mipi_clk_b) begin
        cyc <= cyc + 1;
        $fdisplay(fd, "%0d %b %b %b %b %b %b %b %b %b", cyc,
                  oa, oa2, oa4, ob, ob2, ob4, oc, oc2, oc4);
    end

    initial begin
        rst_a = 0; rst_b = 0; rst_c = 0;
        repeat (3) @(posedge mipi_clk_a);
        rst_a = 1; rst_b = 1; rst_c = 1;
        repeat (300) @(posedge mipi_clk_a);
        rst_a = 0; repeat (7) @(posedge mipi_clk_a);  rst_a = 1;   // 中途复位
        repeat (211) @(posedge mipi_clk_a);
        rst_b = 0; repeat (2) @(posedge mipi_clk_a);  rst_b = 1;   // 短复位（另一时钟域）
        repeat (93)  @(posedge mipi_clk_a);
        rst_a = 0; repeat (1) @(posedge mipi_clk_a);  rst_a = 1;   // 1 拍复位
        repeat (141) @(posedge mipi_clk_a);
        rst_c = 0; repeat (11) @(posedge mipi_clk_a); rst_c = 1;   // 长复位
        repeat (500) @(posedge mipi_clk_a);
        equiv_finish(cyc);
    end
endmodule
