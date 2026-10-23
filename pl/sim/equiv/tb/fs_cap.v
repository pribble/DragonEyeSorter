`timescale 1ns/1ps
// fs_cap 等价回归：VS 同步边沿 + 帧起始单拍脉冲（VIDEO_ENABLE=1），
// 以及纯 4 级延迟模式（VIDEO_ENABLE=0）。
// 覆盖：上电复位保持、随机 VS 边沿与 1~2 拍毛刺、长帧高/低电平、复位中途打断、
//       复位与 VS 边沿不同相位、以及输入 x 态传播。trace 逐拍打印两个配置的全部输出。
module tb_fs_cap;
    `include "equiv_tb_common.vh"

    reg clk = 0;
    reg rstn = 0;
    reg vs = 0;

    wire fs_v;   // VIDEO_ENABLE = 1（实际例化值）
    wire fs_d;   // VIDEO_ENABLE = 0（延迟模式）

    // 第二组：复位序列与 VS 独立，覆盖复位相位不同的情况
    reg rstn2 = 0;
    reg vs2 = 0;
    wire fs_v2;

    fs_cap #(.VIDEO_ENABLE(1)) dut_v  (.I_clk(clk), .I_rstn(rstn),  .I_vs(vs),  .O_fs_cap(fs_v));
    fs_cap #(.VIDEO_ENABLE(0)) dut_d  (.I_clk(clk), .I_rstn(rstn),  .I_vs(vs),  .O_fs_cap(fs_d));
    fs_cap #(.VIDEO_ENABLE(1)) dut_v2 (.I_clk(clk), .I_rstn(rstn2), .I_vs(vs2), .O_fs_cap(fs_v2));

    always #5 clk = ~clk;

    // 采样在 negedge：本拍所有输出（含 x/z）都已稳定，逐拍写入 trace
    always @(negedge clk) begin
        cyc <= cyc + 1;
        $fdisplay(fd, "%0d %b %b %b", cyc, fs_v, fs_d, fs_v2);
    end

    // 确定性的 32bit LFSR，避免依赖 $random 的实现差异
    reg [31:0] rng = 32'h1234_5678;
    function [31:0] rnd;
        input integer dummy;
        begin
            rng = {rng[30:0], rng[31] ^ rng[21] ^ rng[1] ^ rng[0]};
            rnd = rng;
        end
    endfunction

    task tick;
        begin
            @(posedge clk);
            #2;  // 在 posedge 之后 2ns 改输入，保证下一个 posedge 的建立时间
        end
    endtask

    integer i, k, run;

    initial begin
        equiv_trace_open;

        // 上电复位保持，VS 保持低
        rstn = 0; rstn2 = 0; vs = 0; vs2 = 0;
        repeat (4) tick();

        // 释放复位；覆盖 VS 上升沿和空闲保持
        rstn = 1; rstn2 = 1;
        repeat (3) tick();
        vs = 1; repeat (6) tick();
        vs = 0; repeat (5) tick();

        // 随机边沿：run 长度 1~9，短脉冲可产生 1 拍毛刺；长电平覆盖帧空闲
        for (i = 0; i < 600; i = i + 1) begin
            run = rnd(0) % 9 + 1;
            if (rnd(0) % 23 == 0) begin
                // 长保持（模拟帧消隐/帧间隔）
                run = 30 + (rnd(0) % 40);
            end
            vs = ~vs;
            if (rnd(0) % 11 == 0) begin
                // 反向单拍毛刺：只保持 1 拍再恢复
                repeat (run) tick();
                vs = ~vs;
                repeat (1) tick();
            end else begin
                repeat (run) tick();
            end
            if (rnd(0) % 37 == 0) begin
                // 中途复位 1~5 拍，复位期间 VS 继续随机翻转
                rstn = 0;
                k = rnd(0) % 5 + 1;
                repeat (k) begin
                    vs = rnd(0) % 3 == 0;
                    tick();
                end
                rstn = 1;
            end
            if (rnd(0) % 41 == 0) begin
                // 第二组：复位与 VS 不同相位，复位期间 VS 保持原值
                rstn2 = 0;
                vs2 = rnd(0) % 2;
                repeat (3) tick();
                rstn2 = 1;
            end
            // 第二组 VS 独立随机翻转
            if (rnd(0) % 5 == 0) vs2 = ~vs2;
        end

        // 复位期间 VS 为 x：x 态在复位优先级下不应污染输出
        vs = 1'bx;
        repeat (3) tick();
        rstn = 0;
        repeat (2) tick();
        rstn = 1;
        repeat (2) tick();
        vs = 0;
        repeat (4) tick();

        // 收尾：保证最后一次 VS 上升沿之后的 4 级同步链走完
        vs = 1; repeat (3) tick();
        vs = 0; repeat (10) tick();

        equiv_finish(cyc);
    end
endmodule
