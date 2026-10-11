`timescale 1ns/1ps
// ISP auto white balance: the vendor awb (pipelined divider IPs = a new gain every clock, behavioural model) and
// our patched awb (awb_seq_div back to back = a new gain every 49 clocks) side by side on the same 12 frames of
// 256 x 8 pixels with colour noise and some bright (excluded) pixels. Stream timing (valid / user / last) must match
// on every clock. Pixel data: from frame 1 on (frame 0 starts from sums of 1, where the gains swing) and outside the
// first 4 beats of a frame (the vendor resets its gains to 1.0 at every frame start), every colour channel must be
// within +-2 of the vendor's. Also checks that the white balance works (R and B means close to G).
module tb_awb;
    localparam integer W = 256, H = 8, BEATS = W / 4, FRAMES = 12;
    reg clk = 0, rst_n = 0;
    always #5 clk = ~clk;

    reg  [95:0] tdata = 0;
    reg         tvalid = 0, tuser = 0, tlast = 0;
    wire [95:0] o_ref, o_new;
    wire        v_ref, v_new, u_ref, u_new, l_ref, l_new, rdy_ref, rdy_new;

    awb_orig #(.IMG_HEIGHT(H), .IMG_WIDTH(W)) ref_i (
        .I_clk(clk), .I_rst_n(rst_n), .I_tlast(tlast), .I_tuser(tuser), .I_tdata(tdata), .I_tvalid(tvalid),
        .I_tready(rdy_ref), .O_tlast(l_ref), .O_tuser(u_ref), .O_tdata(o_ref), .O_tvalid(v_ref), .O_tready(1'b1));
    awb #(.IMG_HEIGHT(H), .IMG_WIDTH(W)) new_i (
        .I_clk(clk), .I_rst_n(rst_n), .I_tlast(tlast), .I_tuser(tuser), .I_tdata(tdata), .I_tvalid(tvalid),
        .I_tready(rdy_new), .O_tlast(l_new), .O_tuser(u_new), .O_tdata(o_new), .O_tvalid(v_new), .O_tready(1'b1));

    integer fails = 0, passes = 0;
    task check(input cond, input [8*80-1:0] what);
        begin
            if (cond) begin passes = passes + 1; $display("ok   %0s", what); end
            else begin fails = fails + 1; $display("FAIL: %0s", what); end
        end
    endtask

    // ---------------- source: base colour R 90 G 140 B 70 with +-20 noise, ~3 % bright pixels (sum >= 700)
    integer seed = 11;
    function [7:0] clip(input integer v);
        clip = v < 0 ? 0 : v > 255 ? 255 : v;
    endfunction
    task send_frame;
        integer y, b, k, r, g, bl;
        reg [95:0] beat;
        begin
            @(posedge clk); tuser <= 1;
            @(posedge clk); tuser <= 0;
            repeat (3) @(posedge clk);
            for (y = 0; y < H; y = y + 1) begin
                for (b = 0; b < BEATS; b = b + 1) begin
                    for (k = 0; k < 4; k = k + 1) begin
                        if (($random(seed) & 31) == 0) begin r = 250; g = 250; bl = 250; end   // bright: excluded
                        else begin
                            r = 90 + ($random(seed) % 21);
                            g = 140 + ($random(seed) % 21);
                            bl = 70 + ($random(seed) % 21);
                        end
                        beat[24*k +: 24] = {clip(r), clip(g), clip(bl)};
                    end
                    @(posedge clk); tvalid <= 1; tdata <= beat; tlast <= (b == BEATS - 1);
                end
                @(posedge clk); tvalid <= 0; tlast <= 0;
                repeat (20) @(posedge clk);                 // horizontal blanking
            end
            repeat (200) @(posedge clk);                    // vertical blanking (> the 50-clock divider latency)
        end
    endtask

    // ---------------- compare the two outputs on every clock
    integer frame = -1, obeat = 0, timing_bad = 0, data_bad = 0, data_bad_late = 0, beats = 0, maxdiff = 0;
    integer ch, dv;
    integer sr = 0, sg = 0, sb = 0, n = 0, k2;
    reg [95:0] d;
    always @(posedge clk) if (rst_n) begin
        if (v_ref !== v_new || u_ref !== u_new || l_ref !== l_new) timing_bad = timing_bad + 1;
        if (u_new) begin frame = frame + 1; obeat = 0; end
        if (v_new) begin
            beats = beats + 1;
            if (o_ref !== o_new) data_bad = data_bad + 1;
            if (frame >= 1 && obeat >= 4)
                for (ch = 0; ch < 12; ch = ch + 1) begin
                    dv = o_ref[8*ch +: 8] - o_new[8*ch +: 8];
                    if (dv < 0) dv = -dv;
                    if (dv > maxdiff) maxdiff = dv;
                    if (dv > 2) begin
                        data_bad_late = data_bad_late + 1;
                        if (data_bad_late <= 3) $display("  frame %0d beat %0d: ref %h new %h", frame, obeat, o_ref, o_new);
                    end
                end
            if (frame == FRAMES - 2) begin                  // colour balance of a late frame (new design)
                d = o_new;
                for (k2 = 0; k2 < 4; k2 = k2 + 1)
                    if (d[24*k2 +: 8] < 200) begin          // skip the bright pixels
                        sb = sb + d[24*k2 +: 8]; sg = sg + d[24*k2 + 8 +: 8]; sr = sr + d[24*k2 + 16 +: 8]; n = n + 1;
                    end
            end
            obeat = obeat + 1;
        end
    end

    integer f;
    reg [8*80-1:0] msg;
    initial begin
        repeat (10) @(posedge clk);
        rst_n = 1;
        repeat (10) @(posedge clk);
        for (f = 0; f < FRAMES; f = f + 1) send_frame;
        repeat (50) @(posedge clk);

        $sformat(msg, "output beats %0d = input beats %0d", beats, FRAMES * H * BEATS);
        check(beats == FRAMES * H * BEATS, msg);
        $sformat(msg, "valid / user / last identical on every clock (%0d differ)", timing_bad);
        check(timing_bad == 0, msg);
        $sformat(msg, "pixels within +-2 of the vendor's from frame 1 on (%0d channels off, max diff %0d)",
                 data_bad_late, maxdiff);
        check(data_bad_late == 0, msg);
        $display("  beats not bit-identical (any frame / position): %0d of %0d", data_bad, beats);
        $sformat(msg, "gains applied: mean R %0d G %0d B %0d (inputs 90 / 140 / 70)", sr / n, sg / n, sb / n);
        check(sr / n > 130 && sr / n < 150 && sb / n > 130 && sb / n < 150 && sg / n > 135 && sg / n < 145, msg);
        $sformat(msg, "gain registers left 1.0: R %h B %h", new_i.in_r_r1, new_i.in_b_r1);
        check(new_i.in_r_r1 > 20'h14000 && new_i.in_b_r1 > 20'h18000, msg);
        $display("awb: %0d passed, %0d failed", passes, fails);
        $finish;
    end
endmodule
