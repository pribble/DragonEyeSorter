// des_io.v — field inputs: e-stop latch, through-beam / limit capture, quadrature belt encoder; 寄存器 0x500 的
// 偏移 / 位域以 docs/PL设计.md 为准。
// E-stop (PIN15, NO contact to GND, pull-up): debounced EST_DEB_US, latched on press; the latch drives the
// stepper's estop until the PS clears it — a clear is ignored while still pressed. IR1 (NPN) / LIMIT1: debounced
// IN_DEB_US; each IR1 "blocked" edge captures ts / stepper POS / encoder count. Encoder (E6B2-CWZ6C class, A/B/Z):
// 2-FF sync + glitch filter, x4 decode (A leads B counts up), illegal double transitions counted, index latches
// count, 10 ms speed window.
`include "des_common.vh"
module des_io #(
    parameter integer CLK_HZ = 75_000_000, EST_DEB_US = 1000, IN_DEB_US = 20,
    parameter integer ENC_FILT = 8,        // clocks a new A/B/Z level must hold (~107 ns at 75 MHz)
    parameter integer SPEED_MS = 10
) (
    input  wire        clk, wr,
    input  wire [7:0]  waddr,
    input  wire [31:0] wdata,
    input  wire [7:0]  raddr,
    output reg  [31:0] rdata,
    input  wire [31:0] ts,              // free-running timestamp (des_regs FREE_CNT)
    input  wire [31:0] pos,             // stepper position
    input  wire        estop_n, ir1_in, limit1_in,
    input  wire        enc_a, enc_b, enc_z,
    output wire        estop_out,       // to stepper_ctrl.estop
    output wire [31:0] enc_o            // encoder count, to des_regs frame capture (v16)
);
    localparam integer EST_DEB = CLK_HZ / 1_000_000 * EST_DEB_US;   // 75000 clocks
    localparam integer IN_DEB  = CLK_HZ / 1_000_000 * IN_DEB_US;    // 1500 clocks
    localparam integer SPEED_N = CLK_HZ / 1000 * SPEED_MS;          // 750000 clocks
    localparam integer SPD_W   = (SPEED_N > 2) ? $clog2(SPEED_N) : 1;

    // ---------------- synchronisers (2-FF, idle high) + debounce / encoder glitch filters ----------------
    wire [5:0] pin;   // {estop_n, ir1, limit1, enc_a, enc_b, enc_z}
    wire est_pressed, ir_raw_db, lim_raw_db, fa, fb, fz;
    des_sync2 #(.W(6), .INIT(1)) u_pin (.clk(clk),
        .din({estop_n, ir1_in, limit1_in, enc_a, enc_b, enc_z}), .dout(pin));
    des_deb #(.N(EST_DEB),  .INIT(0)) u_est (.clk(clk), .din(~pin[5]), .dout(est_pressed));
    des_deb #(.N(IN_DEB),   .INIT(1)) u_ir  (.clk(clk), .din(pin[4]),  .dout(ir_raw_db));
    des_deb #(.N(IN_DEB),   .INIT(1)) u_lim (.clk(clk), .din(pin[3]),  .dout(lim_raw_db));
    des_deb #(.N(ENC_FILT), .INIT(1)) u_fa  (.clk(clk), .din(pin[2]),  .dout(fa));
    des_deb #(.N(ENC_FILT), .INIT(1)) u_fb  (.clk(clk), .din(pin[1]),  .dout(fb));
    des_deb #(.N(ENC_FILT), .INIT(1)) u_fz  (.clk(clk), .din(pin[0]),  .dout(fz));

    // ---------------- registers / state ----------------
    reg        cfg_ir_pol = 1'b0, cfg_lim_pol = 1'b0, est_latched = 1'b0, est_prev = 1'b0;
    reg        ir_blk_prev = 1'b0, pa = 1'b1, pb = 1'b1, pz = 1'b1;
    reg [31:0] est_ts = 0, est_n = 0, ir_n = 0, ir_ts = 0, ir_pos = 0, ir_enc = 0;
    reg [31:0] enc = 0, enc_err = 0, enc_zpos = 0, enc_zn = 0, enc_speed = 0, enc_ref = 0;
    reg [SPD_W-1:0] spd_div = 0;

    wire ir_blocked = cfg_ir_pol ? ir_raw_db : ~ir_raw_db;
    wire lim_active = cfg_lim_pol ? lim_raw_db : ~lim_raw_db;
    wire wr_ctrl = wr && waddr[7:2] == 6'h01;
    wire a_chg = fa != pa, b_chg = fb != pb;
    assign estop_out = est_latched;
    assign enc_o = enc;

    always @(posedge clk) begin
        if (wr && waddr[7:2] == 6'h0E) begin cfg_ir_pol <= wdata[0]; cfg_lim_pol <= wdata[1]; end

        // e-stop: latch on the debounced press; clear only once released
        est_prev <= est_pressed;
        if (est_pressed && !est_prev) begin est_latched <= 1'b1; est_ts <= ts; est_n <= est_n + 1'b1; end
        else if (wr_ctrl && wdata[0] && !est_pressed) est_latched <= 1'b0;

        // through-beam: capture on the blocked edge
        ir_blk_prev <= ir_blocked;
        if (wr_ctrl && wdata[2]) ir_n <= 0;
        else if (ir_blocked && !ir_blk_prev) begin ir_n <= ir_n + 1'b1; ir_ts <= ts; ir_pos <= pos; ir_enc <= enc; end

        // quadrature x4, index capture, 10 ms speed window
        pa <= fa; pb <= fb; pz <= fz;
        if (wr_ctrl && wdata[1]) begin enc <= 0; enc_ref <= 0; end
        else if (a_chg && b_chg) enc_err <= enc_err + 1'b1;
        else if (a_chg || b_chg) enc <= (pa == fb) ? enc + 1'b1 : enc - 1'b1;
        if (!fz && pz) begin enc_zpos <= enc; enc_zn <= enc_zn + 1'b1; end
        if (spd_div >= SPEED_N - 1) begin spd_div <= 0; enc_speed <= enc - enc_ref; enc_ref <= enc; end
        else spd_div <= spd_div + 1'b1;
    end

    // read: one case table indexed by raddr[7:2], 0x04 (write-only CTRL) and gaps read 0xDEADBEEF
    always @* begin
        case (raddr[7:2])
            6'h00: rdata = {25'd0, fz, fb, fa, lim_active, ir_blocked, est_pressed, est_latched};
            6'h02: rdata = est_ts;    6'h03: rdata = est_n;    6'h04: rdata = ir_n;
            6'h05: rdata = ir_ts;     6'h06: rdata = ir_pos;   6'h07: rdata = ir_enc;
            6'h08: rdata = enc;       6'h09: rdata = enc_err;  6'h0A: rdata = enc_zpos;
            6'h0B: rdata = enc_speed; 6'h0C: rdata = enc_zn;   6'h0D: rdata = 32'h494F3031;
            6'h0E: rdata = {30'd0, cfg_lim_pol, cfg_ir_pol};
            default: rdata = 32'hDEADBEEF;
        endcase
    end
endmodule
