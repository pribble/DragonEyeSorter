`timescale 1ns/1ps
// verifier-isp 独立对抗性等价 TB：awb + signal_delay
// 输入按真实行结构：480 beat/行（1920 像素 /4）+ 74 拍行消隐，每帧 4 行，tuser 在帧首。
// 覆盖：8 帧（前 2 帧随机数据让增益摆动，后 6 帧真实彩色 + 亮像素排除）、随机 O_tready、
//       phase2 随机 valid 缺口（<50 拍，考验 signal_delay 行寄存）、phase3 中途复位后继续。
// 逐拍 trace 全部输出；golden 与新版必须逐字节一致。
module tb_x_awb;
    `include "equiv_tb_common.vh"
    localparam integer BEATS = 480;   // 1920 / 4
    localparam integer HBLANK = 74;
    localparam integer LINES = 4;

    reg clk = 0;
    reg rst_n = 0;
    always #5 clk = ~clk;

    reg         I_tlast = 0, I_tuser = 0, I_tvalid = 0;
    reg  [95:0] I_tdata = 0;
    reg         O_tready = 0;
    wire        I_tready, O_tlast, O_tuser, O_tvalid;
    wire [95:0] O_tdata;

    awb dut (
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

    // ---- stimulus helpers
    integer seed = 32'h5151_2024;
    integer f, y, b, k, r, g, bl;
    reg [95:0] beat;

    function [7:0] clip(input integer v);
        clip = (v < 0) ? 8'd0 : (v > 255) ? 8'd255 : v[7:0];
    endfunction

    task make_beat(input integer mode);
        integer kk;
        begin
            for (kk = 0; kk < 4; kk = kk + 1) begin
                if (mode == 0) begin
                    r  = $random(seed) & 255;
                    g  = $random(seed) & 255;
                    bl = $random(seed) & 255;
                end else if ((($random(seed) & 31) == 0)) begin
                    r = 250; g = 250; bl = 250;                 // bright pixel: sum >= 700, excluded
                end else begin
                    r  = 90  + ($random(seed) % 21);
                    g  = 140 + ($random(seed) % 21);
                    bl = 70  + ($random(seed) % 21);
                end
                beat[24*kk +: 24] = {clip(r), clip(g), clip(bl)};
            end
        end
    endtask

    task send_line(input integer mode, input integer gap, input user);
        integer bb, gg;
        begin
            for (bb = 0; bb < BEATS; bb = bb + 1) begin
                @(posedge clk);
                make_beat(mode);
                I_tdata  <= beat;
                I_tvalid <= 1'b1;
                I_tlast  <= (bb == BEATS - 1);
                I_tuser  <= user && (bb == 0);
                O_tready <= $random(seed);
                // random valid gaps inside the line on request
                if (gap != 0 && (($random(seed) & 15) == 0)) begin
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
            for (gg = 0; gg < gap; gg = gg + 1) @(posedge clk);
        end
    endtask

    task send_frame(input integer mode, input integer gap);
        integer yy;
        begin
            for (yy = 0; yy < LINES; yy = yy + 1) send_line(mode, gap, (yy == 0));
            repeat (40) @(posedge clk);   // vertical blanking
        end
    endtask

    initial begin
        repeat (6) @(posedge clk);
        rst_n <= 1'b1;
        repeat (4) @(posedge clk);

        // phase 1: random data then realistic colour; gain path exercised from frame 2 on
        for (f = 0; f < 2; f = f + 1) send_frame(0, HBLANK);
        for (f = 0; f < 6; f = f + 1) send_frame(1, HBLANK);

        // phase 2: random valid gaps in every line (still < 50 clock stall)
        for (f = 0; f < 3; f = f + 1) send_frame(1, HBLANK);

        // phase 3: mid-stream reset, then two more frames
        rst_n <= 1'b0;
        repeat (4) @(posedge clk);
        rst_n <= 1'b1;
        repeat (2) @(posedge clk);
        for (f = 0; f < 2; f = f + 1) send_frame(1, HBLANK);

        repeat (60) @(posedge clk);

        if (traced != cyc) begin
            $display("x_awb: trace lines %0d != cycles %0d", traced, cyc);
            $finish;
        end
        equiv_finish(cyc);
    end
endmodule
