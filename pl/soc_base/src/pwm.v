`timescale 1ns / 1ns
// 厂家 AXI pwm（design_0 里多个实例，输出只当 lock 状态位）：周期计数 + 占空比比较。
// 注：32 位计数器不再手工收窄——TD 已按 PERIOD 常量窄化（gate 报告仅 25 ripple / 26 seq）。
module pwm #(
    parameter CLK_FREQ  = 50000000,
    parameter PWM_FREQ  = 2,
    parameter DUTY_CYCLE = 50
)(
    input  wire clk,
    input  wire rst_n,
    output reg  pwm_out
);
    localparam PERIOD     = CLK_FREQ / PWM_FREQ;
    localparam DUTY_COUNT = DUTY_CYCLE * (PERIOD / 100);
    reg [31:0] counter;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            counter <= 32'd0;
            pwm_out <= 1'b0;
        end else begin
            counter <= (counter < PERIOD) ? counter + 1'b1 : 32'd0;
            pwm_out <= (counter < DUTY_COUNT);
        end
    end
endmodule
