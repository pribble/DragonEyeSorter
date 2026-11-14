// des_vib.v — vibration acquisition (v17 batch 2): ADXL345 accelerometer on 3-wire SPI (mode 3) + its INT1 pin as
// DATA_READY. 寄存器 0x900 的偏移 / 位域 / 复位值以 docs/PL设计.md 为准. CTRL.b0 = 0 (reset): the pins idle (CS high,
// SCLK high, SDIO released) and nothing is read = no effect on anything else.
//
// - PS configures the sensor through single-byte transfers (XFER): DATA_FORMAT with the 3-wire bit first, then rate,
//   interrupt and power registers. The PL does no sensor setup of its own.
// - Streaming (CTRL.b0): whenever INT1 is high (DATA_READY) and the engine is free, read DATAX0..DATAZ1 in one
//   multi-byte transfer. The time stamp is FREE_CNT latched on the INT1 rising edge (2-clock synchroniser delay),
//   so every sample carries the instant the sensor finished it. INT1 is used as a level (a sample already waiting
//   when streaming starts is still read); after each read the level is ignored for GUARD clocks while it falls.
// - Samples go to a 512 x 64-bit FIFO (block RAM): {ts[19:4], z, y, x}. A full FIFO drops the new sample and counts it.
// - Window statistics per axis over WIN samples (0 = 2048): sum and sum of squares >> 10 (mean and RMS in the PS).
//   One multiplier and one set of adders serve the three axes: the per-axis registers rotate, so no multiplexers are
//   needed. Widths assume ADXL345 data (at most 13 significant bits, full resolution +-16 g): |x| <= 4096, so a window
//   of 2048 needs 24 bits for the sum and 36 for the sum of squares. (Min / max were dropped to fit the device.)
module des_vib #(
    parameter integer HALF = 10,          // SCLK half period in clocks: 75 MHz / 20 = 3.75 MHz (ADXL345: <= 5 MHz)
    parameter integer GUARD = 255         // clocks after a read during which INT1 is ignored
) (
    input  wire        clk, wr, input  wire [7:0]  waddr,
    input  wire [31:0] wdata, input  wire [7:0]  raddr,
    output reg  [31:0] rdata, input  wire [31:0] ts,
    output reg         cs_n, output reg         sclk,
    output reg         sdo, output reg         sdo_oe,
    input  wire        sdi, input  wire        int1
);
    localparam [2:0] S_IDLE = 3'd0, S_CSL = 3'd1, S_BIT = 3'd2, S_END = 3'd3, S_GAP = 3'd4;
    localparam [4:0] HALF_M1 = HALF - 1;
    localparam [7:0] GUARD_C = GUARD;
    initial begin cs_n = 1'b1; sclk = 1'b1; sdo = 1'b0; sdo_oe = 1'b0; end
    // ---------------- inputs ----------------
    reg [1:0] sdi_s = 2'b00, int_s = 2'b00;
    reg       int_d = 1'b0;
    always @(posedge clk) begin sdi_s <= {sdi_s[0], sdi}; int_s <= {int_s[0], int1}; int_d <= int_s[1]; end
    wire int_hi = int_s[1];
    // ---------------- control ----------------
    reg        en = 1'b0, raw_pend = 1'b0;
    reg [15:0] raw_word = 0;
    reg [7:0]  rd_byte = 0;
    reg [10:0] win = 11'd1024;
    reg [31:0] t_edge = 0, last_ts = 0;
    reg [15:0] smp_cnt = 0, ovf_cnt = 0, win_cnt = 0;
    wire clr = wr && waddr[7:2] == 6'h00 && wdata[1];
    // ---------------- SPI engine ----------------
    reg [2:0]  state = S_IDLE;
    reg [4:0]  hcnt = 0;
    reg        phase = 1'b0, kind = 1'b0, gap2 = 1'b0;   // kind 1 = stream read
    reg [5:0]  bitn = 0, nbits = 0;
    reg [4:0]  nout = 0;
    reg [15:0] sh_out = 0;
    reg [47:0] sh_in = 0;
    reg [7:0]  guard = 0;
    reg        smp_done = 1'b0;
    wire       tick = hcnt == 5'd0;
    wire       raw_busy = raw_pend || (state != S_IDLE && !kind);
    // ---------------- FIFO ----------------
    reg [63:0] mem [0:511];
    reg [63:0] head = 0;
    reg [9:0]  wp = 0, rp = 0;
    wire [9:0] level = wp - rp;
    wire       full = level[9];
    // ---------------- statistics ----------------
    reg signed [15:0] v0 = 0, v1 = 0, v2 = 0;
    reg signed [23:0] s0 = 0, s1 = 0, s2 = 0;
    reg [35:0] q0 = 0, q1 = 0, q2 = 0;
    reg signed [31:0] p = 0;
    reg [10:0] cnt = 0;
    reg [1:0]  k = 0;
    reg        st_on = 1'b0, st_ph = 1'b0, latch = 1'b0;
    reg signed [23:0] rs0 = 0, rs1 = 0, rs2 = 0;
    reg [25:0] rq0 = 0, rq1 = 0, rq2 = 0;
    wire       first = cnt == 11'd0;
    wire [10:0] last = win - 1'b1;                       // win 0 -> 2047: windows of 2048
    always @(posedge clk) begin
        smp_done <= 1'b0;
        if (wr) case (waddr[7:2])
            6'h00: en <= wdata[0];
            6'h02: if (!raw_pend) begin raw_pend <= 1'b1; raw_word <= wdata[15:0]; end
            6'h04: win <= wdata[10:0];
            6'h08: if (level != 10'd0) rp <= rp + 1'b1;   // pop the head
        endcase
        if (int_hi && !int_d) t_edge <= ts;
        if (guard != 8'd0) guard <= guard - 1'b1;
        hcnt <= (state == S_IDLE || tick) ? HALF_M1 : hcnt - 1'b1;
        case (state)
            S_IDLE: begin
                cs_n <= 1'b1; sclk <= 1'b1; sdo_oe <= 1'b0;
                if (raw_pend) begin             // [15] read, [13:8] address, [7:0] data
                    kind <= 1'b0; sh_out <= {raw_word[15], 1'b0, raw_word[13:8], raw_word[7:0]};
                    nbits <= 6'd16; nout <= raw_word[15] ? 5'd8 : 5'd16;
                    bitn <= 6'd0; cs_n <= 1'b0; state <= S_CSL;
                end else if (en && int_hi && guard == 8'd0) begin
                    kind <= 1'b1; sh_out <= {8'hF2, 8'h00};     // read, multi-byte, from 0x32 (DATAX0)
                    nbits <= 6'd56; nout <= 5'd8;
                    bitn <= 6'd0; cs_n <= 1'b0; state <= S_CSL;
                end
            end
            S_CSL: if (tick) begin              // CS set-up done: first bit, SCLK low, drive bit 0
                sclk <= 1'b0; phase <= 1'b0; sdo <= sh_out[15]; sdo_oe <= 1'b1; state <= S_BIT;
            end
            S_BIT: if (tick) begin
                if (!phase) begin               // rising edge: the sensor samples SDIO, we sample its output
                    sclk <= 1'b1; phase <= 1'b1;
                    if (bitn >= {1'b0, nout}) sh_in <= {sh_in[46:0], sdi_s[1]};
                    sh_out <= {sh_out[14:0], 1'b0};
                end else if (bitn + 1'b1 == nbits) begin
                    state <= S_END; sdo_oe <= 1'b0;
                end else begin                  // falling edge: next bit (released once the command is out)
                    bitn <= bitn + 1'b1; sclk <= 1'b0; phase <= 1'b0;
                    sdo <= sh_out[15]; sdo_oe <= (bitn + 1'b1 < {1'b0, nout});
                end
            end
            S_END: if (tick) begin cs_n <= 1'b1; gap2 <= 1'b0; state <= S_GAP; end
            S_GAP: if (tick) begin              // CS high for two half periods (ADXL345: >= 150 ns)
                gap2 <= 1'b1;
                if (gap2) begin
                    state <= S_IDLE;
                    if (kind) begin smp_done <= 1'b1; guard <= GUARD_C; end
                    else begin rd_byte <= sh_in[7:0]; raw_pend <= 1'b0; end
                end
            end
            default: state <= S_IDLE;
        endcase
        // ---------------- store ----------------
        if (smp_done) begin                     // sh_in = X0 X1 Y0 Y1 Z0 Z1
            smp_cnt <= smp_cnt + 1'b1; last_ts <= t_edge;
            if (!full) wp <= wp + 1'b1;
            else if (ovf_cnt != 16'hFFFF) ovf_cnt <= ovf_cnt + 1'b1;
            v0 <= {sh_in[39:32], sh_in[47:40]}; v1 <= {sh_in[23:16], sh_in[31:24]}; v2 <= {sh_in[7:0], sh_in[15:8]};
            k <= 2'd0; st_on <= 1'b1; st_ph <= 1'b0;
        end
        // ---------------- statistics: 2 clocks per axis, registers rotate ----------------
        latch <= 1'b0;
        if (st_on) begin
            if (!st_ph) begin p <= v0 * v0; st_ph <= 1'b1; end
            else begin
                st_ph <= 1'b0;
                v0 <= v1; v1 <= v2; v2 <= v0;
                s0 <= s1; s1 <= s2; s2 <= first ? v0 : s0 + v0;   // both arms signed: v0 sign-extends
                q0 <= q1; q1 <= q2; q2 <= first ? {4'd0, p} : q0 + {4'd0, p};
                k <= k + 1'b1;
                if (k == 2'd2) begin
                    st_on <= 1'b0;
                    if (cnt == last) begin cnt <= 11'd0; latch <= 1'b1; end
                    else cnt <= cnt + 1'b1;
                end
            end
        end
        if (latch) begin                        // after the third rotation: axis order x, y, z again
            rs0 <= s0; rs1 <= s1; rs2 <= s2;
            rq0 <= q0[35:10]; rq1 <= q1[35:10]; rq2 <= q2[35:10];
            win_cnt <= win_cnt + 1'b1;
        end
        if (clr) begin
            wp <= 10'd0; rp <= 10'd0; smp_cnt <= 16'd0; ovf_cnt <= 16'd0; win_cnt <= 16'd0; cnt <= 11'd0;
        end
    end
    always @(posedge clk) begin                 // block RAM: write port + registered read of the head
        if (smp_done && !full) mem[wp[8:0]] <= {t_edge[19:4], sh_in[7:0], sh_in[15:8], sh_in[23:16], sh_in[31:24],
                                                 sh_in[39:32], sh_in[47:40]};
        head <= mem[rp[8:0]];
    end
    always @* begin
        case (raddr[7:2])
            6'h00: rdata = {31'd0, en};
            6'h01: rdata = {6'd0, level, 12'd0, ovf_cnt != 16'd0, int_hi, raw_busy, en};
            6'h02: rdata = {23'd0, raw_busy, rd_byte};
            6'h03: rdata = 32'h56494231;      // "VIB1"
            6'h04: rdata = {21'd0, win};
            6'h05: rdata = {ovf_cnt, smp_cnt};
            6'h06: rdata = last_ts;
            6'h07: rdata = {16'd0, win_cnt};
            6'h08: rdata = head[31:0];        // {y, x}
            6'h09: rdata = head[63:32];       // {ts[19:4], z}
            6'h0A: rdata = {{8{rs0[23]}}, rs0}; 6'h0B: rdata = {{8{rs1[23]}}, rs1}; 6'h0C: rdata = {{8{rs2[23]}}, rs2};
            6'h0D: rdata = {6'd0, rq0};       6'h0E: rdata = {6'd0, rq1};       6'h0F: rdata = {6'd0, rq2};
            default: rdata = 32'hDEADBEEF;
        endcase
    end
endmodule
