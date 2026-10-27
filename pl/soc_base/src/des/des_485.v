// des_485.v — RS485 Modbus-RTU transceiver on the board's SP3485 (TX L17 / RX N16 / DE N15, DE and /RE together);
// PS writes the PDU [addr, func, data...] (1..62 bytes, no CRC) to TX_BUF, then CTRL (len + go); PL adds the
// CRC16 and DE timing. 寄存器 0x600 的偏移 / 位域 / 复位值以 docs/PL设计.md 为准.
`include "des_common.vh"
module des_485 #(parameter integer CLK_HZ = 75_000_000) (
    input  wire        clk, wr, input  wire [7:0]  waddr,
    input  wire [31:0] wdata, input  wire [7:0]  raddr,
    output reg  [31:0] rdata, input  wire [31:0] ts,
    input  wire        rx_i, output reg         tx_o = 1'b1,
    output reg         de_o = 1'b0
);
    localparam integer CLK_PER_US = CLK_HZ / 1_000_000;
    localparam integer USW = (CLK_PER_US > 2) ? $clog2(CLK_PER_US) : 1;
    localparam [15:0] DIV_9600 = (CLK_HZ + 4800) / 9600;
    localparam [31:0] ID = 32'h34383501;
    localparam [2:0] S_IDLE = 3'd0, S_PRE = 3'd1, S_TX = 3'd2, S_WAIT = 3'd3, S_RXF = 3'd4;
    localparam [2:0] R_NONE = 3'd0, R_SENT = 3'd1, R_OK = 3'd2, R_TIMEOUT = 3'd3, R_CRC = 3'd4, R_FERR = 3'd5;
    function [15:0] crc_byte(input [15:0] c, input [7:0] d);
        integer i; reg [15:0] x;
        begin x = c ^ {8'd0, d};
            for (i = 0; i < 8; i = i + 1) x = {1'b0, x[15:1]} ^ (x[0] ? 16'hA001 : 16'h0000); crc_byte = x; end
    endfunction
    reg [15:0] div = DIV_9600, t35_us = 16'd4011;
    reg [1:0]  parity = 2'd0;
    reg [31:0] timeout_us = 32'd100000;
    reg [7:0]  tx_buf [0:63], rx_buf [0:63];
    reg [6:0]  tx_len = 7'd0;
    reg        go = 1'b0, want_reply = 1'b0;
    reg [USW-1:0] us_div = 0;
    wire us_tick = us_div == CLK_PER_US - 1;
    wire rx_s, rx_line;
    des_sync2 #(.W(1), .INIT(1)) u_rx  (.clk(clk), .din(rx_i), .dout(rx_s));
    des_deb   #(.N(4), .INIT(1)) u_rxf (.clk(clk), .din(rx_s), .dout(rx_line));
    reg        rx_busy = 1'b0, rx_par = 1'b0, rx_bad = 1'b0;
    reg [15:0] rx_baud = 16'd0;
    reg [3:0]  rx_bit = 4'd0;
    reg [7:0]  rx_sr = 8'd0, rx_byte = 8'd0;
    reg        rx_byte_v = 1'b0, rx_byte_bad = 1'b0;
    reg [31:0] rx_ts_start = 32'd0;
    always @(posedge clk) begin
        us_div <= us_tick ? {USW{1'b0}} : us_div + 1'b1;
        rx_byte_v <= 1'b0;
        if (de_o) rx_busy <= 1'b0;
        else if (!rx_busy) begin
            if (!rx_line) begin
                rx_busy <= 1'b1; rx_baud <= {1'b0, div[15:1]}; rx_bit <= 4'd0; rx_bad <= 1'b0; rx_ts_start <= ts;
            end
        end else if (rx_baud != 0) rx_baud <= rx_baud - 1'b1;
        else begin
            rx_baud <= div - 1'b1;
            if (rx_bit == 0) begin
                if (rx_line) rx_busy <= 1'b0;
                else begin rx_bit <= 4'd1; rx_par <= (parity == 2'd2); end
            end else if (rx_bit <= 8) begin
                rx_sr <= {rx_line, rx_sr[7:1]}; rx_par <= rx_par ^ rx_line; rx_bit <= rx_bit + 1'b1;
            end else if (rx_bit == 9 && parity != 2'd0) begin
                if (rx_line != rx_par) rx_bad <= 1'b1;
                rx_bit <= 4'd10;
            end else begin
                rx_busy <= 1'b0; rx_byte_v <= 1'b1; rx_byte <= rx_sr; rx_byte_bad <= rx_bad | ~rx_line;
            end
        end
    end
    reg [2:0]  state = S_IDLE, res = R_NONE;
    reg [10:0] tx_sr = 11'h7FF;
    reg [3:0]  tx_nb = 4'd0;
    reg [15:0] tx_baud = 16'd0;
    reg [6:0]  tx_idx = 7'd0;
    reg [15:0] tx_crc = 16'hFFFF;
    reg [31:0] tx_t0 = 0, tx_t1 = 0, rx_t0 = 0, rx_t1 = 0, wait_us = 0;
    reg [31:0] cnt_tx = 0, cnt_ok = 0, cnt_timeout = 0, cnt_crc = 0, cnt_ferr = 0, cnt_listen = 0;
    reg [15:0] quiet_us = 16'hFFFF;
    reg        frame_on = 1'b0, f_bad = 1'b0;
    reg [7:0]  f_cnt = 8'd0, rx_len = 8'd0;
    reg [15:0] f_crc = 16'hFFFF;
    reg        ovf = 1'b0, listen_rdy = 1'b0, lf_ok = 1'b0, lf_ferr = 1'b0;
    wire [6:0]  tx_total = tx_len + 7'd2;
    wire [7:0]  tx_b = (tx_idx < tx_len) ? tx_buf[tx_idx[5:0]] : (tx_idx == tx_len) ? tx_crc[7:0] : tx_crc[15:8];
    wire        tx_p = (parity == 2'd2) ? ~^tx_b : ^tx_b;
    wire [10:0] tx_frame = (parity != 2'd0) ? {1'b1, tx_p, tx_b, 1'b0} : {2'b11, tx_b, 1'b0};
    wire [3:0]  tx_bits = (parity != 2'd0) ? 4'd11 : 4'd10;
    wire        busy = go | (state != S_IDLE);
    wire        f_good = (f_cnt >= 8'd4) && (f_crc == 16'h0000);
    // f_end: !rx_byte_v is not needed - rx_byte_v is high exactly one clock after rx_busy, when quiet_us = 0,
    wire        f_end = frame_on && !rx_busy && quiet_us >= t35_us;
    wire        abort = wr && waddr == 8'h00 && wdata[4];
    always @(posedge clk) begin
        if (wr) begin
            case (waddr)
                8'h00: begin
                    if (wdata[2]) begin
                        res <= R_NONE; ovf <= 1'b0; cnt_tx <= 0; cnt_ok <= 0; cnt_timeout <= 0;
                        cnt_crc <= 0; cnt_ferr <= 0; cnt_listen <= 0;
                    end
                    if (wdata[3]) listen_rdy <= 1'b0;
                    if (!busy && wdata[0] && wdata[14:8] != 0 && wdata[14:8] <= 7'd62) begin
                        go <= 1'b1; want_reply <= wdata[1]; tx_len <= wdata[14:8]; res <= R_NONE; listen_rdy <= 1'b0;
                    end
                end
                8'h08: timeout_us <= wdata;
                8'h0C: div <= (wdata[15:0] < 16'd16) ? 16'd16 : wdata[15:0];
                8'h10: parity <= (wdata[1:0] == 2'd3) ? 2'd0 : wdata[1:0];
                8'h14: t35_us <= (wdata[15:0] == 16'd0) ? 16'd1 : wdata[15:0];
                default: if (waddr[7:6] == 2'b10 && !busy) begin
                    tx_buf[{waddr[5:2], 2'b00}] <= wdata[7:0];
                    tx_buf[{waddr[5:2], 2'b01}] <= wdata[15:8];
                    tx_buf[{waddr[5:2], 2'b10}] <= wdata[23:16];
                    tx_buf[{waddr[5:2], 2'b11}] <= wdata[31:24];
                end
            endcase
        end
        if (de_o || rx_busy) quiet_us <= 16'd0;
        else if (us_tick && quiet_us != 16'hFFFF) quiet_us <= quiet_us + 1'b1;
        if (rx_byte_v) begin
            if (!frame_on) begin
                frame_on <= 1'b1; f_cnt <= 8'd1; f_crc <= crc_byte(16'hFFFF, rx_byte); f_bad <= rx_byte_bad;
                rx_t0 <= rx_ts_start; rx_buf[0] <= rx_byte;
            end else begin
                if (f_cnt != 8'hFF) f_cnt <= f_cnt + 1'b1;
                f_crc <= crc_byte(f_crc, rx_byte); f_bad <= f_bad | rx_byte_bad;
                if (f_cnt < 8'd64) rx_buf[f_cnt[5:0]] <= rx_byte;
            end
            rx_t1 <= ts;
        end
        if (f_end) begin
            frame_on <= 1'b0; rx_len <= f_cnt; ovf <= f_cnt > 8'd64;
            if (f_bad) cnt_ferr <= cnt_ferr + 1'b1;
            else if (!f_good) cnt_crc <= cnt_crc + 1'b1;
            if (state == S_RXF) begin
                state <= S_IDLE;
                if (f_bad) res <= R_FERR;
                else if (!f_good) res <= R_CRC;
                else begin res <= R_OK; cnt_ok <= cnt_ok + 1'b1; end
            end else begin
                listen_rdy <= 1'b1; lf_ok <= f_good & ~f_bad; lf_ferr <= f_bad; cnt_listen <= cnt_listen + 1'b1;
            end
        end
        case (state)
            S_IDLE:
                if (go && !frame_on && !rx_busy && quiet_us >= t35_us) begin
                    go <= 1'b0; state <= S_PRE; de_o <= 1'b1; tx_o <= 1'b1; tx_baud <= div - 1'b1;
                    tx_idx <= 7'd0; tx_crc <= 16'hFFFF;
                end
            S_PRE:
                if (tx_baud != 0) tx_baud <= tx_baud - 1'b1;
                else begin
                    state <= S_TX; tx_t0 <= ts;
                    tx_sr <= tx_frame; tx_nb <= tx_bits; tx_baud <= 16'd0; tx_idx <= tx_idx + 1'b1;
                    tx_crc <= crc_byte(tx_crc, tx_b);    // byte 0 is always data (tx_len >= 1)
                end
            S_TX:
                if (tx_baud != 0) tx_baud <= tx_baud - 1'b1;
                else if (tx_nb != 0) begin
                    tx_o <= tx_sr[0]; tx_sr <= {1'b1, tx_sr[10:1]}; tx_nb <= tx_nb - 1'b1; tx_baud <= div - 1'b1;
                end else if (tx_idx != tx_total) begin
                    tx_sr <= tx_frame; tx_nb <= tx_bits; tx_idx <= tx_idx + 1'b1;
                    if (tx_idx < tx_len) tx_crc <= crc_byte(tx_crc, tx_b);
                end else begin
                    de_o <= 1'b0; tx_o <= 1'b1; tx_t1 <= ts; cnt_tx <= cnt_tx + 1'b1; wait_us <= 32'd0;
                    if (want_reply) state <= S_WAIT; else begin state <= S_IDLE; res <= R_SENT; end
                end
            S_WAIT:
                if (frame_on) state <= S_RXF;
                else if (!rx_busy && wait_us >= timeout_us) begin
                    state <= S_IDLE; res <= R_TIMEOUT; cnt_timeout <= cnt_timeout + 1'b1;
                end else if (us_tick) wait_us <= wait_us + 1'b1;
            default: ;
        endcase
        // pending transfer would wait forever; the PS driver aborts after its own deadline
        if (abort) begin
            go <= 1'b0; state <= S_IDLE; de_o <= 1'b0; tx_o <= 1'b1; frame_on <= 1'b0;
        end
    end
    wire [5:0] tb = {raddr[5:2], 2'b00};
    always @* begin
        case (raddr)
            8'h00: rdata = {17'd0, tx_len, 6'd0, want_reply, go};
            8'h04: rdata = {11'd0, lf_ferr, lf_ok, frame_on, rx_line, de_o, rx_len, listen_rdy, ovf,
                            res == R_FERR, res == R_OK, res == R_CRC, res == R_TIMEOUT, res != R_NONE, busy};
            8'h08: rdata = timeout_us;            8'h0C: rdata = {16'd0, div};
            8'h10: rdata = {30'd0, parity};       8'h14: rdata = {16'd0, t35_us};
            8'h18: rdata = tx_t0;                 8'h1C: rdata = tx_t1;
            8'h20: rdata = rx_t0;                 8'h24: rdata = rx_t1;
            8'h28: rdata = cnt_tx;                8'h2C: rdata = cnt_ok;
            8'h30: rdata = cnt_timeout;           8'h34: rdata = cnt_crc;
            8'h38: rdata = cnt_ferr;              8'h3C: rdata = cnt_listen;
            8'h40: rdata = ID;                    8'h44: rdata = {16'd0, quiet_us};
            default:
                if (raddr[7:6] == 2'b10)      rdata = {tx_buf[tb+3], tx_buf[tb+2], tx_buf[tb+1], tx_buf[tb]};
                else if (raddr[7:6] == 2'b11) rdata = {rx_buf[tb+3], rx_buf[tb+2], rx_buf[tb+1], rx_buf[tb]};
                else                          rdata = 32'hDEAD_BEEF;
        endcase
    end
endmodule
