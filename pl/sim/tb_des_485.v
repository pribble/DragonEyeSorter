`timescale 1ns/1ps
// tb_des_485 — des_485 (RS485 Modbus-RTU) against a behavioural slave on the A/B line.
// Golden CRCs from the Modbus spec / common references: 01 05 00 00 FF 00 -> 8C 3A, 01 03 00 00 00 0A -> C5 CD.
// Checks: register defaults, frame bytes + CRC + parity bits as seen on the wire, DE lead (>= 1 bit) and release
// (end of the last stop bit), reply OK / CRC error / parity error / short frame / timeout / overflow, broadcast,
// listen frames, the t3.5 quiet guard before a transfer, writes ignored while busy, abort on a stuck-low bus.
module tb_des_485;
    reg clk = 1'b0;
    always #6.667 clk = ~clk;              // 75 MHz
    reg [31:0] cyc = 0;
    always @(posedge clk) cyc <= cyc + 1;

    reg         wr = 1'b0;
    reg  [7:0]  waddr = 8'd0, raddr = 8'd0;
    reg  [31:0] wdata = 32'd0;
    wire [31:0] rdata;
    reg         sl = 1'b1;                 // slave / other master driving the line (1 = idle)
    wire        tx_o, de_o;
    wire        rx_i = de_o ? 1'b1 : sl;   // RO floats (pull-up) while our driver is on

    des_485 dut (.clk(clk), .wr(wr), .waddr(waddr), .wdata(wdata), .raddr(raddr), .rdata(rdata),
                 .ts(cyc), .rx_i(rx_i), .tx_o(tx_o), .de_o(de_o));

    integer fails = 0, checks = 0;
    task check(input cond, input [8*72-1:0] what);
        begin
            checks = checks + 1;
            if (!cond) begin fails = fails + 1; $display("FAIL %0s (t=%0d)", what, cyc); end
        end
    endtask

    // collision: nobody may drive the line while our DE is on
    always @(posedge clk) if (de_o && !sl) begin fails = fails + 1; $display("FAIL collision at %0d", cyc); end
    // DE edges
    reg de_d = 1'b0; reg [31:0] de_rise = 0, de_fall = 0;
    always @(posedge clk) begin
        de_d <= de_o;
        if (de_o && !de_d) de_rise <= cyc;
        if (!de_o && de_d) de_fall <= cyc;
    end

    // ---------------- register access ----------------
    task reg_wr(input [7:0] a, input [31:0] d);
        begin @(negedge clk); wr = 1'b1; waddr = a; wdata = d; @(negedge clk); wr = 1'b0; end
    endtask
    task reg_rd(input [7:0] a, output [31:0] d);
        begin raddr = a; #1; d = rdata; end
    endtask
    reg [31:0] v, st;
    task wait_idle(input integer max_cycles);
        integer k;
        begin
            k = 0; reg_rd(8'h04, st);
            while (st[0] && k < max_cycles) begin @(posedge clk); k = k + 1; reg_rd(8'h04, st); end
            check(!st[0], "transfer finished");
        end
    endtask

    // ---------------- CRC-16/MODBUS (TB copy, checked against golden values first) ----------------
    function [15:0] crc16(input integer n, input [8*80-1:0] bytes);   // bytes[0] in bits [7:0]
        integer i, j; reg [15:0] c;
        begin
            c = 16'hFFFF;
            for (i = 0; i < n; i = i + 1) begin
                c = c ^ {8'd0, bytes[8*i +: 8]};
                for (j = 0; j < 8; j = j + 1) c = c[0] ? ((c >> 1) ^ 16'hA001) : (c >> 1);
            end
            crc16 = c;
        end
    endfunction

    // ---------------- slave side ----------------
    integer div = 7813, par = 0;           // must match what the test configured in the DUT
    reg [7:0] rxb [0:79]; integer rxn; reg rx_perr, rx_serr;
    reg [31:0] first_start, last_stop_mid;
    // receive one frame from our master: bytes until DE drops; checks parity / stop on the wire
    task slave_recv;
        reg [7:0] b; reg p, pe, done; integer i;
        begin
            rxn = 0; rx_perr = 0; rx_serr = 0; done = 0;
            while (!de_o) @(posedge clk);
            while (!done) begin
                @(posedge clk);
                if (!de_o) done = 1;
                else if (!tx_o) begin
                    if (rxn == 0) first_start = cyc;
                    repeat (div / 2) @(posedge clk);
                    if (tx_o) rx_serr = 1;
                    for (i = 0; i < 8; i = i + 1) begin repeat (div) @(posedge clk); b[i] = tx_o; end
                    if (par != 0) begin
                        repeat (div) @(posedge clk); p = tx_o;
                        pe = (par == 2) ? ~^b : ^b;
                        if (p != pe) rx_perr = 1;
                    end
                    repeat (div) @(posedge clk);
                    if (!tx_o || !de_o) rx_serr = 1;
                    last_stop_mid = cyc;
                    if (rxn < 80) rxb[rxn] = b;
                    rxn = rxn + 1;
                end
            end
        end
    endtask
    // send one byte on the line (bad_par / bad_stop inject errors)
    task slave_byte(input [7:0] b, input bad_par, input bad_stop);
        integer i;
        begin
            sl = 1'b0; repeat (div) @(posedge clk);
            for (i = 0; i < 8; i = i + 1) begin sl = b[i]; repeat (div) @(posedge clk); end
            if (par != 0) begin sl = ((par == 2) ? ~^b : ^b) ^ bad_par; repeat (div) @(posedge clk); end
            sl = ~bad_stop; repeat (div) @(posedge clk);
            sl = 1'b1;
        end
    endtask
    reg [8*80-1:0] fr;                     // frame bytes to send, fr[0] first
    task slave_frame(input integer n, input add_crc, input bad_crc, input integer bad_par_at);
        integer i; reg [15:0] c;
        begin
            c = crc16(n, fr) ^ (bad_crc ? 16'h0001 : 16'h0000);
            for (i = 0; i < n; i = i + 1) slave_byte(fr[8*i +: 8], i == bad_par_at, 1'b0);
            if (add_crc) begin slave_byte(c[7:0], 1'b0, 1'b0); slave_byte(c[15:8], 1'b0, 1'b0); end
        end
    endtask
    task gap_us(input integer us);
        begin repeat (us * 75) @(posedge clk); end
    endtask

    // ---------------- master side helpers ----------------
    task load_tx(input integer n);         // fr -> TX_BUF
        integer i;
        begin
            for (i = 0; i < n; i = i + 4)
                reg_wr(8'h80 + i, {fr[8*(i+3) +: 8], fr[8*(i+2) +: 8], fr[8*(i+1) +: 8], fr[8*i +: 8]});
        end
    endtask
    task go(input integer n, input want_reply);
        begin reg_wr(8'h00, (n << 8) | (want_reply << 1) | 1); end
    endtask
    task expect_wire(input integer n);     // what the slave received = fr[0..n-1] + CRC
        integer i; reg ok; reg [15:0] c;
        begin
            c = crc16(n, fr); ok = (rxn == n + 2);
            for (i = 0; i < n; i = i + 1) if (rxb[i] !== fr[8*i +: 8]) ok = 0;
            if (rxb[n] !== c[7:0] || rxb[n+1] !== c[15:8]) ok = 0;
            check(ok, "frame on the wire = PDU + CRC (low byte first)");
            check(!rx_serr && !rx_perr, "start / parity / stop bits on the wire");
        end
    endtask

    integer i, n, k;
    reg [31:0] t_a, t_b, r0, r1;
    initial begin
        // ---- 0. TB CRC against golden values ----
        fr = 0; fr[47:0] = 48'h00FF_0000_0501;  // 01 05 00 00 FF 00
        check(crc16(6, fr) == 16'h3A8C, "golden CRC 01 05 00 00 FF 00 = 8C 3A");
        fr = 0; fr[47:0] = 48'h0A00_0000_0301;  // 01 03 00 00 00 0A
        check(crc16(6, fr) == 16'hCDC5, "golden CRC 01 03 00 00 00 0A = C5 CD");

        // ---- 1. defaults ----
        repeat (5) @(posedge clk);
        reg_rd(8'h40, v); check(v == 32'h34383501, "ID");
        reg_rd(8'h0C, v); check(v == 7813, "DIV default 7813 (9600 baud)");
        reg_rd(8'h14, v); check(v == 4011, "T35 default 4011 us");
        reg_rd(8'h08, v); check(v == 100000, "TIMEOUT default 100 ms");
        reg_rd(8'h10, v); check(v == 0, "parity default none");
        reg_rd(8'h04, v); check(v[0] == 0 && v[1] == 0 && v[16] == 0, "idle, no result, DE off");
        check(tx_o == 1 && de_o == 0, "line idle after reset");

        // ---- 2. 9600 8N1, FC05 write coil, golden CRC on the wire, DE timing, echo reply after 3 ms ----
        fr = 0; fr[47:0] = 48'h00FF_0000_0501;
        load_tx(6); go(6, 1);
        slave_recv;
        expect_wire(6);
        check(rxb[6] == 8'h8C && rxb[7] == 8'h3A, "FC05 CRC on the wire = 8C 3A");
        check(first_start - de_rise >= div - 1 && first_start - de_rise <= div + 3, "DE up 1 bit before start bit");
        #1; k = de_fall - last_stop_mid; $display("     DE release: %0d clocks after the stop-bit middle (div/2 = %0d)", k, div / 2);
        check(k >= div / 2 - 2 && k <= div / 2 + 4, "DE released at the end of the last stop bit");
        gap_us(3000);
        fr = 0; fr[47:0] = 48'h00FF_0000_0501; slave_frame(6, 1, 0, -1);
        wait_idle(600000);
        check(st[4] && st[1] && !st[3] && !st[2] && !st[5], "reply OK");
        check(st[15:8] == 8, "RX_LEN 8 (with CRC)");
        reg_rd(8'hC0, v); check(v == 32'h0000_0501, "RX_BUF word 0");
        reg_rd(8'hC4, v); check(v == 32'h3A8C_00FF, "RX_BUF word 1 (data + CRC)");
        reg_rd(8'h1C, t_a); reg_rd(8'h20, t_b);
        k = (t_b - t_a) / 75;               // reply latency in us (3000 gap + DE release slack)
        check(k >= 2990 && k <= 3020, "RX_T0 - TX_T1 = slave turnaround (3 ms)");
        reg_rd(8'h28, v); check(v == 1, "CNT_TX 1");
        reg_rd(8'h2C, v); check(v == 1, "CNT_OK 1");

        // ---- 3. 115200 8E1, FC03 read holding registers, parity on the wire, reply with data ----
        div = 651; par = 1;
        reg_wr(8'h0C, 651); reg_wr(8'h10, 1); reg_wr(8'h14, 1750); reg_wr(8'h08, 2000);
        fr = 0; fr[47:0] = 48'h0A00_0000_0301;
        load_tx(6); go(6, 1);
        slave_recv; expect_wire(6);
        check(rxb[6] == 8'hC5 && rxb[7] == 8'hCD, "FC03 CRC on the wire = C5 CD");
        gap_us(300);
        fr = 0; fr[39:0] = 40'h34_12_02_03_01; slave_frame(5, 1, 0, -1);   // 01 03 02 12 34 + CRC
        wait_idle(200000);
        check(st[4] && st[15:8] == 7, "8E1 reply OK, RX_LEN 7");
        reg_rd(8'hC0, v); check(v == 32'h1202_0301, "8E1 reply bytes 0..3");
        reg_rd(8'hC4, v); check(v[7:0] == 8'h34, "8E1 reply byte 4");

        // ---- 4. parity error in the reply -> frame error ----
        fr = 0; fr[47:0] = 48'h0A00_0000_0301; load_tx(6); go(6, 1); slave_recv; gap_us(300);
        fr = 0; fr[39:0] = 40'h34_12_02_03_01; slave_frame(5, 1, 0, 2);
        wait_idle(200000);
        check(st[5] && !st[4], "parity error -> FERR");
        reg_rd(8'h38, v); check(v == 1, "CNT_FERR 1");

        // ---- 5. bad CRC ----
        fr = 0; fr[47:0] = 48'h0A00_0000_0301; load_tx(6); go(6, 1); slave_recv; gap_us(300);
        fr = 0; fr[39:0] = 40'h34_12_02_03_01; slave_frame(5, 1, 1, -1);
        wait_idle(200000);
        check(st[3] && !st[4] && !st[5], "bad CRC -> CRC error");
        reg_rd(8'h34, v); check(v == 1, "CNT_CRC 1");

        // ---- 6. short frame (2 bytes, no CRC) -> CRC error ----
        fr = 0; fr[47:0] = 48'h0A00_0000_0301; load_tx(6); go(6, 1); slave_recv; gap_us(300);
        fr = 0; fr[15:0] = 16'h0301; slave_frame(2, 0, 0, -1);
        wait_idle(200000);
        check(st[3] && st[15:8] == 2, "2-byte reply -> CRC error, RX_LEN 2");

        // ---- 7. no reply -> timeout after TIMEOUT_US (2000 us) ----
        fr = 0; fr[47:0] = 48'h0A00_0000_0301; load_tx(6); go(6, 1); slave_recv;
        t_a = cyc;
        wait_idle(400000);
        k = (cyc - t_a) / 75;
        check(st[2] && !st[4], "silent slave -> TIMEOUT");
        check(k >= 1995 && k <= 2030, "timeout ~2000 us after the last stop bit");
        reg_rd(8'h30, v); check(v == 1, "CNT_TIMEOUT 1");

        // ---- 8. broadcast (no reply expected): done right after TX ----
        fr = 0; fr[47:0] = 48'h00FF_0000_0500; load_tx(6); go(6, 0); slave_recv; expect_wire(6);
        repeat (20) @(posedge clk);
        reg_rd(8'h04, st);
        check(!st[0] && st[1] && !st[4] && !st[2], "broadcast: done, no reply state");

        // ---- 9. listen frame while idle (PC as master), then ack ----
        gap_us(2000);
        fr = 0; fr[47:0] = 48'h0100_0100_0102; slave_frame(6, 1, 0, -1);   // 02 01 00 01 00 01 + CRC
        gap_us(2000);
        reg_rd(8'h04, st);
        check(st[7] && st[19] && !st[20] && st[15:8] == 8, "listen frame: ready, CRC ok, 8 bytes");
        reg_rd(8'hC0, v); check(v == 32'h0100_0102, "listen frame bytes");
        reg_rd(8'h3C, v); check(v == 1, "CNT_LISTEN 1");
        reg_wr(8'h00, 32'h8);               // ack
        reg_rd(8'h04, st); check(!st[7], "listen ack clears ready");

        // ---- 10. quiet guard: go issued during an incoming frame waits for t3.5 after its last byte ----
        fr = 0; fr[47:0] = 48'h0100_0100_0102;
        load_tx(6);
        fork
            begin slave_frame(6, 1, 0, -1); r0 = cyc; end
            begin gap_us(100); go(6, 0); end
        join
        slave_recv;
        k = (de_rise - r0) / 75;
        // silence counts from the middle of the last stop bit (~4 us before the line model's frame end)
        check(k >= 1740 && k <= 1760, "TX starts t3.5 (1750 us) after the incoming frame ended");
        reg_wr(8'h00, 32'h8);

        // ---- 11. CTRL / TX_BUF writes ignored while the transfer runs ----
        fr = 0; fr[47:0] = 48'h00FF_0000_0501; load_tx(6); go(6, 0);
        fork
            slave_recv;
            begin
                repeat (div * 15) @(posedge clk);
                reg_wr(8'h80, 32'hAAAA_AAAA); reg_wr(8'h00, (3 << 8) | 1);
            end
        join
        expect_wire(6);
        reg_rd(8'h00, v); check(v[14:8] == 6, "TX_LEN unchanged by a write while busy");

        // ---- 12. reply longer than the buffer: 70 bytes -> overflow, first 64 kept ----
        fr = 0; fr[47:0] = 48'h0A00_0000_0301; load_tx(6); go(6, 1); slave_recv; gap_us(300);
        fr = 0; for (i = 0; i < 70; i = i + 1) fr[8*i +: 8] = i;
        slave_frame(70, 0, 0, -1);
        wait_idle(2000000);
        check(st[6] && st[15:8] == 70 && st[3], "70-byte reply: overflow, RX_LEN 70, CRC error");
        reg_rd(8'hFC, v); check(v == 32'h3F3E_3D3C, "bytes 60..63 kept");

        // ---- 13. length 0 / 63 rejected ----
        reg_wr(8'h00, (0 << 8) | 1); reg_rd(8'h04, st); check(!st[0], "len 0 rejected");
        reg_wr(8'h00, (63 << 8) | 1); reg_rd(8'h04, st); check(!st[0], "len 63 rejected");

        // ---- 14. bus stuck low: transfer never starts; abort releases it ----
        sl = 1'b0;
        gap_us(500);
        fr = 0; fr[15:0] = 16'h0301; load_tx(2); go(2, 1);
        gap_us(3000);
        reg_rd(8'h04, st); check(st[0] && !de_o, "stuck-low bus: transfer held (busy, DE off)");
        reg_wr(8'h00, 32'h10);
        repeat (3) @(posedge clk);
        reg_rd(8'h04, st); check(!st[0] && !de_o, "abort releases the pending transfer");
        sl = 1'b1;
        gap_us(3000);

        // ---- 15. clear counters ----
        reg_wr(8'h00, 32'h4);
        reg_rd(8'h28, v); r0 = v; reg_rd(8'h2C, v); r1 = v;
        check(r0 == 0 && r1 == 0, "CTRL b2 clears counters");

        $display("des_485: %0d passed, %0d failed", checks - fails, fails);   // run_all.sh format
        if (fails == 0) $display("ALL TESTS PASSED"); else $display("%0d TESTS FAILED", fails);
        $finish;
    end
    initial begin #400_000_000; $display("TIMEOUT: simulation did not finish"); $finish; end
endmodule
