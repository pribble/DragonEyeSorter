// 行为桩（仅 design_top_wrapper 等价回归用；golden 与工作区构建共用同一份）：
//   data_lane_rx_wrapper / channel_aligner_wrapper — 加密 MIPI RX 子模块，改产 CSI 包流
//   raw10_unpacket_4lane / image_correction / isp_top — 明文但正被 isp 域并发重构，
//   这里按同端口桩隔离，只验证 design_top_wrapper 自身胶水（连线 / 时钟 / 复位 / 层次）。
// 假设：所有输出由本地计数器与输入决定，复位后确定、可复现；不代表真实算法时序。
// 端口名/方向/位宽取自 golden rev 9a905cc 的模块头。

// ---------------------------------------------------------------- MIPI RX 桩
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
    // 周期性 CSI 字节流：0x00=帧开始，0x01=帧结束，0x2B=RAW10 长包，其余为载荷。
    // 每 5 拍插入 1 拍 valid 空档，使 csi_unpacket 的上升沿检测扫到不同字节相位。
    reg [7:0] r_data;
    reg [2:0] r_byte_cnt;
    reg [2:0] r_gap;
    reg       r_valid;

    initial begin
        r_data     = 8'h00;
        r_byte_cnt = 3'd0;
        r_gap      = 3'd0;
        r_valid    = 1'b0;
    end

    always @(posedge I_hs_clk or posedge I_rst) begin
        if (I_rst) begin
            r_data     <= 8'h00;
            r_byte_cnt <= 3'd0;
            r_gap      <= 3'd0;
            r_valid    <= 1'b0;
        end
        else begin
            r_byte_cnt <= r_byte_cnt + 1'b1;
            r_gap      <= (r_gap == 3'd4) ? 3'd0 : r_gap + 1'b1;
            r_data     <= (r_byte_cnt == 3'd0) ? 8'h00 :
                          (r_byte_cnt == 3'd1) ? 8'h01 :
                          (r_byte_cnt == 3'd2) ? 8'h2B : {5'b0, r_byte_cnt};
            r_valid    <= (r_gap != 3'd4);
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
                O_ch_aligner_data[k] <= I_ch_data[k];
        end
    end
endmodule

// ---------------------------------------------------------------- ISP 链桩
module raw10_unpacket_4lane (
    input wire        I_clk,
    input wire        I_rst_n,

    input wire        I_csi_frame_start,
    input wire        I_csi_frame_end,
    input wire        I_csi_valid,
    input wire[31:0]  I_csi_data,

    output reg        O_raw10_frame_start,
    output reg        O_raw10_frame_end,
    output reg        O_raw10_valid,
    output reg[39:0]  O_raw10_data
);
    initial begin
        O_raw10_frame_start = 1'b0;
        O_raw10_frame_end   = 1'b0;
        O_raw10_valid       = 1'b0;
        O_raw10_data        = 40'd0;
    end

    always @(posedge I_clk) begin
        O_raw10_frame_start <= I_csi_frame_start;
        O_raw10_frame_end   <= I_csi_frame_end;
        O_raw10_valid       <= I_csi_valid;
        O_raw10_data        <= {I_csi_data, I_csi_data[7:0]};
    end
endmodule

module image_correction #(
    parameter DATA_WIDTH   = 40,
    parameter TDEST_WIDTH  = 10
)(
    input wire                       I_clk,
    input wire                       I_rst_n,

    input wire[DATA_WIDTH-1:0]       I_raw_data,
    input wire                       I_raw_valid,
    input wire                       I_raw_frame_start,
    input wire                       I_raw_frame_end,

    output reg[DATA_WIDTH-1:0]       O_raw_tdata,
    output reg                       O_raw_tlast,
    output reg[TDEST_WIDTH-1:0]      O_raw_tdest,
    output reg                       O_raw_tvalid,
    output reg                       O_raw_tuser,
    input wire                       O_raw_tready
);
    reg [3:0] r_cnt;

    initial begin
        r_cnt        = 4'h0;
        O_raw_tdata  = {DATA_WIDTH{1'b0}};
        O_raw_tlast  = 1'b0;
        O_raw_tdest  = {TDEST_WIDTH{1'b0}};
        O_raw_tvalid = 1'b0;
        O_raw_tuser  = 1'b0;
    end

    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            r_cnt       <= 4'h0;
            O_raw_tdata <= {DATA_WIDTH{1'b0}};
            O_raw_tlast <= 1'b0;
            O_raw_tdest <= {TDEST_WIDTH{1'b0}};
            O_raw_tvalid<= 1'b0;
            O_raw_tuser <= 1'b0;
        end
        else begin
            r_cnt        <= r_cnt + 1'b1;
            O_raw_tdata  <= I_raw_data + r_cnt;
            O_raw_tlast  <= I_raw_valid & (r_cnt == 4'hF);
            O_raw_tdest  <= r_cnt;
            O_raw_tvalid <= I_raw_valid;
            O_raw_tuser  <= I_raw_frame_start;
        end
    end
endmodule

module isp_top (
    input wire          axi4s_video_aclk,
    input wire          I_rst_n,
    input wire          I_tlast,
    input wire          I_tuser,
    input wire[39:0]    I_tdata,
    input wire          I_tvalid,
    input wire[9:0]     I_tdest,
    input wire          O_tready,

    output reg[127:0]   O_tdata,
    output reg          O_tlast,
    output reg          O_tuser,
    output reg          O_tvalid,
    output reg          I_tready
);
    reg [2:0] r_cnt;

    initial begin
        r_cnt    = 3'd0;
        O_tdata  = 128'd0;
        O_tlast  = 1'b0;
        O_tuser  = 1'b0;
        O_tvalid = 1'b0;
        I_tready = 1'b0;
    end

    always @(posedge axi4s_video_aclk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            r_cnt    <= 3'd0;
            O_tdata  <= 128'd0;
            O_tlast  <= 1'b0;
            O_tuser  <= 1'b0;
            O_tvalid <= 1'b0;
            I_tready <= 1'b0;
        end
        else begin
            r_cnt    <= r_cnt + 1'b1;
            O_tdata  <= {I_tdata, I_tdata, I_tdata, {5'b0, r_cnt}};
            O_tlast  <= I_tlast;
            O_tuser  <= I_tuser;
            O_tvalid <= I_tvalid;
            I_tready <= O_tready;
        end
    end
endmodule
