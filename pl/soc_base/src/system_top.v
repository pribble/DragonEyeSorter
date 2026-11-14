// DragonEyeSorter 顶层：PS 块设计（design_0：MIPI RX / ISP / VDMA / HDMI / NPU）
// + 厂家明文 ISP/帧写 RTL + 自研 des_* 寄存器总线与运动/IO 模块的集成。
module system_top (
    output [2:0] O_hdmi_tx_p, output       O_hdmi_clk_p,
    inout        SCL0, inout        SDA0,
    output [2:0] led, output       STEP_PUL,
    output       STEP_DIR, output       STEP_ENA,
    input        ESTOP_IN, input        ENC_A,
    input        ENC_B, input        ENC_Z,
    input        IR1_IN, input        LIMIT1_IN,
    inout        SERVO_BUS, output       FLAP1_PWM,
    output       FLAP2_PWM, output       TRIG_OUT,
    output       CAN0_TXD,
    input        CAN0_RXD, output       RS485_TXD,
    input        RS485_RXD, output       RS485_DE,
    input  wire       I_clk_hs_p, input  wire [3:0] I_data_hs_p,
    input  wire [3:0] I_data_lp_p, input  wire [3:0] I_data_lp_n,
    output wire O_imx334_clk_24m,
    output      O_imax334_rst_n
);
    parameter AXI_ADDR_WIDTH = 33;
    parameter AXI_DATA_WIDTH = 256;
    parameter X_SIZE = 1920;
    parameter Y_SIZE = 1080;
    wire [32:0] slave_hp0_axi_awaddr;
    wire [63:0] slave_hp0_axi_wdata;
    wire  [7:0] slave_hp0_axi_wstrb;
    wire  [5:0] slave_hp0_axi_awid, slave_hp0_axi_bid;
    wire  [3:0] slave_hp0_axi_awcache, slave_hp0_axi_awlen, slave_hp0_axi_awqos;
    wire  [2:0] slave_hp0_axi_awprot;
    wire  [1:0] slave_hp0_axi_awburst, slave_hp0_axi_awsize, slave_hp0_axi_bresp;
    wire        slave_hp0_axi_awlock, slave_hp0_axi_awvalid, slave_hp0_axi_awready,
                slave_hp0_axi_bready, slave_hp0_axi_bvalid, slave_hp0_axi_wlast,
                slave_hp0_axi_wvalid, slave_hp0_axi_wready;
    wire        f2p_irq;
    wire        pll_clk_150;          //synthesis keep
    wire        pll_clk_150_aresetn;
    wire        p2f_clk0_50m;
    wire [31:0] camera_base_addr;
    wire        camera_start, camera_clear;
    reg [27:0] des_hb_cnt = 28'd0;
    always @(posedge p2f_clk0_50m) des_hb_cnt <= des_hb_cnt + 1'b1;
    wire        des_clk, des_wr;
    wire [11:0] des_waddr, des_raddr;
    wire [31:0] des_wdata, des_rdata;
    wire        des_led_ovr, des_led_val;
    assign led[0] = des_led_ovr ? des_led_val : des_hb_cnt[27];
    design_0 design_0_inst (
        .hp1_w_us1_awaddr (slave_hp0_axi_awaddr),
        .hp1_w_us1_awburst(slave_hp0_axi_awburst), .hp1_w_us1_awcache(slave_hp0_axi_awcache),
        .hp1_w_us1_awid   (slave_hp0_axi_awid),
        .hp1_w_us1_awlen  (slave_hp0_axi_awlen),
        .hp1_w_us1_awlock (slave_hp0_axi_awlock),
        .hp1_w_us1_awprot (slave_hp0_axi_awprot),
        .hp1_w_us1_awqos  (slave_hp0_axi_awqos),
        .hp1_w_us1_awsize (slave_hp0_axi_awsize),
        .hp1_w_us1_awvalid(slave_hp0_axi_awvalid), .hp1_w_us1_awready(slave_hp0_axi_awready),
        .hp1_w_us1_wdata  (slave_hp0_axi_wdata),
        .hp1_w_us1_wlast  (slave_hp0_axi_wlast),
        .hp1_w_us1_wstrb  (slave_hp0_axi_wstrb),
        .hp1_w_us1_wvalid (slave_hp0_axi_wvalid),
        .hp1_w_us1_wready (slave_hp0_axi_wready),
        .hp1_w_us1_bid    (slave_hp0_axi_bid),
        .hp1_w_us1_bresp  (slave_hp0_axi_bresp),
        .hp1_w_us1_bvalid (slave_hp0_axi_bvalid),
        .hp1_w_us1_bready (slave_hp0_axi_bready),
        .f2p_irq          (f2p_irq),
        .mipi_iic_i2c0_scl_io(SCL0), .mipi_iic_i2c0_sda_io(SDA0),
        .O_hdmi_tx_p      (O_hdmi_tx_p),
        .O_hdmi_clk_p     (O_hdmi_clk_p),
        .clk_150m         (pll_clk_150),
        .p2f_clk0_50m     (p2f_clk0_50m),
        .rstn_150m        (pll_clk_150_aresetn),
        .vtg_lock         (led[1]),
        .axis_to_video_lock(led[2]),
        .camera_base_addr (camera_base_addr),
        .camera_start     (camera_start),
        .camera_clear     (camera_clear),
        .des_clk          (des_clk),
        .des_wr           (des_wr),
        .des_waddr        (des_waddr),
        .des_wdata        (des_wdata),
        .des_raddr        (des_raddr),
        .des_rdata        (des_rdata),
        .can0_tx          (CAN0_TXD),
        .can0_rx          (CAN0_RXD)
    );
    wire        ud_wclk;              //synthesis keep
    wire        ud_wvs;               //synthesis keep
    wire        ud_wde;               //synthesis keep
    wire [127:0] ud_wdata;
    wire        S_hs_rx_clk;
    wire [127:0] S_ISP_O_tdata;
    wire        S_ISP_O_tlast;
    wire        S_ISP_O_tuser;
    wire        S_ISP_O_tvalid;
    assign O_imax334_rst_n = 1'b1;
    design_top_wrapper design_top_wrapper_u (
        .I_sys_clk_156m   (p2f_clk0_50m),
        .O_imx334_clk_24m (O_imx334_clk_24m),
        .I_clk_hs_p       (I_clk_hs_p),
        .I_data_hs_p      (I_data_hs_p),
        .I_data_lp_p      (I_data_lp_p),
        .I_data_lp_n      (I_data_lp_n),
        .S_hs_rx_clk      (S_hs_rx_clk),
        .O_ISP_tdata      (S_ISP_O_tdata),
        .O_ISP_tlast      (S_ISP_O_tlast),
        .O_ISP_tuser      (S_ISP_O_tuser),
        .O_ISP_tvalid     (S_ISP_O_tvalid)
    );
    wire [127:0] vis_tdata;
    wire         vis_tvalid, vis_tuser;
    wire         cap_start_tgl;
    reg          cap_done_d = 1'b0, cap_done_tgl = 1'b0;
    reg          des_sof_d = 1'b0, des_sof_toggle = 1'b0;
    assign ud_wclk = S_hs_rx_clk;
    assign ud_wvs  = vis_tuser;
    assign ud_wde  = vis_tvalid;
    assign ud_wdata = vis_tdata;      //synthesis keep
    always @(posedge pll_clk_150) begin
        cap_done_d <= f2p_irq;
        if (f2p_irq && !cap_done_d) cap_done_tgl <= ~cap_done_tgl;
    end
    always @(posedge S_hs_rx_clk) begin
        des_sof_d <= S_ISP_O_tuser;
        if (S_ISP_O_tuser && !des_sof_d) des_sof_toggle <= ~des_sof_toggle;
    end
    wire [AXI_ADDR_WIDTH-1:0]   fdma_waddr;
    wire [15:0]                 fdma_wsize;
    wire [AXI_DATA_WIDTH/4-1:0] fdma_wdata;
    wire                        fdma_wareq, fdma_wbusy, fdma_wvalid, wbuf_sync;
    uidbuf_write #(
        .VIDEO_ENABLE    (1'b1),
        .ENABLE_WRITE    (1'b1),
        .ENABLE_READ     (1'b1),
        .AXI_DATA_WIDTH  (AXI_DATA_WIDTH / 4),
        .AXI_ADDR_WIDTH  (AXI_ADDR_WIDTH),
        .W_BUFDEPTH      (1024 * 4),
        .W_DATAWIDTH     (128),
        .W_DSIZEBITS     (25),
        .W_XDIV          (1),
        .W_XSIZE         (X_SIZE / (128 / 32)),
        .W_XSTRIDE       (X_SIZE / (128 / 32)),
        .W_YSIZE         (Y_SIZE)
    ) uidbuf_write_u0 (
        .W_BASEADDR      ({1'b0, camera_base_addr}),
        .W_BUFSIZE       (6),
        .I_ui_clk        (pll_clk_150),
        .I_ui_rstn       (pll_clk_150_aresetn),
        .I_W_FS          (ud_wvs),
        .I_W_clk         (ud_wclk),
        .I_W_wren        (ud_wde),
        .I_W_data        (ud_wdata),
        .O_W_sync_cnt    (wbuf_sync),
        .I_W_buf         (wbuf_sync),
        .O_W_full        (),
        .O_fdma_waddr    (fdma_waddr),
        .O_fdma_wareq    (fdma_wareq),
        .O_fdma_wsize    (fdma_wsize),
        .I_fdma_wbusy    (fdma_wbusy),
        .O_fdma_wdata    (fdma_wdata),
        .I_fdma_wvalid   (fdma_wvalid),
        .O_W_start_tgl   (cap_start_tgl),
        .camera_start    (camera_start),
        .camera_done     (f2p_irq),
        .camera_clear_intr(camera_clear)
    );
    uiFDMA #(
        .M_AXI_ID_WIDTH    (6),
        .M_AXI_ADDR_WIDTH  (AXI_ADDR_WIDTH),
        .M_AXI_DATA_WIDTH  (AXI_DATA_WIDTH / 4),
        .M_AXI_MAX_BURST_LEN(16)
    ) uiFDMA_inst (
        .I_fdma_waddr      (fdma_waddr),
        .I_fdma_wareq      (fdma_wareq),
        .I_fdma_wsize      (fdma_wsize),
        .O_fdma_wbusy      (fdma_wbusy),
        .I_fdma_wdata      (fdma_wdata),
        .O_fdma_wvalid     (fdma_wvalid),
        .I_fdma_wready     (1'b1),
        .I_fdma_raddr      ({AXI_ADDR_WIDTH{1'b0}}),
        .I_fdma_rareq      (1'b0),
        .I_fdma_rsize      (16'd0),
        .O_fdma_rbusy      (),
        .O_fdma_rdata      (),
        .O_fdma_rvalid     (),
        .I_fdma_rready     (1'b1),
        .M_AXI_ACLK        (pll_clk_150),
        .M_AXI_ARESETN     (pll_clk_150_aresetn),
        .M_AXI_AWID        (slave_hp0_axi_awid),
        .M_AXI_AWADDR      (slave_hp0_axi_awaddr),
        .M_AXI_AWLEN       (slave_hp0_axi_awlen),
        .M_AXI_AWSIZE      (slave_hp0_axi_awsize),
        .M_AXI_AWBURST     (slave_hp0_axi_awburst),
        .M_AXI_AWLOCK      (slave_hp0_axi_awlock),
        .M_AXI_AWCACHE     (slave_hp0_axi_awcache),
        .M_AXI_AWPROT      (slave_hp0_axi_awprot),
        .M_AXI_AWVALID     (slave_hp0_axi_awvalid),
        .M_AXI_AWREADY     (slave_hp0_axi_awready),
        .M_AXI_WDATA       (slave_hp0_axi_wdata),
        .M_AXI_WSTRB       (slave_hp0_axi_wstrb),
        .M_AXI_WLAST       (slave_hp0_axi_wlast),
        .M_AXI_WVALID      (slave_hp0_axi_wvalid),
        .M_AXI_WREADY      (slave_hp0_axi_wready),
        .M_AXI_BID         (slave_hp0_axi_bid),
        .M_AXI_BRESP       (slave_hp0_axi_bresp),
        .M_AXI_BVALID      (slave_hp0_axi_bvalid),
        .M_AXI_BREADY      (slave_hp0_axi_bready)
    );
    wire [31:0] regs_rdata, sb_rdata, st_rdata, vis_rdata, pwm_rdata, io_rdata, rs_rdata, des_ts, st_pos, io_enc;
    wire [31:0] bpi_rdata, tg_rdata;
    wire        sb_drive_low, st_pul_n, st_dir_n, st_ena, des_estop, st_cruise;
    wire signed [16:0] bpi_scale;
    wire [1:0]  tg_ld;
    wire [23:0] tg_us;
    assign des_rdata = (des_raddr[11:8] == 4'h1) ? sb_rdata :
                       (des_raddr[11:8] == 4'h2) ? st_rdata :
                       (des_raddr[11:8] == 4'h3) ? vis_rdata :
                       (des_raddr[11:8] == 4'h4) ? pwm_rdata :
                       (des_raddr[11:8] == 4'h5) ? io_rdata :
                       (des_raddr[11:8] == 4'h6) ? rs_rdata :
                       (des_raddr[11:8] == 4'h7) ? bpi_rdata :
                       (des_raddr[11:8] == 4'h8) ? tg_rdata : regs_rdata;
    des_regs #(.VERSION(32'd17)) des_regs_inst (
        .clk(des_clk), .wr(des_wr && des_waddr[11:8] == 4'h0), .waddr(des_waddr), .wdata(des_wdata), .raddr(des_raddr), .rdata(regs_rdata), .sof_toggle(des_sof_toggle), .ts(des_ts),
        .cap_start_toggle(cap_start_tgl), .cap_done_toggle(cap_done_tgl), .pos(st_pos), .enc(io_enc),
        .led_ovr(des_led_ovr), .led_val(des_led_val)
    );
    servo_bus servo_bus_inst (
        .clk(des_clk), .wr(des_wr && des_waddr[11:8] == 4'h1), .waddr(des_waddr[7:0]), .wdata(des_wdata),
        .raddr(des_raddr[7:0]), .rdata(sb_rdata), .ts(des_ts), .bus_i(SERVO_BUS), .bus_drive_low(sb_drive_low)
    );
    assign SERVO_BUS = sb_drive_low ? 1'b0 : 1'bz;
    stepper_ctrl stepper_inst (
        .clk(des_clk), .wr(des_wr && des_waddr[11:8] == 4'h2), .waddr(des_waddr[7:0]), .wdata(des_wdata), .raddr(des_raddr[7:0]), .rdata(st_rdata), .estop(des_estop),
        .pul_n(st_pul_n), .dir_n(st_dir_n), .ena_o(st_ena), .pos_o(st_pos),
        .vscale(bpi_scale), .moving_o(), .cruise_o(st_cruise)
    );
    des_bpi bpi_inst (   // v17: belt speed PI, off (scale 0) until the PS enables it
        .clk(des_clk), .wr(des_wr && des_waddr[11:8] == 4'h7), .waddr(des_waddr[7:0]), .wdata(des_wdata), .raddr(des_raddr[7:0]), .rdata(bpi_rdata), .ts(des_ts),
        .enc(io_enc), .st_cruise(st_cruise), .scale_o(bpi_scale)
    );
    des_ptrig ptrig_inst (   // v17: belt position compare trigger, nothing armed after reset
        .clk(des_clk), .wr(des_wr && des_waddr[11:8] == 4'h8), .waddr(des_waddr[7:0]), .wdata(des_wdata), .raddr(des_raddr[7:0]), .rdata(tg_rdata), .ts(des_ts),
        .pos(st_pos), .enc(io_enc), .pwm_ld(tg_ld), .pwm_us(tg_us), .trig_o(TRIG_OUT)
    );
    des_vision vision_inst (
        .clk(des_clk), .wr(des_wr && des_waddr[11:8] == 4'h3), .waddr(des_waddr[7:0]), .wdata(des_wdata), .raddr(des_raddr[7:0]), .rdata(vis_rdata),
        .pclk(S_hs_rx_clk), .s_tdata(S_ISP_O_tdata), .s_tvalid(S_ISP_O_tvalid), .s_tuser(S_ISP_O_tuser),
        .s_tlast(S_ISP_O_tlast), .m_tdata(vis_tdata), .m_tvalid(vis_tvalid), .m_tuser(vis_tuser)
    );
    des_io io_inst (
        .clk(des_clk), .wr(des_wr && des_waddr[11:8] == 4'h5), .waddr(des_waddr[7:0]), .wdata(des_wdata), .raddr(des_raddr[7:0]), .rdata(io_rdata), .ts(des_ts), .pos(st_pos),
        .estop_n(ESTOP_IN), .ir1_in(IR1_IN), .limit1_in(LIMIT1_IN), .enc_a(ENC_A), .enc_b(ENC_B), .enc_z(ENC_Z),
        .estop_out(des_estop), .enc_o(io_enc)
    );
    des_485 rs485_inst (
        .clk(des_clk), .wr(des_wr && des_waddr[11:8] == 4'h6), .waddr(des_waddr[7:0]), .wdata(des_wdata), .raddr(des_raddr[7:0]), .rdata(rs_rdata), .ts(des_ts),
        .rx_i(RS485_RXD), .tx_o(RS485_TXD), .de_o(RS485_DE)
    );
    des_pwm #(.SLEW_EN(0)) pwm_inst (   // SLEW left out to save area (not used by the PS yet)
        .clk(des_clk), .wr(des_wr && des_waddr[11:8] == 4'h4), .waddr(des_waddr[7:0]), .wdata(des_wdata),
        .raddr(des_raddr[7:0]), .rdata(pwm_rdata), .pwm({FLAP2_PWM, FLAP1_PWM}), .xld(tg_ld), .xus(tg_us)
    );
    localparam STEP_COMMON_CATHODE = 1;
    assign STEP_PUL = STEP_COMMON_CATHODE ? ~st_pul_n : st_pul_n;
    assign STEP_DIR = st_dir_n;
    assign STEP_ENA = STEP_COMMON_CATHODE ? ~st_ena : st_ena;
endmodule
