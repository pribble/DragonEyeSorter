// 出处：MiLianKe（米联客，www.uisrc.com）ISP 参考设计；重构版，接口与逐拍行为不变。
`timescale 1ns / 1ps
module image_cut  #(
    parameter [12:0] IMG_WIDTH  = 640,
    parameter [12:0] IMG_HEIGHT = 480,
    parameter        DATA_WIDTH = 96,
                     SKIP_ROWS_top    = 2,
                     SKIP_ROWS_bottom = 2,
                     SKIP_COLS_left   = 2,
                     SKIP_COLS_right  = 2
)
(
    input                       I_clk  ,
    input                       I_rst_n,
    input                       I_tlast  ,
    input                       I_tuser  ,
    input [DATA_WIDTH-1:0]      I_tdata  ,
    input                       I_tvalid ,
    output                      I_tready ,
    output                      O_tlast  ,
    output                      O_tuser  ,
    output reg [DATA_WIDTH-1:0] O_tdata  ,
    output                      O_tvalid ,
    input                       O_tready
);
    localparam IMG_WIDTH_4x = (IMG_WIDTH >> 2);
    reg [13:0]           h_cnt, v_cnt;              // 位宽/回绕同原版
    reg [DATA_WIDTH-1:0] I_tdata_r;
    reg                  I_tlast_r0, I_tuser_r0, I_tvalid_r0;
    reg                  I_tlast_r1, I_tuser_r1, I_tvalid_r1;

    // 裁剪判定：用上一拍计数，与原版同一时钟沿组合判定一致
    wire w_skip = (v_cnt < SKIP_ROWS_top) || (h_cnt < SKIP_COLS_left) ||
                  (v_cnt >= IMG_HEIGHT - SKIP_ROWS_bottom) ||
                  (h_cnt >= IMG_WIDTH_4x - SKIP_COLS_right);

    // 行列计数 + 裁剪输出：帧头清零，行尾回绕，其余按有效像素递增
    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            h_cnt <= 14'd0; v_cnt <= 14'd0; O_tdata <= {DATA_WIDTH{1'b0}};
        end else begin
            if (I_tuser) begin
                h_cnt <= 14'd0; v_cnt <= 14'd0;
            end else begin
                h_cnt <= I_tvalid_r0 ? ((h_cnt == IMG_WIDTH_4x - 1) ? 14'd0 : h_cnt + 14'd1) : h_cnt;
                v_cnt <= I_tvalid_r0 && (h_cnt == IMG_WIDTH_4x - 1)
                         ? ((v_cnt == IMG_HEIGHT - 1) ? 14'd0 : v_cnt + 14'd1) : v_cnt;
            end
            O_tdata <= w_skip ? {DATA_WIDTH{1'b0}} : I_tdata_r;
        end
    end

    // 两拍流水：行/帧标志 2 拍，数据 1 拍后进输出寄存器
    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            {I_tlast_r0,  I_tlast_r1}  <= 2'd0;
            {I_tuser_r0,  I_tuser_r1}  <= 2'd0;
            {I_tvalid_r0, I_tvalid_r1} <= 2'd0;
            I_tdata_r <= {DATA_WIDTH{1'b0}};
        end else begin
            I_tlast_r0  <= I_tlast;   I_tlast_r1  <= I_tlast_r0;
            I_tuser_r0  <= I_tuser;   I_tuser_r1  <= I_tuser_r0;
            I_tvalid_r0 <= I_tvalid;  I_tvalid_r1 <= I_tvalid_r0;
            I_tdata_r   <= I_tdata;
        end
    end

    assign O_tlast  = I_tlast_r1;
    assign O_tuser  = I_tuser_r1;
    assign O_tvalid = I_tvalid_r1;
    assign I_tready = O_tready;
endmodule
