`timescale 1ns/1ps
// x_fs_cap — fs_cap.v 对抗性等价回归（verifier-platform 独立验证）
// 覆盖：VIDEO_ENABLE=1（实际例化值）的 VS 上升沿单拍捕获、VIDEO_ENABLE=0 的纯延迟模式；
//       VS 毛刺（1~2 拍）、长高/长低、随机 VS、复位中途打断、与 VS 边沿同拍拉复位；
//       输入 x 态（I_vs = x）传播。逐拍 trace 三个实例全部输出。
module tb_x_fs_cap;
    integer fd;
    integer cyc = 0;
    reg [1023:0] tf;

    reg clk = 0;
    always #5 clk = ~clk;

    reg rstn = 0, vs = 0;
    reg rstn2 = 0, vs2 = 0;
    wire fs_v, fs_d, fs_v2;

    fs_cap #(.VIDEO_ENABLE(1)) dut_v  (.I_clk(clk), .I_rstn(rstn),  .I_vs(vs),  .O_fs_cap(fs_v));
    fs_cap #(.VIDEO_ENABLE(0)) dut_d  (.I_clk(clk), .I_rstn(rstn),  .I_vs(vs),  .O_fs_cap(fs_d));
    fs_cap #(.VIDEO_ENABLE(1)) dut_v2 (.I_clk(clk), .I_rstn(rstn2), .I_vs(vs2), .O_fs_cap(fs_v2));

    reg [31:0] lfsr = 32'h7E57_C0DE;

    initial begin
        if (!$value$plusargs("TRACE=%s", tf)) tf = "trace.txt";
        fd = $fopen(tf, "w");
        if (fd == 0) begin $display("cannot open trace"); $finish; end
    end

    always @(negedge clk) begin
        cyc <= cyc + 1;
        $fdisplay(fd, "%0d %b %b %b", cyc, fs_v, fs_d, fs_v2);
        lfsr <= {lfsr[30:0], lfsr[31] ^ lfsr[21] ^ lfsr[1] ^ lfsr[0]};

        if (cyc < 3) begin
            rstn <= 1'b0; rstn2 <= 1'b0;
        end else if (cyc < 500) begin
            rstn <= 1'b1; rstn2 <= 1'b1;
            vs   <= (lfsr[3:0] == 4'h0) ? 1'b1 : 1'b0;   // 随机 VS 脉冲
            vs2  <= (lfsr[7:4] == 4'hF) ? 1'b0 : 1'b1;
        end else if (cyc == 500 || cyc == 501) begin
            rstn <= 1'b0;                               // 复位中途
            vs   <= 1'b1;                               // 复位时 VS 正跳变
        end else if (cyc == 502) begin
            rstn <= 1'b1;
        end else if (cyc >= 1000 && cyc < 1200) begin
            vs   <= 1'b1;                               // 长高
            vs2  <= 1'b0;
        end else if (cyc >= 1200 && cyc < 1400) begin
            vs   <= 1'b0;                               // 长低
            vs2  <= 1'b1;
        end else if (cyc >= 1400) begin
            // 1~3 拍毛刺
            vs   <= (cyc % 7 == 0) ? 1'b1 : 1'b0;
            vs2  <= (cyc % 5 == 0) ? 1'b1 : 1'b0;
        end
        if (cyc == 900) vs <= 1'bx;                     // x 态注入
        if (cyc == 901) vs <= 1'b0;
        if (cyc == 2200) rstn2 <= 1'b0;
        if (cyc == 2203) rstn2 <= 1'b1;
        if (cyc == 2999) begin
            $display("EQUIV_CYCLES %0d", cyc);
            $display("EQUIV_DONE");
            $fclose(fd);
            $finish;
        end
    end
endmodule
