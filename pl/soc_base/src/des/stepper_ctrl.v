// stepper_ctrl: step/dir generator with trapezoidal ramps for TB6600-class drivers; 寄存器 0x200 的偏移 / 位域 /
// 复位值以 docs/PL设计.md 为准. Common-cathode wiring (system_top inverts pul_n; ENA high = energised); keep
module stepper_ctrl #(
    parameter integer CLK_HZ   = 75_000_000,
    parameter integer ACC_DIV  = 7500,
    parameter integer PULSE_US = 5,
    parameter integer DIR_SETUP_US = 10
) (
    input  wire        clk, input  wire        wr,
    input  wire [7:0]  waddr, input  wire [31:0] wdata,
    input  wire [7:0]  raddr, output reg  [31:0] rdata,
    input  wire        estop, output wire        pul_n,
    output wire        dir_n, output wire        ena_o,
    output wire [31:0] pos_o,
    // v17 belt-speed PI (des_bpi): vscale = signed Q0.16, step rate = programmed rate * (1 + vscale). The ramps, the
    // brake distance and the stop-on-step logic keep working on the programmed rate; vscale 0 = v16, clock for clock
    input  wire signed [16:0] vscale,
    output wire        moving_o, output wire        cruise_o
);
    localparam integer PULSE_CLK = CLK_HZ / 1_000_000 * PULSE_US, SETUP_CLK = CLK_HZ / 1_000_000 * DIR_SETUP_US;
    localparam integer ACCW = $clog2(ACC_DIV), PULSEW = $clog2(PULSE_CLK + 1), SETUPW = $clog2(SETUP_CLK + 1);
    reg        enable = 0, mode = 0, vel_dir = 0, moving = 0, dir = 0, halted = 0, stopping = 0;
    reg signed [31:0] target = 0, pos = 0;
    reg [31:0] vmax_inc = 0, acc_inc = 0, vmin_inc = 0, cur_inc = 0, phase = 0, accel_steps = 0, steps_total = 0;
    reg [ACCW-1:0] acc_cnt = 0;
    reg [PULSEW-1:0] pulse_cnt = 0;
    reg [SETUPW-1:0] setup_cnt = 0;
    reg [3:0] req = 0;
    localparam [3:0] REQ_STOP = 0, REQ_HALT = 1, REQ_START = 2, REQ_ZERO = 3;
    wire signed [31:0] dist = dir ? (pos - target) : (target - pos);
    // remaining = dist[31] ? 0 : dist is only ever compared, so compare against dist directly:
    // remaining <= accel_steps == dist[31] | (dist <= accel_steps); remaining == {0,1} == !dist[31] & dist == {0,1}
    wire overshoot = mode && dist[31];
    wire want_down = stopping || overshoot || (mode && (dist[31] || dist <= accel_steps));
    reg signed [49:0] scl = 0;                                   // cur_inc * vscale, one clock late
    always @(posedge clk) scl <= $signed({1'b0, cur_inc}) * vscale;
    // a part-select is unsigned even of a signed vector: without $signed the sum went unsigned and any negative
    // correction gave inc_eff = 0, i.e. no steps (11-18, first PI test on the belt; tb_stepper_vscale)
    wire signed [34:0] inc_s = $signed({3'b000, cur_inc}) + $signed(scl[49:16]);
    wire [31:0] inc_eff = inc_s[34] ? 32'd0 : |inc_s[33:32] ? 32'hFFFF_FFFF : inc_s[31:0];
    wire [32:0] phase_next = {1'b0, phase} + {1'b0, inc_eff};
    wire step_now = moving && (setup_cnt == 0) && phase_next[32] && !(mode && !dist[31] && dist == 32'd0);
    // one ramp distance and one +-acc_inc adder for the vmax approach (the two directions never share a cycle)
    wire        vmax_up = vmax_inc > cur_inc;
    wire [31:0] d_up = vmax_inc - cur_inc, d_dn = cur_inc - vmax_inc;
    wire        ramp_far = vmax_up ? (d_up > acc_inc) : (d_dn > acc_inc);
    wire [32:0] ramp_sum = {1'b0, cur_inc} + {1'b0, vmax_up ? acc_inc : ~acc_inc} + {32'd0, ~vmax_up};
    wire        acc_dec = want_down && accel_steps != 0;
    wire        acc_upd = !want_down && cur_inc < vmax_inc;
    wire [31:0] acc_step = {{31{acc_dec}}, acc_dec | acc_upd};   // -1 / +1 / 0 in one incrementer
    wire [31:0] pos_step = {{31{dir}}, 1'b1};                    // dir ? -1 : +1 in one incrementer
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
                if (mode && pos == target) begin moving <= 0; cur_inc <= 0; end
                else if (want_down) begin
                    if (cur_inc > vmin_inc + acc_inc) cur_inc <= cur_inc - acc_inc;
                    else begin
                        cur_inc <= vmin_inc;
                        if (stopping || overshoot) begin moving <= 0; stopping <= 0; end
                    end
                end else if (cur_inc != vmax_inc)
                    cur_inc <= ramp_far ? ramp_sum[31:0] : vmax_inc;
            end else acc_cnt <= acc_cnt + 1'b1;
            if (setup_cnt == 0) phase <= phase_next[31:0];
            if (step_now) begin
                pulse_cnt <= PULSE_CLK;
                steps_total <= steps_total + 1'b1;
                pos <= pos + pos_step;
                if (mode && !dist[31] && dist == 32'd1) begin moving <= 0; cur_inc <= 0; end
                accel_steps <= accel_steps + acc_step;
            end
        end
    end
    assign pul_n = (pulse_cnt == 0);
    assign dir_n = ~dir;
    assign ena_o = enable;
    assign pos_o = pos;
    assign moving_o = moving;
    assign cruise_o = moving && !want_down && cur_inc == vmax_inc;
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
            default: rdata = 32'hDEADBEEF;
        endcase
    end
endmodule
