// stepper_ctrl.v — 步进脉冲发生器（自研，P0）
// 功能：梯形加减速 + 目标定位 + 已发脉冲计数。方向/使能低有效（共阳驱动器，docs/05）。
// 接口与 docs/03 寄存器映射对齐：BELT_TARGET/SPEED/ACC 写入 → 计数到 TARGET 停止。
// 状态：骨架——直线加减速（S 曲线后续加）；脉冲频率 = clk/SPEED 分频，实现时换 NCO/累加器方案。
`timescale 1ns / 1ps
module stepper_ctrl (
    input  wire        clk,          // 运动控制域时钟（建议 100M）
    input  wire        rst_n,
    // 来自 axi_lite_regs 的软件接口（clk 域一致或上游已 CDC）
    input  wire        wr_en_target,
    input  wire [31:0] target,       // 目标脉冲数（有符号：正=正转）
    input  wire        wr_en_speed,
    input  wire [31:0] speed_div,    // 分频系数：脉冲频率 = clk/speed_div
    input  wire        wr_en_acc,
    input  wire [31:0] acc,          // 预留：加速段长度（脉冲数）
    // 状态回读
    output reg  [31:0] pos_cnt,      // 已发脉冲（有符号累计）
    output reg         busy,
    output reg         done_pulse,   // 到位单拍脉冲（中断源）
    // 驱动器接口（低有效）
    output reg         step_n,       // PUL-
    output reg         dir_n,        // DIR-
    output wire        ena_n         // ENA-，脱机控制，先常有效
);
    assign ena_n = 1'b0; // 保持使能（不脱机）；急停由电源链 + CTRL 位处理

    reg [31:0] div_cnt;
    reg [31:0] target_q;
    reg        dir_q; // 0=正, 1=负
    wire [31:0] remain = dir_q ? (pos_cnt - target_q) : (target_q - pos_cnt);

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            div_cnt    <= 32'd0;
            pos_cnt    <= 32'd0;
            busy       <= 1'b0;
            done_pulse <= 1'b0;
            step_n     <= 1'b1;
            dir_n      <= 1'b1;
            target_q   <= 32'd0;
            dir_q      <= 1'b0;
        end else begin
            done_pulse <= 1'b0;
            if (wr_en_target) begin
                target_q <= target;
                dir_q    <= target[31]; // 有符号
                dir_n    <= target[31]; // 低有效：负转时拉低
                busy     <= 1'b1;
                div_cnt  <= 32'd0;
            end
            if (wr_en_speed)  /* speed_div 上游寄存，此骨架直接用 */;
            if (busy) begin
                if (div_cnt >= speed_div - 1) begin
                    div_cnt <= 32'd0;
                    step_n  <= ~step_n;      // 半周期翻转
                    if (~step_n) begin       // 完整脉冲在上升沿计一次
                        pos_cnt <= dir_q ? pos_cnt - 1 : pos_cnt + 1;
                        if (remain == 0) begin
                            busy       <= 1'b0;
                            done_pulse <= 1'b1;
                        end
                    end
                end else begin
                    div_cnt <= div_cnt + 1'b1;
                end
            end else begin
                step_n <= 1'b1;
            end
        end
    end
endmodule
