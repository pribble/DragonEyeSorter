// des_io.v — field inputs (v11): e-stop latch, through-beam / limit inputs with capture, quadrature belt encoder.
// Replaces the unconnected io_sync.v (two-sample sync, no debounce, no register access).
//
// E-stop (CEP PIN15, NO contact to GND, pull-up): debounced (EST_DEB_US stable), latched on press; the latch drives the
// stepper's estop (no pulses, start commands refused) until the PS clears it — a clear is ignored while the button is
// still pressed. The NC contact cuts the motor / servo power in hardware; this is the "PL knows and stays stopped" part.
// IR1 (through-beam receiver, NPN) / LIMIT1: debounced (IN_DEB_US); on each IR1 "blocked" edge the free-running
// timestamp, the stepper position and the encoder count are captured (tracking error = predicted vs measured edge).
// Encoder (E6B2-CWZ6C class, NPN open collector, A/B/Z): 2-FF sync + ENC_FILT-sample glitch filter, x4 decode
// (A leading B counts up; the PS sets the sign per mounting), illegal double transitions counted, count captured on
// the index edge, speed = count change per SPEED_MS window.
//
// Registers (byte offsets in the 0x500 window, register clock, combinational read like the other DES blocks):
//   0x00 STATUS    R  b0 ESTOP_LATCHED, b1 ESTOP_NOW (debounced, 1 = pressed), b2 IR1_BLOCKED, b3 LIMIT1_ACTIVE,
//                     b4 ENC_A, b5 ENC_B, b6 ENC_Z (filtered levels, 1 = high)
//   0x04 CTRL      W  b0 ESTOP_CLR, b1 ENC_ZERO, b2 IR1_CNT_CLR (write 1 = do it)
//   0x08 ESTOP_TS  R  timestamp of the last latch
//   0x0C ESTOP_CNT R  latch events
//   0x10 IR1_CNT   R  blocked edges
//   0x14 IR1_TS    R  timestamp of the last blocked edge (debounce IN_DEB_US later than the beam)
//   0x18 IR1_POS   R  stepper position at that edge
//   0x1C IR1_ENC   R  encoder count at that edge
//   0x20 ENC_CNT   R  signed x4 count
//   0x24 ENC_ERR   R  illegal transitions (A and B changed together: too fast or noise)
//   0x28 ENC_ZPOS  R  count at the last index edge
//   0x2C ENC_SPEED R  signed counts per SPEED_MS window
//   0x30 ENC_ZCNT  R  index edges
//   0x34 ID        R  0x494F3031 'IO01'
//   0x38 CFG       RW b0 IR1 polarity (0: low = blocked, NPN dark-on), b1 LIMIT1 polarity (0: low = active)
module des_io #(
    parameter integer CLK_HZ     = 75_000_000,
    parameter integer EST_DEB_US = 1000,
    parameter integer IN_DEB_US  = 20,
    parameter integer ENC_FILT   = 8,          // samples a new A/B/Z level must hold (8 x 13.3 ns ~ 107 ns)
    parameter integer SPEED_MS   = 10
) (
    input  wire        clk,
    input  wire        wr,
    input  wire [7:0]  waddr,
    input  wire [31:0] wdata,
    input  wire [7:0]  raddr,
    output reg  [31:0] rdata,
    input  wire [31:0] ts,             // free-running timestamp (des_regs FREE_CNT)
    input  wire [31:0] pos,            // stepper position
    input  wire        estop_n,        // pins (asynchronous)
    input  wire        ir1_in,
    input  wire        limit1_in,
    input  wire        enc_a,
    input  wire        enc_b,
    input  wire        enc_z,
    output wire        estop_out       // to stepper_ctrl.estop
);
    localparam integer EST_DEB = CLK_HZ / 1_000_000 * EST_DEB_US;
    localparam integer IN_DEB  = CLK_HZ / 1_000_000 * IN_DEB_US;
    localparam integer SPEED_N = CLK_HZ / 1000 * SPEED_MS;

    // ---------------- synchronisers ----------------
    reg [1:0] s_est = 2'b11, s_ir = 2'b11, s_lim = 2'b11, s_a = 2'b11, s_b = 2'b11, s_z = 2'b11;
    always @(posedge clk) begin
        s_est <= {s_est[0], estop_n};
        s_ir  <= {s_ir[0], ir1_in};
        s_lim <= {s_lim[0], limit1_in};
        s_a   <= {s_a[0], enc_a};
        s_b   <= {s_b[0], enc_b};
        s_z   <= {s_z[0], enc_z};
    end

    // ---------------- debounced levels (state changes after the input held the new level for N clocks) ----------
    reg        est_pressed = 1'b0, ir_raw_db = 1'b1, lim_raw_db = 1'b1;
    reg [17:0] est_cnt = 0, ir_cnt_db = 0, lim_cnt_db = 0;   // 18 bits: 1 ms at 75 MHz = 75000
    always @(posedge clk) begin
        if ((~s_est[1]) == est_pressed) est_cnt <= 0;
        else if (est_cnt >= EST_DEB - 1) begin est_pressed <= ~s_est[1]; est_cnt <= 0; end
        else est_cnt <= est_cnt + 1;

        if (s_ir[1] == ir_raw_db) ir_cnt_db <= 0;
        else if (ir_cnt_db >= IN_DEB - 1) begin ir_raw_db <= s_ir[1]; ir_cnt_db <= 0; end
        else ir_cnt_db <= ir_cnt_db + 1;

        if (s_lim[1] == lim_raw_db) lim_cnt_db <= 0;
        else if (lim_cnt_db >= IN_DEB - 1) begin lim_raw_db <= s_lim[1]; lim_cnt_db <= 0; end
        else lim_cnt_db <= lim_cnt_db + 1;
    end

    // ---------------- encoder glitch filter ----------------
    reg       fa = 1'b1, fb = 1'b1, fz = 1'b1;
    reg [7:0] ca = 0, cb = 0, cz = 0;
    always @(posedge clk) begin
        if (s_a[1] == fa) ca <= 0; else if (ca >= ENC_FILT - 1) begin fa <= s_a[1]; ca <= 0; end else ca <= ca + 1;
        if (s_b[1] == fb) cb <= 0; else if (cb >= ENC_FILT - 1) begin fb <= s_b[1]; cb <= 0; end else cb <= cb + 1;
        if (s_z[1] == fz) cz <= 0; else if (cz >= ENC_FILT - 1) begin fz <= s_z[1]; cz <= 0; end else cz <= cz + 1;
    end

    // ---------------- registers / state ----------------
    reg        cfg_ir_pol = 1'b0, cfg_lim_pol = 1'b0;
    reg        est_latched = 1'b0, est_prev = 1'b0;
    reg [31:0] est_ts = 0, est_n = 0;
    reg        ir_blk_prev = 1'b0;
    reg [31:0] ir_n = 0, ir_ts = 0, ir_pos = 0, ir_enc = 0;
    reg        pa = 1'b1, pb = 1'b1, pz = 1'b1;
    reg [31:0] enc = 0, enc_err = 0, enc_zpos = 0, enc_zn = 0, enc_speed = 0, enc_ref = 0, spd_div = 0;

    wire ir_blocked = cfg_ir_pol ? ir_raw_db : ~ir_raw_db;
    wire lim_active = cfg_lim_pol ? lim_raw_db : ~lim_raw_db;
    assign estop_out = est_latched;

    wire wr_ctrl = wr && waddr[7:2] == 6'h01;
    wire a_chg = fa != pa, b_chg = fb != pb;

    always @(posedge clk) begin
        if (wr && waddr[7:2] == 6'h0E) begin cfg_ir_pol <= wdata[0]; cfg_lim_pol <= wdata[1]; end

        // e-stop: latch on the debounced press; clear only once released
        est_prev <= est_pressed;
        if (est_pressed && !est_prev) begin
            est_latched <= 1'b1;
            est_ts <= ts;
            est_n <= est_n + 1;
        end else if (wr_ctrl && wdata[0] && !est_pressed) begin
            est_latched <= 1'b0;
        end

        // through-beam: capture on the blocked edge
        ir_blk_prev <= ir_blocked;
        if (wr_ctrl && wdata[2]) ir_n <= 0;
        else if (ir_blocked && !ir_blk_prev) begin
            ir_n <= ir_n + 1;
            ir_ts <= ts;
            ir_pos <= pos;
            ir_enc <= enc;
        end

        // quadrature x4
        pa <= fa;
        pb <= fb;
        pz <= fz;
        if (wr_ctrl && wdata[1]) begin
            enc <= 0;
            enc_ref <= 0;
        end else if (a_chg && b_chg) begin
            enc_err <= enc_err + 1;
        end else if (a_chg || b_chg) begin
            if (pa == fb) enc <= enc + 1;   // A leads B
            else enc <= enc - 1;
        end
        if (!fz && pz) begin            // index: NPN output pulls low
            enc_zpos <= enc;
            enc_zn <= enc_zn + 1;
        end
        if (spd_div >= SPEED_N - 1) begin
            spd_div <= 0;
            enc_speed <= enc - enc_ref;
            enc_ref <= enc;
        end else begin
            spd_div <= spd_div + 1;
        end
    end

    always @* begin
        case (raddr[7:2])
            6'h00: rdata = {25'd0, fz, fb, fa, lim_active, ir_blocked, est_pressed, est_latched};
            6'h02: rdata = est_ts;
            6'h03: rdata = est_n;
            6'h04: rdata = ir_n;
            6'h05: rdata = ir_ts;
            6'h06: rdata = ir_pos;
            6'h07: rdata = ir_enc;
            6'h08: rdata = enc;
            6'h09: rdata = enc_err;
            6'h0A: rdata = enc_zpos;
            6'h0B: rdata = enc_speed;
            6'h0C: rdata = enc_zn;
            6'h0D: rdata = 32'h494F3031;
            6'h0E: rdata = {30'd0, cfg_lim_pol, cfg_ir_pol};
            default: rdata = 32'hDEADBEEF;
        endcase
    end
endmodule
