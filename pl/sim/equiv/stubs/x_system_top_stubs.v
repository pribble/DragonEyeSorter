// verifier-platform 独立行为桩：system_top 整顶层 TB 用（不综合，仅 iverilog 等价仿真）。
// design_0 = PS 块设计：自己产生 150M/50M/des 时钟、des 寄存器写总线、AXI 写响应、
//            camera_* / irq / hdmi / CAN 输出与 I2C 开漏行为；
// design_top_wrapper = MIPI+ISP 顶层：产生 HS RX 时钟与 ISP 视频流（tuser/tlast 帧结构）。
// 所有输出只由输入与固定 LFSR 决定（确定性），golden/新两侧共用。

module design_0 (
    // ---- HP0 AXI 从口（uiFDMA 写通道写进来；本桩只做随机 ready/响应） ----
    input  wire [32:0] hp1_w_us1_awaddr,
    input  wire [1:0]  hp1_w_us1_awburst,
    input  wire [3:0]  hp1_w_us1_awcache,
    input  wire [5:0]  hp1_w_us1_awid,
    input  wire [3:0]  hp1_w_us1_awlen,
    input  wire        hp1_w_us1_awlock,
    input  wire [2:0]  hp1_w_us1_awprot,
    input  wire [3:0]  hp1_w_us1_awqos,
    input  wire [1:0]  hp1_w_us1_awsize,
    input  wire        hp1_w_us1_awvalid,
    output reg         hp1_w_us1_awready,
    input  wire [63:0] hp1_w_us1_wdata,
    input  wire        hp1_w_us1_wlast,
    input  wire [7:0]  hp1_w_us1_wstrb,
    input  wire        hp1_w_us1_wvalid,
    output reg         hp1_w_us1_wready,
    output reg  [5:0]  hp1_w_us1_bid,
    output reg  [1:0]  hp1_w_us1_bresp,
    output reg         hp1_w_us1_bvalid,
    input  wire        hp1_w_us1_bready,
    // ---- 中断 / I2C inout ----
    output reg         f2p_irq,
    inout  wire        mipi_iic_i2c0_scl_io,
    inout  wire        mipi_iic_i2c0_sda_io,
    // ---- HDMI / 时钟 / PWM ----
    output wire [2:0]  O_hdmi_tx_p,
    output wire        O_hdmi_clk_p,
    output wire        clk_150m,
    output wire        p2f_clk0_50m,
    output wire        rstn_150m,
    output reg         pwm_out,
    // ---- 相机 / 帧写 / 寄存器总线 ----
    output wire [31:0] camera_base_addr,
    output reg         camera_start,
    output reg         camera_clear,
    output wire        des_clk,
    output reg         des_wr,
    output reg  [11:0] des_waddr,
    output reg  [31:0] des_wdata,
    output reg  [11:0] des_raddr,
    input  wire [31:0] des_rdata,
    // ---- CAN / lock 状态 ----
    output reg         can0_tx,
    input  wire        can0_rx,
    output reg         vtg_lock,
    output reg         axis_to_video_lock
);
    // 内部时钟（sim 专用振荡器；确定性）
    reg clk150 = 0;
    always #3 clk150 = ~clk150;
    reg clk50 = 0;
    always #10 clk50 = ~clk50;
    assign clk_150m = clk150;
    assign p2f_clk0_50m = clk50;
    assign rstn_150m = (cyc > 4);
    assign des_clk = clk150;
    assign camera_base_addr = 32'h1000_0000;
    assign O_hdmi_clk_p = clk50;
    assign O_hdmi_tx_p = {lfsr[0] ^ hp1_w_us1_wdata[5],
                          lfsr[1] ^ hp1_w_us1_bready,
                          vtg_lock ^ hp1_w_us1_awlen[1]};
    // 注：上面把帧写 AXI 握手/数据折进 HDMI 与 CAN 输出，使 system_top 内部帧写路径
    // （des_vision -> uidbuf_write -> uiFDMA -> AXI）的连线错误能在顶层 trace 中暴露。

    reg [31:0] lfsr = 32'h5EED_D00D;
    reg [15:0] cyc = 16'd0;
    reg        aw_seen = 0;
    reg [3:0]  wleft = 4'd0;

    // I2C 开漏模型：周期性拉低 / 高阻
    assign mipi_iic_i2c0_scl_io = (lfsr[8:6] == 3'b000) ? 1'b0 : 1'bz;
    assign mipi_iic_i2c0_sda_io = (lfsr[11:9] == 3'b000) ? 1'b0 : 1'bz;

    always @(posedge clk150) begin
        lfsr  <= {lfsr[30:0], lfsr[31] ^ lfsr[21] ^ lfsr[1] ^ lfsr[0]};
        cyc   <= cyc + 1'b1;
        // ---- des 寄存器总线：随机写覆盖 regs/io/vision，另外按剧本配置 stepper / servo / des_pwm ----
        des_wr    <= 1'b0;
        des_waddr <= lfsr[15:4];
        des_wdata <= lfsr;
        des_raddr <= lfsr[27:16];
        case (cyc)
            // stepper（0x2xx）：EN -> VMAX -> ACC -> VMIN -> TARGET -> CTRL(START|MODE)
            16'd100: begin des_waddr <= 12'h200; des_wdata <= 32'h0000_0001; des_wr <= 1'b1; end
            16'd102: begin des_waddr <= 12'h20C; des_wdata <= 32'h000F_0000; des_wr <= 1'b1; end
            16'd104: begin des_waddr <= 12'h210; des_wdata <= 32'h0000_8000; des_wr <= 1'b1; end
            16'd106: begin des_waddr <= 12'h214; des_wdata <= 32'h0008_0000; des_wr <= 1'b1; end
            16'd108: begin des_waddr <= 12'h208; des_wdata <= 32'h7FFF_FFFF; des_wr <= 1'b1; end
            16'd110: begin des_waddr <= 12'h200; des_wdata <= 32'h0000_0013; des_wr <= 1'b1; end
            // servo（0x1xx）：填 6 字节 TX buf（ID=2,LEN=3,INSTR=1,参数 0x04/0x05），再 GO
            16'd200: begin des_waddr <= 12'h140; des_wdata <= 32'h0103_0102; des_wr <= 1'b1; end
            16'd202: begin des_waddr <= 12'h144; des_wdata <= 32'h0000_0504; des_wr <= 1'b1; end
            16'd204: begin des_waddr <= 12'h100; des_wdata <= 32'h0000_0601; des_wr <= 1'b1; end
            // des_pwm（0x4xx）：EN -> 最小周期 2600us -> 两路宽度（周期首拍才生效）
            16'd300: begin des_waddr <= 12'h400; des_wdata <= 32'h0000_0003; des_wr <= 1'b1; end
            16'd302: begin des_waddr <= 12'h404; des_wdata <= 32'd2600;        des_wr <= 1'b1; end
            16'd304: begin des_waddr <= 12'h408; des_wdata <= 32'd256;         des_wr <= 1'b1; end
            16'd306: begin des_waddr <= 12'h40C; des_wdata <= 32'd512;         des_wr <= 1'b1; end
            default: des_wr <= (lfsr[4:0] == 5'b00000);   // 其余时间随机写（覆盖 des_regs/des_io/des_vision）
        endcase
        // ---- camera / irq / lock 状态 ----
        camera_start <= (lfsr[18:14] == 5'b00000);
        camera_clear <= (lfsr[23:19] == 5'b00000);
        f2p_irq      <= (lfsr[28:26] == 3'b000);
        vtg_lock     <= (cyc > 6);
        axis_to_video_lock <= (cyc > 10) & lfsr[12];
        can0_tx      <= can0_rx ^ lfsr[2]
                        ^ (hp1_w_us1_wvalid & hp1_w_us1_wready)      // 让帧写 AXI 数据流在顶层输出可观察
                        ^ (hp1_w_us1_awvalid & hp1_w_us1_awready)
                        ^ hp1_w_us1_wdata[0] ^ hp1_w_us1_wdata[17]
                        ^ hp1_w_us1_wstrb[3] ^ hp1_w_us1_awlen[0];
        pwm_out      <= lfsr[3];
        // ---- AXI 写从机：随机 ready + 收满 burst 后随机 BVALID ----
        hp1_w_us1_awready <= lfsr[5];
        hp1_w_us1_wready  <= lfsr[6];
        if (hp1_w_us1_awvalid && hp1_w_us1_awready) begin
            aw_seen <= 1;
            wleft   <= hp1_w_us1_awlen + 1'b1;
        end
        if (hp1_w_us1_wvalid && hp1_w_us1_wready) begin
            if (wleft != 0) wleft <= wleft - 1'b1;
            if (hp1_w_us1_wlast) wleft <= 0;
        end
        if (aw_seen && wleft == 0 && !hp1_w_us1_bvalid) begin
            hp1_w_us1_bvalid <= 1'b1;
            hp1_w_us1_bid    <= lfsr[9:4];
            hp1_w_us1_bresp  <= lfsr[13:12];
        end else if (hp1_w_us1_bvalid && hp1_w_us1_bready) begin
            hp1_w_us1_bvalid <= 1'b0;
        end
    end
endmodule


module design_top_wrapper (
    input  wire         I_sys_clk_156m,
    output wire         O_imx334_clk_24m,
    input  wire         I_clk_hs_p,
    input  wire [3:0]   I_data_hs_p,
    input  wire [3:0]   I_data_lp_p,
    input  wire [3:0]   I_data_lp_n,
    output reg          S_pll_lock,
    output wire [3:0]   S_lane_error,
    output wire         S_hs_rx_clk,
    output reg  [127:0] O_ISP_tdata,
    output reg          O_ISP_tlast,
    output reg          O_ISP_tuser,
    output reg          O_ISP_tvalid
);
    reg        clk24 = 0;
    always @(posedge I_sys_clk_156m) clk24 <= ~clk24;
    assign O_imx334_clk_24m = clk24;
    assign S_hs_rx_clk = I_clk_hs_p;         // HS RX 时钟 = D-PHY 参考时钟
    assign S_lane_error = {I_data_lp_n[0], I_data_lp_p[1], I_data_hs_p[2], I_data_lp_n[3]};

    reg [7:0] beat = 8'd0;
    reg       lock = 1'b0;
    always @(posedge I_clk_hs_p) begin
        S_pll_lock <= lock;
        beat  <= beat + 1'b1;
        // 64 拍一帧：tuser 在帧头，tlast 在帧尾；valid 占空比 3/4
        O_ISP_tuser  <= (beat == 8'd0);
        O_ISP_tlast  <= (beat == 8'd63);
        O_ISP_tvalid <= (beat[1:0] != 2'b00);
        O_ISP_tdata  <= {I_data_lp_p[3:0], I_data_hs_p[3:0], I_data_lp_n[3:0],
                         4'b0000, beat, beat, beat, beat,
                         beat, beat, beat, beat, beat, beat, beat, beat};
    end
    initial begin
        #200 lock = 1'b1;
    end
endmodule
