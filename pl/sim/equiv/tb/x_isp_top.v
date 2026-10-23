`timescale 1ns/1ps
// verifier-isp 独立对抗性等价 TB：isp_top 整链（最重要的一项）
// 40bit AXI4-Stream 随机帧 → BLC → demosaic(raw_matrix/bilinear, wrapper top_demosaic) → data128_96 →
// awb(+signal_delay) → image_cut → gamma → Saturation → contrast → Brightness → data96_128 → 128bit 输出。
// 参数全部沿用 isp_top 内部写死的真实配置（1080p/RGGB/BLC=50/cut=SKIP_COLS_left/awb 默认/gamma 2.2/
// Saturation=130/contrast=140/Brightness=+5）。
// 激励：4 个阶段，按真实视频行结构（480 beat/行 + 72 拍行消隐，tuser=帧首，tlast=行尾）：
//   P1 干净连续流 + 数据极值；P2 随机 tvalid 缺口 + 随机 O_tready 背压；
//   P3 帧中额外 tuser / 行中错位 tlast（对抗边界）；P4 中途复位后继续两帧；结尾排空。
// 逐拍 trace isp_top 全部输出（128bit 数据 + tlast/tuser/tvalid/I_tready，含 x/z 原样）。
module tb_x_isp_top;
    localparam integer BEATS  = 480;   // 1920 px / 4 per beat
    localparam integer HBLANK = 72;    // horizontal blanking beats (real 1080p: (2200-1920)/4 = 70)
    localparam integer LINES  = 6;

    reg clk = 0;
    reg rst_n = 0;
    always #5 clk = ~clk;

    reg  [39:0] I_tdata = 0;
    reg  [9:0]  I_tdest = 0;
    reg         I_tlast = 0, I_tuser = 0, I_tvalid = 0;
    reg         O_tready = 0;
    wire [127:0] O_tdata;
    wire        O_tlast, O_tuser, O_tvalid, I_tready;

    isp_top dut (
        .axi4s_video_aclk(clk),
        .I_rst_n(rst_n),
        .I_tlast(I_tlast),
        .I_tuser(I_tuser),
        .I_tdata(I_tdata),
        .I_tvalid(I_tvalid),
        .I_tdest(I_tdest),
        .O_tready(O_tready),
        .O_tdata(O_tdata),
        .O_tlast(O_tlast),
        .O_tuser(O_tuser),
        .O_tvalid(O_tvalid),
        .I_tready(I_tready)
    );

    // ================= trace =================
    integer fd;
    integer cyc = 0;
    integer traced = 0;
    reg [1023:0] trace_file;
    initial begin
        if (!$value$plusargs("TRACE=%s", trace_file)) begin
            $display("x_isp_top: no TRACE= plusarg");
            $finish;
        end
        fd = $fopen(trace_file, "w");
        if (fd == 0) begin
            $display("x_isp_top: cannot open trace file");
            $finish;
        end
    end
    always @(posedge clk) begin
        cyc = cyc + 1;
        traced = traced + 1;
        $fdisplay(fd, "%0d %b %b %b %b %b", cyc, I_tready, O_tlast, O_tuser, O_tvalid, O_tdata);
    end

    // ================= stimulus =================
    integer seed = 32'h1f15_2024;
    integer f, y, b, k;
    reg [39:0] d;

    function [39:0] rand40(input integer sd);
        rand40 = {$random(sd), sd[7:0]};
    endfunction

    // data pattern for one beat: mode 0 = random, 1 = extremes/checker, 2 = mid colour ramp
    task make_beat(input integer mode);
        integer kk;
        reg [9:0] px;
        begin
            d = 40'h0;
            for (kk = 3; kk >= 0; kk = kk - 1) begin
                case (mode)
                    0: px = {$random(seed)} & 10'h3ff;
                    1: px = ((k + kk) & 1) ? 10'h3ff : 10'h000;
                    default: px = (k * 64 + kk * 16) & 10'h3ff;
                endcase
                d[10*kk +: 10] = px;
            end
        end
    endtask

    // one video line; perturb: 0 clean, 1 random valid gaps + random ready, 2 misplaced tlast/tuser
    task send_line(input integer gap, input integer user, input integer perturb, input integer mode);
        integer bb, kk, gg;
        reg use_gap;
        begin
            for (b = 0; b < BEATS; b = b + 1) begin
                @(posedge clk);
                k = b;
                make_beat(mode);
                I_tdata  <= d;
                I_tdest  <= $random(seed);
                I_tvalid <= 1'b1;
                I_tlast  <= (b == BEATS - 1);
                I_tuser  <= user && (b == 0);
                O_tready <= (perturb == 0) ? 1'b1 : $random(seed);
                if (perturb == 2) begin
                    if ((($random(seed) & 15) == 0)) I_tlast <= 1'b1;                 // misplaced tlast
                    if ((($random(seed) & 63) == 0)) I_tuser <= 1'b1;                 // extra mid-line tuser
                end
                use_gap = (perturb == 1) && ((($random(seed) & 7) == 0));
                if (use_gap) begin
                    for (gg = 0; gg < (1 + ($random(seed) & 3)); gg = gg + 1) begin
                        @(posedge clk);
                        I_tvalid <= 1'b0;
                        I_tlast  <= 1'b0;
                        I_tuser  <= 1'b0;
                    end
                end
            end
            @(posedge clk);
            I_tvalid <= 1'b0; I_tlast <= 1'b0; I_tuser <= 1'b0;
            for (kk = 0; kk < gap; kk = kk + 1) @(posedge clk);
        end
    endtask

    task send_frame(input integer gap, input integer perturb, input integer mode);
        integer yy;
        begin
            for (yy = 0; yy < LINES; yy = yy + 1) send_line(gap, (yy == 0), perturb, mode);
            repeat (40) @(posedge clk);   // vertical blanking
        end
    endtask

    initial begin
        repeat (8) @(posedge clk);
        rst_n <= 1'b1;
        repeat (4) @(posedge clk);

        // P1: clean contiguous frames, extreme/checker data in second frame
        send_frame(HBLANK, 0, 0);
        send_frame(HBLANK, 0, 1);
        send_frame(HBLANK, 0, 2);

        // P2: random valid gaps + random O_tready backpressure
        send_frame(HBLANK, 1, 0);
        send_frame(HBLANK, 1, 2);

        // P3: adversarial tlast/tuser boundaries
        send_frame(HBLANK, 2, 0);
        send_frame(HBLANK, 2, 1);

        // P4: mid-stream reset, then two more frames
        O_tready <= 1'b0;
        repeat (5) @(posedge clk);
        rst_n <= 1'b0;
        repeat (4) @(posedge clk);
        rst_n <= 1'b1;
        repeat (2) @(posedge clk);
        send_frame(HBLANK, 0, 1);
        send_frame(HBLANK, 1, 2);

        // drain: no input, output pipeline empties (covers tready/valid tail behaviour)
        I_tvalid <= 1'b0; I_tlast <= 1'b0; I_tuser <= 1'b0;
        O_tready <= 1'b1;
        repeat (700) @(posedge clk);

        if (traced != cyc) begin
            $display("x_isp_top: trace lines %0d != cycles %0d", traced, cyc);
            $finish;
        end
        $display("EQUIV_CYCLES %0d", cyc);
        $display("EQUIV_DONE");
        $finish;
    end
endmodule
