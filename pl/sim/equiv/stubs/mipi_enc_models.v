// 行为桩（仅 pl/sim/equiv 等价回归用；golden 与工作区构建共用同一份）：
//   data_lane_rx_wrapper    — 加密 D-PHY 数据 lane（hs_rx_wrapper 内每 lane 一份）
//   channel_aligner_wrapper — 加密多通道对齐（hs_rx_wrapper 内一份）
// 假设：输出只由输入引脚 / invert 位 / 本地计数决定，复位后确定、可复现；
//       不模拟真实 D-PHY 采样时序（等价只验证明文 wrapper 的连线 / 合并 / valid 逻辑）。
// 端口名/方向/位宽取自加密文件头（data_lane_rx_wrapper.enc.v、channel_aligner_wrapper.enc.sv）。

module data_lane_rx_wrapper #(
    parameter DEVICE = "EG"
)(
    input wire       I_hs_clk_x4,
    input wire       I_hs_clk_x2,
    input wire       I_hs_clk,
    input wire       I_lp_clk,
    input wire       I_rst,

    input wire       I_lane_hs_invert,
    input wire       I_lane_lp_invert,

    input wire       I_mipi_hs_p,
    input wire       I_mipi_lp_p,
    input wire       I_mipi_lp_n,

    output wire      O_hs_valid,
    output wire[7:0] O_hs_data,

    output wire      O_lane_error
);
    reg [7:0] r_data;
    reg [3:0] r_cnt;
    reg       r_valid;

    initial begin
        r_data  = 8'h00;
        r_cnt   = 4'h0;
        r_valid = 1'b0;
    end

    always @(posedge I_hs_clk or posedge I_rst) begin
        if (I_rst) begin
            r_data  <= 8'h00;
            r_cnt   <= 4'h0;
            r_valid <= 1'b0;
        end
        else begin
            r_cnt   <= r_cnt + 1'b1;
            r_data  <= {I_lane_hs_invert, I_lane_lp_invert, I_mipi_hs_p,
                        I_mipi_lp_p, I_mipi_lp_n, r_cnt[2:0]};
            // 4 lane 同相位 valid（真实 D-PHY 各 lane 同拍有效），便于覆盖 & 归约的 0/1
            r_valid <= r_cnt[1];
        end
    end

    assign O_hs_valid   = r_valid;
    assign O_hs_data    = r_data;
    assign O_lane_error = I_lane_hs_invert ^ I_mipi_lp_n;
endmodule

module channel_aligner_wrapper #(
    parameter CH_NUM     = 4,
    parameter DATA_WIDTH = 8
)(
    input wire                  I_clk,
    input wire                  I_rst_n,

    input wire[CH_NUM-1:0]      I_ch_valid,
    input wire[DATA_WIDTH-1:0]  I_ch_data[CH_NUM-1:0],

    output reg[CH_NUM-1:0]      O_ch_aligner_valid,
    output reg[DATA_WIDTH-1:0]  O_ch_aligner_data[CH_NUM-1:0]
);
    integer k;

    initial begin
        O_ch_aligner_valid = {CH_NUM{1'b0}};
        for (k = 0; k < CH_NUM; k = k + 1)
            O_ch_aligner_data[k] = {DATA_WIDTH{1'b0}};
    end

    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            O_ch_aligner_valid <= {CH_NUM{1'b0}};
            for (k = 0; k < CH_NUM; k = k + 1)
                O_ch_aligner_data[k] <= {DATA_WIDTH{1'b0}};
        end
        else begin
            O_ch_aligner_valid <= I_ch_valid;
            for (k = 0; k < CH_NUM; k = k + 1)
                O_ch_aligner_data[k] <= I_ch_data[k] ^ (k + 8'h20);
        end
    end
endmodule
