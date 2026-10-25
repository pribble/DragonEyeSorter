// servo_bus: Desheng v4.01 master, 115200 8N1 single-wire half duplex, open-drain TX (BSS138 pull-up); 寄存器 0x100
// 的偏移 / 位域以 docs/PL设计.md 为准。PS fills TX buf with [ID,LEN,INSTR,params...]; HW prepends FF FF and appends
// checksum ~sum(ID,LEN,STATUS,params...). 1-byte (~87us) turnaround guard after each transfer; RX timeout from TX end,
// default 3000us. NOTE PDF 2.3.6 SYNC WRITE prints 0x8B but formula gives ~(0x545)&0xFF = 0xBA (5 other examples
// match); HW uses 0xBA.
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
    localparam integer DIV = (CLK_HZ + BAUD / 2) / BAUD, CLK_PER_US = CLK_HZ / 1_000_000, GUARD = DIV * 10;
    localparam integer DIVW = $clog2(DIV + 1), GUARDW = $clog2(GUARD + 1), USW = $clog2(CLK_PER_US);
    localparam [DIVW-1:0] BAUD_FULL = DIV - 1, BAUD_HALF = DIV / 2;
    localparam [1:0] S_IDLE = 2'd0, S_TX = 2'd1, S_RX = 2'd2;
    localparam [2:0] R_NONE = 3'd0, R_DONE = 3'd1, R_TIMEOUT = 3'd2, R_ERR = 3'd3, R_OK = 3'd4;

    reg  [7:0] tx_buf [0:63];             // 64-byte TX buffer, byte i = tx_buf[i]
    reg  [7:0] rx_buf [0:31];             // 32-byte RX buffer, byte i = rx_buf[i]
    reg  [6:0] tx_count = 0, idx = 0;     // idx = TX byte index / RX byte count (half duplex, never both)
    reg        expect_reply = 0, go = 0;
    reg  [2:0] res = R_NONE;              // last transfer result, decoded into the STATUS flag bits
    reg  [7:0] rx_status = 0; reg [5:0] rx_len = 0;   // last reply STATUS byte / LEN (CHK position)
    reg  [31:0] timeout_us = 3000, tx_t0 = 0, tx_t1 = 0, rx_t = 0;
    reg  [31:0] cnt_timeout = 0, cnt_rx_err = 0, cnt_frames = 0;
    reg  [2:0] rx_sync = 3'b111;
    always @(posedge clk) rx_sync <= {rx_sync[1:0], bus_i};
    wire rx_line = rx_sync[2];
    reg tx_line = 1'b1;
    assign bus_drive_low = ~tx_line;
    reg  [1:0] state = S_IDLE; reg [GUARDW-1:0] guard = 0;
    // Shared serial engine: TX and RX never run at the same time, so one baud counter, bit counter,
    // shift register and checksum accumulator serve both directions.
    reg  [7:0] sum = 0;
    reg  [9:0] sr = 10'h3FF;
    reg  [3:0] bits = 0;
    reg  [DIVW-1:0] baud = 0;
    reg  rx_busy = 0;
    reg  [1:0] rx_hdr = 0;
    reg  [31:0] rx_timer = 0;
    reg  [USW-1:0] us_div = 0;
    // TX bytes: 0,1 = FF FF; 2..tx_count+1 = body; tx_count+2 = checksum; one source for load + accumulate.
    wire [7:0] tx_byte = (idx < 2) ? 8'hFF : (idx < tx_count + 2) ? tx_buf[idx - 2] : ~sum;
    always @(posedge clk) begin
        if (wr) case (waddr)
            8'h00: begin
                go <= wdata[0]; expect_reply <= wdata[1];
                tx_count <= (wdata[14:8] > 7'd64) ? 7'd64 : wdata[14:8];
                if (wdata[2]) begin res <= R_NONE;
                    cnt_timeout <= 0; cnt_rx_err <= 0; cnt_frames <= 0; end
            end
            8'h08: timeout_us <= wdata;
            default: if (waddr[7:6] == 2'b01) begin
                tx_buf[{waddr[5:2], 2'b00}] <= wdata[7:0];
                tx_buf[{waddr[5:2], 2'b01}] <= wdata[15:8];
                tx_buf[{waddr[5:2], 2'b10}] <= wdata[23:16];
                tx_buf[{waddr[5:2], 2'b11}] <= wdata[31:24];
            end
        endcase
        case (state)
            S_IDLE: begin
                if (guard != 0) guard <= guard - 1'b1;
                else if (go) begin
                    go <= 0; res <= R_NONE; idx <= 0; rx_status <= 0; sum <= 0; bits <= 0; baud <= 0;
                    state <= S_TX;
                end
            end
            S_TX: begin
                if (baud != 0) baud <= baud - 1'b1;
                else if (bits != 0) begin
                    bits <= bits - 1'b1;
                    if (bits != 1) begin
                        sr <= {1'b1, sr[9:1]}; tx_line <= sr[1]; baud <= BAUD_FULL;
                    end else if (idx != tx_count + 2) begin
                        idx <= idx + 1'b1;
                    end else begin
                        tx_t1 <= ts; cnt_frames <= cnt_frames + 1'b1; tx_line <= 1'b1; idx <= 0;
                        if (expect_reply) begin
                            rx_hdr <= 0; rx_busy <= 0; rx_timer <= 0; us_div <= 0; state <= S_RX;
                        end else begin res <= R_DONE; state <= S_IDLE; guard <= GUARD; end
                    end
                end else begin
                    if (idx == 0) tx_t0 <= ts;
                    sr <= {1'b1, tx_byte, 1'b0};
                    if (idx >= 2 && idx < tx_count + 2) sum <= sum + tx_byte;
                    tx_line <= 1'b0; bits <= 10; baud <= BAUD_FULL;
                end
            end
            S_RX: begin
                if (us_div == CLK_PER_US - 1) begin us_div <= 0; rx_timer <= rx_timer + 1'b1; end
                else us_div <= us_div + 1'b1;
                if (rx_timer >= timeout_us) begin
                    res <= R_TIMEOUT; cnt_timeout <= cnt_timeout + 1'b1; state <= S_IDLE; guard <= GUARD;
                end else if (!rx_busy) begin
                    if (!rx_line) begin rx_busy <= 1'b1; bits <= 0; baud <= BAUD_HALF; end
                end else if (baud != 0) baud <= baud - 1'b1;
                else begin
                    baud <= BAUD_FULL;
                    if (bits == 0) begin if (rx_line) rx_busy <= 1'b0; else bits <= 1'b1; end
                    else if (bits <= 8) begin sr <= {rx_line, sr[7:1]}; bits <= bits + 1'b1; end
                    else begin
                        rx_busy <= 1'b0;
                        if (!rx_line) begin
                            res <= R_ERR; cnt_rx_err <= cnt_rx_err + 1'b1; state <= S_IDLE; guard <= GUARD;
                        end else if (rx_hdr != 2) begin
                            if (sr[7:0] == 8'hF5 && rx_hdr == 1) begin rx_hdr <= 2; idx <= 0; sum <= 0; end
                            else rx_hdr <= (sr[7:0] == 8'hFF) ? 1 : 0;
                        end else begin
                            rx_buf[idx[4:0]] <= sr[7:0];   // idx <= rx_len+1 <= 31: LEN is range-checked at idx==1
                            idx <= idx + 1'b1;
                            if (idx == 2) rx_status <= sr[7:0];
                            if (idx == 1) begin                      // LEN: sum it, validate, remember LEN
                                sum <= sum + sr[7:0]; rx_len <= sr[5:0];
                                if (sr[7:0] < 2 || sr[7:0] > 30) begin
                                    res <= R_ERR; cnt_rx_err <= cnt_rx_err + 1'b1;
                                    state <= S_IDLE; guard <= GUARD;
                                end
                            end else if (idx >= 2 && idx == rx_len + 1'b1) begin   // CHK: compare ~sum
                                rx_t <= ts; state <= S_IDLE; guard <= GUARD;
                                if (sr[7:0] == ~sum) res <= R_OK;
                                else begin res <= R_ERR; cnt_rx_err <= cnt_rx_err + 1'b1; end
                            end else sum <= sum + sr[7:0];
                        end
                    end
                end
            end
        endcase
    end
    wire [5:0] tb = {raddr[5:2], 2'b00}; wire [4:0] rb = {raddr[4:2], 2'b00};
    always @(*) begin
        rdata = 32'hDEAD_BEEF;
        case (raddr)
            8'h00: rdata = {17'd0, tx_count, 6'd0, expect_reply, 1'b0};
            8'h04: rdata = {8'd0, rx_status, 1'b0, 1'b0, (state == S_TX) ? 6'd0 : idx[5:0], 3'd0,
                            res == R_OK, res == R_ERR, res == R_TIMEOUT, res != R_NONE,
                            (state != S_IDLE) | go};
            8'h08: rdata = timeout_us;   8'h0C: rdata = tx_t0;        8'h10: rdata = tx_t1;
            8'h14: rdata = rx_t;         8'h18: rdata = cnt_timeout;  8'h1C: rdata = cnt_rx_err;
            8'h20: rdata = cnt_frames;
            default:
                if (raddr[7:6] == 2'b01) rdata = {tx_buf[tb+3], tx_buf[tb+2], tx_buf[tb+1], tx_buf[tb]};
                else if (raddr[7:5] == 3'b100) rdata = {rx_buf[rb+3], rx_buf[rb+2], rx_buf[rb+1], rx_buf[rb]};
        endcase
    end
endmodule
