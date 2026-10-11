`timescale 1ns/1ps
// des_vision on a small frame (32 x 12): output pixels of every mode against a behavioural model with the same
// boundary rules, ROI statistics (area, bounding box, sums, edges), cycles per frame, settings applied only at the
// next frame start, stream timing (same number of beats; camera bypass 1 clock, filters 6 clocks) with random gaps
// in tvalid.
module tb_des_vision;
    localparam integer W = 32, H = 12, BEATS = W / 4;
    reg pclk = 0, clk = 0;
    always #4 pclk = ~pclk;                    // 125 MHz pixel clock
    always #6.6667 clk = ~clk;                 // 75 MHz register clock

    reg         wr = 0;
    reg  [7:0]  waddr = 0, raddr = 0;
    reg  [31:0] wdata = 0;
    wire [31:0] rdata;
    reg  [127:0] s_tdata = 0;
    reg          s_tvalid = 0, s_tuser = 0, s_tlast = 0;
    wire [127:0] m_tdata;
    wire         m_tvalid, m_tuser, m_tlast;

    des_vision #(.WIDTH(W), .HEIGHT(H)) dut (
        .clk(clk), .wr(wr), .waddr(waddr), .wdata(wdata), .raddr(raddr), .rdata(rdata),
        .pclk(pclk), .s_tdata(s_tdata), .s_tvalid(s_tvalid), .s_tuser(s_tuser), .s_tlast(s_tlast),
        .m_tdata(m_tdata), .m_tvalid(m_tvalid), .m_tuser(m_tuser), .m_tlast(m_tlast));

    integer fails = 0, passes = 0;
    task check(input cond, input [8*72-1:0] what);
        begin
            if (cond) passes = passes + 1;
            else begin fails = fails + 1; $display("FAIL: %0s", what); end
        end
    endtask

    // ---------------- image: background 30, bright rectangle 200 at x 10..21, y 3..8 (R = G = B -> gray = value)
    function [7:0] img(input integer x, input integer y);
        img = (x >= 10 && x <= 21 && y >= 3 && y <= 8) ? 8'd200 : 8'd30;
    endfunction

    // model of the module's window: centre (cx, cy), left neighbour of x = 0 is itself, row above 0 = last row
    function [7:0] px(input integer x, input integer y);
        integer xx, yy;
        begin
            xx = (x < 0) ? 0 : x;
            yy = (y < 0) ? H - 1 : y;
            px = img(xx, yy);
        end
    endfunction
    function [7:0] gauss(input integer cx, input integer cy);
        integer s;
        begin
            s = px(cx-1,cy-1) + 2*px(cx,cy-1) + px(cx+1,cy-1) + 2*px(cx-1,cy) + 4*px(cx,cy) + 2*px(cx+1,cy)
              + px(cx-1,cy+1) + 2*px(cx,cy+1) + px(cx+1,cy+1);
            gauss = s / 16;
        end
    endfunction
    function integer sobel(input integer cx, input integer cy);
        integer gx, gy;
        begin
            gx = (px(cx+1,cy-1) + 2*px(cx+1,cy) + px(cx+1,cy+1)) - (px(cx-1,cy-1) + 2*px(cx-1,cy) + px(cx-1,cy+1));
            gy = (px(cx-1,cy+1) + 2*px(cx,cy+1) + px(cx+1,cy+1)) - (px(cx-1,cy-1) + 2*px(cx,cy-1) + px(cx+1,cy-1));
            sobel = (gx < 0 ? -gx : gx) + (gy < 0 ? -gy : gy);
        end
    endfunction

    // ---------------- register access
    task wreg(input [7:0] a, input [31:0] d);
        begin
            @(posedge clk); wr <= 1; waddr <= a; wdata <= d;
            @(posedge clk); wr <= 0;
        end
    endtask
    task rreg(input [7:0] a, output [31:0] d);
        begin
            @(posedge clk); raddr <= a;
            @(posedge clk); @(posedge clk); d = rdata;
        end
    endtask

    // ---------------- stream source: one frame, random gaps
    integer gap_seed = 7;
    task send_frame;
        integer y, b, k;
        reg [127:0] beat;
        begin
            @(posedge pclk); s_tuser <= 1;
            @(posedge pclk); s_tuser <= 0;
            repeat (5) @(posedge pclk);
            for (y = 0; y < H; y = y + 1)
                for (b = 0; b < BEATS; b = b + 1) begin
                    for (k = 0; k < 4; k = k + 1)
                        beat[32*k +: 32] = {8'hA5, img(4*b+k, y), img(4*b+k, y), img(4*b+k, y)};
                    while (($random(gap_seed) & 3) == 0) begin   // ~25 % idle clocks
                        @(posedge pclk); s_tvalid <= 0; s_tlast <= 0;
                    end
                    @(posedge pclk); s_tvalid <= 1; s_tdata <= beat; s_tlast <= (b == BEATS - 1);
                end
            @(posedge pclk); s_tvalid <= 0; s_tlast <= 0;
            repeat (60) @(posedge pclk);                  // vertical blanking
        end
    endtask

    // ---------------- sink: capture the output of the frame being checked
    reg [7:0] outpix [0:W*H-1];
    integer out_beats = 0, out_frame = 0, ob = 0;
    reg capture = 0;
    // every frame start must reach the frame writer exactly once, also across mode switches
    integer in_sof = 0, out_sof = 0;
    reg in_u_d = 0, out_u_d = 0;
    always @(posedge pclk) begin
        in_u_d <= s_tuser; out_u_d <= m_tuser;
        if (s_tuser && !in_u_d) in_sof = in_sof + 1;
        if (m_tuser && !out_u_d) out_sof = out_sof + 1;
    end
    integer capture_mode = 0;   // mode the captured frame is expected in (alpha check)
    always @(posedge pclk) begin
        if (m_tuser) begin out_beats = 0; ob = 0; end
        if (m_tvalid) begin
            out_beats = out_beats + 1;
            if (capture && ob < W * H / 4) begin
                outpix[4*ob]   = m_tdata[7:0];   outpix[4*ob+1] = m_tdata[39:32];
                outpix[4*ob+2] = m_tdata[71:64]; outpix[4*ob+3] = m_tdata[103:96];
                check(m_tdata[31:24] == (capture_mode == 0 ? 8'hA5 : 8'h00) && m_tdata[15:8] == m_tdata[7:0] &&
                      m_tdata[23:16] == m_tdata[7:0], "output pixel = camera, or {0, v, v, v} when filtered");
            end
            ob = ob + 1;
        end
    end

    // expected value of output pixel at stream position (x, y) for a mode: the result of centre (x-1, y-1)
    function [7:0] expect_out(input integer mode, input integer x, input integer y);
        integer cx, cy, s;
        begin
            cx = x - 1; cy = y - 1;
            case (mode)
                1: expect_out = px(cx, cy);
                2: expect_out = gauss(cx, cy);
                3: expect_out = gauss(cx, cy) >= 128 ? 8'hFF : 8'h00;
                4: begin s = sobel(cx, cy); expect_out = s > 255 ? 8'hFF : s; end
                5: expect_out = sobel(cx, cy) >= 300 ? 8'hFF : 8'h00;
                default: expect_out = img(x, y);
            endcase
        end
    endfunction

    integer mode, x, y, bad, area, sx, sy, x0, x1, y0, y1, edges, cx, cy, latency;
    reg [31:0] r;
    reg [8*72-1:0] msg;
    time t_in, t_out;

    initial begin
        repeat (5) @(posedge clk);
        rreg(8'h14, r); check(r == 32'h56495331, "ID");
        // latency: first input valid beat -> first output valid beat
        fork
            begin @(posedge s_tvalid); t_in = $time; end
            begin @(posedge m_tvalid); t_out = $time; end
        join_none
        send_frame;                                       // warm-up frame (fills the row buffer's "row above 0")
        latency = (t_out - t_in) / 8;
        $sformat(msg, "camera bypass: 1-clock latency (%0d)", latency);
        check(latency == 1, msg);
        check(out_beats == W * H / 4, "every input beat comes out");

        for (mode = 0; mode <= 5; mode = mode + 1) begin
            wreg(8'h00, {23'd0, 1'b1, 5'd0, mode[2:0]});  // FULL statistics
            wreg(8'h04, {5'd0, 11'd300, 8'd0, 8'd128});
            wreg(8'h10, 32'd1);                           // apply at the next frame start
            repeat (10) @(posedge pclk);
            capture_mode = mode;
            capture = 1;
            send_frame;
            capture = 0;
            bad = 0;
            for (y = 1; y < H; y = y + 1)
                for (x = (mode == 0 ? 0 : 1); x < W; x = x + 1)
                    if (outpix[y*W + x] !== expect_out(mode, x, y)) begin
                        if (bad < 3) $display("  mode %0d (%0d,%0d): got %0d want %0d", mode, x, y,
                                              outpix[y*W + x], expect_out(mode, x, y));
                        bad = bad + 1;
                    end
            $sformat(msg, "mode %0d output pixels match the model (%0d bad)", mode, bad);
            check(bad == 0, msg);
        end

        // statistics of the last frame (published at the next frame start): model over all centres
        send_frame;
        area = 0; sx = 0; sy = 0; edges = 0; x0 = 2047; x1 = 0; y0 = 2047; y1 = 0;
        for (cy = 0; cy <= H - 2; cy = cy + 1)
            for (cx = 0; cx <= W - 2; cx = cx + 1) begin
                if (gauss(cx, cy) >= 128) begin
                    area = area + 1; sx = sx + cx; sy = sy + cy;
                    if (cx < x0) x0 = cx; if (cx > x1) x1 = cx; if (cy < y0) y0 = cy; if (cy > y1) y1 = cy;
                end
                if (sobel(cx, cy) >= 300) edges = edges + 1;
            end
        repeat (20) @(posedge clk);
        rreg(8'h24, r); $sformat(msg, "AREA %0d (model %0d, rectangle 72 minus 4 corners)", r, area); check(r == area, msg);
        rreg(8'h28, r); check(r[10:0] == x0 && r[26:16] == x1, "BBOX_X");
        rreg(8'h2C, r); check(r[10:0] == y0 && r[26:16] == y1, "BBOX_Y");
        rreg(8'h30, r); check(r == sx, "SUMX");
        rreg(8'h34, r); check(r == sy, "SUMY");
        rreg(8'h38, r); $sformat(msg, "EDGES %0d (model %0d)", r, edges); check(r == edges, msg);
        rreg(8'h3C, r); $sformat(msg, "CYCLES %0d per frame", r); check(r > W * H / 4 && r < 4 * W * H, msg);
        rreg(8'h40, r); check(r[2:0] == 3'd5 && r[8] == 1'b1 && r[23:16] == 8'd128, "ACTIVE settings");
        $display("  area %0d bbox x %0d..%0d y %0d..%0d centroid (%0.2f, %0.2f) edges %0d", area, x0, x1, y0, y1,
                 1.0 * sx / area, 1.0 * sy / area, edges);

        // ROI: only the left half of the rectangle (x 0..15) -> area / bbox follow
        wreg(8'h00, {23'd0, 1'b0, 5'd0, 3'd3});
        wreg(8'h08, {5'd0, 11'd15, 5'd0, 11'd0});
        wreg(8'h0C, {5'd0, 11'd10, 5'd0, 11'd0});
        wreg(8'h10, 32'd1);
        repeat (10) @(posedge pclk);                      // the APPLY toggle crosses into the pixel clock
        send_frame;   // settings active from this frame
        send_frame;   // ... whose statistics are published at this frame start
        area = 0; x1 = 0;
        for (cy = 0; cy <= H - 2; cy = cy + 1)
            for (cx = 0; cx <= 15; cx = cx + 1)
                if (gauss(cx, cy) >= 128) begin area = area + 1; if (cx > x1) x1 = cx; end
        repeat (20) @(posedge clk);
        rreg(8'h24, r); $sformat(msg, "ROI AREA %0d (model %0d)", r, area); check(r == area, msg);
        rreg(8'h28, r); check(r[26:16] == x1 && x1 == 15, "ROI clips the bounding box at X1");

        // settings written mid-frame do not change the frame in progress
        capture_mode = 3;
        capture = 1;
        fork
            send_frame;
            begin
                repeat (40) @(posedge pclk);              // inside the frame
                wreg(8'h00, {23'd0, 1'b1, 5'd0, 3'd0});   // camera mode
                wreg(8'h10, 32'd1);
            end
        join
        capture = 0;
        bad = 0;
        for (y = 1; y < H; y = y + 1)
            for (x = 1; x < W; x = x + 1)
                if (outpix[y*W + x] !== expect_out(3, x, y)) bad = bad + 1;
        check(bad == 0, "mid-frame APPLY waits for the next frame start");
        capture_mode = 0;
        capture = 1;
        send_frame;
        capture = 0;
        bad = 0;
        for (y = 0; y < H; y = y + 1)
            for (x = 0; x < W; x = x + 1)
                if (outpix[y*W + x] !== img(x, y)) bad = bad + 1;
        check(bad == 0, "... and applies at the next one (camera passthrough)");

        repeat (20) @(posedge pclk);
        $sformat(msg, "one frame start out per frame start in (%0d / %0d)", out_sof, in_sof);
        check(out_sof == in_sof, msg);
        $display("des_vision: %0d passed, %0d failed", passes, fails);
        $finish;
    end
endmodule
