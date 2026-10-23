// des_io.v — field inputs (v11): e-stop latch, through-beam / limit capture, quadrature belt encoder.
// Replaces the deleted io_sync.v (two-sample sync, no debounce, no register access).
//
// E-stop (CEP PIN15, NO contact to GND, pull-up): debounced EST_DEB_US, latched on press; the latch
// drives the stepper's estop until the PS clears it — a clear is ignored while still pressed.
// IR1 (NPN) / LIMIT1: debounced IN_DEB_US; each IR1 "blocked" edge captures timestamp, stepper POS
// and encoder count. Encoder (E6B2-CWZ6C class, A/B/Z): 2-FF sync + glitch filter, x4 decode
// (A leading B counts up), illegal double transitions counted, index latches count, 10 ms speed window.
// Registers (0x500 window, combinational read):
//   0x00 STATUS R b0 ESTOP_LATCHED b1 ESTOP_NOW b2 IR1_BLOCKED b3 LIMIT1_ACTIVE b4..6 enc A/B/Z
//   0x04 CTRL W b0 ESTOP_CLR b1 ENC_ZERO b2 IR1_CNT_CLR / 0x08 ESTOP_TS / 0x0C ESTOP_CNT
//   0x10 IR1_CNT / 0x14 IR1_TS / 0x18 IR1_POS / 0x1C IR1_ENC (blocked-edge captures)
//   0x20 ENC_CNT signed x4 / 0x24 ENC_ERR / 0x28 ENC_ZPOS / 0x2C ENC_SPEED / 0x30 ENC_ZCNT
//   0x34 ID R 0x494F3031 / 0x38 CFG RW b0 IR1 polarity b1 LIMIT1 polarity (0 = low-active)
`include "des_common.vh"
module des_io #(
    parameter integer CLK_HZ     = 75_000_000,
    parameter integer EST_DEB_US = 1000,
    parameter integer IN_DEB_US  = 20,
    parameter integer ENC_FILT   = 8,          // clocks a new A/B/Z level must hold (~107 ns at 75 MHz)
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
    localparam integer EST_DEB = CLK_HZ / 1_000_000 * EST_DEB_US;   // 75000: 17 bits
    localparam integer IN_DEB  = CLK_HZ / 1_000_000 * IN_DEB_US;    // 1500: 11 bits
    localparam integer SPEED_N = CLK_HZ / 1000 * SPEED_MS;          // 750000: 20 bits

    // ---------------- synchronisers (2-FF, idle high) ----------------
    wire [5:0] pin;   // {estop_n, ir1, limit1, enc_a, enc_b, enc_z}
    des_sync2 #(.W(6), .INIT(1)) u_pin (.clk(clk),
        .din({estop_n, ir1_in, limit1_in, enc_a, enc_b, enc_z}), .dout(pin));

    // ---------------- debounce (e-stop / IR1 / LIMIT1) + encoder glitch filters ----------------
    wire est_pressed, ir_raw_db, lim_raw_db, fa, fb, fz;
    des_deb #(.N(EST_DEB),  .WDB(17), .INIT(0)) u_est (.clk(clk), .din(~pin[5]), .dout(est_pressed));
    des_deb #(.N(IN_DEB),   .WDB(11), .INIT(1)) u_ir  (.clk(clk), .din(pin[4]),  .dout(ir_raw_db));
    des_deb #(.N(IN_DEB),   .WDB(11), .INIT(1)) u_lim (.clk(clk), .din(pin[3]),  .dout(lim_raw_db));
    des_deb #(.N(ENC_FILT), .WDB(4),  .INIT(1)) u_fa  (.clk(clk), .din(pin[2]),  .dout(fa));
    des_deb #(.N(ENC_FILT), .WDB(4),  .INIT(1)) u_fb  (.clk(clk), .din(pin[1]),  .dout(fb));
    des_deb #(.N(ENC_FILT), .WDB(4),  .INIT(1)) u_fz  (.clk(clk), .din(pin[0]),  .dout(fz));

    // ---------------- registers / state ----------------
    reg        cfg_ir_pol = 1'b0, cfg_lim_pol = 1'b0;
    reg        est_latched = 1'b0, est_prev = 1'b0;
    reg [31:0] est_ts = 0, est_n = 0;
    reg        ir_blk_prev = 1'b0;
    reg [31:0] ir_n = 0, ir_ts = 0, ir_pos = 0, ir_enc = 0;
    reg        pa = 1'b1, pb = 1'b1, pz = 1'b1;
    reg [31:0] enc = 0, enc_err = 0, enc_zpos = 0, enc_zn = 0, enc_speed = 0, enc_ref = 0;
    reg [19:0] spd_div = 0;

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
