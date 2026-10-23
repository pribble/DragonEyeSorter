`timescale 1ns/1ps
// verifier-isp 独立对抗性等价 TB：bilinear_interpolation（demosaic 链核心）
// 与 dev 的 tb 无关：直接驱动 3x3 线矩阵 + bayer_ypos + 控制信号。
// 覆盖：两种 BAYER_MODE 实例（RGGB=isp_top 实际配置，BGGR=默认）、随机数据/极值、
//       tlast/tuser 边界与错位、随机 valid 缺口、随机 O_tready、两次中途复位。
// trace 每拍写出 两个 DUT 的全部输出；golden 与新版各跑一遍必须逐字节一致。
module tb_x_bilinear;
    `include "equiv_tb_common.vh"
    reg clk = 0;
    reg rst_n = 0;
    always #5 clk = ~clk;

    // ---- stimulus
    reg         I_tlast = 0, I_tuser = 0, I_tvalid = 0;
    reg         bayer_ypos = 0;
    reg  [95:0] m_last = 0, m_cur = 0, m_next = 0;
    reg         O_tready = 0;

    // ---- DUT A: RGGB (isp_top uses RGGB)
    wire        a_tready, a_tlast, a_tuser, a_tvalid;
    wire [127:0] a_tdata;
    bilinear_interpolation #(.BAYER_MODE("RGGB")) dut_rggb (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_tlast(I_tlast), .I_tuser(I_tuser), .I_tvalid(I_tvalid), .I_tready(a_tready),
        .bayer_ypos(bayer_ypos),
        .matrix_last_line(m_last), .matrix_cur_line(m_cur), .matrix_next_line(m_next),
        .O_tlast(a_tlast), .O_tuser(a_tuser), .O_tdata(a_tdata), .O_tvalid(a_tvalid),
        .O_tready(O_tready)
    );

    // ---- DUT B: BGGR (vendor default parameter value)
    wire        b_tready, b_tlast, b_tuser, b_tvalid;
    wire [127:0] b_tdata;
    bilinear_interpolation #(.BAYER_MODE("BGGR")) dut_bggr (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_tlast(I_tlast), .I_tuser(I_tuser), .I_tvalid(I_tvalid), .I_tready(b_tready),
        .bayer_ypos(bayer_ypos),
        .matrix_last_line(m_last), .matrix_cur_line(m_cur), .matrix_next_line(m_next),
        .O_tlast(b_tlast), .O_tuser(b_tuser), .O_tdata(b_tdata), .O_tvalid(b_tvalid),
        .O_tready(O_tready)
    );

    // ---- trace
    integer traced = 0;
    initial equiv_trace_open;
    always @(posedge clk) begin
        cyc = cyc + 1;
        traced = traced + 1;
        $fdisplay(fd, "%0d %b %b %b %b %b %b %b %b %b %b",
            cyc,
            a_tready, a_tlast, a_tuser, a_tvalid, a_tdata,
            b_tready, b_tlast, b_tuser, b_tvalid, b_tdata);
    end

    // ---- stimulus
    integer seed = 32'h1234_5678;
    integer line, beat, g, i;
    reg [95:0] d;

    task drive(input [95:0] data, input last, input user);
        begin
            @(posedge clk);
            m_last  <= data ^ 96'h0f0f_0f0f_0f0f_0f0f_0f0f_0f0f;
            m_cur   <= data;
            m_next  <= {data[94:0], data[95]} ^ 96'h00ff_00ff_00ff_00ff_00ff_00ff;
            I_tvalid <= 1'b1;
            I_tlast  <= last;
            I_tuser  <= user;
        end
    endtask

    initial begin
        repeat (6) @(posedge clk);
        rst_n <= 1'b1;
        repeat (3) @(posedge clk);

        // phase 1: regular lines, extreme + random data, RGGB row parity
        for (line = 0; line < 24; line = line + 1) begin
            bayer_ypos <= line[0];
            for (beat = 0; beat < 16; beat = beat + 1) begin
                if (beat < 4)
                    d = (beat == 0) ? 96'h0 : (beat == 1) ? {96{1'b1}} : (beat == 2) ? 96'h5555_5555_5555_5555_5555_5555 : 96'haaaa_aaaa_aaaa_aaaa_aaaa_aaaa;
                else
                    d = {$random(seed), $random(seed), $random(seed)};
                drive(d, beat == 15, (line == 0) && (beat == 0));
            end
            @(posedge clk);
            I_tvalid <= 1'b0; I_tlast <= 1'b0; I_tuser <= 1'b0;
            repeat (2) @(posedge clk);
        end

        // phase 2: random valid gaps, misplaced tlast/tuser, random ready
        for (line = 0; line < 16; line = line + 1) begin
            bayer_ypos <= $random(seed);
            beat = 0;
            g = 0;
            while (beat < 20) begin
                @(posedge clk);
                O_tready <= $random(seed);
                if (($random(seed) & 3) == 0) begin
                    I_tvalid <= 1'b0; I_tlast <= 0; I_tuser <= 0;
                end else begin
                    I_tvalid <= 1'b1;
                    m_last  <= {$random(seed), $random(seed), $random(seed)};
                    m_cur   <= {$random(seed), $random(seed), $random(seed)};
                    m_next  <= {$random(seed), $random(seed), $random(seed)};
                    // tlast occasionally misplaced (mid line), tuser anomalies mid line
                    I_tlast <= (($random(seed) & 7) == 0) ? 1'b1 : (beat == 19);
                    I_tuser <= (($random(seed) & 63) == 0) ? 1'b1 : 1'b0;
                    beat = beat + 1;
                end
                g = g + 1;
                if (g > 200) begin
                    $display("x_bilinear: stimulus stuck");
                    $finish;
                end
            end
            @(posedge clk);
            I_tvalid <= 1'b0; I_tlast <= 1'b0; I_tuser <= 1'b0;
            O_tready <= 0;
            repeat (2) @(posedge clk);
        end

        // phase 3: mid-stream reset, then more random beats
        repeat (5) @(posedge clk);
        rst_n <= 1'b0;
        O_tready <= 1'b1;
        repeat (4) @(posedge clk);
        rst_n <= 1'b1;
        repeat (2) @(posedge clk);
        for (beat = 0; beat < 30; beat = beat + 1) begin
            @(posedge clk);
            I_tvalid <= 1'b1;
            I_tuser  <= (beat == 0);
            I_tlast  <= (beat == 17);
            m_last  <= {$random(seed), $random(seed), $random(seed)};
            m_cur   <= {$random(seed), $random(seed), $random(seed)};
            m_next  <= {$random(seed), $random(seed), $random(seed)};
            bayer_ypos <= beat[0];
        end
        @(posedge clk);
        I_tvalid <= 0; I_tlast <= 0; I_tuser <= 0;
        repeat (20) @(posedge clk);

        if (traced != cyc) begin
            $display("x_bilinear: trace lines %0d != cycles %0d", traced, cyc);
            $finish;
        end
        equiv_finish(cyc);
    end
endmodule
