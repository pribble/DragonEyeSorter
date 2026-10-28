// 出处：MiLianKe（米联客，www.uisrc.com）ISP 参考设计；重构版，接口与逐拍行为不变。
// MIPI RAW10 解包：4 像素 = 4 个 MSB8 + 1 个低 2bit 字节；每拍 4 字节，5 拍凑 16 像素。
module raw10_unpacket_4lane (
    input  wire        I_clk, I_rst_n,
    input  wire        I_csi_frame_start, I_csi_frame_end, I_csi_valid,
    input  wire [31:0] I_csi_data,
    output reg         O_raw10_frame_start, O_raw10_frame_end, O_raw10_valid,
    output reg  [39:0] O_raw10_data
);

    reg        S_csi_valid_1d;
    reg [31:0] S_csi_data_1d;
    reg [2:0]  S_cnt;              // 0..4 循环，4 为空拍（不足 5 字节前的结束拍）
    reg        S_raw10_wr_en;
    reg [39:0] S_raw10_wr_data;    // 已按 O_raw10_data 的输出字节序存放，输出口直接复用此寄存器

    // 按 S_cnt 把 5 字节窗口 {上一拍 32bit, 当前拍 32bit} 拼成 {P0,P1,P2,P3}（4 分支与原版逐位一致）
    function [39:0] raw10_pack(input [2:0] c, input [31:0] prev, input [31:0] cur);
        case (c)
            3'd0: raw10_pack = {prev[31:24], cur[25:24], prev[23:16], cur[27:26],
                                prev[15: 8], cur[29:28], prev[ 7: 0], cur[31:30]};
            3'd1: raw10_pack = {prev[23:16], cur[17:16], prev[15: 8], cur[19:18],
                                prev[ 7: 0], cur[21:20], cur[31:24], cur[23:22]};
            3'd2: raw10_pack = {prev[15: 8], cur[ 9: 8], prev[ 7: 0], cur[11:10],
                                cur[31:24], cur[13:12], cur[23:16], cur[15:14]};
            3'd3: raw10_pack = {prev[ 7: 0], cur[ 1: 0], cur[31:24], cur[ 3: 2],
                                cur[23:16], cur[ 5: 4], cur[15: 8], cur[ 7: 6]};
            default: raw10_pack = 40'd0;
        endcase
    endfunction

    // 输出字节序固定重排（原版在输出口做）：等价移到写端，S_raw10_wr_data 直接按 O_raw10_data 序存放，
    // 输出口不再需要第二份 40bit 寄存器；x 位按同一置换对应，逐拍 trace 不变
    function [39:0] raw10_out_order(input [39:0] d);
        raw10_out_order = {d[9:0], d[19:10], d[29:20], d[39:30]};
    endfunction

    // 单块实现原版四个 always 的组合：RHS 全部取时钟沿前值，等价于原多块非阻塞写法
    always @(posedge I_clk) begin
        O_raw10_frame_start <= I_csi_frame_start;
        O_raw10_frame_end   <= I_csi_frame_end;
        O_raw10_valid       <= S_raw10_wr_en;
        O_raw10_data        <= S_raw10_wr_data;
        S_csi_valid_1d      <= I_csi_valid;
        S_csi_data_1d       <= I_csi_data;

        if (S_csi_valid_1d) begin
            S_cnt <= (S_cnt == 3'd4) ? 3'd0 : S_cnt + 3'd1;
            if (S_cnt <= 3'd3) begin
                S_raw10_wr_en   <= 1'b1;
                S_raw10_wr_data <= raw10_out_order(raw10_pack(S_cnt, S_csi_data_1d, I_csi_data));
            end else if (S_cnt == 3'd4) begin
                S_raw10_wr_en   <= 1'b0;
                S_raw10_wr_data <= 40'd0;
            end
        end else begin
            S_cnt           <= 3'd0;
            S_raw10_wr_en   <= 1'b0;
            S_raw10_wr_data <= 40'd0;
        end
    end

endmodule
