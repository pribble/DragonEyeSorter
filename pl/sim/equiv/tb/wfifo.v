`timescale 1ns/1ps
// wfifo 等价回归：异步 FIFO（写/读时钟不同频）+ 格雷码指针 CDC + RAM 推断。
// 四个实例覆盖生成器参数空间：
//   w1 = uidbuf_write 实际例化参数（128bit -> 64bit，ADDR 13/14，SHOW_AHEAD=0，NOREG）
//   w2 = 小深度跨位宽（32bit -> 64bit，ADDR 7/6）
//   w3 = SHOW_AHEAD_EN=1（预读模式）
//   w4 = OUTREG_EN="OUTREG"（输出寄存器模式）
// 激励：先连续写不读（各小深度实例迅速写满、afull 抬起），随后连续读排空（empty 抬起），
// 再随机读写；不同实例在不同时刻被异步复位（含写满/排空中途）。
// trace 在 clkw / clkr 两个时钟沿都采样，tag W/R 区分，逐拍打印全部输出（x/z 原样）。
module tb_wfifo;
    integer fd;
    integer wcyc = 0;
    integer rcyc = 0;
    reg [1023:0] tf;

    reg clkw = 0;
    reg clkr = 0;
    always #5    clkw = ~clkw;   // 100 MHz
    always #3.5  clkr = ~clkr;   // ~142.9 MHz，与写时钟异步

    // ---------------- 激励 ----------------
    reg rst1 = 1, rst2 = 1, rst3 = 1, rst4 = 1;
    reg we = 0;
    reg [127:0] di = 0;
    reg re = 0;

    // ---------------- 实例 w1：uidbuf_write 实际参数 ----------------
    wire        w1_valid, w1_full, w1_empty, w1_afull, w1_aempty;
    wire [12:0] w1_wused;
    wire [13:0] w1_rused;
    wire [63:0] w1_dout;

    wfifo #(
        .DATA_WIDTH_W(128), .DATA_WIDTH_R(64),
        .ADDR_WIDTH_W(13),  .ADDR_WIDTH_R(14),
        .AL_FULL_NUM(4094), .AL_EMPTY_NUM(2),
        .SHOW_AHEAD_EN(1'b0), .OUTREG_EN("NOREG")
    ) w1 (
        .rst(rst1), .clkw(clkw), .clkr(clkr), .we(we), .di(di), .re(re), .dout(w1_dout),
        .valid(w1_valid), .full_flag(w1_full), .empty_flag(w1_empty),
        .afull(w1_afull), .aempty(w1_aempty), .wrusedw(w1_wused), .rdusedw(w1_rused)
    );

    // ---------------- 实例 w2：小深度跨位宽 ----------------
    wire        w2_valid, w2_full, w2_empty, w2_afull, w2_aempty;
    wire [6:0]  w2_wused;
    wire [5:0]  w2_rused;
    wire [63:0] w2_dout;

    wfifo #(
        .DATA_WIDTH_W(32), .DATA_WIDTH_R(64),
        .ADDR_WIDTH_W(7),  .ADDR_WIDTH_R(6),
        .AL_FULL_NUM(62),  .AL_EMPTY_NUM(2),
        .SHOW_AHEAD_EN(1'b0), .OUTREG_EN("NOREG")
    ) w2 (
        .rst(rst2), .clkw(clkw), .clkr(clkr), .we(we), .di(di[31:0]), .re(re), .dout(w2_dout),
        .valid(w2_valid), .full_flag(w2_full), .empty_flag(w2_empty),
        .afull(w2_afull), .aempty(w2_aempty), .wrusedw(w2_wused), .rdusedw(w2_rused)
    );

    // ---------------- 实例 w3：SHOW_AHEAD_EN=1 ----------------
    wire        w3_valid, w3_full, w3_empty, w3_afull, w3_aempty;
    wire [3:0]  w3_wused, w3_rused;
    wire [31:0] w3_dout;

    wfifo #(
        .DATA_WIDTH_W(32), .DATA_WIDTH_R(32),
        .ADDR_WIDTH_W(4),  .ADDR_WIDTH_R(4),
        .AL_FULL_NUM(14),  .AL_EMPTY_NUM(2),
        .SHOW_AHEAD_EN(1'b1), .OUTREG_EN("NOREG")
    ) w3 (
        .rst(rst3), .clkw(clkw), .clkr(clkr), .we(we), .di(di[31:0]), .re(re), .dout(w3_dout),
        .valid(w3_valid), .full_flag(w3_full), .empty_flag(w3_empty),
        .afull(w3_afull), .aempty(w3_aempty), .wrusedw(w3_wused), .rdusedw(w3_rused)
    );

    // ---------------- 实例 w4：OUTREG_EN="OUTREG" ----------------
    wire        w4_valid, w4_full, w4_empty, w4_afull, w4_aempty;
    wire [3:0]  w4_wused, w4_rused;
    wire [31:0] w4_dout;

    wfifo #(
        .DATA_WIDTH_W(32), .DATA_WIDTH_R(32),
        .ADDR_WIDTH_W(4),  .ADDR_WIDTH_R(4),
        .AL_FULL_NUM(14),  .AL_EMPTY_NUM(2),
        .SHOW_AHEAD_EN(1'b0), .OUTREG_EN("OUTREG")
    ) w4 (
        .rst(rst4), .clkw(clkw), .clkr(clkr), .we(we), .di(di[31:0]), .re(re), .dout(w4_dout),
        .valid(w4_valid), .full_flag(w4_full), .empty_flag(w4_empty),
        .afull(w4_afull), .aempty(w4_aempty), .wrusedw(w4_wused), .rdusedw(w4_rused)
    );

    // ---------------- trace（两个时钟沿都采样） ----------------
    always @(posedge clkw) begin
        wcyc <= wcyc + 1;
        $fdisplay(fd, "%0d W %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b",
            wcyc,
            w1_valid, w1_full, w1_empty, w1_afull, w1_aempty, w1_wused, w1_rused, w1_dout,
            w2_valid, w2_full, w2_empty, w2_afull, w2_aempty, w2_wused, w2_rused, w2_dout,
            w3_valid, w3_full, w3_empty, w3_afull, w3_aempty, w3_wused, w3_rused, w3_dout,
            w4_valid, w4_full, w4_empty, w4_afull, w4_aempty, w4_wused, w4_rused, w4_dout);
    end

    always @(posedge clkr) begin
        rcyc <= rcyc + 1;
        $fdisplay(fd, "%0d R %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b",
            rcyc,
            w1_valid, w1_full, w1_empty, w1_afull, w1_aempty, w1_wused, w1_rused, w1_dout,
            w2_valid, w2_full, w2_empty, w2_afull, w2_aempty, w2_wused, w2_rused, w2_dout,
            w3_valid, w3_full, w3_empty, w3_afull, w3_aempty, w3_wused, w3_rused, w3_dout,
            w4_valid, w4_full, w4_empty, w4_afull, w4_aempty, w4_wused, w4_rused, w4_dout);
    end

    // ---------------- 确定性 LFSR ----------------
    reg [31:0] rng = 32'h1357_9BDF;
    function [31:0] rnd;
        input integer dummy;
        begin
            rng = {rng[30:0], rng[31] ^ rng[21] ^ rng[1] ^ rng[0]};
            rnd = rng;
        end
    endfunction

    // ---------------- 写侧：先连续写满，再停写，然后随机 ----------------
    always @(posedge clkw) begin
        di <= {rnd(0), rnd(0), rnd(0), rnd(0)};
        if ($time < 100) begin
            we <= 1'b0;
        end else if ($time < 95000) begin
            we <= 1'b1;                 // 连续写：各实例（含 w1 实际参数）写满
        end else if ($time < 120000) begin
            we <= (rnd(0) % 10 < 5);    // 混合读写
        end else if ($time < 145000) begin
            we <= 1'b0;                 // 只读排空
        end else begin
            we <= (rnd(0) % 10 < 4);    // 随机
        end
        // 各实例复位相位不同，且覆盖写满/排空中途
        rst1 <= ($time < 100) || (($time > 138000) && ($time < 138060));
        rst2 <= ($time < 100) || (($time > 118000) && ($time < 118030));
        rst3 <= ($time < 100) || (($time > 40000) && ($time < 40030));
        rst4 <= ($time < 100) || (($time > 70000) && ($time < 70020)) ||
                (($time > 146000) && ($time < 146050));
    end

    // ---------------- 读侧：写满阶段不读，随后连续读排空，再随机 ----------------
    always @(posedge clkr) begin
        if ($time < 100) begin
            re <= 1'b0;
        end else if ($time < 95000) begin
            re <= 1'b0;                 // 写满阶段
        end else if ($time < 120000) begin
            re <= (rnd(0) % 10 < 3);    // 混合读写
        end else if ($time < 145000) begin
            re <= 1'b1;                 // 连续读排空
        end else begin
            re <= (rnd(0) % 10 < 4);    // 随机（含空读）
        end
    end

    initial begin
        if (!$value$plusargs("TRACE=%s", tf)) tf = "trace.txt";
        fd = $fopen(tf, "w");
        if (fd == 0) begin $display("cannot open trace"); $finish; end
        repeat (15000) @(posedge clkw);
        $display("EQUIV_CYCLES %0d", wcyc);
        $display("EQUIV_DONE");
        $fclose(fd);
        $finish;
    end
endmodule
