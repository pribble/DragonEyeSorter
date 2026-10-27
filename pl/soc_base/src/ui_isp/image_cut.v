// 出处：MiLianKe（米联客，www.uisrc.com）ISP 参考设计；重构版，接口与逐拍行为不变。
`timescale 1ns / 1ps
module image_cut #(
    parameter [12:0] IMG_WIDTH  = 640,
    parameter [12:0] IMG_HEIGHT = 480,
    parameter        DATA_WIDTH = 96,
                     SKIP_ROWS_top    = 2,
                     SKIP_ROWS_bottom = 2,
                     SKIP_COLS_left   = 2,
                     SKIP_COLS_right  = 2
) (
    input  wire                  I_clk, I_rst_n,
    input  wire                  I_tlast, I_tuser,
    input  wire [DATA_WIDTH-1:0] I_tdata,
    input  wire                  I_tvalid,
    output wire                  I_tready,
    output wire                  O_tlast, O_tuser,
    output reg  [DATA_WIDTH-1:0] O_tdata,
    output wire                  O_tvalid,
    input  wire                  O_tready
);
    localparam IMG_WIDTH_4x = (IMG_WIDTH >> 2);
    reg [13:0]           h_cnt, v_cnt;              // 位宽/回绕同原版
    reg [DATA_WIDTH-1:0] I_tdata_r;
    reg [1:0]            tl_r, tu_r, tv_r;          // tlast/tuser/tvalid 两拍流水（bit1 为输出）

    // 裁剪判定：用上一拍计数，与原版同一时钟沿组合判定一致
    wire w_skip = (v_cnt < SKIP_ROWS_top) || (h_cnt < SKIP_COLS_left) ||
                  (v_cnt >= IMG_HEIGHT - SKIP_ROWS_bottom) ||
                  (h_cnt >= IMG_WIDTH_4x - SKIP_COLS_right);

    // 行列计数 + 裁剪输出 + 两拍控制流水（原 2 个 always 同复位/同时钟/全非阻塞，合并后逐拍等价）
    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            h_cnt <= 14'd0; v_cnt <= 14'd0; O_tdata <= {DATA_WIDTH{1'b0}};
            {tl_r, tu_r, tv_r} <= 6'd0;
            I_tdata_r <= {DATA_WIDTH{1'b0}};
        end else begin
            if (I_tuser) begin
                h_cnt <= 14'd0; v_cnt <= 14'd0;
            end else begin
                h_cnt <= tv_r[0] ? ((h_cnt == IMG_WIDTH_4x - 1) ? 14'd0 : h_cnt + 14'd1) : h_cnt;
                v_cnt <= tv_r[0] && (h_cnt == IMG_WIDTH_4x - 1)
                         ? ((v_cnt == IMG_HEIGHT - 1) ? 14'd0 : v_cnt + 14'd1) : v_cnt;
            end
            O_tdata <= w_skip ? {DATA_WIDTH{1'b0}} : I_tdata_r;
            // 两拍流水：行/帧标志 2 拍，数据 1 拍后进输出寄存器
            tl_r <= {tl_r[0], I_tlast};
            tu_r <= {tu_r[0], I_tuser};
            tv_r <= {tv_r[0], I_tvalid};
            I_tdata_r <= I_tdata;
        end
    end

    assign O_tlast  = tl_r[1];
    assign O_tuser  = tu_r[1];
    assign O_tvalid = tv_r[1];
    assign I_tready = O_tready;
endmodule
