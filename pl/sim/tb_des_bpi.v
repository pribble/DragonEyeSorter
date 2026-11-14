`timescale 1ns/1ps
// des_bpi (belt speed PI) closed around the real stepper_ctrl and a belt / encoder model.
// 1 MHz clock (1 clock = 1 us) to keep the run short: the Q0.32 "per clock" speeds are 75x their 75 MHz values; the
// PI works on the relative error, so its gains / limits / dead band are the same numbers as on the board.
// Belt model: each step moves the belt mm_per_step * (1 - slip), through a 20 ms first-order lag; the encoder count
// follows the belt with unequal quadrature phases (30 / 20 / 30 / 20 %), the way a real wheel encoder is off.
module tb_des_bpi;
    reg clk = 0;
    always #500 clk = ~clk;                        // 1 MHz

    localparam real MM_STEP = 0.020921, MM_CNT = 0.049227, CLK = 1.0e6;
    localparam real ACC_MM = 400.0, VMIN_MM = 5.0;

    reg        wr = 0;
    reg [11:0] waddr = 0, raddr = 0;
    reg [31:0] wdata = 0;
    wire [31:0] st_rdata, pi_rdata;
    wire [31:0] rdata = raddr[11:8] == 4'h2 ? st_rdata : pi_rdata;
    wire       pul_n, dir_n, ena_o, st_cruise;
    wire [31:0] pos_o;
    wire signed [16:0] scale;
    reg  [31:0] ts = 0, enc = 0;
    always @(posedge clk) ts <= ts + 1;

    stepper_ctrl #(.CLK_HZ(1_000_000), .ACC_DIV(100)) st (
        .clk(clk), .wr(wr && waddr[11:8] == 4'h2), .waddr(waddr[7:0]), .wdata(wdata), .raddr(raddr[7:0]), .rdata(st_rdata),
        .estop(1'b0), .pul_n(pul_n), .dir_n(dir_n), .ena_o(ena_o), .pos_o(pos_o),
        .vscale(scale), .moving_o(), .cruise_o(st_cruise));
    des_bpi #(.PERIOD_DEF(32'd10_000), .TIMEOUT_DEF(32'd100_000)) dut (
        .clk(clk), .wr(wr && waddr[11:8] == 4'h7), .waddr(waddr[7:0]), .wdata(wdata), .raddr(raddr[7:0]), .rdata(pi_rdata),
        .ts(ts), .enc(enc), .st_cruise(st_cruise), .scale_o(scale));

    // ---------------- belt + encoder model ----------------
    real slip = 0.0, belt_cmd = 0.0, belt_x = 0.0, cyc, frac;
    reg  blocked = 0;
    integer enc_sign = -1, want, sub, steps = 0;
    always @(negedge pul_n) begin
        steps = steps + 1;
        if (!blocked) belt_cmd = belt_cmd + (dir_n ? 1.0 : -1.0) * MM_STEP * (1.0 - slip);
    end
    always @(posedge clk) begin
        belt_x = belt_x + (belt_cmd - belt_x) * 0.00005;                                          // tau = 20 ms
        cyc = belt_x / (4.0 * MM_CNT) + 1000.0;                                                   // keep it positive
        frac = cyc - $floor(cyc);
        sub = frac >= 0.80 ? 3 : frac >= 0.50 ? 2 : frac >= 0.30 ? 1 : 0;
        want = enc_sign * (4 * ($rtoi($floor(cyc)) - 1000) + sub);
        if (want > $signed(enc)) enc <= enc + 1;                                                  // one count per clock, as des_io
        else if (want < $signed(enc)) enc <= enc - 1;
    end

    // ---------------- helpers ----------------
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
    task ms(input integer n); begin repeat (n) #(1_000_000); end endtask
    function [31:0] st_inc(input real mm_s); st_inc = mm_s / MM_STEP * 4294967296.0 / CLK; endfunction
    function [31:0] en_inc(input real mm_s); en_inc = mm_s / MM_CNT * 4294967296.0 / CLK; endfunction
    function real mm_of(input [31:0] meas); mm_of = meas * CLK / 4294967296.0 * MM_CNT; endfunction
    function real pct(input real v, input real ref); pct = (v - ref) / ref * 100.0; endfunction
    function real rabs(input real v); rabs = v < 0 ? -v : v; endfunction

    reg [31:0] r, r2, c0, c1;
    real v, tr, lo, hi, rate;
    integer i, s0;
    // step rate over a time span (steps/s)
    task step_rate(input integer span_ms, output real sps);
        integer a;
        begin a = steps; ms(span_ms); sps = (steps - a) * 1000.0 / span_ms; end
    endtask
    // MEAS spread over n windows (min / max, mm/s)
    task spread(input integer n);
        integer k; reg [31:0] m, c, cp;
        begin
            lo = 1e9; hi = 0; rreg(12'h738, cp);
            k = 0;
            while (k < n) begin
                rreg(12'h738, c);
                if (c != cp) begin cp = c; rreg(12'h728, m); if (mm_of(m) < lo) lo = mm_of(m); if (mm_of(m) > hi) hi = mm_of(m); k = k + 1; end
                else #(200_000);
            end
        end
    endtask
    task start_vel(input real mm_s);   // velocity mode, forward
        begin
            wreg(12'h20C, st_inc(mm_s)); wreg(12'h210, st_inc(ACC_MM) / 10000); wreg(12'h214, st_inc(VMIN_MM));
            wreg(12'h200, 32'h03);
        end
    endtask

    localparam integer SMAX = 5243;                 // 8 % in Q0.16
    initial begin
        rreg(12'h748, r); check(r == 32'h42504931, "ID");
        rreg(12'h71C, r); rreg(12'h724, r2); check(r == 32'hDEADBEEF && r2 == 32'hDEADBEEF, "PERIOD / SETTLE are write-only");
        wreg(12'h714, 32'd40000); rreg(12'h714, r); check(r == 16384, "SMAX capped at 25 % (16384)");
        // KP 0.5 (Q4.12), KI 0.1 per window (Q0.16), scale limit 8 %, no dead band
        wreg(12'h708, en_inc(50.0)); wreg(12'h70C, 32'd2048); wreg(12'h710, 32'd6554);
        wreg(12'h714, SMAX); wreg(12'h718, 0);

        // ---- 1. PI off: open loop, belt 4 % slow; measurement exact despite the unequal quadrature phases ----
        slip = 0.04;
        start_vel(50.0); ms(600);
        spread(40);
        $sformat(msg, "PI off: MEAS %0.3f..%0.3f mm/s = belt 48.00 +-0.3 %%", lo, hi);
        check(rabs(pct(lo, 48.0)) < 0.3 && rabs(pct(hi, 48.0)) < 0.3, msg);
        rreg(12'h730, r); rreg(12'h73C, r2); check(r == 0 && r2 == 0, "PI off: SCALE 0, no PI steps");
        rreg(12'h738, c0); ms(1000); rreg(12'h738, c1);
        $sformat(msg, "PERIOD reset value: %0d windows in 1 s (10 ms windows, each closed on the next like edge)", c1 - c0);
        check(c1 - c0 >= 80 && c1 - c0 <= 100, msg);
        rreg(12'h704, r); check(r[6], "STATUS.b6: the encoder counts down (model default)");
        step_rate(200, rate);
        $sformat(msg, "PI off: step rate %0.1f = 2389.9 steps/s (v16 speed)", rate); check(rabs(pct(rate, 50.0 / MM_STEP)) < 0.3, msg);
        rreg(12'h224, c0);   // steps of the start ramp = (v^2 - vmin^2) / 2a at the programmed 50 mm/s
        v = (50.0 * 50.0 - VMIN_MM * VMIN_MM) / (2.0 * ACC_MM) / MM_STEP;
        $sformat(msg, "BRAKE %0d steps = %0.0f +-3 %% (programmed speed)", c0, v); check(rabs(pct(c0, v)) < 3.0, msg);

        // ---- 2. PI on: converges to the set-point, scale = slip compensation ----
        wreg(12'h700, 32'h1); ms(1500);
        spread(30);
        $sformat(msg, "PI on: MEAS %0.3f..%0.3f mm/s = 50.00 +-0.3 %%", lo, hi);
        check(rabs(pct(lo, 50.0)) < 0.3 && rabs(pct(hi, 50.0)) < 0.3, msg);
        rreg(12'h730, r); tr = $signed(r) / 655.36;
        $sformat(msg, "PI on: SCALE %0.2f %% (1/0.96 - 1 = 4.17 %%)", tr); check(rabs(tr - 4.167) < 0.3, msg);
        rreg(12'h704, r); check(r[0] && !r[2] && !r[3] && r[5], "STATUS: enabled, cruise, no stall, not saturated");
        step_rate(200, rate);
        $sformat(msg, "PI on: step rate %0.1f = 2489.5 steps/s", rate); check(rabs(pct(rate, 50.0 / MM_STEP / 0.96)) < 0.4, msg);

        // ---- 3. the PI must not touch the stepper's brake bookkeeping, however long it trims ----
        for (i = 0; i < 30; i = i + 1) begin slip = (i % 2) ? 0.02 : 0.06; ms(100); end
        slip = 0.04; ms(800);
        rreg(12'h224, r); check(r == c0, "BRAKE unchanged after 3 s of trimming up and down");

        // ---- 4. load step 4 % -> 7 %: back within 0.3 % in 0.6 s ----
        slip = 0.07; ms(600);
        spread(20);
        $sformat(msg, "load step: MEAS %0.3f..%0.3f mm/s back at 50.00 +-0.3 %% after 0.6 s", lo, hi);
        check(rabs(pct(lo, 50.0)) < 0.3 && rabs(pct(hi, 50.0)) < 0.3, msg);

        // ---- 5. more slip than the limit can cover: output pinned at the limit, flag set; recovery without windup ----
        slip = 0.15; ms(1200);
        rreg(12'h730, r); rreg(12'h704, r2);
        check($signed(r) == SMAX && r2[3], "saturation: SCALE == +SMAX exactly, SAT set");
        rreg(12'h728, r); $sformat(msg, "saturation: MEAS %0.2f = 50 * 0.85 * 1.08 = 45.90 mm/s", mm_of(r)); check(rabs(pct(mm_of(r), 45.9)) < 0.4, msg);
        slip = 0.0; ms(700);
        spread(20);
        $sformat(msg, "after saturation: MEAS %0.3f..%0.3f mm/s at 50.00 +-0.3 %% within 0.7 s (no windup)", lo, hi);
        check(rabs(pct(lo, 50.0)) < 0.3 && rabs(pct(hi, 50.0)) < 0.3, msg);
        rreg(12'h704, r); check(!r[3], "SAT cleared");

        // ---- 6. stop / restart: scale and integrator frozen while braking, standing and ramping up ----
        slip = 0.04; ms(1000);
        rreg(12'h730, c0);
        wreg(12'h200, 32'h05);                       // decelerate to a stop
        ms(400);
        rreg(12'h204, r); check(!r[0], "belt stopped");
        rreg(12'h730, c1); rreg(12'h73C, s0);
        check(c1 == c0, "SCALE unchanged through the stop");
        ms(300); rreg(12'h73C, r); check(r == s0, "no PI step while standing");
        rreg(12'h704, r); check(r[2] && !r[5], "STALL flagged while standing (no encoder events), not in cruise");
        wreg(12'h200, 32'h03);                       // start again
        ms(60); rreg(12'h730, c1); check(c1 == c0, "SCALE held during the start ramp");
        ms(300);
        spread(20);
        $sformat(msg, "after restart: MEAS %0.3f..%0.3f mm/s at 50.00 +-0.3 %% 0.36 s after the start (integrator kept)", lo, hi);
        check(rabs(pct(lo, 50.0)) < 0.3 && rabs(pct(hi, 50.0)) < 0.3, msg);

        // ---- 7. speed change 50 -> 80 mm/s: frozen through the ramp whatever the write order, then converges ----
        rreg(12'h73C, s0);
        wreg(12'h708, en_inc(80.0)); wreg(12'h20C, st_inc(80.0));
        ms(70); rreg(12'h73C, r); check(r - s0 <= 1, "at most one PI step slips in before the 75 ms speed ramp");
        ms(1500);
        spread(20);
        $sformat(msg, "80 mm/s: MEAS %0.3f..%0.3f mm/s = 80.00 +-0.3 %% (same gains)", lo, hi);
        check(rabs(pct(lo, 80.0)) < 0.3 && rabs(pct(hi, 80.0)) < 0.3, msg);
        wreg(12'h20C, st_inc(50.0)); wreg(12'h708, en_inc(50.0)); ms(1200);

        // ---- 8. dead band: an error inside it does not move the output ----
        wreg(12'h718, 32'd1311);                     // +-2 % = 1 mm/s at 50 mm/s
        ms(200); rreg(12'h730, c0);
        slip = 0.05; ms(500);                        // 0.5 mm/s slow: inside the band
        rreg(12'h730, c1); check(c1 == c0, "dead band 2 %: a 1 % error leaves SCALE untouched");
        rreg(12'h728, r); $sformat(msg, "dead band: belt left at %0.2f mm/s (0.5 mm/s slow)", mm_of(r)); check(rabs(mm_of(r) - 49.48) < 0.15, msg);
        wreg(12'h718, 0); ms(800);
        rreg(12'h72C, r); i = $signed(r); if (i < 0) i = -i;
        $sformat(msg, "dead band off: |ERR| %0.2f %% < 0.3 %%", i / 655.36); check(i < 197, msg);

        // ---- 9. belt blocked (stepper keeps stepping): STALL, MEAS 0, output bounded at the limit ----
        blocked = 1; ms(700);
        rreg(12'h704, r); rreg(12'h728, r2); check(r[2] && r2 == 0, "blocked belt: STALL set, MEAS 0");
        rreg(12'h730, r); rreg(12'h72C, r2); check($signed(r) == SMAX && r2 == 65536, "blocked belt: ERR 100 %, SCALE stops at +SMAX");
        blocked = 0; belt_cmd = belt_x; ms(1200);
        rreg(12'h704, r); check(!r[2], "STALL cleared when the belt moves again");
        spread(20);
        $sformat(msg, "after the block: MEAS %0.3f..%0.3f mm/s at 50.00 +-0.3 %%", lo, hi);
        check(rabs(pct(lo, 50.0)) < 0.3 && rabs(pct(hi, 50.0)) < 0.3, msg);

        // ---- 10. clear, disable ----
        wreg(12'h700, 32'h3);
        rreg(12'h730, r); rreg(12'h734, r2); check(r == 0 && r2 == 0, "CTRL.b1: integrator and SCALE cleared");
        ms(1000);
        rreg(12'h730, r); check($signed(r) > 0, "PI running again after the clear");
        wreg(12'h700, 32'h0);
        rreg(12'h730, r); rreg(12'h734, r2); check(r == 0 && r2 == 0, "disable: SCALE 0 at once");
        ms(100); step_rate(200, rate);
        $sformat(msg, "disable: step rate %0.1f back at 2389.9 steps/s", rate); check(rabs(pct(rate, 50.0 / MM_STEP)) < 0.3, msg);
        wreg(12'h200, 32'h05); ms(400);

        // ---- 11. position mode with the PI on: still stops on the exact step ----
        wreg(12'h700, 32'h1);
        rreg(12'h218, r); rreg(12'h73C, c1);
        wreg(12'h208, r + 32'd40000); wreg(12'h200, 32'h13);
        ms(2500);
        rreg(12'h204, r2); check(r2[0], "position move: cruising");
        rreg(12'h73C, s0); check(s0 - c1 > 100, "position move: more than 100 PI steps in 2.5 s of cruise");
        rreg(12'h730, r); check($signed(r) > 2500, "position move: SCALE above 3.8 % (belt 5 % slow)");
        rreg(12'h218, c0); wreg(12'h208, c0 + 32'd2000);         // stop-at: rewrite the target while moving
        ms(1500);
        rreg(12'h204, r2); rreg(12'h218, c1);
        check(!r2[0] && r2[2] && c1 == c0 + 32'd2000, "stop-at with the PI on: stopped on the exact step, AT_TARGET");

        // ---- 12. no set-point: nothing to control ----
        wreg(12'h700, 32'h0); wreg(12'h708, 0); wreg(12'h700, 32'h1);
        wreg(12'h200, 32'h03); ms(600);
        rreg(12'h730, r); rreg(12'h728, r2); check(r == 0 && r2 != 0, "SET 0: speed still measured, SCALE stays 0");
        wreg(12'h708, en_inc(50.0));

        // ---- 13. encoder counting the other way ----
        wreg(12'h700, 32'h0);
        enc_sign = 1; ms(300);                       // the model walks the count over to the mirrored value
        wreg(12'h700, 32'h1); ms(1500);
        spread(20);
        $sformat(msg, "count direction reversed: MEAS %0.3f..%0.3f mm/s at 50.00 +-0.3 %%", lo, hi);
        check(rabs(pct(lo, 50.0)) < 0.3 && rabs(pct(hi, 50.0)) < 0.3, msg);
        rreg(12'h704, r); check(!r[6], "STATUS.b6 clear for an up-counting encoder");

        $display("des_bpi: %0d passed, %0d failed", passes, fails);
        $finish;
    end
    initial begin #(60_000_000_000.0); $display("TIMEOUT"); $finish; end
endmodule
