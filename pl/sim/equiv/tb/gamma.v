`timescale 1ns/1ps
// gamma.v 行为等价回归：golden 版（9 套 LUT + GAMMA_10x 10 选 1，isp_top 写死 22）
// vs 新版（只保留 lut_2_2 路径）。只比较端口行为，内部实现不看。
// 激励：4 像素 × RGB × 256 全码值扫描、随机 96bit 数据流、valid/ready 随机背压、
// tlast/tuser 随机、两处中途异步复位、0/255 极值；固定种子，两侧逐拍 trace 必须完全相同。
module tb_gamma;
    `include "equiv_tb_common.vh"

    reg clk = 0;
    reg rst_n = 0;
    reg [95:0] tdata = 96'd0;
    reg tvalid = 0;
    reg tlast = 0;
    reg tuser = 0;
    reg o_tready = 0;

    wire i_tready;
    wire o_tlast, o_tuser, o_tvalid;
    wire [95:0] o_tdata;

    gamma u_dut (
        .I_clk   (clk),
        .I_rst_n (rst_n),
        .I_tlast (tlast),
        .I_tuser (tuser),
        .I_tdata (tdata),
        .I_tvalid(tvalid),
        .I_tready(i_tready),
        .O_tlast (o_tlast),
        .O_tuser (o_tuser),
        .O_tdata (o_tdata),
        .O_tvalid(o_tvalid),
        .O_tready(o_tready)
    );

    always #5 clk = ~clk;

    initial equiv_trace_open;

    // 逐拍记录被测模块全部输出（x/z 原样打印；I_tready 也是输出，含在 compare 里）
    always @(posedge clk) begin
        cyc <= cyc + 1;
        $fdisplay(fd, "%0d %b %b %b %b %b", cyc, i_tready, o_tlast, o_tuser, o_tdata, o_tvalid);
    end

    integer seed = 32'h2A187E31;    // 固定种子：两侧激励逐拍一致
    integer n, k, pix, ch, val, off;
    reg [95:0] tmp;
    reg vld, lst, usr, rdy;

    task drv;
        input [95:0] d;
        input        v;
        input        l;
        input        u;
        input        r;
        begin
            tdata = d; tvalid = v; tlast = l; tuser = u; o_tready = r;
        end
    endtask

    // 一拍随机激励（在 negedge 驱动，避免与 DUT 时钟沿竞争）
    task step_random;
        begin
            @(negedge clk);
            tmp = {$random(seed), $random(seed), $random(seed)};
            vld = $random(seed) & 1;
            lst = ($random(seed) & 7) == 0;
            usr = ($random(seed) & 15) == 0;
            rdy = $random(seed) & 1;
            drv(tmp, vld, lst, usr, rdy);
        end
    endtask

    initial begin
        // ---- 阶段 0：上电异步复位 ----
        rst_n = 0;
        drv(96'd0, 1'b0, 1'b0, 1'b0, 1'b0);
        repeat (6) @(negedge clk);
        rst_n = 1;

        // ---- 阶段 1：4 像素 × RGB × 256 码全扫（覆盖 LUT 全表与通道映射）----
        for (n = 0; n < 4 * 3 * 256; n = n + 1) begin
            @(negedge clk);
            pix = n / 768;
            ch  = (n / 256) % 3;
            val = n % 256;
            tmp = {$random(seed), $random(seed), $random(seed)};
            off = 24 * pix + 16 - 8 * ch;
            tmp[off +: 8] = val[7:0];
            drv(tmp, 1'b1, (val == 255), (val == 0), (val & 1));
        end

        // ---- 阶段 2：随机 96bit 数据 + 随机 valid/ready/tlast/tuser ----
        for (n = 0; n < 1500; n = n + 1) step_random;

        // ---- 阶段 3：数据流中途异步复位（复位期间输入照常随机）----
        rst_n = 0;
        repeat (5) step_random;
        rst_n = 1;
        for (n = 0; n < 40; n = n + 1) step_random;

        // ---- 阶段 4：0/255 极值、逐通道极值、长背压 ----
        @(negedge clk); drv(96'd0,       1'b1, 1'b1, 1'b1, 1'b1);   // 全 0
        @(negedge clk); drv({12{8'hFF}}, 1'b1, 1'b0, 1'b0, 1'b1);   // 全 255
        @(negedge clk); drv({12{8'h80}}, 1'b1, 1'b1, 1'b0, 1'b0);   // 0x80 + 背压
        for (k = 0; k < 12; k = k + 1) begin
            @(negedge clk);
            tmp = 96'd0;
            tmp[8*k +: 8] = 8'hFF;
            drv(tmp, 1'b1, 1'b1, 1'b1, 1'b1);
            @(negedge clk);
            tmp = {12{8'hFF}};
            tmp[8*k +: 8] = 8'h00;
            drv(tmp, 1'b1, 1'b0, 1'b1, 1'b0);
        end

        // ---- 阶段 5：连续 valid、tready 长时间为 0 后放开 ----
        for (n = 0; n < 12; n = n + 1) begin
            @(negedge clk);
            tmp = {$random(seed), $random(seed), $random(seed)};
            drv(tmp, 1'b1, (n == 11), (n == 0), 1'b0);
        end
        for (n = 0; n < 12; n = n + 1) begin
            @(negedge clk);
            tmp = {$random(seed), $random(seed), $random(seed)};
            drv(tmp, 1'b1, (n == 11), (n == 0), 1'b1);
        end

        // ---- 阶段 6：复位释放后继续随机 ----
        rst_n = 0;
        repeat (3) @(negedge clk);
        rst_n = 1;
        for (n = 0; n < 800; n = n + 1) step_random;

        // ---- 收尾：排空流水线 ----
        drv(96'd0, 1'b0, 1'b0, 1'b0, 1'b0);
        repeat (10) @(negedge clk);
        equiv_finish(cyc);
    end
endmodule
