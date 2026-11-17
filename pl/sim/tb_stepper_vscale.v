// tb_stepper_vscale.v — stepper_ctrl step rate under the des_bpi speed correction vscale (signed Q0.16).
// 11-18 on the real belt: the PI's first step was negative (the metering wheel read 1 % above the set speed) and the
// belt stopped dead: `scl[49:16]` is a part-select, i.e. unsigned, so `$signed(...) + scl[49:16]` was evaluated
// unsigned and any negative correction gave inc_eff = 0 (no steps). tb_des_bpi only ever corrected upwards (slip).
// Velocity mode, vmin = vmax (no ramp), 1 MHz clock, 1000 steps/s programmed; 0.1 s windows.
`timescale 1ns / 1ps
module tb;
    reg clk = 0, wr = 0;
    reg [7:0] waddr = 0;
    reg [31:0] wdata = 0;
    reg signed [16:0] vscale = 0;
    wire pul_n, dir_n, ena, moving, cruise;
    wire [31:0] pos, rdata;
    integer pass = 0, fail = 0, p0, n, k;
    stepper_ctrl #(.CLK_HZ(1_000_000), .ACC_DIV(100), .PULSE_US(5), .DIR_SETUP_US(10)) dut (
        .clk(clk), .wr(wr), .waddr(waddr), .wdata(wdata), .raddr(8'd0), .rdata(rdata), .estop(1'b0),
        .pul_n(pul_n), .dir_n(dir_n), .ena_o(ena), .pos_o(pos), .vscale(vscale), .moving_o(moving), .cruise_o(cruise));
    always #500 clk = ~clk;
    task w(input [7:0] a, input [31:0] d); begin @(posedge clk); wr <= 1; waddr <= a; wdata <= d; @(posedge clk); wr <= 0; end endtask
    task check(input signed [16:0] vs, input integer lo, input integer hi, input [8*24-1:0] what);
        begin
            vscale = vs;
            repeat (100) @(posedge clk);
            p0 = pos;
            repeat (100000) @(posedge clk);
            n = $signed(pos) - p0;
            if (n >= lo && n <= hi) begin pass = pass + 1; $display("ok   %0s: %0d steps (%0d..%0d)", what, n, lo, hi); end
            else begin fail = fail + 1; $display("FAIL %0s: %0d steps, want %0d..%0d", what, n, lo, hi); end
        end
    endtask
    localparam [31:0] INC = 32'd4294967;   // 1000 steps/s at 1 MHz, Q0.32
    initial begin
        w(8'h0C, INC); w(8'h10, 32'd1000); w(8'h14, INC);
        w(8'h00, 32'h0000_0001);           // enable, velocity mode
        w(8'h00, 32'h0000_0003);           // + START
        repeat (20000) @(posedge clk);
        check(17'sd0, 99, 101, "vscale 0");
        check(17'sd5243, 107, 109, "vscale +8 %");
        check(17'sd655, 100, 102, "vscale +1 %");
        check(-17'sd655, 98, 100, "vscale -1 %");
        check(-17'sd5243, 91, 93, "vscale -8 %");
        check(-17'sd16384, 74, 76, "vscale -25 % (hard cap)");
        check(17'sd0, 99, 101, "back to 0");
        $display("stepper_vscale: %0d passed, %0d failed", pass, fail);
        $finish;
    end
endmodule
