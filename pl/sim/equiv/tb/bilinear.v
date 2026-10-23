`timescale 1ns/1ps
// 等价回归 TB：bilinear_interpolation 单模块。
// 覆盖：4 种合法 BAYER_MODE（u0 用参数默认值 BGGR）+ 未知模式字符串（golden 输出 X 的边界行为）、
// 每拍随机 3x3 矩阵/ypos/tvalid、tuser/tlast 随机、随机 O_tready、中途复位、数据极值。
module tb_bilinear;
    integer fd;
    integer cyc = 0;
    reg [1023:0] tf;
    integer seed = 32'h2a5b_7c19;

    reg clk = 0;
    reg rst_n = 0;

    reg  [95:0] m_last = 0;
    reg  [95:0] m_cur  = 0;
    reg  [95:0] m_next = 0;
    reg         ypos   = 0;
    reg         i_tlast  = 0;
    reg         i_tuser  = 0;
    reg         i_tvalid = 0;
    reg         o_tready = 0;

    wire [127:0] td [0:4];
    wire [4:0]   tl, tu, tv, irdy;

    always #5 clk = ~clk;

    // 0: 默认参数（BGGR）；1: GBRG；2: RGGB；3: GRBG；4: 未知模式（非法参数字符串）
    bilinear_interpolation u0 (
        .I_clk(clk), .I_rst_n(rst_n), .I_tlast(i_tlast), .I_tuser(i_tuser),
        .I_tvalid(i_tvalid), .I_tready(irdy[0]), .bayer_ypos(ypos),
        .matrix_last_line(m_last), .matrix_cur_line(m_cur), .matrix_next_line(m_next),
        .O_tlast(tl[0]), .O_tuser(tu[0]), .O_tdata(td[0]), .O_tvalid(tv[0]), .O_tready(o_tready));
    bilinear_interpolation #(.BAYER_MODE("GBRG")) u1 (
        .I_clk(clk), .I_rst_n(rst_n), .I_tlast(i_tlast), .I_tuser(i_tuser),
        .I_tvalid(i_tvalid), .I_tready(irdy[1]), .bayer_ypos(ypos),
        .matrix_last_line(m_last), .matrix_cur_line(m_cur), .matrix_next_line(m_next),
        .O_tlast(tl[1]), .O_tuser(tu[1]), .O_tdata(td[1]), .O_tvalid(tv[1]), .O_tready(o_tready));
    bilinear_interpolation #(.BAYER_MODE("RGGB")) u2 (
        .I_clk(clk), .I_rst_n(rst_n), .I_tlast(i_tlast), .I_tuser(i_tuser),
        .I_tvalid(i_tvalid), .I_tready(irdy[2]), .bayer_ypos(ypos),
        .matrix_last_line(m_last), .matrix_cur_line(m_cur), .matrix_next_line(m_next),
        .O_tlast(tl[2]), .O_tuser(tu[2]), .O_tdata(td[2]), .O_tvalid(tv[2]), .O_tready(o_tready));
    bilinear_interpolation #(.BAYER_MODE("GRBG")) u3 (
        .I_clk(clk), .I_rst_n(rst_n), .I_tlast(i_tlast), .I_tuser(i_tuser),
        .I_tvalid(i_tvalid), .I_tready(irdy[3]), .bayer_ypos(ypos),
        .matrix_last_line(m_last), .matrix_cur_line(m_cur), .matrix_next_line(m_next),
        .O_tlast(tl[3]), .O_tuser(tu[3]), .O_tdata(td[3]), .O_tvalid(tv[3]), .O_tready(o_tready));
    bilinear_interpolation #(.BAYER_MODE("XXXX")) u4 (
        .I_clk(clk), .I_rst_n(rst_n), .I_tlast(i_tlast), .I_tuser(i_tuser),
        .I_tvalid(i_tvalid), .I_tready(irdy[4]), .bayer_ypos(ypos),
        .matrix_last_line(m_last), .matrix_cur_line(m_cur), .matrix_next_line(m_next),
        .O_tlast(tl[4]), .O_tuser(tu[4]), .O_tdata(td[4]), .O_tvalid(tv[4]), .O_tready(o_tready));

    always @(negedge clk) begin
        #2;
        if (fd != 0) begin
            $fdisplay(fd, "%0d %b %b %b %h %b | %b %b %b %h %b | %b %b %b %h %b | %b %b %b %h %b | %b %b %b %h %b",
                cyc,
                tl[0], tu[0], tv[0], td[0], irdy[0],
                tl[1], tu[1], tv[1], td[1], irdy[1],
                tl[2], tu[2], tv[2], td[2], irdy[2],
                tl[3], tu[3], tv[3], td[3], irdy[3],
                tl[4], tu[4], tv[4], td[4], irdy[4]);
        end
        cyc = cyc + 1;
    end

    task beat(input [95:0] a, input [95:0] b, input [95:0] c, input y,
              input last, input user, input valid, input rdy);
        begin
            @(negedge clk);
            m_last = a; m_cur = b; m_next = c; ypos = y;
            i_tlast = last; i_tuser = user; i_tvalid = valid; o_tready = rdy;
        end
    endtask

    // 顺序执行的随机激励（同一 seed，golden/新版逐拍一致）
    task rand_stream(input integer n, input integer valid_pct);
        integer i;
        reg [95:0] a, b, c;
        reg y, l, u, v, r;
        begin
            for (i = 0; i < n; i = i + 1) begin
                a = {$random(seed), $random(seed), $random(seed)};
                b = {$random(seed), $random(seed), $random(seed)};
                c = {$random(seed), $random(seed), $random(seed)};
                r = $random(seed);
                y = $random(seed);
                l = (($random(seed) % 100) < 5);
                u = (($random(seed) % 100) < 3);
                v = (($random(seed) % 100) < valid_pct);
                beat(a, b, c, y, l, u, v, r);
            end
        end
    endtask

    task do_reset(input integer n);
        integer i;
        begin
            @(negedge clk); rst_n = 0;
            for (i = 0; i < n; i = i + 1) beat({96{1'b0}}, {96{1'b0}}, {96{1'b0}}, 1'b0, 1'b0, 1'b0, 1'b0, 1'b1);
            @(negedge clk); rst_n = 1;
        end
    endtask

    initial begin
        if (!$value$plusargs("TRACE=%s", tf)) tf = "trace.txt";
        fd = $fopen(tf, "w");
        if (fd == 0) begin $display("cannot open trace"); $finish; end

        @(negedge clk); rst_n = 0;
        rand_stream(4, 30);
        @(negedge clk); rst_n = 1;

        rand_stream(2000, 80);                 // 随机数据 + tvalid 空泡
        rand_stream(40, 100);                  // 全 1 矩阵、ypos=1（四种模式饱和边界）
        rand_stream(40, 100);                  // 全 0 矩阵、ypos=0
        rand_stream(300, 60);
        do_reset(3);                           // 中途复位
        rand_stream(1500, 80);
        rand_stream(30, 100);
        rand_stream(400, 70);

        repeat (6) @(negedge clk);
        #200;
        $display("EQUIV_CYCLES %0d", cyc);
        $display("EQUIV_DONE");
        $fclose(fd);
        $finish;
    end
endmodule
