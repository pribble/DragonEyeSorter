`timescale 1ns/1ps
// des_io: e-stop debounce / latch / clear rules, IR1 debounce + capture (timestamp, stepper position, encoder count),
// encoder x4 counting both ways, glitch rejection, illegal transitions, index capture, speed window, CFG polarity.
// Run with CLK_HZ = 1 MHz (1 us per clock) so the millisecond debounces stay short.
module tb_des_io;
    reg clk = 0;
    always #500 clk = ~clk;   // 1 MHz

    reg        wr = 0;
    reg  [7:0] waddr = 0, raddr = 0;
    reg  [31:0] wdata = 0, ts = 0, pos = 32'd12345;
    wire [31:0] rdata;
    reg  estop_n = 1, ir1 = 1, lim = 1, a = 1, b = 1, z = 1;
    wire estop_out;
    always @(posedge clk) ts <= ts + 1;
    des_io #(.CLK_HZ(1_000_000), .EST_DEB_US(1000), .IN_DEB_US(20), .ENC_FILT(3), .SPEED_MS(10)) dut (
        .clk(clk), .wr(wr), .waddr(waddr), .wdata(wdata), .raddr(raddr), .rdata(rdata), .ts(ts), .pos(pos),
        .estop_n(estop_n), .ir1_in(ir1), .limit1_in(lim), .enc_a(a), .enc_b(b), .enc_z(z), .estop_out(estop_out));

    integer fails = 0, passes = 0;
    reg [8*96-1:0] msg;
    task check(input cond, input [8*96-1:0] what);
        begin
            if (cond) passes = passes + 1;
            else begin fails = fails + 1; $display("FAIL: %0s", what); end
        end
    endtask
    task wreg(input [7:0] ad, input [31:0] d);
        begin @(posedge clk); wr <= 1; waddr <= ad; wdata <= d; @(posedge clk); wr <= 0; end
    endtask
    task rreg(input [7:0] ad, output [31:0] d);
        begin @(posedge clk); raddr <= ad; @(posedge clk); d = rdata; end
    endtask
    task clocks(input integer n); begin repeat (n) @(posedge clk); end endtask
    // one quadrature step: up = A leads B (00 -> 10 -> 11 -> 01 -> 00 with A as the high bit)
    reg [1:0] q = 2'b11;
    task qstep(input up, input integer hold);
        begin
            if (up) q = (q == 2'b00) ? 2'b10 : (q == 2'b10) ? 2'b11 : (q == 2'b11) ? 2'b01 : 2'b00;
            else    q = (q == 2'b00) ? 2'b01 : (q == 2'b01) ? 2'b11 : (q == 2'b11) ? 2'b10 : 2'b00;
            a = q[1]; b = q[0];
            clocks(hold);
        end
    endtask

    reg [31:0] r, t0;
    integer k;
    initial begin
        clocks(10);
        rreg(8'h34, r); check(r == 32'h494F3031, "ID");
        rreg(8'h00, r); check(r[3:0] == 4'b0000 && !estop_out, "rest: nothing latched, beam clear, limit off");

        // ---- e-stop ----
        estop_n = 0; clocks(500); estop_n = 1; clocks(50);
        rreg(8'h00, r); check(r[0] == 0 && !estop_out, "0.5 ms bounce: not latched");
        estop_n = 0; clocks(1100);
        rreg(8'h00, r); check(r[1:0] == 2'b11 && estop_out, "pressed 1.1 ms: latched + now pressed");
        rreg(8'h0C, r); check(r == 1, "one latch event");
        rreg(8'h08, r); check(r > 1000, "latch timestamp");
        wreg(8'h04, 32'h1); rreg(8'h00, r); check(r[0] == 1, "clear ignored while still pressed");
        estop_n = 1; clocks(1100);
        rreg(8'h00, r); check(r[1:0] == 2'b01 && estop_out, "released: still latched until cleared");
        wreg(8'h04, 32'h1); rreg(8'h00, r); check(r[0] == 0 && !estop_out, "cleared after release");

        // ---- encoder ----
        for (k = 0; k < 400; k = k + 1) qstep(1, 6);
        rreg(8'h20, r); $sformat(msg, "400 steps forward -> %0d", $signed(r)); check(r == 400, msg);
        for (k = 0; k < 120; k = k + 1) qstep(0, 6);
        rreg(8'h20, r); $sformat(msg, "120 back -> %0d", $signed(r)); check(r == 280, msg);
        a = ~a; clocks(2); a = ~a; clocks(10);   // 2-clock glitch on A
        rreg(8'h20, r); check(r == 280, "glitch on A rejected");
        rreg(8'h24, r); check(r == 0, "no errors so far");
        a = ~a; b = ~b; q = {a, b}; clocks(10);   // both channels at once
        rreg(8'h24, r); check(r == 1, "simultaneous A/B change counted as error");
        rreg(8'h20, t0);
        z = 0; clocks(10); z = 1; clocks(10);
        rreg(8'h28, r); check(r == t0, "index captures the count");
        rreg(8'h30, r); check(r == 1, "one index edge");
        wreg(8'h04, 32'h2); rreg(8'h20, r); check(r == 0, "ENC_ZERO");
        // speed: one count every 25 clocks for 3 windows of 10 ms = 10000 clocks -> 400 per window
        for (k = 0; k < 1300; k = k + 1) qstep(1, 25);
        rreg(8'h2C, r); $sformat(msg, "speed 400 counts / 10 ms (%0d)", $signed(r)); check(r >= 399 && r <= 401, msg);
        for (k = 0; k < 900; k = k + 1) qstep(0, 25);
        rreg(8'h2C, r); $sformat(msg, "reverse speed -400 (%0d)", $signed(r)); check($signed(r) <= -399 && $signed(r) >= -401, msg);

        // ---- through-beam ----
        ir1 = 0; clocks(5); ir1 = 1; clocks(40);
        rreg(8'h10, r); check(r == 0, "5 us flicker ignored");
        rreg(8'h20, t0);
        pos = 32'd777;
        ir1 = 0; clocks(30);
        rreg(8'h00, r); check(r[2] == 1, "beam blocked (debounced)");
        rreg(8'h10, r); check(r == 1, "one blocked edge");
        rreg(8'h18, r); check(r == 777, "stepper position captured");
        rreg(8'h1C, r); check(r == t0, "encoder count captured");
        rreg(8'h14, r); check(r > 0, "edge timestamp");
        ir1 = 1; clocks(30);
        wreg(8'h38, 32'h1);   // polarity: high = blocked -> now the clear beam reads blocked
        rreg(8'h00, r); check(r[2] == 1, "CFG polarity inverts IR1");
        wreg(8'h38, 32'h0);
        wreg(8'h04, 32'h4); rreg(8'h10, r); check(r == 0, "IR1 counter clear");
        lim = 0; clocks(30); rreg(8'h00, r); check(r[3] == 1, "limit active low");

        $display("des_io: %0d passed, %0d failed", passes, fails);
        $finish;
    end
endmodule
