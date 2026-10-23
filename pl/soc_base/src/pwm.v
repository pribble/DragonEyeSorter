`timescale 1ns / 1ns
// 厂家 AXI pwm 模块（design_0 里 3 个实例，输出只当 lock 状态位）：
// 32 位周期计数器 + 占空比比较，周期 = CLK_FREQ/PWM_FREQ 个时钟，高电平 = DUTY_CYCLE%。
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
