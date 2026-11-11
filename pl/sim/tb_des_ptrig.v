`timescale 1ns/1ps
// des_ptrig (belt position compare trigger) with the real des_pwm behind it. 1 MHz clock: 1 clock = 1 us.
// Fire instant, wrap-safe compare, late flag, both directions, encoder source, PWM load + early period restart
// (latency against a plain register write), no truncated pulse, SLEW ramp, TRIG_OUT pulse, servo go, disarm.
module tb_des_ptrig;
    reg clk = 0;
    always #500 clk = ~clk;                        // 1 MHz

    reg        wr = 0;
    reg [11:0] waddr = 0, raddr = 0;
    reg [31:0] wdata = 0;
    wire [31:0] pw_rdata, tg_rdata;
    wire [31:0] rdata = raddr[11:8] == 4'h4 ? pw_rdata : tg_rdata;
    reg  [31:0] ts = 0, pos = 0, enc = 0;
    always @(posedge clk) ts <= ts + 1;
    wire [1:0]  pwm, pwm_ld;
    wire [23:0] pwm_us;
    wire        servo_go, trig_o;

    des_ptrig #(.CLK_HZ(1_000_000)) dut (
        .clk(clk), .wr(wr && waddr[11:8] == 4'h8), .waddr(waddr[7:0]), .wdata(wdata), .raddr(raddr[7:0]), .rdata(tg_rdata),
        .ts(ts), .pos(pos), .enc(enc), .pwm_ld(pwm_ld), .pwm_us(pwm_us), .servo_go(servo_go), .trig_o(trig_o));
    des_pwm #(.CLK_HZ(1_000_000)) pw (
        .clk(clk), .wr(wr && waddr[11:8] == 4'h4), .waddr(waddr[7:0]), .wdata(wdata), .raddr(raddr[7:0]), .rdata(pw_rdata),
        .pwm(pwm), .xld(pwm_ld), .xus(pwm_us));

    integer fails = 0, passes = 0;
    reg [8*100-1:0] msg;
    task check(input cond, input [8*100-1:0] what);
        begin
            if (cond) begin passes = passes + 1; $display("ok   %0s", what); end
            else begin fails = fails + 1; $display("FAIL: %0s", what); end
        end
    endtask
    task wreg(input [11:0] a, input [31:0] d);
        begin @(posedge clk); wr <= 1; waddr <= a; wdata <= d; @(posedge clk); wr <= 0; end
    endtask
    task rreg(input [11:0] a, output [31:0] d);
        begin @(posedge clk); raddr <= a; @(posedge clk); #1 d = rdata; end
    endtask
    task us(input integer n); begin repeat (n) @(posedge clk); end endtask
    // walk the position one count every `per` clocks
    task walk(input [31:0] to, input integer per);
        begin while (pos != to) begin repeat (per) @(posedge clk); #1 pos = ($signed(to - pos) > 0) ? pos + 1 : pos - 1; end end
    endtask
    task walk_enc(input [31:0] to, input integer per);
        begin while (enc != to) begin repeat (per) @(posedge clk); #1 enc = ($signed(to - enc) > 0) ? enc + 1 : enc - 1; end end
    endtask
    // entry CFG word: data, pulse TRIG_OUT, action, source, direction, arm
    function [31:0] cfg(input [11:0] data, input o, input [2:0] act, input src, input dir, input arm);
        cfg = {4'd0, data, 8'd0, o, act, 1'b0, src, dir, arm};
    endfunction

    // ---------------- monitors (times in us) ----------------
    real rise0 = 0, w0 = 0, rise1 = 0, w1 = 0, t_trig_r = 0, w_trig = 0;
    integer n0 = 0, wlog [0:63], go_clocks = 0, bad_w0 = 0;
    always @(posedge pwm[0]) rise0 = $realtime / 1000.0;
    always @(negedge pwm[0]) begin
        w0 = $realtime / 1000.0 - rise0; wlog[n0 % 64] = $rtoi(w0 + 0.5); n0 = n0 + 1;
    end
    always @(posedge pwm[1]) rise1 = $realtime / 1000.0;
    always @(negedge pwm[1]) w1 = $realtime / 1000.0 - rise1;
    always @(posedge trig_o) t_trig_r = $realtime / 1000.0;
    always @(negedge trig_o) w_trig = $realtime / 1000.0 - t_trig_r;
    always @(posedge clk) if (servo_go) go_clocks = go_clocks + 1;
    // reference fire instant: ts in the clock where the watched source first equals the watched value
    reg [31:0] watch = 0, t_ref = 0;
    reg        watch_enc = 0, seen = 1;
    always @(posedge clk) if (!seen && (watch_enc ? enc : pos) == watch) begin t_ref <= ts; seen <= 1; end
    task arm_watch(input [31:0] v, input e); begin watch = v; watch_enc = e; seen = 0; end endtask

    reg [31:0] r, r2, r3, c0;
    real t0, lat;
    integer k, n_before;
    initial begin
        rreg(12'h80C, r); check(r == 32'h50544731, "ID");
        rreg(12'h804, r); rreg(12'h810, r2); check(r == 0 && r2 == 100, "reset: nothing armed, TRIG_OUT 100 us");
        rreg(12'h418, r); check(r == 0, "des_pwm SLEW reset value 0");

        // ---- 1. rising compare: fires in the clock the position reaches the threshold, not before ----
        wreg(12'h840, 32'd1000); wreg(12'h844, cfg(0, 1, 0, 0, 0, 1));
        arm_watch(32'd1000, 0);
        walk(32'd990, 20);
        rreg(12'h804, r); check(r[0] && !r[8], "armed, not fired at 990");
        walk(32'd1010, 20);
        rreg(12'h804, r); rreg(12'h848, r2); rreg(12'h84C, r3);
        check(!r[0] && r[8] && !r[16], "fired once, disarmed, not late");
        check(r2 == t_ref, "TS = the clock in which the position reads 1000");
        check(r3 == 32'd1000, "POS latched = 1000");
        rreg(12'h808, r); check(r == 1, "FIRE_CNT 1");
        us(200);
        $sformat(msg, "TRIG_OUT pulse %0.0f us", w_trig); check(w_trig == 100.0, msg);
        walk(32'd1200, 5);
        rreg(12'h808, r); check(r == 1, "no second fire without re-arming");

        // ---- 2. threshold already passed when armed: fires at once, LATE ----
        wreg(12'h840, 32'd500); wreg(12'h844, cfg(0, 0, 0, 0, 0, 1));
        us(3);
        rreg(12'h804, r); rreg(12'h84C, r2);
        $sformat(msg, "late arm: fired at once with LATE, POS 1200 (STATUS %0h, POS %0d)", r, r2); check(r[8] && r[16] && r2 == 32'd1200, msg);

        // ---- 3. falling direction ----
        wreg(12'h850, 32'd900); wreg(12'h854, cfg(0, 0, 0, 0, 1, 1));
        arm_watch(32'd900, 0);
        walk(32'd880, 10);
        rreg(12'h858, r); rreg(12'h85C, r2); rreg(12'h804, r3);
        check(r == t_ref && r2 == 32'd900 && r3[9] && !r3[17], "falling: fired at 900 exactly, not late");

        // ---- 4. across the 32-bit wrap (0x7FFFFFF0 -> 0x80000010) ----
        pos = 32'h7FFF_FFF0;
        us(2);
        wreg(12'h840, 32'h8000_0010); wreg(12'h844, cfg(0, 0, 0, 0, 0, 1));
        arm_watch(32'h8000_0010, 0);
        us(5);
        rreg(12'h804, r); check(r[0] && !r[8], "wrap: armed below the sign change, not fired");
        walk(32'h8000_0020, 5);
        rreg(12'h848, r); rreg(12'h84C, r2); rreg(12'h804, r3);
        check(r == t_ref && r2 == 32'h8000_0010 && !r3[16], "wrap: fired at 0x80000010 exactly");

        // ---- 5. encoder as the source ----
        enc = 32'hFFFF_FF00;                         // the belt encoder counts down going forward
        us(2);
        wreg(12'h860, 32'hFFFF_FE00); wreg(12'h864, cfg(0, 0, 0, 1, 1, 1));
        arm_watch(32'hFFFF_FE00, 1);
        walk(32'h8000_0100, 3);                      // the stepper position moving must not matter
        rreg(12'h804, r); check(r[2] && !r[10], "encoder entry ignores the stepper position");
        walk_enc(32'hFFFF_FDF0, 4);
        rreg(12'h868, r); rreg(12'h86C, r2);
        check(r == t_ref && r2 == 32'hFFFF_FE00, "encoder source: fired at the encoder threshold");

        // ---- 6. PWM load: restarts the period, new pulse out within microseconds ----
        pos = 0; us(2);
        wreg(12'h408, 32'd1500); wreg(12'h40C, 32'd1500); wreg(12'h400, 32'h3);
        us(45_000);
        // (a) plain register write at +10 ms into the period: the new width waits for the next period (v16 path)
        @(posedge pwm[0]); t0 = $realtime / 1000.0; us(10_000);
        wreg(12'h408, 32'd1100); lat = $realtime / 1000.0;
        @(posedge pwm[0]); lat = $realtime / 1000.0 - lat;
        @(negedge pwm[0]);
        $sformat(msg, "register write at +10 ms: new pulse %0.0f us later, width %0.0f", lat, w0);
        check(lat > 9_990 && lat < 10_010 && w0 == 1100.0, msg);
        wreg(12'h408, 32'd1500); us(45_000);
        // (b) trigger at +10 ms into the period
        wreg(12'h840, 32'd100); wreg(12'h844, cfg(12'd1100, 1, 1, 0, 0, 1));
        n_before = n0;
        @(posedge pwm[0]); us(10_000);
        rreg(12'h410, c0);
        pos <= 32'd100; @(posedge trig_o); lat = $realtime / 1000.0;
        @(posedge pwm[0]); lat = $realtime / 1000.0 - lat;
        @(negedge pwm[0]);
        $sformat(msg, "trigger at +10 ms: new pulse %0.0f us after the fire, width %0.0f", lat, w0);
        check(lat <= 4 && w0 == 1100.0, msg);
        rreg(12'h410, r); check(r == c0 + 1, "CNT counts the restarted period");
        rreg(12'h408, r); check(r == 1100, "CH0_US reads the loaded width");
        // (c) trigger while the old pulse is still high (+0.7 ms): that pulse completes, restart at +5 ms
        wreg(12'h408, 32'd1500); us(45_000);
        wreg(12'h840, 32'd200); wreg(12'h844, cfg(12'd1100, 1, 1, 0, 0, 1));
        @(posedge pwm[0]); t0 = $realtime / 1000.0; us(700);
        pos <= 32'd200; @(posedge trig_o); lat = $realtime / 1000.0;
        @(negedge pwm[0]);
        $sformat(msg, "trigger inside a pulse: the running pulse keeps its %0.0f us", w0); check(w0 == 1500.0, msg);
        @(posedge pwm[0]); lat = $realtime / 1000.0 - lat;
        $sformat(msg, "trigger at +0.7 ms: next pulse at +%0.0f us from the last start (5000), %0.0f us after the fire",
                 $realtime / 1000.0 - t0, lat);
        check($realtime / 1000.0 - t0 == 5000.0 && lat < 4_310, msg);
        @(negedge pwm[0]); check(w0 == 1100.0, "new width after the restart");
        // no pulse of any other width was ever produced
        bad_w0 = 0;
        for (k = n_before; k < n0; k = k + 1) if (wlog[k % 64] != 1500 && wlog[k % 64] != 1100) bad_w0 = bad_w0 + 1;
        check(bad_w0 == 0 && n0 - n_before < 64, "every ch0 pulse was 1500 or 1100 us wide (none truncated)");

        // ---- 7. two entries in the same clock, one per channel ----
        wreg(12'h840, 32'd300); wreg(12'h844, cfg(12'd1200, 0, 1, 0, 0, 1));
        wreg(12'h850, 32'd300); wreg(12'h854, cfg(12'd1800, 0, 2, 0, 0, 1));
        rreg(12'h808, c0);
        us(12_000); pos <= 32'd300; us(4);
        rreg(12'h808, r); rreg(12'h848, r2); rreg(12'h858, r3);
        check(r == c0 + 2 && r2 == r3, "both entries fired in the same clock, FIRE_CNT +2");
        us(6_000);
        $sformat(msg, "both channels loaded: ch0 %0.0f, ch1 %0.0f us", w0, w1); check(w0 == 1200.0 && w1 == 1800.0, msg);

        // ---- 8. SLEW: the output width walks to the target, 40 us per period ----
        wreg(12'h418, 32'd40); us(25_000);
        n_before = n0;
        wreg(12'h408, 32'd1000);
        us(8 * 20_000);
        while (wlog[n_before % 64] == 1200 && n_before < n0) n_before = n_before + 1;   // first pulse that moved
        bad_w0 = 0;
        for (k = 0; k < 6; k = k + 1) if (wlog[(n_before + k) % 64] != (k < 5 ? 1200 - 40 * (k + 1) : 1000)) bad_w0 = bad_w0 + 1;
        $sformat(msg, "SLEW 40: widths %0d %0d %0d %0d %0d %0d, then %0.0f", wlog[n_before % 64], wlog[(n_before + 1) % 64],
                 wlog[(n_before + 2) % 64], wlog[(n_before + 3) % 64], wlog[(n_before + 4) % 64], wlog[(n_before + 5) % 64], w0);
        check(bad_w0 == 0 && w0 == 1000.0, msg);
        wreg(12'h418, 32'd0); wreg(12'h408, 32'd1500); us(45_000);
        check(w0 == 1500.0, "SLEW 0: the width jumps as in v16");

        // ---- 9. servo go, disarm ----
        go_clocks = 0;
        wreg(12'h840, 32'd400); wreg(12'h844, cfg(0, 0, 3, 0, 0, 1));
        pos <= 32'd400; us(6);
        check(go_clocks == 1, "action 3: servo_go high for exactly one clock");
        wreg(12'h840, 32'd600); wreg(12'h844, cfg(0, 0, 0, 0, 0, 1));
        wreg(12'h850, 32'd600); wreg(12'h854, cfg(0, 0, 0, 0, 0, 1));
        wreg(12'h854, cfg(0, 0, 0, 0, 0, 0));       // entry 1 disarmed by its own CFG
        rreg(12'h804, r); check(r[0] && !r[1], "CFG arm = 0 disarms one entry");
        wreg(12'h800, 32'h1);                        // all off
        rreg(12'h804, r); rreg(12'h808, c0); check(r == 0, "CTRL.b0: all disarmed, flags cleared");
        pos <= 32'd700; us(6);
        rreg(12'h808, r); check(r == c0, "no fire after the disarm");

        $display("des_ptrig: %0d passed, %0d failed", passes, fails);
        $finish;
    end
    initial begin #(5_000_000_000.0); $display("TIMEOUT"); $finish; end
endmodule
