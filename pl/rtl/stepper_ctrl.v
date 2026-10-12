// Step/dir generator with trapezoidal ramps for a TB6600-class driver (common-anode inputs via 74HCT245).
// PUL is active-low; ENA- low releases the motor, so ENA is driven high while enabled. Keep v <= 50k steps/s
// (5 us pulses need a 10 us period).
// Speed is a phase increment per clock in Q0.32 steps (v[steps/s] = inc * CLK_HZ / 2^32); PS converts units.
// Every ACC_DIV clocks the speed moves by ACC_INC toward its target. Position mode decelerates when the
// remaining distance is <= the steps spent accelerating (symmetric ramp), then stops exactly at TARGET.
// TARGET may be rewritten while moving (belt "stop at" for picking). If it ends up behind the direction of
// travel (written too late), the move ramps down and stops past it instead of freezing (STATUS AT_TARGET = 0).
// Rewriting TARGET to exactly the current position while moving stops right there (STATUS AT_TARGET = 1).
//
// Register block (byte offsets inside the 0x200 window):
//   0x00 CTRL     W: b0 ENABLE, b1 START (pulse), b2 STOP (ramp down, pulse), b3 HALT (immediate, pulse),
//                    b4 MODE (0 velocity, 1 position), b5 ZERO_POS (pulse), b6 VEL_DIR (velocity mode)
//                 R: b0 ENABLE, b4 MODE, b6 VEL_DIR
//   0x04 STATUS   R: b0 MOVING, b1 DIR, b2 AT_TARGET, b3 HALTED (estop or HALT), b4 ENABLED
//   0x08 TARGET   RW signed absolute position (steps), position mode
//   0x0C VMAX_INC RW cruise speed (Q0.32 steps/clock)
//   0x10 ACC_INC  RW speed change per ramp tick
//   0x14 VMIN_INC RW start / stop speed
//   0x18 POS      R  signed position (steps)
//   0x1C CUR_INC  R  current speed
//   0x20 STEPS    R  total steps emitted
//   0x24 BRAKE    R  steps needed to ramp down from the current speed (= steps spent accelerating)
module stepper_ctrl #(
    parameter integer CLK_HZ   = 75_000_000,
    parameter integer ACC_DIV  = 7500,              // ramp tick = 10 kHz at 75 MHz
    parameter integer PULSE_US = 5,
    parameter integer DIR_SETUP_US = 10
) (
    input  wire        clk,
    input  wire        wr,
    input  wire [7:0]  waddr,
    input  wire [31:0] wdata,
    input  wire [7:0]  raddr,
    output reg  [31:0] rdata,
    input  wire        estop,           // 1 = stop pulses immediately
    output wire        pul_n,
    output wire        dir_n,
    output wire        ena_o,           // pin level: 1 = driver energised, 0 = released (TB6600 ENA- low = off-line)
    output wire [31:0] pos_o            // current position, for the camera capture latch
);
    localparam integer PULSE_CLK = CLK_HZ / 1_000_000 * PULSE_US;
    localparam integer SETUP_CLK = CLK_HZ / 1_000_000 * DIR_SETUP_US;

    reg        enable = 0, mode = 0, vel_dir = 0;
    reg signed [31:0] target = 0;
    reg [31:0] vmax_inc = 0, acc_inc = 0, vmin_inc = 0;
    reg signed [31:0] pos = 0;
    reg [31:0] steps_total = 0;

    reg [31:0] cur_inc = 0;
    reg [31:0] phase = 0;
    reg        moving = 0, dir = 0, halted = 0, stopping = 0;
    reg [31:0] accel_steps = 0;
    reg [15:0] acc_cnt = 0;
    reg [15:0] pulse_cnt = 0;
    reg [15:0] setup_cnt = 0;
    reg        start_req = 0, stop_req = 0, halt_req = 0, zero_req = 0;

    // remaining distance in position mode (always >= 0 while moving toward target)
    wire signed [31:0] dist = dir ? (pos - target) : (target - pos);
    wire [31:0] remaining = dist[31] ? 32'd0 : dist;
    wire at_target = (pos == target);

    // speed target for this tick
    wire overshoot = mode && dist[31];          // target now behind the direction of travel
    wire decel_pos = mode && (remaining <= accel_steps);
    wire want_down = stopping || decel_pos || overshoot;

    wire [32:0] phase_next = {1'b0, phase} + {1'b0, cur_inc};
    wire step_now = moving && (setup_cnt == 0) && phase_next[32] && !(mode && !overshoot && remaining == 0);

    always @(posedge clk) begin
        // ---------- register writes ----------
        start_req <= 0; stop_req <= 0; halt_req <= 0; zero_req <= 0;
        if (wr) case (waddr)
            8'h00: begin
                enable  <= wdata[0];
                mode    <= wdata[4];
                vel_dir <= wdata[6];
                start_req <= wdata[1];
                stop_req  <= wdata[2];
                halt_req  <= wdata[3];
                zero_req  <= wdata[5];
            end
            8'h08: target   <= wdata;
            8'h0C: vmax_inc <= wdata;
            8'h10: acc_inc  <= wdata;
            8'h14: vmin_inc <= wdata;
            default: ;
        endcase

        if (zero_req && !moving) pos <= 0;

        // ---------- pulse output timing ----------
        if (pulse_cnt != 0) pulse_cnt <= pulse_cnt - 1'b1;
        if (setup_cnt != 0) setup_cnt <= setup_cnt - 1'b1;

        if (estop || halt_req || !enable) begin
            if (moving) halted <= 1'b1;
            moving <= 0; stopping <= 0; cur_inc <= 0; phase <= 0; accel_steps <= 0;
        end else if (start_req && !moving) begin
            halted <= 0;
            stopping <= 0;
            accel_steps <= 0;
            phase <= 32'hFFFF_FFFF;   // first step right after the DIR setup time, not one start-speed period later
            acc_cnt <= 0;
            cur_inc <= vmin_inc;
            setup_cnt <= SETUP_CLK;
            if (mode) begin
                dir <= (target < pos);
                moving <= (target != pos);
            end else begin
                dir <= vel_dir;
                moving <= 1'b1;
            end
        end else if (moving) begin
            if (stop_req) stopping <= 1'b1;

            // ramp tick
            if (acc_cnt == ACC_DIV - 1) begin
                acc_cnt <= 0;
                // TARGET rewritten to exactly current position mid-move: no steps are left, stop immediately
                // without waiting for decel complete (v9 bugfix: step_now gate at L71 freezes accel_steps,
                // so we never reach the normal remaining==1 last-step exit at L147).
                if (mode && remaining == 0) begin moving <= 0; cur_inc <= 0; end
                else if (want_down) begin
                    if (cur_inc > vmin_inc + acc_inc) cur_inc <= cur_inc - acc_inc;
                    else begin
                        cur_inc <= vmin_inc;
                        // ramped down: stop
                        if (stopping || overshoot) begin moving <= 0; stopping <= 0; end
                    end
                end else if (cur_inc < vmax_inc) begin
                    cur_inc <= (vmax_inc - cur_inc > acc_inc) ? cur_inc + acc_inc : vmax_inc;
                end else if (cur_inc > vmax_inc) begin
                    cur_inc <= (cur_inc - vmax_inc > acc_inc) ? cur_inc - acc_inc : vmax_inc;
                end
            end else acc_cnt <= acc_cnt + 1'b1;

            // step generation
            if (setup_cnt == 0) phase <= phase_next[31:0];
            if (step_now) begin
                pulse_cnt <= PULSE_CLK;
                steps_total <= steps_total + 1'b1;
                pos <= dir ? pos - 1 : pos + 1;
                if (!want_down && cur_inc < vmax_inc) accel_steps <= accel_steps + 1'b1;
                else if (want_down && accel_steps != 0) accel_steps <= accel_steps - 1'b1;
                if (mode && remaining == 1) begin moving <= 0; cur_inc <= 0; end    // last step
            end
        end
    end

    assign pul_n = ~(pulse_cnt != 0);
    assign dir_n = ~dir;
    assign ena_o = enable;
    assign pos_o = pos;

    always @(*) begin
        case (raddr)
            8'h00: rdata = {25'd0, vel_dir, 1'b0, mode, 3'd0, enable};
            8'h04: rdata = {27'd0, enable, halted, at_target, dir, moving};
            8'h08: rdata = target;
            8'h0C: rdata = vmax_inc;
            8'h10: rdata = acc_inc;
            8'h14: rdata = vmin_inc;
            8'h18: rdata = pos;
            8'h1C: rdata = cur_inc;
            8'h20: rdata = steps_total;
            8'h24: rdata = accel_steps;
            default: rdata = 32'hDEAD_BEEF;
        endcase
    end
endmodule
