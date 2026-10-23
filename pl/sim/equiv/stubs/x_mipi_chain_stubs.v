// verifier-platform 独立桩：design_top_wrapper 里被替换的明文子模块（raw10/image_correction/isp_top）
// 目的：x_design_top 等价 TB 只测 MIPI 明文链路（design_top_wrapper + dphy/hs_rx/rx_clk_gen + csi_unpacket）
// 的连线与行为；下游 ISP 子模块用「对输入敏感、确定性」的桩隔离，避免跨域重构耦合。
// golden 与工作区两侧共用同一份。

module raw10_unpacket_4lane (
    input  wire        I_clk,
    input  wire        I_rst_n,
    input  wire        I_csi_frame_start,
    input  wire        I_csi_frame_end,
    input  wire        I_csi_valid,
    input  wire [31:0] I_csi_data,
    output reg         O_raw10_frame_start,
    output reg         O_raw10_frame_end,
    output reg         O_raw10_valid,
    output reg  [39:0] O_raw10_data
);
    // 对每个输入都敏感：4 lane×10bit 解包用 40bit 拼接 + 帧起/止打拍。
    reg        v_d = 1'b0;
    reg [31:0] d_d = 32'd0;
    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            O_raw10_frame_start <= 1'b0;
            O_raw10_frame_end   <= 1'b0;
            O_raw10_valid       <= 1'b0;
            O_raw10_data        <= 40'd0;
            v_d <= 1'b0;
            d_d <= 32'd0;
        end else begin
            O_raw10_frame_start <= I_csi_frame_start;
            O_raw10_frame_end   <= I_csi_frame_end;
            O_raw10_valid       <= I_csi_valid ^ v_d;
            O_raw10_data        <= {I_csi_data[9:8], d_d[7:0],
                                    I_csi_data[19:16], d_d[15:12],
                                    I_csi_data[27:24], d_d[23:20],
                                    I_csi_data[31:28], d_d[31:28],
                                    I_csi_frame_start, I_csi_frame_end,
                                    I_csi_valid, I_csi_data[3], I_csi_data[2],
                                    I_csi_data[1], I_csi_data[0]};
            v_d <= I_csi_valid;
            d_d <= I_csi_data;
        end
    end
endmodule


module image_correction #(
    parameter DATA_WIDTH  = 40,
    parameter TDEST_WIDTH = 10
)(
    input  wire                  I_clk,
    input  wire                  I_rst_n,
    input  wire [DATA_WIDTH-1:0] I_raw_data,
    input  wire                  I_raw_valid,
    input  wire                  I_raw_frame_start,
    input  wire                  I_raw_frame_end,
    output reg  [DATA_WIDTH-1:0] O_raw_tdata,
    output reg                   O_raw_tlast,
    output reg  [TDEST_WIDTH-1:0] O_raw_tdest,
    output reg                   O_raw_tvalid,
    output reg                   O_raw_tuser
);
    reg [TDEST_WIDTH-1:0] tdest = {TDEST_WIDTH{1'b0}};
    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            O_raw_tdata  <= {DATA_WIDTH{1'b0}};
            O_raw_tlast  <= 1'b0;
            O_raw_tdest  <= {TDEST_WIDTH{1'b0}};
            O_raw_tvalid <= 1'b0;
            O_raw_tuser  <= 1'b0;
            tdest        <= {TDEST_WIDTH{1'b0}};
        end else begin
            tdest        <= tdest + {TDEST_WIDTH{1'b0}} + 1'b1;
            O_raw_tdata  <= I_raw_data ^ {DATA_WIDTH{1'b0}};
            O_raw_tlast  <= I_raw_frame_end;
            O_raw_tdest  <= tdest;
            O_raw_tvalid <= I_raw_valid;
            O_raw_tuser  <= I_raw_frame_start;
        end
    end
endmodule


module isp_top (
    input  wire         axi4s_video_aclk,
    input  wire         I_rst_n,
    input  wire         I_tlast,
    input  wire         I_tuser,
    input  wire [39:0]  I_tdata,
    input  wire         I_tvalid,
    input  wire [9:0]   I_tdest,
    input  wire         O_tready,
    output reg  [127:0] O_tdata,
    output reg          O_tlast,
    output reg          O_tuser,
    output reg          O_tvalid,
    output wire         I_tready
);
    reg [3:0] beat = 4'd0;
    always @(posedge axi4s_video_aclk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            O_tdata  <= 128'd0;
            O_tlast  <= 1'b0;
            O_tuser  <= 1'b0;
            O_tvalid <= 1'b0;
            beat     <= 4'd0;
        end else begin
            beat     <= beat + 1'b1;
            O_tdata  <= {I_tdata, I_tdata, I_tdata, {40{1'b0}}} + {{124{1'b0}}, beat};
            O_tlast  <= I_tlast;
            O_tuser  <= I_tuser;
            O_tvalid <= I_tvalid;
        end
    end
    assign I_tready = ~O_tready;   // 故意对 O_tready 敏感，验证 design_top 的连线
endmodule
