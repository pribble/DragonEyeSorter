// 出处：南京微酷机器人 / VLKUS（www.uisrc.com）ISP 参考设计；重构版，接口与逐拍行为不变。
`timescale 1ns / 1ps

module BLC #(
    parameter [9:0]  Black_level_offset_r0 = 15,
    parameter [9:0]  Black_level_offset_r1 = 15,
    parameter [9:0]  Black_level_offset_r2 = 15,
    parameter [9:0]  Black_level_offset_r3 = 15
)
(
    input                   I_clk  ,
    input                   I_rst_n,

    input                   I_tlast   ,
    input                   I_tuser   ,
    input  [39:0]           I_tdata   ,
    input                   I_tvalid  ,
    input  [9:0]            I_tdest   ,
    output                  I_tready  ,

    output                  O_tlast   ,
    output                  O_tuser   ,
    output [39:0]           O_tdata   ,
    output                  O_tvalid  ,
    output [9:0]            O_tdest   ,
    input                   O_tready
);

    // 单通道黑电平矫正：pix > offset 相减，否则钳 0；按 if/else 写以保持原版 x 语义
    function [9:0] blc_sub(input [9:0] pix, input [9:0] offset);
        if (pix > offset) blc_sub = pix - offset;
        else              blc_sub = 10'd0;
    endfunction

    // 原版是 4 个独立 10bit 寄存器，同一条件同一时刻更新，合并成 40bit 等价
    reg [39:0] O_tdata_r;

    always @(posedge I_clk) begin       // 无复位：与厂版一致，复位期间保持 x
        if (I_tvalid)
            O_tdata_r <= { blc_sub(I_tdata[39:30], Black_level_offset_r3),
                           blc_sub(I_tdata[29:20], Black_level_offset_r2),
                           blc_sub(I_tdata[19:10], Black_level_offset_r1),
                           blc_sub(I_tdata[ 9: 0], Black_level_offset_r0) };
    end

    reg I_tlast_r, I_tuser_r, I_tvalid_r;

    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) {I_tlast_r, I_tuser_r, I_tvalid_r} <= 3'd0;
        else begin
            I_tlast_r  <= I_tlast;
            I_tuser_r  <= I_tuser;
            I_tvalid_r <= I_tvalid;
        end
    end

    assign O_tlast  = I_tlast_r;
    assign O_tuser  = I_tuser_r;
    assign O_tdata  = O_tdata_r;
    assign O_tvalid = I_tvalid_r;
    // O_tdest 原版即未驱动（高阻 z），为保持接口与行为不变保留
    assign I_tready = O_tready;

endmodule
