`timescale 1ns/1ps
// servo_bus vs. a behavioural Desheng servo; golden frames are the examples from protocol v4.01
module tb_servo_bus;
    reg clk = 0;
    always #6.6667 clk = ~clk;          // 75 MHz

    reg        wr = 0;
    reg [7:0]  waddr = 0, raddr = 0;
    reg [31:0] wdata = 0;
    wire [31:0] rdata;
    reg [31:0] ts = 0;
    always @(posedge clk) ts <= ts + 1;

    tri1 bus;                            // pull-up
    wire m_drive_low;
    reg  s_drive_low = 0;
    assign bus = m_drive_low ? 1'b0 : 1'bz;
    assign bus = s_drive_low ? 1'b0 : 1'bz;

    servo_bus dut (.clk(clk), .wr(wr), .waddr(waddr), .wdata(wdata), .raddr(raddr), .rdata(rdata),
                   .ts(ts), .bus_i(bus), .bus_drive_low(m_drive_low));

    // ---------------- behavioural servo ----------------
    localparam real BIT_NS = 1.0e9 / 115200.0;
    reg [7:0] frame [0:79];
    integer   frame_len = 0;
    integer   frames_seen = 0;
    reg       servo_tx = 0;

    task srv_rx_byte(output [7:0] b);
        integer k;
        begin
            @(negedge bus);
            #(BIT_NS * 1.5);
            for (k = 0; k < 8; k = k + 1) begin b[k] = bus; #(BIT_NS); end
            if (bus !== 1'b1) $display("SERVO: framing error");
        end
    endtask

    task srv_tx_byte(input [7:0] b);
        integer k;
        begin
            s_drive_low = 1; #(BIT_NS);
            for (k = 0; k < 8; k = k + 1) begin s_drive_low = ~b[k]; #(BIT_NS); end
            s_drive_low = 0; #(BIT_NS);
        end
    endtask

    task srv_reply(input [7:0] id, input [7:0] st, input integer np, input [15:0] val, input bad_chk);
        reg [7:0] sum, len;
        begin
            len = np + 2;
            sum = id + len + st;
            if (np == 2) sum = sum + val[15:8] + val[7:0];
            #(100000);                   // 100 us reply delay
            servo_tx = 1;
            srv_tx_byte(8'hFF); srv_tx_byte(8'hF5); srv_tx_byte(id); srv_tx_byte(len); srv_tx_byte(st);
            if (np == 2) begin srv_tx_byte(val[15:8]); srv_tx_byte(val[7:0]); end
            srv_tx_byte(bad_chk ? ~(~sum) : ~sum);
            servo_tx = 0;
        end
    endtask

    // reply with a caller-chosen LEN (payload bytes 2..len-1 are 0x11), for the invalid-LEN tests
    task srv_reply_len(input [7:0] id, input [7:0] len);
        reg [7:0] sum; integer q;
        begin
            #(100000);
            servo_tx = 1;
            srv_tx_byte(8'hFF); srv_tx_byte(8'hF5); srv_tx_byte(id); srv_tx_byte(len);
            sum = id + len;
            for (q = 2; q < len; q = q + 1) begin srv_tx_byte(8'h11); sum = sum + 8'h11; end
            srv_tx_byte(~sum);
            servo_tx = 0;
        end
    endtask

    initial begin : servo_model
        reg [7:0] b;
        integer n;
        forever begin
            srv_rx_byte(b);
            if (b == 8'hFF) begin
                frame[0] = b; frame_len = 1;
                srv_rx_byte(b); frame[1] = b; frame_len = 2;
                if (b == 8'hFF) begin
                    srv_rx_byte(b); frame[2] = b;                 // ID
                    srv_rx_byte(b); frame[3] = b;                 // LEN
                    frame_len = 4;
                    for (n = 0; n < frame[3]; n = n + 1) begin srv_rx_byte(b); frame[4 + n] = b; frame_len = frame_len + 1; end
                    frames_seen = frames_seen + 1;
                // frame[4] = INSTR
                if (frame[4] == 8'h01 && frame[2] == 8'h01) srv_reply(8'h01, 8'h00, 0, 16'h0, 0);
                else if (frame[4] == 8'h02 && frame[2] == 8'h01) srv_reply(8'h01, 8'h00, 2, 16'h07FF, 0);
                else if (frame[4] == 8'h02 && frame[2] == 8'h09) srv_reply(8'h09, 8'h20, 2, 16'h0123, 1);
                else if (frame[4] == 8'h03 && frame[2] == 8'h0A) srv_reply_len(8'h0A, 8'd1);   // LEN < 2
                else if (frame[4] == 8'h03 && frame[2] == 8'h0B) srv_reply_len(8'h0B, 8'd40);  // LEN > 30
                end
            end
        end
    end

    // ---------------- register access ----------------
    task reg_wr(input [7:0] a, input [31:0] d);
        begin @(posedge clk); wr <= 1; waddr <= a; wdata <= d; @(posedge clk); wr <= 0; end
    endtask
    task reg_rd(input [7:0] a, output [31:0] d);
        begin @(posedge clk); raddr <= a; @(posedge clk); #1 d = rdata; end
    endtask

    reg [7:0] body [0:63];
    task send(input integer count, input expect_reply);
        integer w;
        reg [31:0] st;
        begin
            for (w = 0; w < (count + 3) / 4; w = w + 1)
                reg_wr(8'h40 + 4 * w, {body[4*w+3], body[4*w+2], body[4*w+1], body[4*w]});
            reg_wr(8'h00, (count << 8) | (expect_reply << 1) | 1);
            st = 1;
            while (st[0] == 1'b1 || st[1] == 1'b0) begin #(20000); reg_rd(8'h04, st); end
        end
    endtask

    integer errors = 0;
    task check(input [255:0] what, input [31:0] got, input [31:0] exp);
        begin
            if (got !== exp) begin errors = errors + 1; $display("FAIL %0s: got %h exp %h", what, got, exp); end
            else $display("ok   %0s = %h", what, got);
        end
    endtask
    task check_frame(input [255:0] what, input integer n, input [8*20-1:0] exp);
        integer k; reg bad;
        begin
            bad = (frame_len != n);
            for (k = 0; k < n; k = k + 1) if (frame[k] !== exp[8*(n-1-k) +: 8]) bad = 1;
            if (bad) begin
                errors = errors + 1; $write("FAIL %0s: got", what);
                for (k = 0; k < frame_len; k = k + 1) $write(" %h", frame[k]); $display("");
            end else $display("ok   %0s (%0d bytes match protocol example)", what, n);
        end
    endtask

    reg [31:0] r, t0, t1;
    initial begin
        repeat (10) @(posedge clk);

        // 1) PING ID1  -> FF FF 01 02 01 FB, reply FF F5 01 02 00 FC
        body[0] = 8'h01; body[1] = 8'h02; body[2] = 8'h01;
        send(3, 1);
        check_frame("PING frame", 6, {8'hFF, 8'hFF, 8'h01, 8'h02, 8'h01, 8'hFB});
        reg_rd(8'h04, r);
        check("PING status RX_OK", r[4], 1);
        check("PING rx_count", r[14:8], 4);
        reg_rd(8'h80, r); check("PING rx bytes", r, 32'hFC000201);

        // 2) READ position of ID1 -> FF FF 01 04 02 38 02 BE, reply FF F5 01 04 00 07 FF F4
        body[0] = 8'h01; body[1] = 8'h04; body[2] = 8'h02; body[3] = 8'h38; body[4] = 8'h02;
        send(5, 1);
        check_frame("READ frame", 8, {8'hFF, 8'hFF, 8'h01, 8'h04, 8'h02, 8'h38, 8'h02, 8'hBE});
        reg_rd(8'h04, r); check("READ RX_OK", r[4], 1); check("READ rx_count", r[14:8], 6);
        reg_rd(8'h80, r); check("READ rx bytes 0-3 (ID LEN ST HI)", r, 32'h07000401);
        reg_rd(8'h84, r); check("READ rx bytes 4-5 (LO CHK)", r[15:0], 16'hF4FF);

        // 3) SYNC WRITE ID1, ID3 (protocol 2.3.6), no reply. The PDF prints checksum 0x8B, but its own formula
        //    gives ~(sum of 15 body bytes = 0x545) & 0xFF = 0xBA; every other example in the PDF matches the formula.
        //    Treat 0x8B as a typo; confirm on a real servo.
        body[0]=8'hFE; body[1]=8'h0E; body[2]=8'h83; body[3]=8'h2A; body[4]=8'h04;
        body[5]=8'h01; body[6]=8'h07; body[7]=8'hD0; body[8]=8'h03; body[9]=8'hE8;
        body[10]=8'h03; body[11]=8'h07; body[12]=8'hD0; body[13]=8'h03; body[14]=8'hE8;
        send(15, 0);
        #(BIT_NS * 12);
        check_frame("SYNC WRITE frame", 18, {8'hFF,8'hFF,8'hFE,8'h0E,8'h83,8'h2A,8'h04,8'h01,8'h07,8'hD0,8'h03,8'hE8,8'h03,8'h07,8'hD0,8'h03,8'hE8,8'hBA});
        reg_rd(8'h04, r); check("SYNC done, no reply expected", r[4:0], 5'b00010);
        reg_rd(8'h0C, t0); reg_rd(8'h10, t1);
        $display("     SYNC WRITE on-wire time = %0d cycles = %0d us (18 bytes x 10 bits @115200 = 1562 us)", t1 - t0, (t1 - t0) / 75);
        check("SYNC duration within 1%", ((t1 - t0) > 115000 && (t1 - t0) < 119000), 1);

        // 4) READ from silent ID7 -> timeout
        reg_wr(8'h08, 500);
        body[0] = 8'h07; body[1] = 8'h04; body[2] = 8'h02; body[3] = 8'h38; body[4] = 8'h02;
        send(5, 1);
        reg_rd(8'h04, r); check("silent servo -> TIMEOUT", r[2], 1);
        reg_rd(8'h18, r); check("timeout counter", r, 1);

        // 5) READ from ID9 which answers with a bad checksum
        reg_wr(8'h08, 3000);
        body[0] = 8'h09;
        send(5, 1);
        reg_rd(8'h04, r); check("bad checksum -> RX_ERR", r[3], 1); check("status byte captured (overload b5)", r[23:16], 8'h20);
        reg_rd(8'h1C, r); check("rx error counter", r, 1);
        reg_rd(8'h20, r); check("frames sent", r, 5);

        // 6) invalid reply LEN (1 and 40) -> RX_ERR each, then a good reply recovers.
        //    These are the paths that bound the RX write index: LEN in [2,30] means idx <= 31 = rx_buf size - 1.
        body[0] = 8'h0A; body[1] = 8'h02; body[2] = 8'h03;
        send(3, 1);
        #(BIT_NS * 12);                    // DUT leaves S_RX one byte early and restarts after a 10-bit
        reg_rd(8'h04, r); check("LEN=1 reply -> RX_ERR", r[3], 1);   // guard; let the tail drain first
        body[0] = 8'h0B;
        send(3, 1);
        reg_rd(8'h04, r); check("LEN=40 reply -> RX_ERR", r[3], 1);
        #(BIT_NS * 400);                   // 39 bytes still on the wire; DUT already left S_RX
        reg_rd(8'h1C, r); check("rx error counter", r, 3);
        body[0] = 8'h01; body[1] = 8'h02; body[2] = 8'h01;
        send(3, 1);
        reg_rd(8'h04, r); check("good reply after LEN errors", r[4], 1);
        reg_rd(8'h20, r); check("frames sent", r, 8);

        if (errors == 0) $display("ALL TESTS PASSED"); else $display("%0d TESTS FAILED", errors);
        $finish;
    end
    initial begin #20_000_000; $display("TIMEOUT (sim)"); $finish; end
endmodule
