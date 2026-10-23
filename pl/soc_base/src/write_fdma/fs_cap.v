`timescale 1ns / 1ps
/*******************************MILIANKE*******************************
*Company : MiLianKe Electronic Technology Co., Ltd.
*WebSite : https://www.milianke.com      TechWeb : https://www.uisrc.com
*Create Date : 2022/09/25
*Description : 视频 VS 同步 + 帧起始单拍脉冲发生
*Copyright : Copyright (c) MiLianKe, All rights reserved.  Revision 3.1
*Signal description
*1) _i input   2) _o output   3) _n activ low   4) _r registered
*2026 PL refactor: 行为逐拍不变；删除未使用的 CNT_FS/CNT_FS_n 计数器和冗余端口 keep 注释
*********************************************************************/

module fs_cap #(
parameter  integer  VIDEO_ENABLE   = 1
)
(
input  wire I_clk,
input  wire I_rstn,
input  wire I_vs,
output reg  O_fs_cap
);

//----VS 4 级同步链：跨时钟域采样，ASYNC_REG 供综合做同步约束----------------
(* ASYNC_REG = "TRUE" *) reg vs_i_r1;
(* ASYNC_REG = "TRUE" *) reg vs_i_r2;
(* ASYNC_REG = "TRUE" *) reg vs_i_r3;
(* ASYNC_REG = "TRUE" *) reg vs_i_r4;

always @(posedge I_clk) begin
    vs_i_r1 <= I_vs;
    vs_i_r2 <= vs_i_r1;
    vs_i_r3 <= vs_i_r2;
    vs_i_r4 <= vs_i_r3;
end

//----同步后 vs_i_r3 的下降沿打出一拍帧起始；非视频模式下输出 4 级延迟的 VS----
always @(posedge I_clk) begin
    if (!I_rstn) begin
        O_fs_cap <= 1'b0;
    end
    else if (VIDEO_ENABLE == 1) begin
        if (vs_i_r3 & ~vs_i_r4) begin
            O_fs_cap <= 1'b1;
        end
        else begin
            O_fs_cap <= 1'b0;
        end
    end
    else begin
        O_fs_cap <= vs_i_r4;
    end
end

endmodule
