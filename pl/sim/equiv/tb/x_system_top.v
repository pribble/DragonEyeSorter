`timescale 1ns/1ps
// x_system_top — system_top.v 整顶层行为对照（verifier-platform 独立验证）
// golden vs 工作区 system_top：同一 TB + 同一组「PS 块设计 / MIPI 顶层」行为桩（golden、新两侧共用），
// 其余依赖（自研 des_*、帧写 DDR 三模块 + wfifo）用 golden/工作区各自版本 → 相当于整机集成等价。
// 覆盖：PS 侧 des 寄存器写总线（随机写地址/数据）驱动 des_regs/stepper/servo/des_pwm/des_vision；
//       MIPI 侧 ISP 视频流经 des_vision → uidbuf_write → uiFDMA → AXI 写应答闭环；
//       led 心跳/override、STEP 共阴反相、SERVO 开漏、FLAP PWM、CAN 环回、HDMI/相机时钟输出、
//       现场 IO 输入抖动。逐拍 trace system_top 全部输出（含 inout 的 z 态）。
module tb_x_system_top;
    integer fd;
    integer cyc = 0;
    reg [1023:0] tf;

    reg clk_hs = 0;
    always #4 clk_hs = ~clk_hs;      // D-PHY 参考时钟 / HS RX 时钟（桩直通）

    reg [3:0] data_hs_p = 4'b1010;
    reg [3:0] data_lp_p = 4'b1100;
    reg [3:0] data_lp_n = 4'b0011;

    reg estop_in = 1;
    reg enc_a = 0, enc_b = 0, enc_z = 0;
    reg ir1_in = 1, limit1_in = 1;
    reg can0_rxd = 0;

    // inout
    wire scl0, sda0;

    wire [2:0] o_hdmi_tx_p;
    wire       o_hdmi_clk_p;
    wire [2:0] led;
    wire       step_pul, step_dir, step_ena;
    wire       servo_bus;
    wire       flap1_pwm, flap2_pwm;
    wire       can0_txd;
    wire       o_imx334_clk_24m, o_imax334_rst_n;

    system_top dut (
        .O_hdmi_tx_p(o_hdmi_tx_p), .O_hdmi_clk_p(o_hdmi_clk_p),
        .SCL0(scl0), .SDA0(sda0),
        .led(led),
        .STEP_PUL(step_pul), .STEP_DIR(step_dir), .STEP_ENA(step_ena),
        .ESTOP_IN(estop_in),
        .ENC_A(enc_a), .ENC_B(enc_b), .ENC_Z(enc_z),
        .IR1_IN(ir1_in), .LIMIT1_IN(limit1_in),
        .SERVO_BUS(servo_bus),
        .FLAP1_PWM(flap1_pwm), .FLAP2_PWM(flap2_pwm),
        .CAN0_TXD(can0_txd), .CAN0_RXD(can0_rxd),
        .I_clk_hs_p(clk_hs),
        .I_data_hs_p(data_hs_p), .I_data_lp_p(data_lp_p), .I_data_lp_n(data_lp_n),
        .O_imx334_clk_24m(o_imx334_clk_24m),
        .O_imax334_rst_n(o_imax334_rst_n)
    );

    reg [31:0] lfsr = 32'hC001_D00D;
    reg [9:0]  slow = 10'd0;

    initial begin
        if (!$value$plusargs("TRACE=%s", tf)) tf = "trace.txt";
        fd = $fopen(tf, "w");
        if (fd == 0) begin $display("cannot open trace"); $finish; end
    end

    always @(negedge clk_hs) begin
        cyc  <= cyc + 1;
        slow <= slow + 1'b1;
        $fdisplay(fd, "%0d %b %b %h %b %b %b %b %b %b %b %b %b %b", cyc,
                  o_hdmi_tx_p, o_hdmi_clk_p, led, step_pul, step_dir, step_ena,
                  servo_bus, flap1_pwm, flap2_pwm, can0_txd,
                  o_imx334_clk_24m, o_imax334_rst_n, scl0, sda0);
        lfsr <= {lfsr[30:0], lfsr[31] ^ lfsr[21] ^ lfsr[1] ^ lfsr[0]};
        // 现场 IO 抖动（慢节奏，避免每拍都变）
        if (slow[4:0] == 5'd0) begin
            estop_in   <= (lfsr[0:0] == 1'b1);
            enc_a      <= lfsr[1];
            enc_b      <= lfsr[2];
            enc_z      <= lfsr[3];
            ir1_in     <= lfsr[4];
            limit1_in  <= lfsr[5];
            can0_rxd   <= lfsr[6];
            data_hs_p  <= lfsr[11:8];
            data_lp_p  <= lfsr[15:12];
            data_lp_n  <= lfsr[19:16];
        end
        if (cyc == 200000) begin
            $display("EQUIV_CYCLES %0d", cyc);
            $display("EQUIV_DONE");
            $fclose(fd);
            $finish;
        end
    end
endmodule
