`timescale 1ns/1ps
// 等价回归 TB：demosaic 整链（top_demosaic.v = mipi_to_raw_converter + zhenghe + 2x line_buffer + raw_matrix_3x3 + bilinear_interpolation）。
// 两个实例共用同一路 AXI4-Stream 激励，参数覆盖：
//   dut_a: IMG 32x6, data_complete_delay=5   —— 小参数，行/列/行号快速回绕，多帧 + 复位恢复
//   dut_b: IMG 1920x8, data_complete_delay=50 —— 实际宽度（480 组/行），zhenghe valid_width 两分支、11 位地址满回绕
// 激励：干净多帧（数据极值 0x00/0xFF/0xAA/0x55/递增）、随机 tvalid 空泡、随机 tuser/tlast 边界、随机 O_tready、中途复位。
module tb_demosaic;
    integer fd;
    integer cyc = 0;
    reg [1023:0] tf;
    integer seed = 32'h3c9e_51b7;

    reg clk = 0;
    reg rst_n = 0;

    reg  [39:0] in_tdata  = 40'b0;
    reg  [9:0]  in_tdest  = 10'b0;
    reg         in_tlast  = 1'b0;
    reg         in_tvalid = 1'b0;
    reg         in_tuser  = 1'b0;
    reg         o_tready_a = 1'b1;
    reg         o_tready_b = 1'b1;

    wire        in_ready_a, in_ready_b;
    wire        a_tlast, a_tuser, a_tvalid;
    wire [127:0] a_tdata;
    wire        b_tlast, b_tuser, b_tvalid;
    wire [127:0] b_tdata;

    always #5 clk = ~clk;

    demosaic #(
        .IMG_HEIGHT(6), .IMG_WIDTH(32), .data_complete_delay(5), .BAYER_MODE("RGGB")
    ) dut_a (
        .I_clk(clk), .I_rst_n(rst_n),
        .axi4s_video_tdata(in_tdata), .axi4s_video_tdest(in_tdest),
        .axi4s_video_tlast(in_tlast), .axi4s_video_tvalid(in_tvalid), .axi4s_video_tuser(in_tuser),
        .axi4s_video_tready(in_ready_a),
        .O_tlast(a_tlast), .O_tuser(a_tuser), .O_tdata(a_tdata), .O_tvalid(a_tvalid), .O_tready(o_tready_a));

    demosaic #(
        .IMG_HEIGHT(8), .IMG_WIDTH(1920), .data_complete_delay(50), .BAYER_MODE("RGGB")
    ) dut_b (
        .I_clk(clk), .I_rst_n(rst_n),
        .axi4s_video_tdata(in_tdata), .axi4s_video_tdest(in_tdest),
        .axi4s_video_tlast(in_tlast), .axi4s_video_tvalid(in_tvalid), .axi4s_video_tuser(in_tuser),
        .axi4s_video_tready(in_ready_b),
        .O_tlast(b_tlast), .O_tuser(b_tuser), .O_tdata(b_tdata), .O_tvalid(b_tvalid), .O_tready(o_tready_b));

    always @(negedge clk) begin
        #2;
        if (fd != 0) begin
            $fdisplay(fd, "%0d %b %b %h %b %b | %b %b %h %b %b",
                cyc, a_tlast, a_tuser, a_tdata, a_tvalid, in_ready_a,
                     b_tlast, b_tuser, b_tdata, b_tvalid, in_ready_b);
        end
        cyc = cyc + 1;
    end

    task beat(input [39:0] d, input last, input user, input valid);
        begin
            @(negedge clk);
            in_tdata = d;
            in_tdest = $random(seed);
            in_tlast = last;
            in_tuser = user;
            in_tvalid = valid;
            o_tready_a = (($random(seed) % 4) != 0);
            o_tready_b = (($random(seed) % 8) != 0);
        end
    endtask

    task quiet(input integer n);
        integer i;
        begin
            for (i = 0; i < n; i = i + 1) beat(40'b0, 1'b0, 1'b0, 1'b0);
        end
    endtask

    function [39:0] pat(input integer kind, input integer r, input integer c);
        begin
            case ((kind + r) % 5)
                0: pat = 40'hFF_FFFF_FFFF;
                1: pat = 40'h00_0000_0000;
                2: pat = 40'hAA_AAAA_AAAA;
                3: pat = 40'h55_5555_5555;
                default: pat = {r[9:0], c[9:0], r[9:0], c[9:0]};
            endcase
        end
    endfunction

    // 每行 chunks 个有效拍（tlast 在最后一拍）+ gap 个空泡；kind 选择数据极值/递增
    task frame(input integer rows, input integer chunks, input integer gap, input integer kind);
        integer r, c;
        begin
            for (r = 0; r < rows; r = r + 1) begin
                for (c = 0; c < chunks; c = c + 1) begin
                    beat(pat(kind, r, c), (c == chunks - 1), (r == 0) && (c == 0), 1'b1);
                end
                quiet(gap);
            end
        end
    endtask

    task rand_stream(input integer n, input integer valid_pct, input integer last_pct, input integer user_pct);
        integer i;
        reg [39:0] d;
        reg l, u, v;
        begin
            for (i = 0; i < n; i = i + 1) begin
                d = {$random(seed), $random(seed)};
                l = (($random(seed) % 100) < last_pct);
                u = (($random(seed) % 100) < user_pct);
                v = (($random(seed) % 100) < valid_pct);
                beat(d, l, u, v);
            end
        end
    endtask

    task do_reset(input integer n);
        integer i;
        begin
            @(negedge clk); rst_n = 1'b0;
            for (i = 0; i < n; i = i + 1) beat(40'b0, 1'b0, 1'b0, 1'b0);
            @(negedge clk); rst_n = 1'b1;
        end
    endtask

    initial begin
        if (!$value$plusargs("TRACE=%s", tf)) tf = "trace.txt";
        fd = $fopen(tf, "w");
        if (fd == 0) begin $display("cannot open trace"); $finish; end

        // 1) 上电复位
        @(negedge clk); rst_n = 1'b0;
        quiet(4);
        @(negedge clk); rst_n = 1'b1;

        // 2) 小参数多帧：无 gap / 有 gap / 行间隙不等
        frame(6, 8, 0, 0);
        frame(6, 8, 1, 2);
        frame(6, 8, 3, 3);

        // 3) 实际宽度帧：480 组/行 + 行后 100 拍 idles（真实相机行消隐）
        //    行周期 ≈ 580 > 52+480，zhenghe 读窗口才能完整覆盖 480 组；同时触发 cnt_first > IMG_WIDTH_4x 分支
        frame(8, 480, 100, 0);
        frame(8, 480, 100, 2);

        // 4) 随机流（tvalid 空泡 ≈40%）
        rand_stream(4000, 60, 2, 1);

        // 5) 随机边界：tuser/tlast 密集成灾（非法组合也要逐拍一致）
        rand_stream(400, 70, 9, 5);

        // 6) 中途复位
        do_reset(3);
        rand_stream(600, 60, 2, 1);

        // 7) 复位后恢复：小帧 + 实际宽度帧
        frame(6, 8, 0, 0);
        frame(8, 480, 100, 3);

        repeat (8) @(negedge clk);
        #200;
        $display("EQUIV_CYCLES %0d", cyc);
        $display("EQUIV_DONE");
        $fclose(fd);
        $finish;
    end
endmodule
