// pl/sim/equiv/tb/equiv_tb_common.vh —— 36 个等价回归 TB 的公共样板（`include 写在 module 内）
//
// 只抽「与激励序列、trace 每拍格式无关」的部分：trace 文件句柄/文件名、打开 task、
// 收尾 task（EQUIV_CYCLES / EQUIV_DONE / fclose / finish 的文本与顺序与原先手写完全一致）。
// 各 TB 的 trace 格式字符串、激励、cycle 计数增量、LFSR 等仍留在 TB 原地 —— 改本文件不得改变行为。
// run_equiv.sh 编译时自动加 -I<tb 目录>，因此 `include "equiv_tb_common.vh"` 与 cwd 无关。
//
// 用法（TB 内）：
//     `include "equiv_tb_common.vh"      // 得到 fd / tf / cyc，以及下面 2 个 task
//     initial equiv_trace_open;          // 打开 $value$plusargs("TRACE=%s") 指定的 trace
//     always @(posedge clk) begin cyc <= cyc + 1; $fdisplay(...); end
//     equiv_finish(cyc);                 // 收尾（等价于原来的 $display×2 + $fclose + $finish）
// 需要多个计数器（如 wfifo 的 wcyc/rcyc）或自定义循环变量的 TB 自己声明额外变量。
`ifndef EQUIV_TB_COMMON_VH
`define EQUIV_TB_COMMON_VH

integer fd;          // trace 文件句柄
integer cyc = 0;     // 逐拍自增计数（trace 首列，EQUIV_CYCLES 用）
reg [1023:0] tf;     // trace 文件名

task equiv_trace_open;
    begin
        if (!$value$plusargs("TRACE=%s", tf)) tf = "trace.txt";
        fd = $fopen(tf, "w");
        if (fd == 0) begin $display("cannot open trace"); $finish; end
    end
endtask

// 打印等价判据并退出：stdout 文本与顺序、trace 关闭时机都和原先手写的一致
task equiv_finish;
    input integer cycles;
    begin
        $display("EQUIV_CYCLES %0d", cycles);
        $display("EQUIV_DONE");
        $fclose(fd);
        $finish;
    end
endtask

`endif
