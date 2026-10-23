`timescale 1ns/1ps
// x_pwm — pwm.v 对抗性等价回归（verifier-platform 独立验证，不用 dev 的 tb/pwm.v）
// 覆盖：
//   * 实际使用的默认参数实例（50MHz / 2Hz / 50%）
//   * 缩周期实例 → 完整周期可在短时间内跑完（含 counter==PERIOD 的翻转边界）
//   * 占空比边界：DUTY=0 / DUTY=100（÷100 截断：DUTY_COUNT=0 与 DUTY_COUNT=PERIOD）
//   * 奇数整除：CLK_FREQ=1001 / PWM_FREQ=3 → PERIOD=333，DUTY=50 → DUTY_COUNT=166（截断）
//   * PWM_FREQ > CLK_FREQ → PERIOD=0（除法下溢边界）
//   * 复位对抗：上电复位、周期中途复位、1 拍复位、连续随机复位（固定 LFSR，确定性）
// 逐拍记录全部输出，跑 EQUIV_CYCLES + EQUIV_DONE；golden vs 新必须逐字节一致。
module tb_x_pwm;
    integer fd;
    integer cyc = 0;
    reg [1023:0] tf;

    reg clk = 0;
    always #5 clk = ~clk;

    // 各实例独立复位（全部由下面的单一 always 块驱动，避免多驱动竞争）
    reg rst_a = 0, rst_b = 0, rst_c = 0, rst_d = 0, rst_e = 0, rst_f = 0, rst_g = 0;
    wire out_a, out_b, out_c, out_d, out_e, out_f, out_g;

    pwm #(.CLK_FREQ(50000000), .PWM_FREQ(2),  .DUTY_CYCLE(50))  dut_a (.clk(clk), .rst_n(rst_a), .pwm_out(out_a));
    pwm #(.CLK_FREQ(1000),     .PWM_FREQ(2),  .DUTY_CYCLE(30))  dut_b (.clk(clk), .rst_n(rst_b), .pwm_out(out_b));
    pwm #(.CLK_FREQ(1000),     .PWM_FREQ(10), .DUTY_CYCLE(0))   dut_c (.clk(clk), .rst_n(rst_c), .pwm_out(out_c));
    pwm #(.CLK_FREQ(1000),     .PWM_FREQ(10), .DUTY_CYCLE(100)) dut_d (.clk(clk), .rst_n(rst_d), .pwm_out(out_d));
    pwm #(.CLK_FREQ(1001),     .PWM_FREQ(3),  .DUTY_CYCLE(50))  dut_e (.clk(clk), .rst_n(rst_e), .pwm_out(out_e));
    pwm #(.CLK_FREQ(100),      .PWM_FREQ(200),.DUTY_CYCLE(50))  dut_f (.clk(clk), .rst_n(rst_f), .pwm_out(out_f));
    pwm #(.CLK_FREQ(50),       .PWM_FREQ(1),  .DUTY_CYCLE(37))  dut_g (.clk(clk), .rst_n(rst_g), .pwm_out(out_g));

    // 确定性伪随机（32-bit LFSR，固定种子 32'h1ACE_B00C）
    reg [31:0] lfsr = 32'h1ACE_B00C;

    initial begin
        if (!$value$plusargs("TRACE=%s", tf)) tf = "trace.txt";
        fd = $fopen(tf, "w");
        if (fd == 0) begin $display("cannot open trace"); $finish; end
    end

    always @(posedge clk) begin
        cyc <= cyc + 1;
        $fdisplay(fd, "%0d %b %b %b %b %b %b %b", cyc,
                  out_a, out_b, out_c, out_d, out_e, out_f, out_g);
        // ---- 复位剧本（单一驱动源；读到的 cyc 是上一拍结束时的值，确定性）----
        lfsr <= {lfsr[30:0], lfsr[31] ^ lfsr[21] ^ lfsr[1] ^ lfsr[0]};
        if (cyc < 3) begin
            rst_a <= 1'b0; rst_b <= 1'b0; rst_c <= 1'b0; rst_d <= 1'b0;
            rst_e <= 1'b0; rst_f <= 1'b0; rst_g <= 1'b0;
        end else if ((cyc >= 397 && cyc <= 401) || (cyc == 1399) ||
                     (cyc >= 3811 && cyc <= 4010)) begin
            // 周期中途 / 1 拍 / 长复位：只打 A，其余随机
            rst_a <= 1'b0;
            rst_b <= ~(lfsr[2:1] == 2'b00);
            rst_c <= ~(lfsr[5:3] == 3'b000);
            rst_d <= ~(lfsr[8:6] == 3'b000);
            rst_e <= ~(lfsr[11:9] == 3'b000);
            rst_f <= ~(lfsr[14:12] == 3'b000);
            rst_g <= ~(lfsr[17:15] == 3'b000);
        end else if (cyc >= 8400) begin
            rst_a <= 1'b1; rst_b <= 1'b1; rst_c <= 1'b1; rst_d <= 1'b1;
            rst_e <= 1'b1; rst_f <= 1'b1; rst_g <= 1'b1;
        end else begin
            rst_a <= 1'b1;
            rst_b <= ~(lfsr[2:1] == 2'b00);
            rst_c <= ~(lfsr[5:3] == 3'b000);
            rst_d <= ~(lfsr[8:6] == 3'b000);
            rst_e <= ~(lfsr[11:9] == 3'b000);
            rst_f <= ~(lfsr[14:12] == 3'b000);
            rst_g <= ~(lfsr[17:15] == 3'b000);
        end
        if (cyc == 12000) begin
            $display("EQUIV_CYCLES %0d", cyc);
            $display("EQUIV_DONE");
            $fclose(fd);
            $finish;
        end
    end
endmodule
