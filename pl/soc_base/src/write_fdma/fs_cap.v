`timescale 1ns / 1ps
// 视频 VS 同步 + 帧起始单拍脉冲（VIDEO_ENABLE=1 取下降沿；=0 直通 4 级延迟 VS）。
// 2026 PL refactor: 行为逐拍不变；删未使用的 CNT_FS/CNT_FS_n 计数器与冗余 keep 注释。
// 注：O_fs_cap 的 if/else 不能写成 `O_fs_cap <= vs_i_r3 & ~vs_i_r4`——条件为 x 时
//     if 取 else 支（输出 0），表达式会给 x，逐拍 trace 会分叉（等价 TB 已卡）。
module fs_cap #(
parameter  integer  VIDEO_ENABLE   = 1
)
(
input  wire I_clk,
input  wire I_rstn,
input  wire I_vs,
output reg  O_fs_cap
);
// VS 4 级同步链：跨时钟域采样，ASYNC_REG 供综合做同步约束
(* ASYNC_REG = "TRUE" *) reg vs_i_r1, vs_i_r2, vs_i_r3, vs_i_r4;

always @(posedge I_clk) begin
    vs_i_r1 <= I_vs;
    vs_i_r2 <= vs_i_r1;
    vs_i_r3 <= vs_i_r2;
    vs_i_r4 <= vs_i_r3;
end

// VIDEO_ENABLE=1：vs_i_r3 的下降沿打出一拍帧起始；=0：输出 4 级延迟的 VS
always @(posedge I_clk) begin
    if (!I_rstn) O_fs_cap <= 1'b0;
    else if (VIDEO_ENABLE == 1) begin
        if (vs_i_r3 & ~vs_i_r4) O_fs_cap <= 1'b1;
        else                    O_fs_cap <= 1'b0;
    end
    else O_fs_cap <= vs_i_r4;
end

endmodule
