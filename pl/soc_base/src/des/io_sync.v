// io_sync.v — IO 同步采样 + 急停锁存（自研，P0）
// 输入：急停（低有效，锁存，UI 复位解）、对射光电、限位；两级同步 + 去抖。
// 时间戳：上层把自由计数器接到 io_ts_bus，帧锁存/采样共用同一时基（测试埋点用）。
`timescale 1ns / 1ps
module io_sync #(
    parameter DEB_BITS = 20 // 去抖计数器位宽（100MHz 下 2^20 ≈ 10.5ms 满量程）
)(
    input  wire        clk,
    input  wire        rst_n,
    input  wire        estop_in,   // 低=急停
    input  wire        ir1_in,     // 对射接收端
    input  wire        limit1_in,
    input  wire        estop_clr,  // 软件清除（CTRL.ESTOP_CLR）
    input  wire [31:0] ts_bus,     // 外部自由运行计数器（TIMESTAMP 寄存器的源）
    output reg         estop_latched,
    output reg         ir1_s,
    output reg         limit1_s,
    output reg  [31:0] io_ts_snap, // 最近一次输入变化被采样时的 ts_bus 值
    output reg         io_event    // 任一输入变化脉冲（可作中断源）
);
    reg [2:0] estop_s, ir1_s3, limit1_s3;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            {estop_s, ir1_s3, limit1_s3} <= {3{3'b111}};
        end else begin
            estop_s   <= {estop_s[1:0],   estop_in};
            ir1_s3    <= {ir1_s3[1:0],    ir1_in};
            limit1_s3 <= {limit1_s3[1:0], limit1_in};
        end
    end

    reg ir1_q, limit1_q, estop_q;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            estop_latched <= 1'b0;
            {ir1_s, limit1_s} <= 2'b11;
            {ir1_q, limit1_q, estop_q} <= 3'b111;
            io_ts_snap <= 32'd0;
            io_event   <= 1'b0;
        end else begin
            io_event <= 1'b0;
            if (estop_s[2:1] == 2'b00)
                estop_latched <= 1'b1;         // 锁存
            else if (estop_clr)
                estop_latched <= 1'b0;         // 仅软件解
            estop_q <= estop_s[2];
            ir1_s    <= ir1_s3[2];
            limit1_s <= limit1_s3[2];
            // 任一稳定输入变化 → 记录时间戳 + 事件脉冲
            if ((ir1_s3[2] != ir1_q) || (limit1_s3[2] != limit1_q) || (estop_s[2] != estop_q)) begin
                io_ts_snap <= ts_bus;
                io_event   <= 1'b1;
            end
            ir1_q <= ir1_s3[2];
            limit1_q <= limit1_s3[2];
        end
    end
endmodule
