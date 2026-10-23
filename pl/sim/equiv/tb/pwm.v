`timescale 1ns/1ps
// pwm.v 等价回归：实际使用的 3 个实例都是默认参数（50MHz / 2Hz / 50%），
// 另加一组缩小周期的实例覆盖完整周期 / 复位中途 / 占空比 <50% 的行为。
module tb_pwm;
    `include "equiv_tb_common.vh"

    reg clk = 0;
    reg rst_a = 0;
    reg rst_b = 0;
    wire out_a, out_b;

    pwm #(.CLK_FREQ(50000000), .PWM_FREQ(2), .DUTY_CYCLE(50)) dut_a (
        .clk(clk), .rst_n(rst_a), .pwm_out(out_a));
    pwm #(.CLK_FREQ(1000), .PWM_FREQ(2), .DUTY_CYCLE(30)) dut_b (
        .clk(clk), .rst_n(rst_b), .pwm_out(out_b));

    always #5 clk = ~clk;

    initial equiv_trace_open;

    always @(posedge clk) begin
        cyc <= cyc + 1;
        $fdisplay(fd, "%0d %b %b", cyc, out_a, out_b);
    end

    initial begin
        rst_a = 0; rst_b = 0;
        repeat (3) @(posedge clk);
        rst_a = 1; rst_b = 1;
        repeat (3000) @(posedge clk);
        rst_b = 0; repeat (7) @(posedge clk); rst_b = 1;   // mid-period reset
        repeat (2500) @(posedge clk);
        rst_b = 0; repeat (1) @(posedge clk); rst_b = 1;   // one-cycle reset
        repeat (1000) @(posedge clk);
        repeat (250000) @(posedge clk);                    // long run: 2Hz instance several periods
        rst_a = 0; repeat (4) @(posedge clk); rst_a = 1;
        repeat (50000) @(posedge clk);
        equiv_finish(cyc);
    end
endmodule
