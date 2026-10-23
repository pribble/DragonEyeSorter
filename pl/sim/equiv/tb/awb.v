`timescale 1ns/1ps
// awb（厂家自动白平衡，已换 awb_seq_div）+ signal_delay（50 拍延迟线）行为等价回归。
// 同一激励分别驱动 golden RTL（baseline.rev）与工作区 RTL，逐拍 trace 必须逐字节相同。
//
// 覆盖：
//   * 多帧：帧间随机空档、tuser 宽度 1 拍 / 2 拍、帧中 tuser、帧中异步复位（拉低 I_rst_n 3 拍）；
//   * 彩色噪声（基线 R90 / G140 / B70 ±20，换色到 R150 / G100 / B60 让增益重新收敛）；
//   * 亮像素排除边界：每拍 pixel0 随机为 750（亮）/ 700（边界，排除）/ 699（边界，计入）/ 普通噪声；
//   * 随机 I_tvalid 气泡 + 随机 O_tready（awb 的 I_tready 只是 O_tready 直通，内部不反压）；
//   * 增益收敛后再跑多帧（div_done 每 49 拍更新一次 in_r_r1 / in_b_r1 的通路）；
//   * 高增益饱和：基色 (60,250,30) 让 gain_R≈4.2 / gain_B≈8.3，覆盖输出 255 钳位分支；
//   * 输入饿死：帧中停 valid 700 拍，读端跑到未写地址，O_tdata 必须出现 x（x 传播不能变成 0）；
//   * 两个参数实例：256x8（一行 64 beat）与 192x6（一行 48 beat，恰好 DELAY_CYCLE-3 = 47 的边界）。
// I_tlast 不被 awb 使用（端口保留），恒 0，不参与 trace。
module tb_awb;
    `include "equiv_tb_common.vh"
    localparam integer WA = 256, HA = 8;   // 实例 A：IMG_WIDTH_4x = 64
    localparam integer WB = 192, HB = 6;   // 实例 B：IMG_WIDTH_4x = 48

    reg clk = 0;
    always #5 clk = ~clk;

    reg         rst_n = 0;
    reg  [95:0] tdata = 0;
    reg         tvalid = 0, tuser = 0;
    reg         o_tready = 0;

    wire [95:0] a_odata, b_odata;
    wire        a_olast, a_ouser, a_ovalid, a_iready;
    wire        b_olast, b_ouser, b_ovalid, b_iready;

    awb #(.IMG_HEIGHT(HA), .IMG_WIDTH(WA)) dut_a (
        .I_clk(clk), .I_rst_n(rst_n), .I_tlast(1'b0), .I_tuser(tuser), .I_tdata(tdata),
        .I_tvalid(tvalid), .I_tready(a_iready),
        .O_tlast(a_olast), .O_tuser(a_ouser), .O_tdata(a_odata), .O_tvalid(a_ovalid),
        .O_tready(o_tready));

    awb #(.IMG_HEIGHT(HB), .IMG_WIDTH(WB)) dut_b (
        .I_clk(clk), .I_rst_n(rst_n), .I_tlast(1'b0), .I_tuser(tuser), .I_tdata(tdata),
        .I_tvalid(tvalid), .I_tready(b_iready),
        .O_tlast(b_olast), .O_tuser(b_ouser), .O_tdata(b_odata), .O_tvalid(b_ovalid),
        .O_tready(o_tready));

    // ---------------- trace：每拍写全部输出（x/z 原样）
    initial equiv_trace_open;
    always @(posedge clk) begin
        cyc <= cyc + 1;
        $fdisplay(fd, "%0d %b %b %h %b %b %b %b %h %b %b",
                  cyc, a_olast, a_ouser, a_odata, a_ovalid, a_iready,
                       b_olast, b_ouser, b_odata, b_ovalid, b_iready);
    end

    // ---------------- 确定性随机（固定种子；两侧二进制同源同版本 -> 序列一致）
    integer seed = 32'h1234_5678;
    function integer rn(input integer m);   // 0 .. m-1
        begin rn = ($random(seed) & 32'h7fff_ffff) % m; end
    endfunction

    function [7:0] clipv(input integer v);
        begin
            if (v < 0) clipv = 8'd0;
            else if (v > 255) clipv = 8'd255;
            else clipv = v;
        end
    endfunction

    // ---------------- 激励任务
    task do_reset(input integer n);
        begin
            rst_n <= 0; tvalid <= 0; tuser <= 0;
            repeat (n) @(posedge clk);
            rst_n <= 1;
            @(posedge clk);
        end
    endtask

    task idle(input integer n);             // n 拍 tvalid = 0
        integer i;
        begin
            for (i = 0; i < n; i = i + 1) begin
                @(posedge clk);
                tvalid <= 0; tuser <= 0; o_tready <= rn(2);
            end
        end
    endtask

    task tuser_pulse(input integer n);      // n 拍 tuser（1 拍正常，2 拍覆盖边界）
        integer i;
        begin
            for (i = 0; i < n; i = i + 1) begin
                @(posedge clk);
                tuser <= 1; tvalid <= 0; o_tready <= rn(2);
            end
            @(posedge clk);
            tuser <= 0;
        end
    endtask

    integer base_r = 90, base_g = 140, base_b = 70;

    // 发 nb 拍数据（每拍 4 像素）；i == reset_at 时先在帧中异步复位 3 拍。
    task send_beats(input integer nb, input integer reset_at);
        integer i, k, r, g, b, nbub;
        reg [95:0] beat;
        begin
            for (i = 0; i < nb; i = i + 1) begin
                if (i == reset_at) do_reset(3);
                for (k = 0; k < 4; k = k + 1) begin
                    if (k == 0) begin
                        // pixel0：亮像素排除边界（统计只看 pixel0 的 R+G+B < 700）
                        case (rn(8))
                            0:       begin r = 250; g = 250; b = 250; end   // 750：排除
                            1:       begin r = 255; g = 255; b = 190; end   // 700：排除（边界）
                            2:       begin r = 255; g = 255; b = 189; end   // 699：计入（边界）
                            default: begin r = base_r + rn(41) - 20;
                                           g = base_g + rn(41) - 20;
                                           b = base_b + rn(41) - 20; end
                        endcase
                    end else begin                                     // 其余像素：彩色噪声
                        r = base_r + rn(41) - 20;
                        g = base_g + rn(41) - 20;
                        b = base_b + rn(41) - 20;
                    end
                    beat[24*k +: 24] = {clipv(r), clipv(g), clipv(b)};
                end
                @(posedge clk);
                if (rn(7) == 0) begin           // 随机 valid 气泡：本拍不送数据
                    tvalid <= 0; o_tready <= rn(2);
                    nbub = 1 + rn(3);
                    repeat (nbub) begin @(posedge clk); tvalid <= 0; o_tready <= rn(2); end
                end
                tvalid <= 1; tdata <= beat; tuser <= 0; o_tready <= rn(2);
            end
            @(posedge clk);
            tvalid <= 0;
        end
    endtask

    // 一整帧：tuser（1/8 概率 2 拍）+ 随机空档 + nb 拍；mid_tuser_at >= 0 时帧中插 tuser。
    task frame(input integer nb, input integer mid_tuser_at, input integer reset_at);
        begin
            tuser_pulse((rn(8) == 0) ? 2 : 1);
            idle(rn(4));
            if (mid_tuser_at >= 0 && mid_tuser_at < nb) begin
                send_beats(mid_tuser_at, reset_at);
                tuser_pulse(1);
                send_beats(nb - mid_tuser_at, -1);
            end else begin
                send_beats(nb, reset_at);
            end
            idle(rn(4));
        end
    endtask

    integer f;
    localparam integer NBA = HA * WA / 4;   // 512 拍/帧
    initial begin
        // 上电复位
        rst_n = 0; tdata = 0; tvalid = 0; tuser = 0; o_tready = 0;
        repeat (6) @(posedge clk);
        rst_n <= 1;
        repeat (4) @(posedge clk);

        // E：输入饿死（放在最前面：上电后延迟线 BRAM 内容还是 x，读到未写地址才能看到 x 传播）
        // 55 拍后停 valid 700 拍：读端继续读 addr 55..63，这些地址从未写过 -> O_tdata = x
        tuser_pulse(1);
        idle(rn(4));
        send_beats(55, -1);
        idle(700);
        send_beats(NBA - 55, -1);
        idle(rn(8));

        // A：基线色 6 帧（增益从复位值 1.0 收敛）
        for (f = 0; f < 6; f = f + 1) frame(NBA, -1, -1);

        // B：tuser 边界帧 —— 帧中 tuser、紧贴 tuser 发数据、普通帧
        frame(NBA, NBA / 2, -1);
        frame(NBA, 0, -1);
        frame(NBA, -1, -1);

        // C：帧中异步复位，复位后继续
        frame(NBA, -1, 180);
        frame(NBA, -1, -1);

        // D：换色，增益重新收敛后再跑若干帧
        base_r = 150; base_g = 100; base_b = 60;
        for (f = 0; f < 4; f = f + 1) frame(NBA, -1, -1);

        // E2：正常帧后再次长停 valid，覆盖读相位中途断流（此时 BRAM 已写满，输出应为旧数据而非 x）
        tuser_pulse(1);
        idle(rn(4));
        send_beats(300, -1);
        idle(80);
        send_beats(NBA - 300, -1);
        idle(rn(8));

        // F：收敛后继续跑几帧
        for (f = 0; f < 3; f = f + 1) frame(NBA, -1, -1);

        // G：高增益 / 饱和 —— 基色 (60,250,30) 让 gain_R≈4.2、gain_B≈8.3，R/B 输出大量 >=256，
        //    覆盖 new_r[11:8] ? 255 : new_r[7:0]（原 28 位写法 [27:24] ? 255 : [23:16]）的钳位分支
        base_r = 60; base_g = 250; base_b = 30;
        for (f = 0; f < 10; f = f + 1) frame(NBA, -1, -1);

        idle(20);
        equiv_finish(cyc);
    end
endmodule
