`timescale 1ns/1ps
// des_vib (vibration acquisition) against a behavioural ADXL345: 3-wire SPI mode 3, 4-wire until DATA_FORMAT.SPI is
// set (reads then float high), multi-byte reads coherent (a conversion during a read is applied at CS high),
// DATA_READY on INT1 cleared by reading DATAZ1. The model checks the SPI timing (SCLK high at CS edges, half period,
// CS high time) and that the two sides never drive SDIO together. 75 MHz clock, sensor ODR shortened to 30 us.
module tb_des_vib;
    reg clk = 0;
    always #6.667 clk = ~clk;                     // 75 MHz
    reg  [31:0] ts = 0;
    always @(posedge clk) ts <= ts + 1;

    reg        wr = 0;
    reg [11:0] waddr = 0, raddr = 0;
    reg [31:0] wdata = 0;
    wire [31:0] rdata;
    wire cs_n, sclk, d_sdo, d_oe;
    wire int1;
    reg  m_oe = 0, m_out = 1;
    tri1 sdio;                                    // module pull-up
    assign sdio = d_oe ? d_sdo : 1'bz;
    assign sdio = m_oe ? m_out : 1'bz;

    des_vib dut (.clk(clk), .wr(wr && waddr[11:8] == 4'h9), .waddr(waddr[7:0]), .wdata(wdata), .raddr(raddr[7:0]),
                 .rdata(rdata), .ts(ts), .cs_n(cs_n), .sclk(sclk), .sdo(d_sdo), .sdo_oe(d_oe), .sdi(sdio), .int1(int1));

    // ---------------- ADXL345 model ----------------
    localparam real ODR_NS = 30000.0;
    reg [7:0] r [0:63];
    integer i, bitc = 0, contention = 0, t_viol = 0, ngen = 0, pend = 0;
    reg [7:0] cmd = 0, dsh = 0;
    reg rw = 0, mb = 0, rd37 = 0, drdy = 0;
    reg [5:0] addr = 0;
    real t_csr = -1.0e9, t_sc = 0.0, half_min = 1.0e9;
    integer gx [0:4095], gy [0:4095], gz [0:4095], gt [0:4095];
    wire three_wire = r[6'h31][6];
    assign int1 = drdy && r[6'h2E][7] && !r[6'h2F][7];
    initial begin
        for (i = 0; i < 64; i = i + 1) r[i] = 8'h00;
        r[0] = 8'hE5; r[6'h2C] = 8'h0A;
    end
    task convert;   // new sample n: x sine, y = n mod 2000 - 1000 (identifies the sample), z about 1 g
        integer x, y, z;
        begin
            x = $rtoi(300.0 * $sin(6.2831853 * ngen / 32.0) + (ngen % 3) - 1);
            y = (ngen % 2000) - 1000;
            z = 256 - (ngen % 5);
            gx[ngen % 4096] = x; gy[ngen % 4096] = y; gz[ngen % 4096] = z; gt[ngen % 4096] = ts;
            r[6'h32] = x[7:0]; r[6'h33] = x[15:8]; r[6'h34] = y[7:0]; r[6'h35] = y[15:8];
            r[6'h36] = z[7:0]; r[6'h37] = z[15:8];
            ngen = ngen + 1; drdy = 1;
        end
    endtask
    initial begin
        wait (r[6'h2D][3]);                       // POWER_CTL.Measure
        forever begin
            #(ODR_NS);
            if (cs_n) convert; else pend = 1;     // coherent multi-byte reads
        end
    end
    always @(negedge cs_n) begin
        bitc = 0; rd37 = 0;
        if (!sclk) t_viol = t_viol + 1;           // mode 3: SCLK high when CS falls
        if ($realtime - t_csr < 150.0) t_viol = t_viol + 1;
    end
    always @(posedge cs_n) begin
        t_csr = $realtime; m_oe = 0;
        if (!sclk) t_viol = t_viol + 1;
        if (rd37) drdy = 0;
        if (pend) begin pend = 0; convert; end
    end
    always @(sclk) if (!cs_n) begin
        if ($realtime - t_sc < half_min && t_sc > 0.0) half_min = $realtime - t_sc;
        t_sc = $realtime;
    end
    always @(posedge sclk) if (!cs_n) begin
        if (bitc < 8) begin
            cmd = {cmd[6:0], sdio};
            if (bitc == 7) begin rw = cmd[7]; mb = cmd[6]; addr = cmd[5:0]; end
        end else if (!rw) begin
            dsh = {dsh[6:0], sdio};
            if ((bitc - 8) % 8 == 7) begin r[addr] = dsh; if (mb) addr = addr + 1; end
        end else if ((bitc - 8) % 8 == 7) begin
            if (addr == 6'h37) rd37 = 1;
            if (mb) addr = addr + 1;
        end
        bitc = bitc + 1;
    end
    always @(negedge sclk) if (!cs_n && bitc >= 8 && rw && three_wire) begin
        m_oe <= #5 1'b1; m_out <= #5 r[addr][7 - (bitc - 8) % 8];
    end
    always @(posedge clk) if (d_oe && m_oe) contention = contention + 1;

    // ---------------- helpers ----------------
    integer fails = 0, passes = 0;
    reg [8*110-1:0] msg;
    task check(input cond, input [8*110-1:0] what);
        begin
            if (cond) begin passes = passes + 1; $display("ok   %0s", what); end
            else begin fails = fails + 1; $display("FAIL: %0s", what); end
        end
    endtask
    task wreg(input [11:0] a, input [31:0] d);
        begin @(posedge clk); wr <= 1; waddr <= a; wdata <= d; @(posedge clk); wr <= 0; end
    endtask
    task rreg(input [11:0] a, output [31:0] d);
        begin @(posedge clk); raddr <= a; @(posedge clk); #1 d = rdata; end
    endtask
    task us(input integer n); begin repeat (n) #1000; end endtask
    reg [31:0] r0, r1, r2;
    task xfer(input [15:0] w, output [7:0] b);   // single-byte transfer, waits for the end
        begin
            wreg(12'h908, {16'd0, w});
            r0 = 32'h100;
            while (r0[8]) rreg(12'h908, r0);
            b = r0[7:0];
        end
    endtask
    // pop one FIFO entry
    reg signed [15:0] ex, ey, ez; reg [15:0] et;
    task pop;
        begin
            rreg(12'h920, r0); rreg(12'h924, r1); wreg(12'h920, 0);
            ex = r0[15:0]; ey = r0[31:16]; ez = r1[15:0]; et = r1[31:16];
        end
    endtask

    // generation index of a popped sample: search back from the newest for its y
    task find(input integer yy, output integer n);
        integer q;
        begin
            n = -1;
            for (q = ngen - 1; q >= 0 && q >= ngen - 1999 && n < 0; q = q - 1) if (gy[q % 4096] == yy) n = q;
        end
    endtask
    reg [7:0] b;
    integer n0, k, bad, d0, w, sum, sq, mn, mx, j, ax, lvl, nw;
    integer sumx, sumy, sumz, sqx, sqy, sqz, mnx, mny, mnz, mxx, mxy, mxz;
    initial begin
        rreg(12'h90C, r0); check(r0 == 32'h56494231, "ID");
        rreg(12'h904, r0); check(r0 == 0 && cs_n && sclk && !d_oe, "reset: idle, CS high, SCLK high, SDIO released");

        // ---- 1. single-byte transfers; 4-wire until DATA_FORMAT says 3-wire ----
        xfer(16'h8000, b); check(b == 8'hFF, "DEVID read before 3-wire mode: line floats high (0xFF)");
        xfer(16'h3149, b); check(r[6'h31] == 8'h49, "write DATA_FORMAT 0x49 (3-wire, full resolution, +-4 g)");
        xfer(16'h8000, b); $sformat(msg, "DEVID in 3-wire mode = 0x%0h (0xE5)", b); check(b == 8'hE5, msg);
        xfer(16'h2C0F, b); xfer(16'h2F00, b); xfer(16'h2E80, b);
        check(r[6'h2C] == 8'h0F && r[6'h2F] == 8'h00 && r[6'h2E] == 8'h80, "BW_RATE 0x0F, INT_MAP 0, INT_ENABLE DATA_READY");
        xfer(16'hAC00, b); check(b == 8'h0F, "read back BW_RATE");
        $sformat(msg, "SCLK half period %0.1f ns (3.75 MHz = 133.3)", half_min); check(half_min > 133.0 && half_min < 134.0, msg);

        // ---- 2. streaming: the sample already waiting at enable is read, then every one, in order ----
        wreg(12'h910, 32'd16);                    // statistics window 16 samples
        xfer(16'h2D08, b);                        // POWER_CTL.Measure: conversions start
        wait (int1); us(5);
        check(int1 && !d_oe, "DATA_READY already high before streaming is enabled");
        wreg(12'h900, 32'h3);                     // enable + clear
        us(30 * 100 + 20);
        rreg(12'h904, r0); lvl = r0[25:16];
        $sformat(msg, "100 samples in 3 ms: FIFO level %0d", lvl); check(lvl >= 100 && lvl <= 102, msg);
        r2 = 0; r0 = 1;
        while (r2 != r0) begin rreg(12'h914, r2); rreg(12'h918, r1); rreg(12'h914, r0); end
        rreg(12'h924, r0); rreg(12'h920, r0); find($signed(r0[31:16]), n0);   // the head = first captured sample
        d0 = r1 - gt[(n0 + r2[15:0] - 1) % 4096];
        $sformat(msg, "LAST_TS = DATA_READY edge + %0d clocks (synchroniser); first sample = the one waiting at enable",
                 d0);
        check(d0 >= 2 && d0 <= 4 && n0 >= 0, msg);
        bad = 0;
        for (k = 0; k < 100; k = k + 1) begin
            pop;
            j = (n0 + k) % 4096;
            if (ex != gx[j] || ey != gy[j] || ez != gz[j]) bad = bad + 1;
            if (k > 0 && et != (((gt[j] + d0) >> 4) & 16'hFFFF)) bad = bad + 1;
        end
        $sformat(msg, "100 samples in order, values and time stamps exact (%0d mismatches)", bad); check(bad == 0, msg);

        // ---- 3. window statistics (16 samples): latest latched window against the model ----
        rreg(12'h91C, r0); nw = r0[15:0];
        rreg(12'h928, r0); sumx = $signed(r0); rreg(12'h92C, r0); sumy = $signed(r0); rreg(12'h930, r0); sumz = $signed(r0);
        rreg(12'h934, r0); sqx = r0; rreg(12'h938, r0); sqy = r0; rreg(12'h93C, r0); sqz = r0;
        rreg(12'h940, r0); mnx = $signed(r0[15:0]); mxx = $signed(r0[31:16]);
        rreg(12'h944, r0); mny = $signed(r0[15:0]); mxy = $signed(r0[31:16]);
        rreg(12'h948, r0); mnz = $signed(r0[15:0]); mxz = $signed(r0[31:16]);
        rreg(12'h91C, r1);
        check(r1[15:0] == nw && nw >= 6, "statistics read inside one window (no update in between)");
        bad = 0;
        for (ax = 0; ax < 3; ax = ax + 1) begin
            sum = 0; sq = 0; mn = 32767; mx = -32768;
            for (k = 0; k < 16; k = k + 1) begin
                j = (n0 + (nw - 1) * 16 + k) % 4096;
                w = ax == 0 ? gx[j] : ax == 1 ? gy[j] : gz[j];
                sum = sum + w; sq = sq + w * w; if (w < mn) mn = w; if (w > mx) mx = w;
            end
            if (sum != (ax == 0 ? sumx : ax == 1 ? sumy : sumz)) bad = bad + 1;
            if ((sq >> 10) != (ax == 0 ? sqx : ax == 1 ? sqy : sqz)) bad = bad + 1;
            if (mn != (ax == 0 ? mnx : ax == 1 ? mny : mnz) || mx != (ax == 0 ? mxx : ax == 1 ? mxy : mxz)) bad = bad + 1;
        end
        $sformat(msg, "window %0d: sum / sum of squares >> 10 / min / max of x y z all exact (%0d mismatches; x %0d..%0d)",
                 nw, bad, mnx, mxx);
        check(bad == 0, msg);

        // ---- 4. a register transfer while streaming: answered, no sample lost ----
        rreg(12'h914, r2); k = r2[15:0];
        xfer(16'h8000, b);
        us(30 * 10);
        rreg(12'h914, r2); rreg(12'h904, r0);
        check(b == 8'hE5, "DEVID read while streaming");
        lvl = r0[25:16];
        bad = 0;
        for (k = 0; k < lvl; k = k + 1) begin
            pop; j = (n0 + 100 + k) % 4096;
            if (ex != gx[j] || ey != gy[j] || ez != gz[j]) bad = bad + 1;
        end
        $sformat(msg, "%0d more samples drained after the transfer, still consecutive (%0d mismatches)", lvl, bad);
        check(bad == 0 && lvl >= 10, msg);

        // ---- 5. overflow: 512 entries kept, the rest counted ----
        wreg(12'h900, 32'h3);
        us(30 * 530);
        rreg(12'h904, r0); rreg(12'h914, r2);
        $sformat(msg, "overflow: level %0d, %0d samples, %0d dropped, flag %0d", r0[25:16], r2[15:0], r2[31:16], r0[3]);
        check(r0[25:16] == 512 && r2[31:16] == r2[15:0] - 512 && r0[3], msg);
        rreg(12'h920, r0); find($signed(r0[31:16]), n0);
        bad = 0;
        for (k = 0; k < 512; k = k + 1) begin
            pop; j = (n0 + k) % 4096;
            if (ex != gx[j] || ey != gy[j] || ez != gz[j]) bad = bad + 1;
        end
        $sformat(msg, "the 512 kept are the first 512 after the clear (%0d mismatches)", bad); check(bad == 0, msg);

        // ---- 6. disable, clear ----
        wreg(12'h900, 32'h0); us(20);             // a read in flight at the disable still completes
        rreg(12'h914, r2); us(30 * 5); rreg(12'h914, r1);
        check(r1 == r2 && int1, "disabled: nothing read, DATA_READY left high");
        wreg(12'h900, 32'h2);
        rreg(12'h904, r0); rreg(12'h914, r2); rreg(12'h91C, r1);
        check(r0[25:16] == 0 && r2 == 0 && r1 == 0 && !r0[3], "clear: FIFO empty, counters 0");

        $sformat(msg, "SPI timing violations %0d, SDIO driven by both sides %0d clocks", t_viol, contention);
        check(t_viol == 0 && contention == 0, msg);
        $display("des_vib: %0d passed, %0d failed", passes, fails);
        $finish;
    end
    initial begin #(200_000_000.0); $display("TIMEOUT"); $finish; end
endmodule
