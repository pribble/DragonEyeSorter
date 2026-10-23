`timescale 1ns/1ps
// verifier-isp 独立对抗性等价 TB：demosaic 整链（厂家 6 模块）
// 与 dev-demosaic 的 tb/demosaic.v 同一口径：golden top_demosaic.v 有 TD 专有嵌套块注释、Icarus 不认，
// 而该 wrapper 是纯连线无逻辑，故这里按 wrapper 原始连线直接例化 raw_matrix_3x3_buffer + bilinear
// （两个模块的端口在本 TB 里都会成为可见边界，trace 覆盖它们全部输出，比只 trace wrapper 输出更严）。
//   instance A = wrapper 原参数：IMG_HEIGHT=1080 / IMG_WIDTH=1920 / data_complete_delay=50 / RGGB
//                （= isp_top 实际配置，480 beat/行 + 真实行消隐）
//   instance B = IMG 8x64 小参数 + 默认 BGGR：行/列/帧计数器快速回卷，valid_width 分支全覆盖
// 激励：多帧、极值数据、随机 valid 缺口、随机 tlast/tuser 边界、随机 O_tready、中途复位后再来一帧。
// TD blk_mem_gen_*/RTL/ram_*.v 是明文 wrapper（不改），其 DR1_LOGIC_ERAM 原语由共享桩 vendor_eram.v 提供。
module tb_x_demosaic;
    `include "equiv_tb_common.vh"
    reg clk = 0;
    reg rst_n = 0;
    always #5 clk = ~clk;

    // ================= instance A: real parameters =================
    localparam integer AB = 480;   // 1920/4 beats per line
    localparam integer AG = 72;    // horizontal blanking (needed by zhenghe valid_width/data_complete_delay)
    reg  [39:0] a_tdata = 0;
    reg  [9:0]  a_tdest = 0;
    reg         a_tlast = 0, a_tvalid = 0, a_tuser = 0, a_otready = 0;
    wire        a_in_ready, a_rm_tlast, a_rm_tuser, a_rm_tvalid, a_rm_tready, a_ypos;
    wire [95:0] a_mlast, a_mcur, a_mnext;
    wire        a_otlast, a_otuser, a_otvalid;
    wire [127:0] a_otdata;
    raw_matrix_3x3_buffer #(.IMG_HEIGHT(1080), .IMG_WIDTH(1920), .data_complete_delay(50)) rm_a (
        .I_clk(clk), .I_rst_n(rst_n),
        .axi4s_video_tdata(a_tdata), .axi4s_video_tdest(a_tdest),
        .axi4s_video_tlast(a_tlast), .axi4s_video_tvalid(a_tvalid), .axi4s_video_tuser(a_tuser),
        .axi4s_video_tready(a_in_ready),
        .O_tlast(a_rm_tlast), .O_tuser(a_rm_tuser), .O_tvalid(a_rm_tvalid), .O_tready(a_rm_tready),
        .bayer_ypos(a_ypos),
        .matrix_last_line(a_mlast), .matrix_cur_line(a_mcur), .matrix_next_line(a_mnext)
    );
    bilinear_interpolation #(.BAYER_MODE("RGGB")) bl_a (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_tlast(a_rm_tlast), .I_tuser(a_rm_tuser), .I_tvalid(a_rm_tvalid), .I_tready(a_rm_tready),
        .bayer_ypos(a_ypos),
        .matrix_last_line(a_mlast), .matrix_cur_line(a_mcur), .matrix_next_line(a_mnext),
        .O_tlast(a_otlast), .O_tuser(a_otuser), .O_tdata(a_otdata), .O_tvalid(a_otvalid),
        .O_tready(a_otready)
    );

    // ================= instance B: small parameters (counter wrap) =================
    localparam integer BB = 16;    // 64/4 beats per line
    localparam integer BG = 120;   // blanking long enough to fully read one line
    reg  [39:0] b_tdata = 0;
    reg  [9:0]  b_tdest = 0;
    reg         b_tlast = 0, b_tvalid = 0, b_tuser = 0, b_otready = 0;
    wire        b_in_ready, b_rm_tlast, b_rm_tuser, b_rm_tvalid, b_rm_tready, b_ypos;
    wire [95:0] b_mlast, b_mcur, b_mnext;
    wire        b_otlast, b_otuser, b_otvalid;
    wire [127:0] b_otdata;
    raw_matrix_3x3_buffer #(.IMG_HEIGHT(8), .IMG_WIDTH(64), .data_complete_delay(50)) rm_b (
        .I_clk(clk), .I_rst_n(rst_n),
        .axi4s_video_tdata(b_tdata), .axi4s_video_tdest(b_tdest),
        .axi4s_video_tlast(b_tlast), .axi4s_video_tvalid(b_tvalid), .axi4s_video_tuser(b_tuser),
        .axi4s_video_tready(b_in_ready),
        .O_tlast(b_rm_tlast), .O_tuser(b_rm_tuser), .O_tvalid(b_rm_tvalid), .O_tready(b_rm_tready),
        .bayer_ypos(b_ypos),
        .matrix_last_line(b_mlast), .matrix_cur_line(b_mcur), .matrix_next_line(b_mnext)
    );
    bilinear_interpolation bl_b (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_tlast(b_rm_tlast), .I_tuser(b_rm_tuser), .I_tvalid(b_rm_tvalid), .I_tready(b_rm_tready),
        .bayer_ypos(b_ypos),
        .matrix_last_line(b_mlast), .matrix_cur_line(b_mcur), .matrix_next_line(b_mnext),
        .O_tlast(b_otlast), .O_tuser(b_otuser), .O_tdata(b_otdata), .O_tvalid(b_otvalid),
        .O_tready(b_otready)
    );

    // ================= trace: every output of both instances =================
    integer traced = 0;
    initial equiv_trace_open;
    always @(posedge clk) begin
        cyc = cyc + 1;
        traced = traced + 1;
        $fdisplay(fd, "%0d | %b %b %b %b %b %b %b %b %b %b %b %b %b | %b %b %b %b %b %b %b %b %b %b %b %b %b",
            cyc,
            // A: raw_matrix outputs + bilinear outputs
            a_in_ready, a_rm_tlast, a_rm_tuser, a_rm_tvalid, a_ypos, a_mlast, a_mcur, a_mnext,
            a_rm_tready, a_otlast, a_otuser, a_otvalid, a_otdata,
            // B: raw_matrix outputs + bilinear outputs
            b_in_ready, b_rm_tlast, b_rm_tuser, b_rm_tvalid, b_ypos, b_mlast, b_mcur, b_mnext,
            b_rm_tready, b_otlast, b_otuser, b_otvalid, b_otdata);
    end

    // ================= stimulus =================
    integer seed = 32'h6d6f_2024;
    integer k_a, k_b;
    reg [39:0] d;

    function [39:0] rand40(input integer sd);
        rand40 = {$random(sd), sd[7:0]};
    endfunction

    // one line of the real-size instance; perturb=1 => occasional mid-line tlast + valid gaps
    task line_a(input integer gap, input user, input integer perturb);
        integer bb, kk;
        reg use_gap;
        reg [39:0] dv;
        begin
            for (bb = 0; bb < AB; bb = bb + 1) begin
                @(posedge clk);
                case (bb)
                    0: dv = 40'h0;
                    1: dv = {40{1'b1}};
                    default: dv = ((bb % 37) == 0) ? 40'hff_000_3ff_000_3ff : rand40(seed);
                endcase
                a_tdata   <= dv;
                a_tdest   <= $random(seed);
                a_tvalid  <= 1'b1;
                a_tlast   <= (bb == AB - 1);
                a_tuser   <= user && (bb == 0);
                a_otready <= $random(seed);
                if (perturb != 0 && (($random(seed) & 7) == 0)) a_tlast <= 1'b1;
                use_gap = (perturb != 0) && (($random(seed) & 7) == 0);
                if (use_gap) begin
                    for (kk = 0; kk < (1 + ($random(seed) & 3)); kk = kk + 1) begin
                        @(posedge clk);
                        a_tvalid <= 1'b0;
                        a_tlast  <= 1'b0;
                        a_tuser  <= 1'b0;
                    end
                end
            end
            @(posedge clk);
            a_tvalid <= 1'b0; a_tlast <= 1'b0; a_tuser <= 1'b0;
            for (kk = 0; kk < gap; kk = kk + 1) @(posedge clk);
        end
    endtask

    task frame_a(input integer gap, input integer perturb);
        integer yy;
        begin
            for (yy = 0; yy < 5; yy = yy + 1) line_a(gap, (yy == 0), perturb);
            repeat (30) @(posedge clk);
        end
    endtask

    task line_b(input integer user);
        integer bb, kk;
        begin
            for (bb = 0; bb < BB; bb = bb + 1) begin
                @(posedge clk);
                b_tdata   <= rand40(seed);
                b_tdest   <= $random(seed);
                b_tvalid  <= 1'b1;
                b_tlast   <= (bb == BB - 1);
                b_tuser   <= user && (bb == 0);
                b_otready <= $random(seed);
            end
            @(posedge clk);
            b_tvalid <= 1'b0; b_tlast <= 1'b0; b_tuser <= 1'b0;
            for (kk = 0; kk < BG; kk = kk + 1) @(posedge clk);
        end
    endtask

    task frame_b;
        integer yy;
        begin
            for (yy = 0; yy < 12; yy = yy + 1) line_b(yy == 0);
            repeat (20) @(posedge clk);
        end
    endtask

    initial begin   // instance A stream
        repeat (8) @(posedge clk);
        frame_a(AG, 0);
        frame_a(AG, 0);
        frame_a(AG, 1);
        frame_a(AG, 1);
        for (k_a = 0; k_a < 200; k_a = k_a + 1) @(posedge clk);
    end

    initial begin   // instance B stream
        repeat (8) @(posedge clk);
        frame_b;
        frame_b;
        frame_b;
        frame_b;
        frame_b;
        for (k_b = 0; k_b < 200; k_b = k_b + 1) @(posedge clk);
    end

    initial begin   // shared reset timeline + end (reset lands mid-stream)
        repeat (7000) @(posedge clk);
        rst_n <= 1'b0;
        repeat (4) @(posedge clk);
        rst_n <= 1'b1;
        repeat (9000) @(posedge clk);
        if (traced != cyc) begin
            $display("x_demosaic: trace lines %0d != cycles %0d", traced, cyc);
            $finish;
        end
        equiv_finish(cyc);
    end

    initial begin
        repeat (6) @(posedge clk);
        rst_n <= 1'b1;
    end
endmodule
