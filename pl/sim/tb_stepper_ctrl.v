`timescale 1ns/1ps
// stepper_ctrl: position moves, direction setup time, velocity mode stop, halt and estop,
// target rewritten while moving (stop-at ahead / target already passed), BRAKE register
module tb_stepper_ctrl;
    reg clk = 0;
    always #6.6667 clk = ~clk;                     // 75 MHz

    reg        wr = 0;
    reg [7:0]  waddr = 0, raddr = 0;
    reg [31:0] wdata = 0;
    wire [31:0] rdata;
    reg        estop = 0;
    wire       pul_n, dir_n, ena_o;

    // ramp tick 1 MHz to keep the simulation short
    stepper_ctrl #(.ACC_DIV(75)) dut (.clk(clk), .wr(wr), .waddr(waddr), .wdata(wdata), .raddr(raddr), .rdata(rdata),
                                     .estop(estop), .pul_n(pul_n), .dir_n(dir_n), .ena_o(ena_o), .pos_o(pos_o));
    wire [31:0] pos_o;

    // speed units: inc = v * 2^32 / 75e6
    function [31:0] inc_of(input real v); inc_of = v * 4294967296.0 / 75.0e6; endfunction
    localparam real VMAX = 50000.0, VMIN = 2000.0;

    // ---------------- independent pulse monitor ----------------
    integer mon_pos = 0, mon_pulses = 0;
    real    t_fall = 0, t_prev_fall = -1, min_period = 1e18, min_width = 1e18, t_dir_change = 0;
    real    min_dir_setup = 1e18;
    real    t_first = -1;                     // first pulse of the run (START -> first step latency)
    reg     last_dir_n = 1;
    always @(dir_n) begin t_dir_change = $realtime; end
    always @(negedge pul_n) begin
        t_fall = $realtime;
        if (t_first < 0) t_first = $realtime;
        if (t_prev_fall >= 0 && t_fall - t_prev_fall < min_period) min_period = t_fall - t_prev_fall;
        if (t_fall - t_dir_change < min_dir_setup) min_dir_setup = t_fall - t_dir_change;
        t_prev_fall = t_fall;
        mon_pulses = mon_pulses + 1;
        mon_pos = dir_n ? mon_pos + 1 : mon_pos - 1;   // dir_n = ~dir, dir=0 is +
    end
    always @(posedge pul_n) if (mon_pulses > 0 && $realtime - t_fall < min_width) min_width = $realtime - t_fall;

    // ---------------- helpers ----------------
    task reg_wr(input [7:0] a, input [31:0] d);
        begin @(posedge clk); wr <= 1; waddr <= a; wdata <= d; @(posedge clk); wr <= 0; end
    endtask
    task reg_rd(input [7:0] a, output [31:0] d);
        begin @(posedge clk); raddr <= a; @(posedge clk); #1 d = rdata; end
    endtask
    task wait_idle;
        reg [31:0] s;
        begin s = 1; while (s[0]) begin #(50000); reg_rd(8'h04, s); end end
    endtask

    integer errors = 0;
    task check(input [255:0] what, input integer got, input integer exp);
        begin
            if (got !== exp) begin errors = errors + 1; $display("FAIL %0s: got %0d exp %0d", what, got, exp); end
            else $display("ok   %0s = %0d", what, got);
        end
    endtask
    task check_ge(input [255:0] what, input real got, input real lim);
        begin
            if (got < lim) begin errors = errors + 1; $display("FAIL %0s: %0.2f < %0.2f", what, got, lim); end
            else $display("ok   %0s = %0.2f (>= %0.2f)", what, got, lim);
        end
    endtask

    reg [31:0] r;
    real t_start, t_end;
    initial begin
        repeat (5) @(posedge clk);
        reg_wr(8'h0C, inc_of(VMAX));
        reg_wr(8'h14, inc_of(VMIN));
        reg_wr(8'h10, inc_of(5.0));                  // +5 steps/s per 1 us tick = 5e6 steps/s^2
        reg_wr(8'h00, 32'h11);                       // enable, position mode
        @(posedge clk); #1;
        check("ENA pin high when enabled", ena_o, 1);

        // 1) 0 -> +1000
        reg_wr(8'h08, 1000);
        t_start = $realtime;
        reg_wr(8'h00, 32'h13);                       // enable | start | position
        wait_idle;
        t_end = $realtime;
        reg_rd(8'h18, r); check("pos after +1000 move", $signed(r), 1000);
        check("monitor pos", mon_pos, 1000);
        // first step right after the 10 us DIR setup, not one start-speed period (1 / VMIN = 500 us) later
        check("first step within 12 us of START", (t_first - t_start) < 12000.0, 1);
        reg_rd(8'h04, r); check("AT_TARGET", r[2], 1);
        check_ge("min pulse width ns", min_width, 4900.0);
        check_ge("min step period ns (>= 1/VMAX)", min_period, 1.0e9 / VMAX * 0.98);
        check_ge("dir setup before first pulse ns", min_dir_setup, 9900.0);
        $display("     move time %0.2f ms (ideal trapezoid ~30 ms)", (t_end - t_start) / 1.0e6);

        // 2) back to -200 (direction change)
        min_dir_setup = 1e18;
        reg_wr(8'h08, -200);
        reg_wr(8'h00, 32'h13);
        wait_idle;
        reg_rd(8'h18, r); check("pos after move to -200", $signed(r), -200);
        check("monitor pos", mon_pos, -200);
        check_ge("dir setup after reversal ns", min_dir_setup, 9900.0);

        // 3) velocity mode run then STOP
        reg_wr(8'h00, 32'h03);                       // enable | start, velocity mode, dir +
        #(8_000_000);
        reg_rd(8'h04, r); check("velocity mode moving", r[0], 1);
        reg_wr(8'h00, 32'h05);                       // enable | stop
        wait_idle;
        reg_rd(8'h18, r); check("hw pos == monitor after velocity run", $signed(r), mon_pos);
        reg_rd(8'h20, r); check("STEPS total == pulses", r, mon_pulses);

        // 4) HALT during a move
        reg_wr(8'h08, 100000);
        reg_wr(8'h00, 32'h13);
        #(3_000_000);
        reg_wr(8'h00, 32'h19);                       // enable | halt | position
        #(20_000);
        r = mon_pulses; #(200_000);
        check("no pulses after HALT", mon_pulses - r, 0);
        reg_rd(8'h04, r); check("HALTED flag", r[3], 1); check("not moving", r[0], 0);
        reg_rd(8'h18, r); check("hw pos == monitor after halt", $signed(r), mon_pos);

        // 5) estop input
        reg_wr(8'h00, 32'h13);
        #(3_000_000);
        estop = 1; #(20_000);
        r = mon_pulses; #(200_000);
        check("no pulses during estop", mon_pulses - r, 0);
        estop = 0;
        reg_rd(8'h04, r); check("HALTED after estop", r[3], 1);

        // 6) belt "stop at": cruise toward a far target, then rewrite TARGET to a point ahead
        reg_wr(8'h08, 32'd200000);
        reg_wr(8'h00, 32'h13);
        #(3_000_000);
        reg_rd(8'h24, r); check_ge("BRAKE steps while cruising", r, 1.0);
        reg_rd(8'h18, r);
        check("pos_o port == POS register", $signed(pos_o), $signed(r));
        t_start = $signed(r) + 3000;
        reg_wr(8'h08, $signed(r) + 3000);
        wait_idle;
        reg_rd(8'h18, r); check("stopped exactly at rewritten target", $signed(r), $rtoi(t_start));
        reg_rd(8'h04, r); check("AT_TARGET after stop-at", r[2], 1);
        reg_rd(8'h24, r); check("BRAKE steps 0 at rest", r, 0);

        // 7) TARGET rewritten behind the direction of travel: must ramp down and stop, not freeze
        reg_wr(8'h08, 32'd400000);
        reg_wr(8'h00, 32'h13);
        #(3_000_000);
        reg_rd(8'h18, r);
        t_start = $signed(r);
        reg_wr(8'h08, $signed(r) - 50);
        t_end = $realtime;
        wait_idle;
        $display("     overshoot stop took %0.2f us", ($realtime - t_end) / 1000.0);
        check_ge("overshoot ramps down (us)", ($realtime - t_end) / 1000.0, 1000.0);
        reg_rd(8'h04, r); check("stopped (not frozen) after target passed", r[0], 0);
        check("AT_TARGET 0 after overshoot", r[2], 0);
        reg_rd(8'h18, r); check_ge("ran past the passed target", $signed(r), t_start);
        check("hw pos == monitor after overshoot", $signed(r), mon_pos);

        // 8) TARGET rewritten to exactly current position during movement (v9 bugfix regression)
        reg_wr(8'h08, 32'd500000);
        reg_wr(8'h00, 32'h13);
        #(3_000_000);
        reg_rd(8'h18, r);
        r = $signed(r);                              // save current position
        reg_wr(8'h08, r);                            // write TARGET = POS while moving
        t_end = $realtime;
        // must stop within finite time (2 ms), not freeze with MOVING=1 forever
        #(2_000_000);
        reg_rd(8'h04, r); check("stopped after TARGET==POS rewrite", r[0], 0);
        check("AT_TARGET after TARGET==POS", r[2], 1);
        reg_rd(8'h18, r); check("hw pos == monitor after TARGET==POS", $signed(r), mon_pos);
        $display("     TARGET==POS stop took %0.2f us", ($realtime - t_end) / 1000.0);
        // verify next START works (not permanently locked)
        t_start = $signed(r);                        // save position for next target
        reg_wr(8'h08, t_start + 5000);
        reg_wr(8'h00, 32'h13);
        #(1_000_000);
        reg_rd(8'h04, r); check("can start again after TARGET==POS", r[0], 1);
        wait_idle;
        reg_rd(8'h18, r); check("reached next target after unlock", $signed(r), t_start + 5000);

        // 9) release
        reg_wr(8'h00, 32'h10);
        @(posedge clk); #1;
        check("ENA pin low when released", ena_o, 0);

        if (errors == 0) $display("ALL TESTS PASSED"); else $display("%0d TESTS FAILED", errors);
        $finish;
    end
    initial begin #400_000_000; $display("TIMEOUT (sim)"); $finish; end
endmodule
