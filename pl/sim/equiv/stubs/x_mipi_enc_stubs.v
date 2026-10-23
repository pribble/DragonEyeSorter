// verifier-platform 独立桩：MIPI 加密子模块（data_lane_rx_wrapper / channel_aligner_wrapper）
// golden 与工作区两侧共用。data_lane 桩按「引脚编码的 lane 号」把同一 32bit 字流拆成 4 个字节，
// 字流是刻意构造的 CSI-2 RAW10 包序列（帧起/帧止/数据头/长度 0/长度 63/垃圾 DT/连续数据），
// 使 csi_unpacket 的 head 检测、长度计数、valid 间隙等边界都被覆盖；两侧行为完全一致。

module data_lane_rx_wrapper #(
    parameter DEVICE = "EG"
)(
    input  wire       I_hs_clk_x4,
    input  wire       I_hs_clk_x2,
    input  wire       I_hs_clk,
    input  wire       I_lp_clk,
    input  wire       I_rst,
    input  wire       I_lane_hs_invert,
    input  wire       I_lane_lp_invert,
    input  wire       I_mipi_hs_p,
    input  wire       I_mipi_lp_p,
    input  wire       I_mipi_lp_n,
    output wire       O_hs_valid,
    output wire [7:0] O_hs_data,
    output wire       O_lane_error
);
    // TB 用 I_mipi_hs_p / I_mipi_lp_p 引脚编码 lane 号（0..3）
    wire [1:0] lane_id = {I_mipi_hs_p, I_mipi_lp_p};

    // 构造 CSI-2 字流：每 16 个字中第 0 个字是「包头候选」（前一拍 valid=0 制造上升沿），
    // 其余为数据/填充字；包头类型按 8 周期循环。
    function [31:0] w32;
        input [15:0] ix;
        reg [2:0] hsel;
        reg [3:0] slot;
        begin
            slot = ix[3:0];
            hsel = ix[6:4];      // 每 8 个包头循环一次
            if (slot == 4'd0) begin
                case (hsel)
                    3'd0: w32 = 32'h00_A5_5A_0F;  // 帧起始（DT=0x00）
                    3'd1: w32 = 32'h2B_00_08_00;  // 数据包头，长度 = 0x0008>>2 = 2 个字
                    3'd2: w32 = 32'h2B_00_00_00;  // 数据包头，长度 = 0（长度下溢边界）
                    3'd3: w32 = 32'h01_00_00_00;  // 帧结束（DT=0x01）
                    3'd4: w32 = 32'h55_DE_AD_01;  // 未知 DT：应被忽略
                    3'd5: w32 = 32'h2B_00_14_00;  // 数据包头，长度 = 0x0014>>2 = 5
                    3'd6: w32 = 32'h2B_00_FC_00;  // 数据包头，长度 = 0x00FC>>2 = 63（跨越多包头）
                    3'd7: w32 = 32'h00_00_00_00;  // DT=0（帧起始），带全 0 负载
                endcase
            end else begin
                // 数据字/填充字：由字序号确定性展开，含交替 0/1 与高负载位
                w32 = {ix[11:8], ~ix[11:8], ix[15:12], 8'hC3} ^ {16'h0F0F, ix[7:0]};
            end
        end
    endfunction

    reg [15:0] idx   = 16'd0;
    reg        phase = 1'b0;
    reg        valid_r = 1'b0;
    reg  [7:0] data_r  = 8'd0;
    reg [31:0] word_r  = 32'd0;

    always @(posedge I_hs_clk_x2 or posedge I_rst) begin
        if (I_rst) begin
            idx     <= 16'd0;
            phase   <= 1'b0;
            valid_r <= 1'b0;
            data_r  <= 8'd0;
            word_r  <= 32'd0;
        end else begin
            phase <= ~phase;
            if (phase) begin           // 每 2 个 clk_x2 推进一个 32bit 字
                idx    <= idx + 1'b1;
                word_r <= w32(idx);
                // 字 idx%16==15 的 valid=0 → 下一个字（包头候选）产生上升沿
                valid_r <= (idx[3:0] != 4'd15);
            end
            data_r <= word_r[lane_id*8 +: 8];
        end
    end

    assign O_hs_valid   = valid_r;
    assign O_hs_data    = data_r;
    assign O_lane_error = I_lane_hs_invert ^ I_lane_lp_invert;
endmodule


module channel_aligner_wrapper #(
    parameter CH_NUM     = 4,
    parameter DATA_WIDTH = 8
)(
    input  wire                  I_clk,
    input  wire                  I_rst_n,
    input  wire [CH_NUM-1:0]     I_ch_valid,
    input  wire [DATA_WIDTH-1:0] I_ch_data [CH_NUM-1:0],
    output wire [CH_NUM-1:0]     O_ch_aligner_valid,
    output wire [DATA_WIDTH-1:0] O_ch_aligner_data [CH_NUM-1:0]
);
    // 2 级对齐流水（所有通道同延迟），对 valid 与 data 都保真，reset 高有效。
    reg [CH_NUM-1:0]     v1, v2;
    reg [DATA_WIDTH-1:0] d1 [CH_NUM-1:0];
    reg [DATA_WIDTH-1:0] d2 [CH_NUM-1:0];
    integer i;
    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            v1 <= {CH_NUM{1'b0}};
            v2 <= {CH_NUM{1'b0}};
            for (i = 0; i < CH_NUM; i = i + 1) begin
                d1[i] <= {DATA_WIDTH{1'b0}};
                d2[i] <= {DATA_WIDTH{1'b0}};
            end
        end else begin
            v1 <= I_ch_valid;
            v2 <= v1;
            for (i = 0; i < CH_NUM; i = i + 1) begin
                d1[i] <= I_ch_data[i];
                d2[i] <= d1[i];
            end
        end
    end
    genvar g;
    generate for (g = 0; g < CH_NUM; g = g + 1) begin : GEN_ALIGN
        assign O_ch_aligner_valid[g]   = v2[g];
        assign O_ch_aligner_data[g]    = d2[g];
    end endgenerate
endmodule
