// 行为桩（仅 pl/sim/equiv 等价回归用；golden 与工作区构建共用同一份）：
//   DR1_PHY_LCLK  — rx_clk_gen 的 DR1 时钟分频原语
//   DR1_LOGIC_ODDR— lane_lvds_10_1 的 DR1 DDR 输出原语
// 假设：按端口名给出确定、可复现的时钟/数据行为，不模拟真实 TD 原语时序。
// 端口名/方向/位宽取自 golden rev 9a905cc 的例化处。

module DR1_PHY_LCLK #(
    parameter CEMD   = "SYNC",
    parameter CLKSEL = "ORG",
    parameter DIV    = 2,
    parameter LCLK   = "BYPASS"
)(
    input wire clkin,
    input wire rst,
    input wire ce,

    output wire clkout,
    output wire clkdivout
);
    reg [3:0] r_cnt;

    initial r_cnt = 4'h0;

    always @(posedge clkin or posedge rst) begin
        if (rst)
            r_cnt <= 4'h0;
        else if (ce)
            r_cnt <= r_cnt + 1'b1;
    end

    assign clkout    = clkin;                    // LCLK = BYPASS
    assign clkdivout = (DIV == 2) ? r_cnt[0] : r_cnt[1];
endmodule

module DR1_LOGIC_ODDR (
    input wire clk,
    input wire rst,
    input wire d1,
    input wire d0,

    output wire q
);
    reg r_q;

    initial r_q = 1'b0;

    always @(clk or posedge rst) begin
        if (rst)
            r_q <= 1'b0;
        else if (clk)
            r_q <= d1;                           // 上升沿取 d1
        else
            r_q <= d0;                           // 下降沿取 d0
    end

    assign q = r_q;
endmodule
