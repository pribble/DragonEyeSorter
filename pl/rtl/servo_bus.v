// Desheng serial-bus servo master (protocol v4.01): 115200 8N1, single-wire half duplex, open-drain TX.
// PS fills TX buffer with frame body [ID, LEN, INSTR, params...]; hardware adds FF FF header and checksum,
// then optionally waits for an FF F5 reply and stores [ID, LEN, STATUS, params..., CHK] in the RX buffer.
//
// Register block (byte offsets inside the 0x100 window):
//   0x00 CTRL       W: b0 GO (self-clearing), b1 EXPECT_REPLY, b2 CLEAR (status + counters), b[14:8] TX_COUNT
//   0x04 STATUS     R: b0 BUSY, b1 DONE, b2 TIMEOUT, b3 RX_ERR (checksum/length/framing), b4 RX_OK,
//                      b[14:8] RX_COUNT, b[23:16] servo status byte
//   0x08 TIMEOUT_US RW: reply timeout measured from end of TX (default 3000 us)
//   0x0C TX_T0  R: timestamp of first start bit      0x10 TX_T1 R: timestamp after last stop bit
//   0x14 RX_T   R: timestamp of reply completion     0x18 CNT_TIMEOUT  0x1C CNT_RX_ERR  0x20 CNT_FRAMES
//   0x40-0x7F TX buffer (64 bytes)   0x80-0x9F RX buffer (32 bytes); byte i is bits [8*(i%4)+:8] of word i/4
module servo_bus #(
    parameter integer CLK_HZ = 75_000_000,
    parameter integer BAUD   = 115_200
) (
    input  wire        clk,
    input  wire        wr,
    input  wire [7:0]  waddr,
    input  wire [31:0] wdata,
    input  wire [7:0]  raddr,
    output reg  [31:0] rdata,
    input  wire [31:0] ts,
    input  wire        bus_i,
    output wire        bus_drive_low
);
    localparam integer DIV      = (CLK_HZ + BAUD / 2) / BAUD;
    localparam integer CLK_PER_US = CLK_HZ / 1_000_000;

    // ---------------- registers ----------------
    reg  [7:0]  tx_buf [0:63];
    reg  [7:0]  rx_buf [0:31];
    reg  [6:0]  tx_count = 7'd0;
    reg         expect_reply = 1'b0;
    reg  [31:0] timeout_us = 32'd3000;
    reg  [31:0] tx_t0 = 0, tx_t1 = 0, rx_t = 0;
    reg  [31:0] cnt_timeout = 0, cnt_rx_err = 0, cnt_frames = 0;
    reg         st_done = 0, st_timeout = 0, st_rx_err = 0, st_rx_ok = 0;
    reg  [6:0]  rx_count = 0;
    reg  [7:0]  rx_status = 0;
    reg         go = 1'b0;

    // ---------------- bus line ----------------
    reg [2:0] rx_sync = 3'b111;
    always @(posedge clk) rx_sync <= {rx_sync[1:0], bus_i};
    wire rx_line = rx_sync[2];

    reg tx_line = 1'b1;
    assign bus_drive_low = ~tx_line;

    // ---------------- main FSM ----------------
    localparam S_IDLE = 3'd0, S_TX = 3'd1, S_RX = 3'd2;
    reg [2:0]  state = S_IDLE;

    // bus turnaround: after any transfer keep the line idle for one byte time before the next frame
    localparam integer GUARD = DIV * 10;
    reg [15:0] guard = 0;

    // TX byte stream: 0,1 = FF FF, 2..tx_count+1 = body, tx_count+2 = checksum
    reg [6:0]  tx_idx = 0;
    reg [7:0]  tx_sum = 0;
    reg [9:0]  tx_shift = 10'h3FF;
    reg [3:0]  tx_bits = 0;
    reg [15:0] tx_div = 0;

    // RX
    reg [15:0] rx_div = 0;
    reg [3:0]  rx_bits = 0;
    reg [7:0]  rx_shift = 0;
    reg        rx_busy = 0;
    reg [1:0]  rx_hdr = 0;      // 0: want FF, 1: want F5, 2: payload
    reg [7:0]  rx_sum = 0;
    reg [7:0]  rx_need = 0;     // payload bytes still expected (incl. checksum)
    reg [31:0] rx_timer = 0;
    reg [7:0]  us_div = 0;

    wire [7:0] tx_byte_now = (tx_idx < 2) ? 8'hFF :
                             (tx_idx < tx_count + 2) ? tx_buf[tx_idx - 2] : ~tx_sum;

    always @(posedge clk) begin
        if (wr) begin
            case (waddr)
                8'h00: begin
                    go           <= wdata[0];
                    expect_reply <= wdata[1];
                    tx_count     <= (wdata[14:8] > 7'd64) ? 7'd64 : wdata[14:8];
                    if (wdata[2]) begin
                        st_done <= 0; st_timeout <= 0; st_rx_err <= 0; st_rx_ok <= 0;
                        cnt_timeout <= 0; cnt_rx_err <= 0; cnt_frames <= 0;
                    end
                end
                8'h08: timeout_us <= wdata;
                default: if (waddr >= 8'h40 && waddr < 8'h80) begin
                    tx_buf[{waddr[5:2], 2'b00}] <= wdata[7:0];
                    tx_buf[{waddr[5:2], 2'b01}] <= wdata[15:8];
                    tx_buf[{waddr[5:2], 2'b10}] <= wdata[23:16];
                    tx_buf[{waddr[5:2], 2'b11}] <= wdata[31:24];
                end
            endcase
        end

        case (state)
            S_IDLE: begin
                tx_line <= 1'b1;
                if (guard != 0) guard <= guard - 1'b1;
                else if (go) begin
                    go <= 1'b0;
                    st_done <= 0; st_timeout <= 0; st_rx_err <= 0; st_rx_ok <= 0;
                    rx_count <= 0; rx_status <= 0;
                    tx_idx <= 0; tx_sum <= 0; tx_bits <= 0; tx_div <= 0;
                    state <= S_TX;
                end
            end

            S_TX: begin
                if (tx_bits == 0 && tx_div == 0) begin
                    // load next byte: start bit, 8 data bits LSB first, stop bit
                    if (tx_idx == 0) tx_t0 <= ts;
                    tx_shift <= {1'b1, tx_byte_now, 1'b0};
                    if (tx_idx >= 2 && tx_idx < tx_count + 2) tx_sum <= tx_sum + tx_buf[tx_idx - 2];
                    tx_line <= 1'b0;
                    tx_bits <= 4'd10;
                    tx_div  <= DIV - 1;
                end else if (tx_div != 0) begin
                    tx_div <= tx_div - 1'b1;
                end else begin
                    tx_bits <= tx_bits - 1'b1;
                    if (tx_bits == 1) begin
                        // stop bit finished
                        if (tx_idx == tx_count + 2) begin
                            tx_t1 <= ts;
                            cnt_frames <= cnt_frames + 1'b1;
                            tx_line <= 1'b1;
                            tx_bits <= 0;
                            if (expect_reply) begin
                                rx_hdr <= 0; rx_busy <= 0; rx_timer <= 0; us_div <= 0;
                                state <= S_RX;
                            end else begin
                                st_done <= 1'b1;
                                state <= S_IDLE; guard <= GUARD;
                            end
                        end else begin
                            tx_idx <= tx_idx + 1'b1;
                            tx_bits <= 0;     // next cycle loads the next byte
                        end
                    end else begin
                        tx_shift <= {1'b1, tx_shift[9:1]};
                        tx_line  <= tx_shift[1];
                        tx_div   <= DIV - 1;
                    end
                end
            end

            S_RX: begin
                // timeout counter in microseconds
                if (us_div == CLK_PER_US - 1) begin
                    us_div <= 0;
                    rx_timer <= rx_timer + 1'b1;
                end else us_div <= us_div + 1'b1;

                if (rx_timer >= timeout_us) begin
                    st_timeout <= 1'b1; st_done <= 1'b1;
                    cnt_timeout <= cnt_timeout + 1'b1;
                    state <= S_IDLE; guard <= GUARD;
                end else if (!rx_busy) begin
                    if (!rx_line) begin
                        rx_busy <= 1'b1; rx_bits <= 0; rx_div <= DIV / 2;
                    end
                end else if (rx_div != 0) begin
                    rx_div <= rx_div - 1'b1;
                end else begin
                    rx_div <= DIV - 1;
                    if (rx_bits == 0) begin
                        if (rx_line) rx_busy <= 1'b0;          // glitch, not a start bit
                        else rx_bits <= 1;
                    end else if (rx_bits <= 8) begin
                        rx_shift <= {rx_line, rx_shift[7:1]};
                        rx_bits <= rx_bits + 1'b1;
                    end else begin
                        // stop bit: one full byte in rx_shift
                        rx_busy <= 1'b0;
                        if (!rx_line) begin
                            st_rx_err <= 1'b1; st_done <= 1'b1; cnt_rx_err <= cnt_rx_err + 1'b1;
                            state <= S_IDLE; guard <= GUARD;
                        end else case (rx_hdr)
                            2'd0: if (rx_shift == 8'hFF) rx_hdr <= 2'd1;
                            2'd1: if (rx_shift == 8'hF5) begin rx_hdr <= 2'd2; rx_count <= 0; rx_sum <= 0; end
                                  else if (rx_shift != 8'hFF) rx_hdr <= 2'd0;
                            default: begin
                                if (rx_count < 32) rx_buf[rx_count[4:0]] <= rx_shift;
                                rx_count <= rx_count + 1'b1;
                                if (rx_count == 0) begin
                                    rx_sum <= rx_shift;                         // ID
                                end else if (rx_count == 1) begin
                                    rx_sum <= rx_sum + rx_shift;                // LEN
                                    rx_need <= rx_shift;
                                    if (rx_shift < 2 || rx_shift > 8'd30) begin
                                        st_rx_err <= 1'b1; st_done <= 1'b1; cnt_rx_err <= cnt_rx_err + 1'b1;
                                        state <= S_IDLE; guard <= GUARD;
                                    end
                                end else if (rx_need == 1) begin                // checksum byte
                                    rx_t <= ts;
                                    st_done <= 1'b1;
                                    if (rx_shift == ~rx_sum) st_rx_ok <= 1'b1;
                                    else begin st_rx_err <= 1'b1; cnt_rx_err <= cnt_rx_err + 1'b1; end
                                    state <= S_IDLE; guard <= GUARD;
                                end else begin
                                    if (rx_count == 2) rx_status <= rx_shift;   // STATUS
                                    rx_sum <= rx_sum + rx_shift;
                                    rx_need <= rx_need - 1'b1;
                                end
                            end
                        endcase
                    end
                end
            end

            default: state <= S_IDLE;
        endcase
    end

    // ---------------- read mux ----------------
    wire [5:0] tb = {raddr[5:2], 2'b00};
    wire [4:0] rb = {raddr[4:2], 2'b00};
    always @(*) begin
        case (raddr)
            8'h00: rdata = {17'd0, tx_count, 6'd0, expect_reply, 1'b0};
            8'h04: rdata = {8'd0, rx_status, 1'b0, rx_count, 3'd0, st_rx_ok, st_rx_err, st_timeout, st_done, (state != S_IDLE) | go};
            8'h08: rdata = timeout_us;
            8'h0C: rdata = tx_t0;
            8'h10: rdata = tx_t1;
            8'h14: rdata = rx_t;
            8'h18: rdata = cnt_timeout;
            8'h1C: rdata = cnt_rx_err;
            8'h20: rdata = cnt_frames;
            default:
                if (raddr >= 8'h40 && raddr < 8'h80)
                    rdata = {tx_buf[tb + 3], tx_buf[tb + 2], tx_buf[tb + 1], tx_buf[tb]};
                else if (raddr >= 8'h80 && raddr < 8'hA0)
                    rdata = {rx_buf[rb + 3], rx_buf[rb + 2], rx_buf[rb + 1], rx_buf[rb]};
                else
                    rdata = 32'hDEAD_BEEF;
        endcase
    end
endmodule
