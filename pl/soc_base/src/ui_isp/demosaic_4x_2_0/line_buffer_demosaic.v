// 出处：Milianke uisrc ISP demo（ui_isp/demosaic_4x_2_0/line_buffer_demosaic.v, XiaoQingquan 2024/10）
// 单行缓冲：按 4 像素组（32bit 数据 + tuser 位）延迟一行输出；首行写满后锁定读使能。
`timescale 1ns / 1ps
module line_buffer_demosaic #(
    parameter IMG_HEIGHT = 1080,   // 图像高度
    parameter IMG_WIDTH  = 1920    // 图像宽度
) (
    input  wire        I_clk, I_rst_n,
    input  wire        I_tuser, I_valid,       // I_tuser = 帧起始
    input  wire [32:0] I_data,
    output wire        O_valid,
    output wire [32:0] O_data
);

    localparam IMG_WIDTH_4x = IMG_WIDTH >> 2;   // 每行的 4 像素组数
    // 读写地址只在 IMG_WIDTH_4x-1 回绕，值域恒 < IMG_WIDTH_4x，按 $clog2 收窄（BRAM 11bit 地址口零扩展）
    localparam AW = ($clog2(IMG_WIDTH_4x) > 0) ? $clog2(IMG_WIDTH_4x) : 1;

    reg  [AW-1:0]  addra, addrb;       // 写地址 / 读地址
    reg  [32:0] data_d0;               // 数据打一拍
    reg         valid_d0;              // 有效打一拍
    reg         tlast_lock_d0;         // 首次写满一行后置位（读侧对齐用）

    wire rd_en = I_valid & tlast_lock_d0;

    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            data_d0 <= 33'd0;
            valid_d0 <= 1'b0;
            {addra, addrb} <= 0;
            tlast_lock_d0 <= 1'b0;
        end else begin
            // 数据/有效固定打一拍（不受 tuser 影响）
            data_d0  <= I_data;
            valid_d0 <= I_valid;

            if (I_tuser) begin
                {addra, addrb} <= 0;
                tlast_lock_d0  <= 1'b0;
            end else begin
                if (valid_d0)                  addra <= (addra == IMG_WIDTH_4x - 1) ? 0 : addra + 1'b1;
                if (addra == IMG_WIDTH_4x - 1) tlast_lock_d0 <= 1'b1;
                if (rd_en)                     addrb <= (addrb == IMG_WIDTH_4x - 1) ? 0 : addrb + 1'b1;
            end
        end
    end

    assign O_valid = valid_d0 & tlast_lock_d0;

    blk_mem_gen_demosaic blk_mem_gen_demosaic_r0 (
        .clka(I_clk), .wea(valid_d0), .addra(addra), .dia(data_d0),
        .clkb(I_clk), .addrb(addrb), .dob(O_data)
    );

endmodule
