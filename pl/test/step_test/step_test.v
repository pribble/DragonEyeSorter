/*
 * step_test.v  --  W1 传送带步进电机通路验证
 *
 * 目的：用 FPGA 直接发一串固定频率脉冲，确认
 *   12V 供电 / 电机相序 / 拨码电流档 / 光耦输入 / TB6600 本体
 * 五件事同时是好的。这是 W2 运动控制的第一块里程碑。
 *
 * 接线（共阴极，仅两根线，测试专用）：
 *   CEP PIN2 (GND) ---- TB6600  PUL-
 *   CEP PIN3 (Y12) ---- TB6600  PUL+
 *   DIR / ENA 全部悬空
 *
 * ⚠ 注意：这里是"共阴极 / 高有效"，和最终设计（74HCT245 + 5V 共阳极 /
 *   低有效）相反。测试用两根线是为了完全避开 5V，不需要电平转换、
 *   不需要万用表、对 BANK11 零风险。最终版仍按 §13.3d 的共阳接法。
 *
 * 时钟：I_sysclk = H16, 100MHz, LVCMOS33
 */

module step_test(
    input  wire I_sysclk,      // H16  100MHz
    output wire O_step_pul     // Y12  -> TB6600 PUL+
);

    // ---- 速度选择 ----------------------------------------------------
    // 输出频率 = 100MHz / 2^(TAP+1)
    //
    //   TAP   频率      16细分(3200/圈)     1细分(200/圈)
    //   16    763 Hz    14.3 RPM            229 RPM
    //   17    381 Hz     7.1 RPM            114 RPM   <-- 默认，最稳
    //   18    191 Hz     3.6 RPM             57 RPM
    //   19     95 Hz     1.8 RPM             29 RPM
    //
    // 步进电机不能从静止直接起高速。381Hz 在 16 细分下只有 24 整步/秒，
    // 起步完全没有压力。先用 17 跑通，再往上加。
    parameter TAP = 17;

    reg [31:0] cnt = 32'd0;

    always @(posedge I_sysclk) begin
        cnt <= cnt + 1'b1;
    end

    assign O_step_pul = cnt[TAP];

endmodule
