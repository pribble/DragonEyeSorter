// servo_bus: Desheng v4.01 master, 115200 8N1 single-wire half duplex, open-drain TX (BSS138 pull-up).
// PS fills TX buf with [ID,LEN,INSTR,params...]; HW prepends FF FF, appends checksum ~sum(ID,LEN,STATUS,params...).
// NOTE PDF 2.3.6 SYNC WRITE prints 0x8B but formula gives ~(0x545)&0xFF = 0xBA (5 other examples match); HW uses 0xBA.
// 1-byte (~87us) turnaround guard after each transfer; RX timeout from TX end, default 3000us.
// Regs (0x100): 0x00 CTRL b0 GO(sc) b1 EXPECT_REPLY b2 CLEAR b[14:8] TX_COUNT(clamp64);
// 0x04 STATUS b0 BUSY b1 DONE b2 TIMEOUT b3 RX_ERR b4 RX_OK b[14:8] RX_COUNT b[23:16] status;
// 0x08 TIMEOUT_US; 0x0C/0x10/0x14 TX_T0/TX_T1/RX_T; 0x18/0x1C/0x20 CNT_TIMEOUT/RX_ERR/FRAMES;
// 0x40-0x7F TX 64B; 0x80-0x9F RX 32B ([ID,LEN,STATUS,params,CHK]); byte i = word[i/4][8*(i%4)+:8].
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
    localparam integer DIV_HALF = DIV / 2;
    function [DIVW-1:0] baud_reload(input half); baud_reload = half ? DIV_HALF : DIV - 1; endfunction

    reg  [7:0] tx_buf [0:63];
    reg  [7:0] rx_buf [0:31];
    reg  [6:0] tx_count = 0;
    reg  [5:0] rx_count = 0, rx_need = 0;
    reg        expect_reply = 0, go = 0;
    reg        st_done = 0, st_timeout = 0, st_rx_err = 0, st_rx_ok = 0;
    reg  [7:0] rx_status = 0;
    reg  [31:0] timeout_us = 3000, tx_t0 = 0, tx_t1 = 0, rx_t = 0;
    reg  [31:0] cnt_timeout = 0, cnt_rx_err = 0, cnt_frames = 0;

    reg [2:0] rx_sync = 3'b111;
    always @(posedge clk) rx_sync <= {rx_sync[1:0], bus_i};
    wire rx_line = rx_sync[2];

    reg tx_line = 1'b1;
    assign bus_drive_low = ~tx_line;

    localparam S_IDLE = 2'd0, S_TX = 2'd1, S_RX = 2'd2;
    reg [1:0] state = S_IDLE;
    reg [GUARDW-1:0] guard = 0;

    reg [6:0] tx_idx = 0;
    reg [7:0] tx_sum = 0, rx_shift = 0, rx_sum = 0;
    reg [9:0] tx_shift = 10'h3FF;
    reg [3:0] tx_bits = 0, rx_bits = 0;
    reg [DIVW-1:0] tx_div = 0, rx_div = 0;
    reg rx_busy = 0;
    reg [1:0] rx_hdr = 0;
    reg [31:0] rx_timer = 0;
    reg [USW-1:0] us_div = 0;

    // TX bytes: 0,1 = FF FF; 2..tx_count+1 = body; tx_count+2 = checksum; one source for load + accumulate.
    wire [7:0] tx_byte = (tx_idx < 2) ? 8'hFF :
                         (tx_idx < tx_count + 2) ? tx_buf[tx_idx - 2] : ~tx_sum;

    always @(posedge clk) begin
        if (wr) begin
            case (waddr)
                8'h00: begin
                    go <= wdata[0]; expect_reply <= wdata[1];
                    tx_count <= (wdata[14:8] > 7'd64) ? 7'd64 : wdata[14:8];
                    if (wdata[2]) begin st_done <= 0; st_timeout <= 0; st_rx_err <= 0; st_rx_ok <= 0;
                        cnt_timeout <= 0; cnt_rx_err <= 0; cnt_frames <= 0; end
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
                    go <= 0; st_done <= 0; st_timeout <= 0; st_rx_err <= 0; st_rx_ok <= 0;
                    rx_count <= 0; rx_status <= 0; tx_idx <= 0; tx_sum <= 0; tx_bits <= 0; tx_div <= 0;
                    state <= S_TX;
                end
            end

            S_TX: begin
                if (tx_bits == 0 && tx_div == 0) begin
                    if (tx_idx == 0) tx_t0 <= ts;
                    tx_shift <= {1'b1, tx_byte, 1'b0};
                    if (tx_idx >= 2 && tx_idx < tx_count + 2) tx_sum <= tx_sum + tx_byte;
                    tx_line <= 1'b0; tx_bits <= 4'd10; tx_div <= baud_reload(0);
                end else if (tx_div != 0) tx_div <= tx_div - 1'b1;
                else begin
                    tx_bits <= tx_bits - 1'b1;
                    if (tx_bits == 1) begin
                        if (tx_idx == tx_count + 2) begin
                            tx_t1 <= ts; cnt_frames <= cnt_frames + 1'b1; tx_line <= 1'b1; tx_bits <= 0;
                            if (expect_reply) begin
                                rx_hdr <= 0; rx_busy <= 0; rx_timer <= 0; us_div <= 0;
                                state <= S_RX;
                            end else begin st_done <= 1'b1; state <= S_IDLE; guard <= GUARD; end
                        end else begin tx_idx <= tx_idx + 1'b1; tx_bits <= 0; end
                    end else begin
                        tx_shift <= {1'b1, tx_shift[9:1]}; tx_line <= tx_shift[1]; tx_div <= baud_reload(0);
                    end
                end
            end

            S_RX: begin
                if (us_div == CLK_PER_US - 1) begin us_div <= 0; rx_timer <= rx_timer + 1'b1; end
                else us_div <= us_div + 1'b1;
                if (rx_timer >= timeout_us) begin
                    st_timeout <= 1; st_done <= 1; cnt_timeout <= cnt_timeout + 1'b1;
                    state <= S_IDLE; guard <= GUARD;
                end else if (!rx_busy) begin
                    if (!rx_line) begin rx_busy <= 1; rx_bits <= 0; rx_div <= baud_reload(1); end
                end else if (rx_div != 0) rx_div <= rx_div - 1'b1;
                else begin
                    rx_div <= baud_reload(0);
                    if (rx_bits == 0) begin
                        if (rx_line) rx_busy <= 0; else rx_bits <= 1;
                    end else if (rx_bits <= 8) begin
                        rx_shift <= {rx_line, rx_shift[7:1]};
                        rx_bits <= rx_bits + 1'b1;
                    end else begin
                        rx_busy <= 1'b0;
                        if (!rx_line) begin
                            st_rx_err <= 1; st_done <= 1; cnt_rx_err <= cnt_rx_err + 1'b1;
                            state <= S_IDLE; guard <= GUARD;
                        end else case (rx_hdr)
                            2'd0: if (rx_shift == 8'hFF) rx_hdr <= 1;
                            2'd1: if (rx_shift == 8'hF5) begin rx_hdr <= 2; rx_count <= 0; rx_sum <= 0; end
                                  else if (rx_shift != 8'hFF) rx_hdr <= 0;
                            default: begin
                                if (rx_count < 32) rx_buf[rx_count[4:0]] <= rx_shift;
                                rx_count <= rx_count + 1'b1;
                                if (rx_count == 0) rx_sum <= rx_shift;
                                else if (rx_count == 1) begin
                                    rx_sum <= rx_sum + rx_shift; rx_need <= rx_shift[5:0];
                                    if (rx_shift < 2 || rx_shift > 30) begin st_rx_err <= 1; st_done <= 1;
                                        cnt_rx_err <= cnt_rx_err + 1'b1; state <= S_IDLE; guard <= GUARD; end
                                end else if (rx_need == 1) begin
                                    rx_t <= ts; st_done <= 1; state <= S_IDLE; guard <= GUARD;
                                    if (rx_shift == ~rx_sum) st_rx_ok <= 1;
                                    else begin st_rx_err <= 1; cnt_rx_err <= cnt_rx_err + 1'b1; end
                                end else begin
                                    if (rx_count == 2) rx_status <= rx_shift;
                                    rx_sum <= rx_sum + rx_shift; rx_need <= rx_need - 1'b1;
                                end
                            end
                        endcase
                    end
                end
            end

            default: state <= S_IDLE;
        endcase
    end

    wire [5:0] tb = {raddr[5:2], 2'b00}; wire [4:0] rb = {raddr[4:2], 2'b00};
    always @(*) begin
        case (raddr)
            8'h00: rdata = {17'd0, tx_count, 6'd0, expect_reply, 1'b0};
            8'h04: rdata = {8'd0, rx_status, 1'b0, 1'b0, rx_count, 3'd0,
                            st_rx_ok, st_rx_err, st_timeout, st_done, (state != S_IDLE) | go};
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
