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
// One 48 / 24 restoring divider serves both divisions (numerator and quotient share one shift register); one 18 x 17
// multiplier serves both products.
// Sized for the device (the 32-bit first version did not route): times and speeds are 24 bits (windows up to 223 ms
// at 75 MHz, speeds up to 2^-8 counts per clock), the window length is the saturating clock count since the reference
// event (FREE_CNT counts every clock, so no time stamps are kept), counts per window are 16 bits, the counters are
// 16 bits, the configuration registers except SET and SMAX are write-only. `ts` is no longer used.
module des_bpi #(
    parameter [31:0] PERIOD_DEF = 32'd750_000, TIMEOUT_DEF = 32'd7_500_000   // 10 ms / 100 ms at 75 MHz; 24 bits used
) (
    input  wire        clk, wr, input  wire [7:0]  waddr,
    input  wire [31:0] wdata, input  wire [7:0]  raddr,
    output reg  [31:0] rdata, input  wire [31:0] ts,
    input  wire [31:0] enc,
    input  wire        st_cruise,
    output wire signed [16:0] scale_o
);
    localparam [14:0] SMAX_CAP = 15'd16384;                      // hard limit of the scale: +-25 %
    localparam [2:0] S_IDLE = 3'd0, S_DIV = 3'd1, S_MEAS = 3'd2, S_ERR = 3'd3, S_REL = 3'd4, S_MULI = 3'd5,
                     S_ACC = 3'd6, S_OUT = 3'd7;
    reg        en = 1'b0, ref_ok = 1'b0, win_ok = 1'b0, win_ok_l = 1'b0, stall = 1'b0, sat = 1'b0, ran = 1'b0;
    reg        second = 1'b0, e_neg = 1'b0, down = 1'b0, go = 1'b0;
    reg [23:0] set = 0, period = PERIOD_DEF[23:0], timeout = TIMEOUT_DEF[23:0], wt = 0, meas = 0, den = 0, rem = 0;
    reg [1:0]  enc_d = 0;
    reg [15:0] ref_enc = 0, kp = 0, ki = 0, db = 0, meas_cnt = 0, step_cnt = 0;
    reg [14:0] smax = 0;
    reg [7:0]  settle = 8'd5, settle_cnt = 0;
    reg [2:0]  state = S_IDLE;
    reg [5:0]  dcnt = 0;
    reg [47:0] nq = 0;                                           // numerator shifting out at the top, quotient in at the bottom
    reg signed [17:0] err = 0;                                   // relative error, Q0.16, +-1.0
    reg signed [16:0] scale = 0;
    reg signed [34:0] prod = 0;
    reg signed [31:0] acc = 0;                                   // Q0.32, within +-SMAX << 16
    wire evt = enc[1:0] != enc_d && enc[1:0] == 2'b00;           // the count moves one at a time (des_io)
    wire clr = wr && waddr[7:2] == 6'h00 && wdata[1];
    wire [15:0] dm = enc[15:0] - ref_enc;                        // counts since the reference event, signed
    wire [15:0] dm_abs = dm[15] ? -dm : dm;
    // restoring divider: one quotient bit per clock, most significant first
    wire [24:0] d_sh = {rem, nq[47]};
    wire [25:0] d_df = {1'b0, d_sh} - {2'b00, den};
    wire        d_ge = !d_df[25];
    wire [24:0] e_raw = {1'b0, set} - {1'b0, meas};              // bit 24 = MEAS above SET
    wire [23:0] e_abs = e_raw[24] ? -e_raw[23:0] : e_raw[23:0];
    wire [16:0] rel_mag = |nq[47:16] ? 17'd65536 : {1'b0, nq[15:0]};
    wire signed [16:0] k_s = (state == S_ACC) ? {1'b0, kp} : {1'b0, ki};
    wire signed [35:0] a_lim = {5'd0, smax, 16'd0};
    wire signed [35:0] a_sum = acc + prod;
    wire signed [35:0] a_cl = a_sum > a_lim ? a_lim : a_sum < -a_lim ? -a_lim : a_sum;
    wire signed [23:0] p_term = prod >>> 12;
    wire signed [16:0] i_term = acc >>> 16;
    wire signed [24:0] u = p_term + i_term;
    wire signed [24:0] s_lim = {10'd0, smax};
    wire        u_hi = u > s_lim, u_lo = u < -s_lim;
    assign scale_o = scale;
    always @(posedge clk) begin
        if (wr) case (waddr[7:2])
            6'h00: en <= wdata[0];
            6'h02: set <= wdata[23:0];
            6'h03: kp <= wdata[15:0];
            6'h04: ki <= wdata[15:0];
            6'h05: smax <= (wdata > {17'd0, SMAX_CAP}) ? SMAX_CAP : wdata[14:0];
            6'h06: db <= wdata[15:0];
            6'h07: period <= wdata[23:0];
            6'h08: timeout <= wdata[23:0];
            6'h09: settle <= wdata[7:0];
        endcase
        enc_d <= enc[1:0];
        if (!(&wt)) wt <= wt + 1'b1;
        if (!st_cruise) begin win_ok <= 1'b0; settle_cnt <= 8'd0; end
        // ---------------- window ----------------
        // wt restarts at 1 in the clock of the reference event, so at a later event it is the clocks in between
        if (evt && !ref_ok) begin
            ref_ok <= 1'b1; ref_enc <= enc[15:0]; wt <= 24'd1; win_ok <= st_cruise;
        end else if (evt && wt >= period && state == S_IDLE) begin   // |dm| << 32 / dt
            nq <= {dm_abs, 32'd0}; den <= wt; rem <= 24'd0; dcnt <= 6'd0; down <= dm[15];
            ref_enc <= enc[15:0]; wt <= 24'd1;
            win_ok_l <= win_ok && st_cruise; win_ok <= st_cruise;
            second <= 1'b0; state <= S_DIV;
        end else if (wt >= timeout && state == S_IDLE) begin      // belt standing: speed 0, the next event re-arms
            ref_ok <= 1'b0; wt <= 24'd0; stall <= 1'b1; meas <= 24'd0; meas_cnt <= meas_cnt + 1'b1;
            win_ok_l <= win_ok && st_cruise; win_ok <= st_cruise;
            state <= S_ERR;
        end
        // ---------------- measure + PI ----------------
        case (state)
            S_DIV: begin
                rem <= d_ge ? d_df[23:0] : d_sh[23:0];
                nq <= {nq[46:0], d_ge};
                dcnt <= dcnt + 1'b1;
                if (dcnt == 6'd47) state <= second ? S_REL : S_MEAS;
            end
            S_MEAS: begin
                meas <= |nq[47:24] ? 24'hFF_FFFF : nq[23:0];
                meas_cnt <= meas_cnt + 1'b1; stall <= 1'b0;
                state <= S_ERR;
            end
            S_ERR: begin   // |SET - MEAS| << 16 / SET
                e_neg <= e_raw[24];
                nq <= {8'd0, e_abs, 16'd0}; den <= set; rem <= 24'd0; dcnt <= 6'd0;
                if (win_ok_l && settle_cnt != 8'hFF) settle_cnt <= settle_cnt + 1'b1;
                second <= 1'b1;
                state <= (set == 24'd0) ? S_IDLE : S_DIV;         // no set-point: nothing to control
            end
            S_REL: begin
                err <= (rel_mag <= {1'b0, db}) ? 18'sd0 : e_neg ? -$signed({1'b0, rel_mag}) : $signed({1'b0, rel_mag});
                state <= S_MULI;
            end
            S_MULI: begin
                prod <= err * k_s;                                // e * KI
                go <= en && win_ok_l && settle_cnt > settle;
                state <= S_ACC;
            end
            S_ACC: begin
                if (go) acc <= a_cl[31:0];
                prod <= err * k_s;                                // e * KP
                state <= S_OUT;
            end
            S_OUT: begin
                ran <= 1'b0;
                if (go && en) begin
                    scale <= u_hi ? s_lim[16:0] : u_lo ? -s_lim[16:0] : u[16:0];
                    sat <= u_hi || u_lo; step_cnt <= step_cnt + 1'b1; ran <= 1'b1;
                end
                state <= S_IDLE;
            end
            default: ;
        endcase
        if (!en || clr) begin acc <= 32'sd0; scale <= 17'sd0; sat <= 1'b0; end
    end
    always @* begin
        case (raddr[7:2])
            6'h00: rdata = {31'd0, en};
            6'h01: rdata = {25'd0, down, st_cruise, ref_ok, sat, stall, ran, en};
            6'h02: rdata = {8'd0, set};
            6'h05: rdata = {17'd0, smax};
            6'h0A: rdata = {8'd0, meas};      6'h0B: rdata = {{14{err[17]}}, err};
            6'h0C: rdata = {{15{scale[16]}}, scale};
            6'h0D: rdata = {{16{acc[31]}}, acc[31:16]};
            6'h0E: rdata = {16'd0, meas_cnt}; 6'h0F: rdata = {16'd0, step_cnt};
            6'h12: rdata = 32'h42504931;      // "BPI1"
            default: rdata = 32'hDEADBEEF;
        endcase
    end
endmodule
