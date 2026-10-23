`timescale 1ns/1ps
// verifier-isp 独立对抗性等价 TB：ui_isp 前端 9 个厂家模块（与 dev 的测试无关）
//   BLC x2(默认 offset / 极端 offset 0,1023,512,1)        image_correction x1(真实 480 列行结构)
//   image_cut x2(小尺寸 + 极端 SKIP，快速命中原/越界比较器) Brightness x2(isp_top 值 / 大 ADD 溢出钳位)
//   contrast x2(默认 160 / 255 极值)                       Saturation x2(isp_top 130 / 负号 383)
//   data128_96 / data96_128（组合，逐拍全图案）
//   raw10_unpacket_4lane（随机 csi burst + frame_start/end）
// 每拍写全部 DUT 全部输出；覆盖随机 valid 缺口、tlast/tuser 边界与错位、随机 ready、中途复位。
module tb_x_isp_front;
    `include "equiv_tb_common.vh"
    reg clk = 0;
    reg rst_n = 0;
    always #5 clk = ~clk;

    // =========================================================== BLC
    reg  [39:0] blc_a_data = 0, blc_b_data = 0;
    reg  [9:0]  blc_a_dest = 0, blc_b_dest = 0;
    reg         blc_a_last = 0, blc_a_user = 0, blc_a_valid = 0, blc_a_ready = 0;
    reg         blc_b_last = 0, blc_b_user = 0, blc_b_valid = 0, blc_b_ready = 0;
    wire        blc_a_tready, blc_a_otlast, blc_a_otuser, blc_a_otvalid;
    wire [39:0] blc_a_otdata;
    wire [9:0]  blc_a_otdest;
    wire        blc_b_tready, blc_b_otlast, blc_b_otuser, blc_b_otvalid;
    wire [39:0] blc_b_otdata;
    wire [9:0]  blc_b_otdest;
    BLC #(.Black_level_offset_r0(50), .Black_level_offset_r1(50),
          .Black_level_offset_r2(50), .Black_level_offset_r3(50)) blc_a (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_tlast(blc_a_last), .I_tuser(blc_a_user), .I_tdata(blc_a_data),
        .I_tvalid(blc_a_valid), .I_tdest(blc_a_dest), .I_tready(blc_a_tready),
        .O_tlast(blc_a_otlast), .O_tuser(blc_a_otuser), .O_tdata(blc_a_otdata),
        .O_tvalid(blc_a_otvalid), .O_tdest(blc_a_otdest), .O_tready(blc_a_ready)
    );
    BLC #(.Black_level_offset_r0(0), .Black_level_offset_r1(1023),
          .Black_level_offset_r2(512), .Black_level_offset_r3(1)) blc_b (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_tlast(blc_b_last), .I_tuser(blc_b_user), .I_tdata(blc_b_data),
        .I_tvalid(blc_b_valid), .I_tdest(blc_b_dest), .I_tready(blc_b_tready),
        .O_tlast(blc_b_otlast), .O_tuser(blc_b_otuser), .O_tdata(blc_b_otdata),
        .O_tvalid(blc_b_otvalid), .O_tdest(blc_b_otdest), .O_tready(blc_b_ready)
    );

    // =========================================================== image_correction
    reg  [39:0] ic_data = 0;
    reg         ic_valid = 0, ic_fs = 0, ic_fe = 0, ic_ready = 0;
    wire [39:0] ic_otdata;
    wire        ic_otlast, ic_otvalid, ic_otuser, ic_tready;
    wire [9:0]  ic_otdest;
    image_correction ic (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_raw_data(ic_data), .I_raw_valid(ic_valid),
        .I_raw_frame_start(ic_fs), .I_raw_frame_end(ic_fe),
        .O_raw_tdata(ic_otdata), .O_raw_tlast(ic_otlast), .O_raw_tdest(ic_otdest),
        .O_raw_tvalid(ic_otvalid), .O_raw_tuser(ic_otuser), .O_raw_tready(ic_ready)
    );

    // =========================================================== image_cut x2
    reg  [95:0] cut_a_data = 0, cut_b_data = 0;
    reg         cut_a_last = 0, cut_a_user = 0, cut_a_valid = 0, cut_a_ready = 0;
    reg         cut_b_last = 0, cut_b_user = 0, cut_b_valid = 0, cut_b_ready = 0;
    wire [95:0] cut_a_otdata, cut_b_otdata;
    wire        cut_a_tready, cut_a_otlast, cut_a_otuser, cut_a_otvalid;
    wire        cut_b_tready, cut_b_otlast, cut_b_otuser, cut_b_otvalid;
    image_cut #(.IMG_WIDTH(8), .IMG_HEIGHT(4), .DATA_WIDTH(96),
                .SKIP_ROWS_top(1), .SKIP_ROWS_bottom(1),
                .SKIP_COLS_left(1), .SKIP_COLS_right(0)) cut_a (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_tlast(cut_a_last), .I_tuser(cut_a_user), .I_tdata(cut_a_data),
        .I_tvalid(cut_a_valid), .I_tready(cut_a_tready),
        .O_tlast(cut_a_otlast), .O_tuser(cut_a_otuser), .O_tdata(cut_a_otdata),
        .O_tvalid(cut_a_otvalid), .O_tready(cut_a_ready)
    );
    image_cut #(.IMG_WIDTH(8), .IMG_HEIGHT(4), .DATA_WIDTH(96),
                .SKIP_ROWS_top(0), .SKIP_ROWS_bottom(4),
                .SKIP_COLS_left(0), .SKIP_COLS_right(2)) cut_b (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_tlast(cut_b_last), .I_tuser(cut_b_user), .I_tdata(cut_b_data),
        .I_tvalid(cut_b_valid), .I_tready(cut_b_tready),
        .O_tlast(cut_b_otlast), .O_tuser(cut_b_otuser), .O_tdata(cut_b_otdata),
        .O_tvalid(cut_b_otvalid), .O_tready(cut_b_ready)
    );

    // =========================================================== Brightness x2
    reg  [95:0] br_a_data = 0, br_b_data = 0;
    reg         br_a_last = 0, br_a_user = 0, br_a_valid = 0, br_a_ready = 0;
    reg         br_b_last = 0, br_b_user = 0, br_b_valid = 0, br_b_ready = 0;
    wire [95:0] br_a_otdata, br_b_otdata;
    wire        br_a_tready, br_a_otlast, br_a_otuser, br_a_otvalid;
    wire        br_b_tready, br_b_otlast, br_b_otuser, br_b_otvalid;
    Brightness_adjustment #(.BRIGHTNESS_ADD(20), .BRIGHTNESS_MINUS(0)) br_a (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_tlast(br_a_last), .I_tuser(br_a_user), .I_tdata(br_a_data),
        .I_tvalid(br_a_valid), .I_tready(br_a_tready),
        .O_tlast(br_a_otlast), .O_tuser(br_a_otuser), .O_tdata(br_a_otdata),
        .O_tvalid(br_a_otvalid), .O_tready(br_a_ready)
    );
    Brightness_adjustment #(.BRIGHTNESS_ADD(300), .BRIGHTNESS_MINUS(100)) br_b (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_tlast(br_b_last), .I_tuser(br_b_user), .I_tdata(br_b_data),
        .I_tvalid(br_b_valid), .I_tready(br_b_tready),
        .O_tlast(br_b_otlast), .O_tuser(br_b_otuser), .O_tdata(br_b_otdata),
        .O_tvalid(br_b_otvalid), .O_tready(br_b_ready)
    );

    // =========================================================== contrast x2
    reg  [95:0] ct_a_data = 0, ct_b_data = 0;
    reg         ct_a_last = 0, ct_a_user = 0, ct_a_valid = 0, ct_a_ready = 0;
    reg         ct_b_last = 0, ct_b_user = 0, ct_b_valid = 0, ct_b_ready = 0;
    wire [95:0] ct_a_otdata, ct_b_otdata;
    wire        ct_a_tready, ct_a_otlast, ct_a_otuser, ct_a_otvalid;
    wire        ct_b_tready, ct_b_otlast, ct_b_otuser, ct_b_otvalid;
    contrast_adj #(.contrast_level(160)) ct_a (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_tlast(ct_a_last), .I_tuser(ct_a_user), .I_tdata(ct_a_data),
        .I_tvalid(ct_a_valid), .I_tready(ct_a_tready),
        .O_tlast(ct_a_otlast), .O_tuser(ct_a_otuser), .O_tdata(ct_a_otdata),
        .O_tvalid(ct_a_otvalid), .O_tready(ct_a_ready)
    );
    contrast_adj #(.contrast_level(255)) ct_b (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_tlast(ct_b_last), .I_tuser(ct_b_user), .I_tdata(ct_b_data),
        .I_tvalid(ct_b_valid), .I_tready(ct_b_tready),
        .O_tlast(ct_b_otlast), .O_tuser(ct_b_otuser), .O_tdata(ct_b_otdata),
        .O_tvalid(ct_b_otvalid), .O_tready(ct_b_ready)
    );

    // =========================================================== Saturation x2
    reg  [95:0] sa_a_data = 0, sa_b_data = 0;
    reg         sa_a_last = 0, sa_a_user = 0, sa_a_valid = 0, sa_a_ready = 0;
    reg         sa_b_last = 0, sa_b_user = 0, sa_b_valid = 0, sa_b_ready = 0;
    wire [95:0] sa_a_otdata, sa_b_otdata;
    wire        sa_a_tready, sa_a_otlast, sa_a_otuser, sa_a_otvalid;
    wire        sa_b_tready, sa_b_otlast, sa_b_otuser, sa_b_otvalid;
    Saturation_adj #(.ADJUST_VAL(130)) sa_a (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_tlast(sa_a_last), .I_tuser(sa_a_user), .I_tdata(sa_a_data),
        .I_tvalid(sa_a_valid), .I_tready(sa_a_tready),
        .O_tlast(sa_a_otlast), .O_tuser(sa_a_otuser), .O_tdata(sa_a_otdata),
        .O_tvalid(sa_a_otvalid), .O_tready(sa_a_ready)
    );
    Saturation_adj #(.ADJUST_VAL(9'd383)) sa_b (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_tlast(sa_b_last), .I_tuser(sa_b_user), .I_tdata(sa_b_data),
        .I_tvalid(sa_b_valid), .I_tready(sa_b_tready),
        .O_tlast(sa_b_otlast), .O_tuser(sa_b_otuser), .O_tdata(sa_b_otdata),
        .O_tvalid(sa_b_otvalid), .O_tready(sa_b_ready)
    );

    // =========================================================== data width converters (comb)
    reg  [127:0] d128_data = 0;
    reg  [95:0]  d96_data = 0;
    wire [95:0]  d128_otdata;
    wire [127:0] d96_otdata;
    data128_96 d128 (.I_tdata(d128_data), .O_tdata(d128_otdata));
    data96_128 u_d96 (.I_tdata(d96_data),  .O_tdata(d96_otdata));

    // =========================================================== raw10_unpacket_4lane
    reg  [31:0] r10_csi = 0;
    reg         r10_fs = 0, r10_fe = 0, r10_valid = 0;
    wire        r10_ofs, r10_ofe, r10_ovalid;
    wire [39:0] r10_odata;
    raw10_unpacket_4lane r10 (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_csi_frame_start(r10_fs), .I_csi_frame_end(r10_fe),
        .I_csi_valid(r10_valid), .I_csi_data(r10_csi),
        .O_raw10_frame_start(r10_ofs), .O_raw10_frame_end(r10_ofe),
        .O_raw10_valid(r10_ovalid), .O_raw10_data(r10_odata)
    );

    // =========================================================== trace
    integer traced = 0;
    initial equiv_trace_open;
    always @(posedge clk) begin
        cyc = cyc + 1;
        traced = traced + 1;
        $fdisplay(fd, "%0d | %b %b %b %b %b %b | %b %b %b %b %b %b | %b %b %b %b %b %b | %b %b %b %b %b | %b %b %b %b %b | %b %b %b %b %b | %b %b %b %b %b | %b %b %b %b %b | %b %b %b %b %b | %b %b %b %b %b | %b %b %b %b %b | %b %b | %b %b %b %b",
            cyc,
            // BLC a: I_tready, O_tlast, O_tuser, O_tdata, O_tvalid, O_tdest
            blc_a_tready, blc_a_otlast, blc_a_otuser, blc_a_otdata, blc_a_otvalid, blc_a_otdest,
            // BLC b
            blc_b_tready, blc_b_otlast, blc_b_otuser, blc_b_otdata, blc_b_otvalid, blc_b_otdest,
            // image_correction: I_raw_tready, O_raw_tlast, O_raw_tuser, O_raw_tdata, O_raw_tvalid, O_raw_tdest
            ic_tready, ic_otlast, ic_otuser, ic_otdata, ic_otvalid, ic_otdest,
            // image_cut a
            cut_a_tready, cut_a_otlast, cut_a_otuser, cut_a_otdata, cut_a_otvalid,
            // image_cut b
            cut_b_tready, cut_b_otlast, cut_b_otuser, cut_b_otdata, cut_b_otvalid,
            // Brightness a
            br_a_tready, br_a_otlast, br_a_otuser, br_a_otdata, br_a_otvalid,
            // Brightness b
            br_b_tready, br_b_otlast, br_b_otuser, br_b_otdata, br_b_otvalid,
            // contrast a
            ct_a_tready, ct_a_otlast, ct_a_otuser, ct_a_otdata, ct_a_otvalid,
            // contrast b
            ct_b_tready, ct_b_otlast, ct_b_otuser, ct_b_otdata, ct_b_otvalid,
            // Saturation a
            sa_a_tready, sa_a_otlast, sa_a_otuser, sa_a_otdata, sa_a_otvalid,
            // Saturation b
            sa_b_tready, sa_b_otlast, sa_b_otuser, sa_b_otdata, sa_b_otvalid,
            // data128_96 / data96_128
            d128_otdata, d96_otdata,
            // raw10_unpacket_4lane
            r10_ofs, r10_ofe, r10_ovalid, r10_odata);
    end
    // =========================================================== stimulus
    integer seed_a = 32'h1111_2222;
    integer seed_b = 32'h3333_4444;
    integer seed_c = 32'h5555_6666;
    integer ia, ka, ib, kb, ic_i, ic_k, ci, bi, xi, yi, di, ri, rk;
    reg [39:0] d40;

    // deterministic pseudo-random 96-bit data word
    function [95:0] rand96(input integer sd);
        rand96 = {$random(sd), $random(sd), $random(sd)};
    endfunction

    initial begin   // BLC a: 8-beat bursts with holes
        repeat (8) @(posedge clk);
        for (ia = 0; ia < 90; ia = ia + 1) begin
            for (ka = 0; ka < 8; ka = ka + 1) begin
                @(posedge clk);
                blc_a_valid <= 1'b1;
                blc_a_data  <= {$random(seed_a), seed_a[7:0]};
                blc_a_dest  <= $random(seed_a);
                blc_a_last  <= (ka == 7);
                blc_a_user  <= (ka == 0) && (ia == 0);
                blc_a_ready <= $random(seed_a);
            end
            @(posedge clk);
            blc_a_valid <= 1'b0; blc_a_last <= 1'b0; blc_a_user <= 1'b0;
            if (($random(seed_a) & 1) == 0) repeat (1 + ($random(seed_a) & 3)) @(posedge clk);
        end
    end

    initial begin   // BLC b: random holes, extreme data words
        repeat (8) @(posedge clk);
        for (ib = 0; ib < 500; ib = ib + 1) begin
            @(posedge clk);
            blc_b_ready <= $random(seed_b);
            if (($random(seed_b) & 3) == 0) begin
                blc_b_valid <= 1'b0; blc_b_last <= 1'b0; blc_b_user <= 1'b0;
            end else begin
                blc_b_valid <= 1'b1;
                case ($random(seed_b) & 3)
                    0: blc_b_data <= 40'h0;
                    1: blc_b_data <= {40{1'b1}};
                    2: blc_b_data <= 40'hff_000_3ff_000_3ff;
                    default: blc_b_data <= {$random(seed_b), seed_b[7:0]};
                endcase
                blc_b_dest <= $random(seed_b);
                blc_b_last <= (($random(seed_b) & 15) == 0);
                blc_b_user <= (($random(seed_b) & 63) == 0);
            end
        end
    end

    initial begin   // image_correction: 4 lines of 480 valid beats + 14 idle (triggers `last`)
        repeat (10) @(posedge clk);
        ic_ready <= 1'b1;
        ic_fs <= 1'b1;
        @(posedge clk);
        ic_fs <= 1'b0;
        for (ic_i = 0; ic_i < 4; ic_i = ic_i + 1) begin
            for (ic_k = 0; ic_k < 480; ic_k = ic_k + 1) begin
                @(posedge clk);
                ic_valid <= 1'b1;
                ic_data  <= {$random(seed_c), seed_c[7:0]};
                ic_ready <= $random(seed_c);
            end
            @(posedge clk);
            ic_valid <= 1'b0;
            repeat (14) @(posedge clk);
            // occasional long gap in the middle of line 3 to exercise cnt/last
            if (ic_i == 2) begin
                ic_fe <= 1'b1;
                @(posedge clk);
                ic_fe <= 1'b0;
                repeat (25) @(posedge clk);
            end
        end
        @(posedge clk);
        ic_fe <= 1'b1;
        @(posedge clk);
        ic_fe <= 1'b0;
    end

    initial begin   // image_cut a/b: random stream, tuser every 2/3 beats
        repeat (8) @(posedge clk);
        for (ci = 0; ci < 500; ci = ci + 1) begin
            @(posedge clk);
            cut_a_ready <= $random(seed_a);
            cut_b_ready <= $random(seed_b);
            if (($random(seed_a) & 3) == 0) begin
                cut_a_valid <= 1'b0; cut_a_last <= 1'b0; cut_a_user <= 1'b0;
            end else begin
                cut_a_valid <= 1'b1;
                cut_a_data  <= rand96(seed_a);
                cut_a_last  <= (($random(seed_a) & 7) == 0);
                cut_a_user  <= ((ci % 2) == 0) && (($random(seed_a) & 3) != 0);
            end
            if (($random(seed_b) & 7) == 0) begin
                cut_b_valid <= 1'b0; cut_b_last <= 1'b0; cut_b_user <= 1'b0;
            end else begin
                cut_b_valid <= 1'b1;
                cut_b_data  <= (($random(seed_b) & 3) == 0) ? 96'h0 : rand96(seed_b);
                cut_b_last  <= (($random(seed_b) & 3) == 0);
                cut_b_user  <= ((ci % 3) == 0);
            end
        end
    end

    initial begin   // Brightness a/b
        repeat (8) @(posedge clk);
        for (bi = 0; bi < 500; bi = bi + 1) begin
            @(posedge clk);
            br_a_ready <= $random(seed_a);
            br_b_ready <= $random(seed_b);
            br_a_valid <= (($random(seed_a) & 3) != 0);
            br_b_valid <= (($random(seed_b) & 3) != 0);
            br_a_data  <= rand96(seed_a);
            br_b_data  <= (($random(seed_b) & 3) == 0) ? {96{1'b1}} : rand96(seed_b);
            br_a_last  <= (($random(seed_a) & 15) == 0);
            br_a_user  <= (($random(seed_a) & 31) == 0);
            br_b_last  <= (($random(seed_b) & 15) == 0);
            br_b_user  <= (($random(seed_b) & 31) == 0);
        end
    end

    initial begin   // contrast a/b
        repeat (8) @(posedge clk);
        for (xi = 0; xi < 500; xi = xi + 1) begin
            @(posedge clk);
            ct_a_ready <= $random(seed_a);
            ct_b_ready <= $random(seed_b);
            ct_a_valid <= (($random(seed_a) & 3) != 0);
            ct_b_valid <= (($random(seed_b) & 3) != 0);
            ct_a_data  <= rand96(seed_a);
            ct_b_data  <= (($random(seed_b) & 3) == 0) ? 96'h0 : rand96(seed_b);
            ct_a_last  <= (($random(seed_a) & 15) == 0);
            ct_a_user  <= (($random(seed_a) & 31) == 0);
            ct_b_last  <= (($random(seed_b) & 15) == 0);
            ct_b_user  <= (($random(seed_b) & 31) == 0);
        end
    end

    initial begin   // Saturation a/b
        repeat (8) @(posedge clk);
        for (yi = 0; yi < 500; yi = yi + 1) begin
            @(posedge clk);
            sa_a_ready <= $random(seed_a);
            sa_b_ready <= $random(seed_b);
            sa_a_valid <= (($random(seed_a) & 3) != 0);
            sa_b_valid <= (($random(seed_b) & 3) != 0);
            sa_a_data  <= rand96(seed_a);
            sa_b_data  <= (($random(seed_b) & 3) == 0) ? {96{1'b1}} : rand96(seed_b);
            sa_a_last  <= (($random(seed_a) & 15) == 0);
            sa_a_user  <= (($random(seed_a) & 31) == 0);
            sa_b_last  <= (($random(seed_b) & 15) == 0);
            sa_b_user  <= (($random(seed_b) & 31) == 0);
        end
    end

    initial begin   // width converters: every-cycle patterns
        repeat (8) @(posedge clk);
        for (di = 0; di < 3000; di = di + 1) begin
            @(posedge clk);
            d128_data <= {$random(seed_a), $random(seed_a), $random(seed_a), $random(seed_a)};
            d96_data  <= {$random(seed_b), $random(seed_b), $random(seed_b)};
            if (di == 17) d128_data <= 128'h0;
            if (di == 18) d128_data <= {128{1'b1}};
            if (di == 19) d96_data <= 96'h0;
            if (di == 20) d96_data <= {96{1'b1}};
        end
    end

    initial begin   // raw10 unpacket: csi bursts with frame start/end
        repeat (8) @(posedge clk);
        for (ri = 0; ri < 80; ri = ri + 1) begin
            @(posedge clk);
            r10_fs <= 1'b1;
            r10_valid <= 1'b0;
            @(posedge clk);
            r10_fs <= 1'b0;
            for (rk = 0; rk < 6; rk = rk + 1) begin
                @(posedge clk);
                r10_valid <= 1'b1;
                r10_csi   <= {$random(seed_c), seed_c[7:0]};
            end
            @(posedge clk);
            r10_valid <= 1'b0;
            @(posedge clk);
            r10_fe <= 1'b1;
            @(posedge clk);
            r10_fe <= 1'b0;
            repeat (4) @(posedge clk);
        end
    end

    // =========================================================== timeline (shared reset + end)
    initial begin
        repeat (6) @(posedge clk);
        rst_n <= 1'b1;
        repeat (1700) @(posedge clk);
        rst_n <= 1'b0;
        repeat (4) @(posedge clk);
        rst_n <= 1'b1;
        repeat (1300) @(posedge clk);
        if (traced != cyc) begin
            $display("x_isp_front: trace lines %0d != cycles %0d", traced, cyc);
            $finish;
        end
        equiv_finish(cyc);
    end
endmodule
