// servo_bus_master.v — 德晟 DS-SY15A 总线主机（自研，P0）
// 协议：v4.01，115200 8N1 半双工，帧头 FF FF，SYNC WRITE(0x83) 广播同步写；
// 回读位置(0x38)/电流(0x2E)/温度(0x3F)。总线经 BSS138 电平转换接 CEP PIN19（docs/05）。
// 职责边界：PS 算 IK/路径点，写入 ARM_POS[0..5]；本模块在 ARM_GO 写 1 时组帧串行发送，
// 随后转入遥测轮询（逐个 ID READ），回填 ARM_FB_*。
// 状态：骨架——发送路径（SYNC WRITE 组帧 + TX 移位）先行，遥测与超时计数下一版。
`timescale 1ns / 1ps
module servo_bus_master #(
    parameter CLK_HZ = 100_000_000,
    parameter BAUD   = 115_200
)(
    input  wire        clk,
    input  wire        rst_n,
    // 寄存器接口（clk 域）
    input  wire [5:0]  arm_go,          // 写 1 触发一次 SYNC WRITE（各位对应轴，预留分组）
    input  wire [47:0] arm_pos,         // 6 轴目标（每轴 8bit 协议原始值，PS 侧换算）
    input  wire [15:0] arm_time,        // 运动时长（协议参数）
    output reg         busy,
    output reg         done,
    output reg         timeout,         // 帧间无应答超时（遥测阶段使用）
    // 单线半双工（FPGA 侧驱动 BSS138 低压侧）
    output reg         tx_serial,
    input  wire        rx_serial
);
    localparam BIT_TICK = CLK_HZ / BAUD;

    // 帧格式（SYNC WRITE 写 6 轴位置 + 时长，一次广播）：
    // FF FF FE 17 83 26 06 <p1..p6> <t_lo t_hi> chk
    // 长度 0x17 = 参数字节数；ID 0xFE 广播；起始地址 0x26（v4.01 位置组），逐轴核对后定版
    localparam FRAME_LEN = 16;
    reg [7:0]  frame [0:FRAME_LEN-1];
    reg [3:0]  fidx;
    reg [3:0]  bidx;
    reg [15:0] tick;
    reg        sending;

    // TX 移位：帧内容 → tx_serial
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tx_serial <= 1'b1; // 空闲高
            tick      <= 16'd0;
            fidx      <= 4'd0;
            bidx      <= 4'd0;
            sending   <= 1'b0;
            busy      <= 1'b0;
            done      <= 1'b0;
            timeout   <= 1'b0;
        end else begin
            done <= 1'b0;
            if (|arm_go && !sending) begin
                // 组帧（校验和 = ~(id+len+instr+params) & FF，实现时填）
                frame[0]<=8'hFF; frame[1]<=8'hFF; frame[2]<=8'hFE; frame[3]<=8'h17;
                frame[4]<=8'h83; frame[5]<=8'h26; frame[6]<=8'h06;
                frame[7]<=arm_pos[7:0];   frame[8]<=arm_pos[15:8];
                frame[9]<=arm_pos[23:16]; frame[10]<=arm_pos[31:24];
                frame[11]<=arm_pos[39:32];frame[12]<=arm_pos[47:40];
                frame[13]<=arm_time[7:0]; frame[14]<=arm_time[15:8];
                frame[15]<=8'h00; // TODO: 校验和
                sending <= 1'b1; busy <= 1'b1;
                fidx <= 4'd0; bidx <= 4'd0; tick <= 16'd0;
                tx_serial <= 1'b0; // 起始位
            end else if (sending) begin
                if (tick == BIT_TICK-1) begin
                    tick <= 16'd0;
                    if (bidx == 4'd8) begin // 字节完（含1起始+8数据，停止位由空闲高实现）
                        bidx <= 4'd0;
                        if (fidx == FRAME_LEN-1) begin
                            sending <= 1'b0; busy <= 1'b0; done <= 1'b1;
                            tx_serial <= 1'b1;
                        end else begin
                            fidx <= fidx + 4'd1;
                            tx_serial <= 1'b0; // 下一字节起始位
                        end
                    end else begin
                        tx_serial <= frame[fidx][bidx];
                        bidx <= bidx + 4'd1;
                    end
                end else begin
                    tick <= tick + 16'd1;
                end
            end else begin
                tx_serial <= 1'b1;
            end
        end
    end

    // TODO（下一版）：RX 移位 + 遥测轮询状态机 + 超时/校验计数 → ARM_FB_*/BUS_STAT

endmodule
