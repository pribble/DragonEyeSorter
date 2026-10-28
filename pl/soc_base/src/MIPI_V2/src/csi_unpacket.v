// CSI-2 packet parser: detects frame-start / frame-end / RAW10 long-packet headers and forwards
// payload words while the long packet is active. 2026 两轮零行为重构：帧起/帧止两个 always 合并、
// 注释收敛。2026-10-28 减资源（逐拍等价）：删掉 S_packet_head_data 的 16 位 gated mux，直接比较
// I_hs_data[31:24]。证明：gated 值 = head_en ? data : 0，所有用到它的都是「先与 head_en 相与的
// 条件」或「head_en 为 1 才成立的 if 分支」；head_en 为 0/x 时 gated 比较只可能得到 0/x，条件
// 与 if 分支结果都是「不成立」，输出与直接比较逐位一致（含 x/z；x_csi_unpacket 对抗验证）。
module csi_unpacket (
    input  wire        I_clk, I_rst_n, I_hs_valid,
    input  wire [31:0] I_hs_data,
    output reg         O_csi_frame_start, O_csi_frame_end, O_csi_valid,
    output reg  [31:0] O_csi_data
);
    localparam FRAME_DATA_DT  = 8'h2B;   // RAW10
    localparam FRAME_START_DT = 8'h00;
    localparam FRAME_END_DT   = 8'h01;

    reg        S_hs_valid_1d;
    reg [15:0] S_data_length;
    reg        S_data_en;
    reg [15:0] S_data_en_cnt;
    // a packet header is the first valid word after an invalid cycle
    wire       S_packet_head_en = ~S_hs_valid_1d & I_hs_valid;
    wire       S_head_is_data   = S_packet_head_en & (I_hs_data[31:24] == FRAME_DATA_DT);

    always @(posedge I_clk) S_hs_valid_1d <= I_hs_valid;

    // 帧起 / 帧止检测（原两个 always，合并排版，语义不变）
    always @(posedge I_clk) begin
        if (S_packet_head_en && I_hs_data[31:24] == FRAME_START_DT) O_csi_frame_start <= 1'b1;
        else                                                        O_csi_frame_start <= 1'b0;
        if (S_packet_head_en && I_hs_data[31:24] == FRAME_END_DT)   O_csi_frame_end <= 1'b1;
        else                                                        O_csi_frame_end <= 1'b0;
    end

    // long-packet word count = {byte1, byte2} >> 2 (RAW10: 4 pixels per 5 bytes)
    always @(posedge I_clk) begin
        if (I_hs_valid) begin
            if (S_head_is_data) S_data_length <= {I_hs_data[15:8], I_hs_data[23:16]} >> 2;
        end else
            S_data_length <= 'd0;
    end

    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n)             S_data_en <= 1'b0;
        else if (S_head_is_data)  S_data_en <= 1'b1;
        else if (S_data_en_cnt == S_data_length-1) S_data_en <= 1'b0;
    end

    always @(posedge I_clk) begin
        if (S_data_en) S_data_en_cnt <= S_data_en_cnt + 'd1;
        else           S_data_en_cnt <= 'd0;
    end

    always @(posedge I_clk) begin
        if (S_data_en) begin
            O_csi_valid <= 1'b1;
            O_csi_data  <= I_hs_data;
        end else begin
            O_csi_valid <= 1'b0;
            O_csi_data  <= 'd0;
        end
    end
endmodule
