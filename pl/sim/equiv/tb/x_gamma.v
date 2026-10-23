`timescale 1ns/1ps
// verifier-isp 独立对抗性等价 TB：gamma（特化到 GAMMA=2.2 前后）
// 不传 GAMMA_10x 覆盖（golden 默认值 = 22 = isp_top 实际配置；新版参数已删，实例化必须兼容两者）。
// 覆盖：随机 96bit + 极值(0x00/0xFF/棋盘)、随机 valid 缺口、tuser/tlast 错位、随机 O_tready、
//       连续写满后突然停顿再排空、一次中途复位。
module tb_x_gamma;
    `include "equiv_tb_common.vh"
    reg clk = 0;
    reg rst_n = 0;
    always #5 clk = ~clk;

    reg         I_tlast = 0, I_tuser = 0, I_tvalid = 0;
    reg  [95:0] I_tdata = 0;
    reg         O_tready = 0;
    wire        I_tready, O_tlast, O_tuser, O_tvalid;
    wire [95:0] O_tdata;

    gamma dut (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_tlast(I_tlast), .I_tuser(I_tuser), .I_tdata(I_tdata), .I_tvalid(I_tvalid),
        .I_tready(I_tready),
        .O_tlast(O_tlast), .O_tuser(O_tuser), .O_tdata(O_tdata), .O_tvalid(O_tvalid),
        .O_tready(O_tready)
    );

    integer traced = 0;
    initial equiv_trace_open;
    always @(posedge clk) begin
        cyc = cyc + 1;
        traced = traced + 1;
        $fdisplay(fd, "%0d %b %b %b %b %b", cyc, I_tready, O_tlast, O_tuser, O_tdata, O_tvalid);
    end

    // ---- stimulus
    integer seed = 32'h0bad_f00d;
    integer i;
    reg [95:0] d;

    initial begin
        repeat (5) @(posedge clk);
        rst_n <= 1'b1;
        repeat (3) @(posedge clk);

        // phase 1: deterministic extremes then random, contiguous
        for (i = 0; i < 64; i = i + 1) begin
            @(posedge clk);
            I_tvalid <= 1'b1;
            O_tready <= 1'b1;
            I_tlast  <= (i == 63);
            I_tuser  <= (i == 0);
            case (i % 6)
                0: d = 96'h0;
                1: d = {96{1'b1}};
                2: d = 96'h5555_5555_5555_5555_5555_5555;
                3: d = 96'haaaa_aaaa_aaaa_aaaa_aaaa_aaaa;
                4: d = 96'h0100_ff80_7f00_0100_ff80_7f00;
                default: d = {$random(seed), $random(seed), $random(seed)};
            endcase
            I_tdata <= d;
        end

        // phase 2: random valid gaps / ready backpressure / misplaced control
        for (i = 0; i < 600; i = i + 1) begin
            @(posedge clk);
            O_tready <= $random(seed);
            if (($random(seed) & 3) == 0) begin
                I_tvalid <= 1'b0;
                I_tlast  <= 1'b0;
                I_tuser  <= 1'b0;
            end else begin
                I_tvalid <= 1'b1;
                I_tdata  <= {$random(seed), $random(seed), $random(seed)};
                I_tlast  <= (($random(seed) & 31) == 0);
                I_tuser  <= (($random(seed) & 63) == 0);
            end
        end

        // phase 3: continuous full-rate burst then abrupt stop (drain)
        @(posedge clk);
        I_tvalid <= 1'b1; I_tlast <= 0; I_tuser <= 0; O_tready <= 1;
        for (i = 0; i < 300; i = i + 1) begin
            @(posedge clk);
            I_tdata <= {$random(seed), $random(seed), $random(seed)};
            I_tlast <= (i == 299);
        end
        @(posedge clk);
        I_tvalid <= 1'b0; I_tlast <= 1'b0; I_tuser <= 1'b0;
        repeat (12) @(posedge clk);

        // phase 4: mid-stream reset then more beats
        rst_n <= 1'b0;
        repeat (4) @(posedge clk);
        rst_n <= 1'b1;
        repeat (2) @(posedge clk);
        for (i = 0; i < 40; i = i + 1) begin
            @(posedge clk);
            I_tvalid <= 1'b1;
            I_tdata  <= {$random(seed), $random(seed), $random(seed)};
            I_tlast  <= (i == 20);
            I_tuser  <= (i == 0);
            O_tready <= i[0];
        end
        @(posedge clk);
        I_tvalid <= 0; I_tlast <= 0; I_tuser <= 0;
        repeat (12) @(posedge clk);

        if (traced != cyc) begin
            $display("x_gamma: trace lines %0d != cycles %0d", traced, cyc);
            $finish;
        end
        equiv_finish(cyc);
    end
endmodule
