`timescale 1ns/1ps
// des_pwm: period, pulse widths, change only at a period start (no truncated / doubled pulse), enable / disable,
// width 0, clamp, lamp, period counter. Short period (2600 us minimum) to keep the simulation fast.
module tb_des_pwm;
    reg clk = 0;
    always #6.6667 clk = ~clk;   // 75 MHz

    reg        wr = 0;
    reg  [7:0] waddr = 0, raddr = 0;
    reg  [31:0] wdata = 0;
    wire [31:0] rdata;
    wire [1:0] pwm;
    wire       lamp;
    des_pwm dut (.clk(clk), .wr(wr), .waddr(waddr), .wdata(wdata), .raddr(raddr), .rdata(rdata), .pwm(pwm), .lamp(lamp));

    integer fails = 0, passes = 0;
    reg [8*80-1:0] msg;
    task check(input cond, input [8*80-1:0] what);
        begin
            if (cond) passes = passes + 1;
            else begin fails = fails + 1; $display("FAIL: %0s", what); end
        end
    endtask
    task wreg(input [7:0] a, input [31:0] d);
        begin @(posedge clk); wr <= 1; waddr <= a; wdata <= d; @(posedge clk); wr <= 0; end
    endtask
    task rreg(input [7:0] a, output [31:0] d);
        begin @(posedge clk); raddr <= a; @(posedge clk); d = rdata; end   // AXI bridge timing: data at the next edge
    endtask

    // pulse monitor per channel: widths and rising-edge intervals in us
    real rise0 = 0, last_w0 = 0, last_t0 = 0, rise1 = 0, last_w1 = 0;
    integer n0 = 0, n1 = 0;
    always @(posedge pwm[0]) begin if (rise0 > 0) last_t0 = ($realtime - rise0) / 1000.0; rise0 = $realtime; end
    always @(negedge pwm[0]) begin last_w0 = ($realtime - rise0) / 1000.0; n0 = n0 + 1; end
    always @(posedge pwm[1]) rise1 = $realtime;
    always @(negedge pwm[1]) begin last_w1 = ($realtime - rise1) / 1000.0; n1 = n1 + 1; end

    reg [31:0] r, c0;
    // the 75 MHz clock is #6.6667 -> 6.667 ns at 1 ps precision: +0.005 % on every interval, hence a relative tolerance
    function near(input real v, input real want); near = v > want * 0.9999 && v < want * 1.0001; endfunction
    integer k;
    initial begin
        rreg(8'h14, r); check(r == 32'h50574D31, "ID");
        wreg(8'h04, 32'd1000);                     // below the minimum -> clamped
        rreg(8'h04, r); check(r == 2600, "period clamped to 2600 us");
        wreg(8'h08, 32'd1500);
        wreg(8'h0C, 32'd9999);                     // clamped to 2550
        wreg(8'h00, 32'h103);                      // both channels + lamp
        #1 check(lamp == 1'b1, "lamp on");
        #(3 * 2600 * 1000);
        $sformat(msg, "ch0 width 1500 us (%0.2f)", last_w0); check(near(last_w0, 1500), msg);
        $sformat(msg, "period 2600 us (%0.3f)", last_t0); check(near(last_t0, 2600), msg);
        $sformat(msg, "ch1 clamped to 2550 us (%0.2f)", last_w1); check(near(last_w1, 2550), msg);
        // change in the middle of a pulse: the running pulse keeps its width, the next one has the new width
        @(posedge pwm[0]); #(500 * 1000);
        wreg(8'h08, 32'd800);
        @(negedge pwm[0]);
        $sformat(msg, "running pulse not truncated (%0.2f)", last_w0); check(near(last_w0, 1500), msg);
        @(negedge pwm[0]);
        $sformat(msg, "next pulse 800 us (%0.2f)", last_w0); check(near(last_w0, 800), msg);
        // period counter
        rreg(8'h10, c0);
        #(2 * 2600 * 1000 + 1000);
        rreg(8'h10, r); check(r == c0 + 2, "CNT counts periods");
        // width 0 and disable: no pulses
        k = n0;
        wreg(8'h08, 32'd0);
        #(3 * 2600 * 1000);
        check(n0 <= k + 1 && pwm[0] == 1'b0, "width 0: no pulses");
        k = n1;
        wreg(8'h00, 32'h001);                      // ch1 off, lamp off
        #(3 * 2600 * 1000);
        check(n1 <= k + 1 && pwm[1] == 1'b0 && lamp == 1'b0, "channel disable / lamp off");
        $display("des_pwm: %0d passed, %0d failed", passes, fails);
        $finish;
    end
endmodule
