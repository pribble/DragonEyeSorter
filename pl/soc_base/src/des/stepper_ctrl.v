// stepper_ctrl: step/dir generator with trapezoidal ramps for TB6600-class drivers.
// FPGA 3.3V direct drive, common-cathode wiring (PUL high active: system_top inverts pul_n;
// ENA high = energised, ENA- low releases). Keep v <= 50k steps/s (5us pulses need 10us period).
// Speed = phase increment per clock, Q0.32 steps: v[steps/s] = inc * CLK_HZ / 2^32 (PS converts).
// Ramp tick = ACC_DIV clocks (10kHz @75MHz): speed moves by ACC_INC toward target each tick
// (add/sub only, no mult/div in synthesis). Position mode brakes when remaining <= accel_steps.
// TARGET rewritable mid-move: == POS stops in place (AT_TARGET=1); behind travel ramps past it
// (AT_TARGET=0, never freezes). estop/HALT stops pulses immediately, sets HALTED.
// Regs (0x200 window): 0x00 CTRL b0 EN b1 START(p) b2 STOP(p) b3 HALT(p) b4 MODE b5 ZERO_POS(p) b6 VEL_DIR
// (R: b0/b4/b6); 0x04 STATUS b0 MOVING b1 DIR b2 AT_TARGET b3 HALTED b4 ENABLED; 0x08 TARGET (signed);
// 0x0C VMAX_INC; 0x10 ACC_INC; 0x14 VMIN_INC; 0x18 POS (signed); 0x1C CUR_INC; 0x20 STEPS; 0x24 BRAKE.
module stepper_ctrl #(
    parameter integer CLK_HZ   = 75_000_000,
    parameter integer ACC_DIV  = 7500,
    parameter integer PULSE_US = 5,
    parameter integer DIR_SETUP_US = 10
) (
    input  wire        clk,
    input  wire        wr,
    input  wire [7:0]  waddr,
    input  wire [31:0] wdata,
    input  wire [7:0]  raddr,
    output reg  [31:0] rdata,
    input  wire        estop,
    output wire        pul_n,
    output wire        dir_n,
    output wire        ena_o,
    output wire [31:0] pos_o
);
    localparam integer PULSE_CLK = CLK_HZ / 1_000_000 * PULSE_US, SETUP_CLK = CLK_HZ / 1_000_000 * DIR_SETUP_US;
    localparam integer ACCW = $clog2(ACC_DIV), PULSEW = $clog2(PULSE_CLK + 1), SETUPW = $clog2(SETUP_CLK + 1);
    reg        enable = 0, mode = 0, vel_dir = 0, moving = 0, dir = 0, halted = 0, stopping = 0;
    reg signed [31:0] target = 0, pos = 0;
    reg [31:0] vmax_inc = 0, acc_inc = 0, vmin_inc = 0, cur_inc = 0, phase = 0, accel_steps = 0, steps_total = 0;
    reg [ACCW-1:0] acc_cnt = 0;
    reg [PULSEW-1:0] pulse_cnt = 0;
    reg [SETUPW-1:0] setup_cnt = 0;
    reg [3:0] req = 0;                     // pulse requests, one per CTRL command bit
    localparam [3:0] REQ_STOP = 0, REQ_HALT = 1, REQ_START = 2, REQ_ZERO = 3;
    wire signed [31:0] dist = dir ? (pos - target) : (target - pos);   // travelled distance to target
    wire [31:0] remaining = dist[31] ? 0 : dist;
    wire overshoot = mode && dist[31];
    wire want_down = stopping || overshoot || (mode && remaining <= accel_steps);
    wire [32:0] phase_next = {1'b0, phase} + {1'b0, cur_inc};
    wire step_now = moving && (setup_cnt == 0) && phase_next[32] && !(mode && !overshoot && remaining == 0);
    always @(posedge clk) begin
        req <= 0;
        if (wr) case (waddr)
            8'h00: begin
                enable <= wdata[0]; mode <= wdata[4]; vel_dir <= wdata[6];
                req <= {wdata[5], wdata[1], wdata[3], wdata[2]};
            end
            8'h08: target <= wdata;
            8'h0C: vmax_inc <= wdata;
            8'h10: acc_inc <= wdata;
            8'h14: vmin_inc <= wdata;
        endcase
        if (req[REQ_ZERO] && !moving) pos <= 0;
        {pulse_cnt, setup_cnt} <= {pulse_cnt - (pulse_cnt != 0), setup_cnt - (setup_cnt != 0)};
        if (estop || req[REQ_HALT] || !enable) begin
            if (moving) halted <= 1'b1;
            moving <= 0; stopping <= 0; cur_inc <= 0; phase <= 0; accel_steps <= 0;
        end else if (req[REQ_START] && !moving) begin
            halted <= 0; stopping <= 0; accel_steps <= 0; acc_cnt <= 0;
            phase <= 32'hFFFF_FFFF;                    // first step right after DIR setup, not one vmin period later
            cur_inc <= vmin_inc; setup_cnt <= SETUP_CLK;
            dir <= mode ? (target < pos) : vel_dir; moving <= mode ? (target != pos) : 1'b1;
        end else if (moving) begin
            if (req[REQ_STOP]) stopping <= 1'b1;
            if (acc_cnt == ACC_DIV - 1) begin
                acc_cnt <= 0;
                // TARGET==POS mid-move emits no step, so stop here; a target BEHIND travel ramps past instead.
                if (mode && pos == target) begin moving <= 0; cur_inc <= 0; end
                else if (want_down) begin
                    if (cur_inc > vmin_inc + acc_inc) cur_inc <= cur_inc - acc_inc;
                    else begin
                        cur_inc <= vmin_inc;
                        if (stopping || overshoot) begin moving <= 0; stopping <= 0; end
                    end
                end else if (cur_inc != vmax_inc)
                    cur_inc <= ((vmax_inc > cur_inc ? vmax_inc - cur_inc : cur_inc - vmax_inc) > acc_inc) ? (vmax_inc > cur_inc ? cur_inc + acc_inc : cur_inc - acc_inc) : vmax_inc;
            end else acc_cnt <= acc_cnt + 1'b1;
            // phase advance + step emission: accel_steps follows the ramp direction per step
            if (setup_cnt == 0) phase <= phase_next[31:0];
            if (step_now) begin
                pulse_cnt <= PULSE_CLK;
                steps_total <= steps_total + 1'b1;
                pos <= dir ? pos - 1 : pos + 1;
                if (!want_down && cur_inc < vmax_inc) accel_steps <= accel_steps + 1'b1;
                else if (want_down && accel_steps != 0) accel_steps <= accel_steps - 1'b1;
                if (mode && remaining == 1) begin moving <= 0; cur_inc <= 0; end
            end
        end
    end

    assign pul_n = (pulse_cnt == 0);
    assign dir_n = ~dir;
    assign ena_o = enable;
    assign pos_o = pos;

    always @(*) begin
        case (raddr)
            8'h00: rdata = {25'd0, vel_dir, 1'b0, mode, 3'd0, enable};
            8'h04: rdata = {27'd0, enable, halted, (pos == target), dir, moving};
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
