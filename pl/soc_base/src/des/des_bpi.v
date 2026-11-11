// des_bpi.v — belt speed PI in the PL (v17): speed of the metering-wheel encoder by the M/T method (counts and clock
// time between like quadrature edges), fixed-point PI on the relative error, output = signed Q0.16 scale on the
// stepper's step rate. 寄存器 0x700 的偏移 / 位域 / 复位值以 docs/PL设计.md 为准. CTRL.b0 = 0 (reset): scale 0 = v16.
//
// Measurement: an "event" is the count arriving at a multiple of 4, i.e. always the same electrical edge, so the
// A/B phase and duty errors of the encoder cancel. A window closes at the first event at least PERIOD clocks after the
// previous one: MEAS = |dm| * 2^32 / dt (Q0.32 counts per clock), dt known to one clock. No event for TIMEOUT clocks
// = belt standing: MEAS 0, STALL set.
// PI: e = (SET - MEAS) / SET (Q0.16, dead band DB), scale = clamp(e * KP >> 12 + acc >> 16, +-SMAX), acc += e * KI,
// acc clamped to the same limit (anti-windup). The gains do not depend on the speed or on the clock. A step is taken
// only for a window that lay entirely in the stepper's cruise, and after SETTLE such windows in a row; otherwise acc
// and scale hold (start and stop ramps, speed changes, the belt standing at the pick line).
// One 48 / 32 restoring divider serves both divisions; one 18 x 17 multiplier serves both products.
module des_bpi #(
    parameter [31:0] PERIOD_DEF = 32'd750_000, TIMEOUT_DEF = 32'd7_500_000   // 10 ms / 100 ms at 75 MHz
) (
    input  wire        clk, wr, input  wire [7:0]  waddr,
    input  wire [31:0] wdata, input  wire [7:0]  raddr,
    output reg  [31:0] rdata, input  wire [31:0] ts,
    input  wire [31:0] enc,
    input  wire        st_cruise,
    output wire signed [16:0] scale_o
);
    localparam [15:0] SMAX_CAP = 16'd16384;                      // hard limit of the scale: +-25 %
    localparam [3:0] S_IDLE = 4'd0, S_LOAD = 4'd1, S_DIV = 4'd2, S_MEAS = 4'd3, S_ERR = 4'd4, S_REL = 4'd5,
                     S_MULP = 4'd6, S_MULI = 4'd7, S_ACC = 4'd8, S_OUT = 4'd9;
    reg        en = 1'b0, ref_ok = 1'b0, win_ok = 1'b0, win_ok_l = 1'b0, stall = 1'b0, sat = 1'b0, ran = 1'b0, second = 1'b0;
    reg [31:0] set = 0, period = PERIOD_DEF, timeout = TIMEOUT_DEF, enc_d = 0, ref_ts = 0, ref_enc = 0, wt = 0;
    reg [15:0] kp = 0, ki = 0, smax = 0, db = 0;
    reg [7:0]  settle = 8'd5, settle_cnt = 0;
    reg signed [31:0] dm = 0;
    reg [31:0] dt = 0, meas = 0, meas_cnt = 0, step_cnt = 0, den = 0, e_mag = 0;
    reg        e_neg = 1'b0;
    reg [3:0]  state = S_IDLE;
    reg [5:0]  dcnt = 0;
    reg [47:0] num = 0, quo = 0;
    reg [32:0] rem = 0;
    reg signed [17:0] err = 0;                                   // relative error, Q0.16, +-1.0
    reg signed [16:0] scale = 0;
    reg signed [34:0] prod = 0, pprod = 0;
    reg signed [35:0] acc = 0, acc_t = 0;                        // Q0.32
    wire evt = enc != enc_d && enc[1:0] == 2'b00;
    wire clr = wr && waddr[7:2] == 6'h00 && wdata[1];
    wire [31:0] dm_abs = dm[31] ? -dm : dm;
    // restoring divider: one quotient bit per clock, most significant first
    wire [33:0] d_sh = {rem, num[47]};
    wire [33:0] d_df = d_sh - {2'b00, den};
    wire        d_ge = !d_df[33];
    wire [32:0] e_raw = {1'b0, set} - {1'b0, meas};              // bit 32 = MEAS above SET
    wire [16:0] rel_mag = |quo[47:16] && quo[47:0] > 48'd65536 ? 17'd65536 : quo[16:0];
    wire signed [16:0] kp_s = {1'b0, kp}, ki_s = {1'b0, ki};
    wire signed [35:0] a_lim = {4'd0, smax, 16'd0};
    wire signed [36:0] a_sum = acc + prod;
    wire signed [35:0] a_cl = a_sum > a_lim ? a_lim : a_sum < -a_lim ? -a_lim : a_sum[35:0];
    wire signed [31:0] p_term = pprod >>> 12, i_term = acc_t >>> 16;
    wire signed [32:0] u = p_term + i_term;
    wire signed [32:0] s_lim = {17'd0, smax};
    wire        u_hi = u > s_lim, u_lo = u < -s_lim;
    assign scale_o = scale;
    always @(posedge clk) begin
        if (wr) case (waddr[7:2])
            6'h00: en <= wdata[0];
            6'h02: set <= wdata;
            6'h03: kp <= wdata[15:0];
            6'h04: ki <= wdata[15:0];
            6'h05: smax <= (wdata > {16'd0, SMAX_CAP}) ? SMAX_CAP : wdata[15:0];
            6'h06: db <= wdata[15:0];
            6'h07: period <= wdata;
            6'h08: timeout <= wdata;
            6'h09: settle <= wdata[7:0];
        endcase
        enc_d <= enc;
        if (!(&wt)) wt <= wt + 1'b1;
        if (!st_cruise) begin win_ok <= 1'b0; settle_cnt <= 8'd0; end
        // ---------------- window ----------------
        if (evt && !ref_ok) begin
            ref_ok <= 1'b1; ref_ts <= ts; ref_enc <= enc; wt <= 0; win_ok <= st_cruise;
        end else if (evt && wt >= period && state == S_IDLE) begin
            dm <= enc - ref_enc; dt <= ts - ref_ts; ref_ts <= ts; ref_enc <= enc; wt <= 0;
            win_ok_l <= win_ok && st_cruise; win_ok <= st_cruise;
            second <= 1'b0; state <= S_LOAD;
        end else if (wt >= timeout && state == S_IDLE) begin      // belt standing: speed 0, the next event re-arms
            ref_ok <= 1'b0; wt <= 0; stall <= 1'b1; meas <= 32'd0; meas_cnt <= meas_cnt + 1'b1;
            win_ok_l <= win_ok && st_cruise; win_ok <= st_cruise;
            state <= S_ERR;
        end
        // ---------------- measure + PI ----------------
        case (state)
            S_LOAD: begin   // first: |dm| << 32 / dt; second: |SET - MEAS| << 16 / SET
                num <= second ? {e_mag, 16'd0} : {dm_abs > 32'd65535 ? 16'hFFFF : dm_abs[15:0], 32'd0};
                den <= second ? set : dt;
                rem <= 33'd0; quo <= 48'd0; dcnt <= 6'd0; state <= S_DIV;
            end
            S_DIV: begin
                rem <= d_ge ? d_df[32:0] : d_sh[32:0];
                quo <= {quo[46:0], d_ge};
                num <= num << 1;
                dcnt <= dcnt + 1'b1;
                if (dcnt == 6'd47) state <= second ? S_REL : S_MEAS;
            end
            S_MEAS: begin
                meas <= (den == 32'd0 || |quo[47:32]) ? 32'hFFFF_FFFF : quo[31:0];
                meas_cnt <= meas_cnt + 1'b1; stall <= 1'b0;
                state <= S_ERR;
            end
            S_ERR: begin
                e_neg <= e_raw[32]; e_mag <= e_raw[32] ? -e_raw[31:0] : e_raw[31:0];
                if (win_ok_l && settle_cnt != 8'hFF) settle_cnt <= settle_cnt + 1'b1;
                second <= 1'b1;
                state <= (set == 32'd0) ? S_IDLE : S_LOAD;        // no set-point: nothing to control
            end
            S_REL: begin
                err <= (rel_mag <= {1'b0, db}) ? 18'sd0 : e_neg ? -$signed({1'b0, rel_mag}) : $signed({1'b0, rel_mag});
                state <= S_MULP;
            end
            S_MULP: begin prod <= err * kp_s; state <= S_MULI; end
            S_MULI: begin pprod <= prod; prod <= err * ki_s; state <= S_ACC; end
            S_ACC: begin acc_t <= a_cl; state <= S_OUT; end
            S_OUT: begin
                ran <= 1'b0;
                if (en && win_ok_l && settle_cnt > settle) begin
                    acc <= acc_t; scale <= u_hi ? s_lim[16:0] : u_lo ? -s_lim[16:0] : u[16:0];
                    sat <= u_hi || u_lo; step_cnt <= step_cnt + 1'b1; ran <= 1'b1;
                end
                state <= S_IDLE;
            end
            default: ;
        endcase
        if (!en || clr) begin acc <= 36'sd0; scale <= 17'sd0; sat <= 1'b0; end
    end
    always @* begin
        case (raddr[7:2])
            6'h00: rdata = {31'd0, en};
            6'h01: rdata = {26'd0, st_cruise, ref_ok, sat, stall, ran, en};
            6'h02: rdata = set;               6'h03: rdata = {16'd0, kp};
            6'h04: rdata = {16'd0, ki};       6'h05: rdata = {16'd0, smax};
            6'h06: rdata = {16'd0, db};       6'h07: rdata = period;
            6'h08: rdata = timeout;           6'h09: rdata = {24'd0, settle};
            6'h0A: rdata = meas;              6'h0B: rdata = {{14{err[17]}}, err};
            6'h0C: rdata = {{15{scale[16]}}, scale};
            6'h0D: rdata = {{12{acc[35]}}, acc[35:16]};
            6'h0E: rdata = meas_cnt;          6'h0F: rdata = step_cnt;
            6'h10: rdata = dm;                6'h11: rdata = dt;
            6'h12: rdata = 32'h42504931;      // "BPI1"
            default: rdata = 32'hDEADBEEF;
        endcase
    end
endmodule
