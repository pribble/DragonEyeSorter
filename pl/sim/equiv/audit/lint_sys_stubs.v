// verifier-platform lint 专用桩（仅 verilator --lint-only，不参与仿真/综合）。
// 本文件由 pl/sim/equiv/audit/gen_lint_stubs.py 从 golden 的 design_0.v / design_top_wrapper.v
// 模块头自动生成，端口方向/位宽与真实工程一致；两侧共用。
// 目的：让 system_top 单独 lint 时不被 PS 块设计与 MIPI 顶层内部实现打断。

module design_0 (
    inout     wire              mipi_iic_i2c0_scl_io ,
    inout     wire              mipi_iic_i2c0_sda_io ,
    input     wire              f2p_irq ,
    input     wire       [31:0] hp1_w_us1_araddr ,
    input     wire        [1:0] hp1_w_us1_arburst ,
    input     wire        [3:0] hp1_w_us1_arcache ,
    input     wire        [4:0] hp1_w_us1_arid ,
    input     wire        [7:0] hp1_w_us1_arlen ,
    input     wire        [0:0] hp1_w_us1_arlock ,
    input     wire        [2:0] hp1_w_us1_arprot ,
    input     wire        [3:0] hp1_w_us1_arqos ,
    input     wire        [2:0] hp1_w_us1_arsize ,
    input     wire              hp1_w_us1_arvalid ,
    input     wire       [31:0] hp1_w_us1_awaddr ,
    input     wire        [1:0] hp1_w_us1_awburst ,
    input     wire        [3:0] hp1_w_us1_awcache ,
    input     wire        [4:0] hp1_w_us1_awid ,
    input     wire        [7:0] hp1_w_us1_awlen ,
    input     wire        [0:0] hp1_w_us1_awlock ,
    input     wire        [2:0] hp1_w_us1_awprot ,
    input     wire        [3:0] hp1_w_us1_awqos ,
    input     wire        [2:0] hp1_w_us1_awsize ,
    input     wire              hp1_w_us1_awvalid ,
    input     wire              hp1_w_us1_bready ,
    input     wire              hp1_w_us1_rready ,
    input     wire       [63:0] hp1_w_us1_wdata ,
    input     wire              hp1_w_us1_wlast ,
    input     wire        [7:0] hp1_w_us1_wstrb ,
    input     wire              hp1_w_us1_wvalid ,
    output    wire              clk_150m ,
    output    wire              rstn_150m ,
    output    wire              pwm_out ,
    output    wire       [31:0] camera_base_addr ,
    output    wire              camera_start ,
    output    wire              camera_clear ,
    output    wire              des_clk ,
    output    wire              des_wr ,
    output    wire       [11:0] des_waddr ,
    output    wire       [31:0] des_wdata ,
    output    wire       [11:0] des_raddr ,
    input     wire       [31:0] des_rdata ,
    output    wire              can0_tx ,
    input     wire              can0_rx ,
    output    wire              vtg_lock ,
    output    wire              axis_to_video_lock ,
    output    wire              hp1_w_us1_arready ,
    output    wire              hp1_w_us1_awready ,
    output    wire        [4:0] hp1_w_us1_bid ,
    output    wire        [1:0] hp1_w_us1_bresp ,
    output    wire              hp1_w_us1_bvalid ,
    output    wire       [63:0] hp1_w_us1_rdata ,
    output    wire        [4:0] hp1_w_us1_rid ,
    output    wire              hp1_w_us1_rlast ,
    output    wire        [1:0] hp1_w_us1_rresp ,
    output    wire              hp1_w_us1_rvalid ,
    output    wire              hp1_w_us1_wready ,
    output    wire              O_hdmi_clk_p ,
    output    wire        [2:0] O_hdmi_tx_p ,
    output    wire              p2f_clk0_50m
);
endmodule

module design_top_wrapper (
    /*
        input 33MHz system clock
    */
    input wire       I_sys_clk_156m,
    
    /*
    	input hs clock
    */
	// input wire		 I_clk_hs_p,
	// input wire		 I_clk_lp_p,
    /*
    	input hs data
    */
	// input wire[3:0]	 I_data_hs_p, 
    // input wire[3:0]	 I_data_lp_p,
    // input wire[3:0]	 I_data_lp_n,   
    /*
    	output CSI LP DIR
    */
    // output wire		I_csi_lp_ck_dir,
	// output wire		I_csi_lp_d0_dir,
    // output wire 	I_csi_lp_d1_dir,
	// output wire		I_csi_lp_d2_dir,
    // output wire 	I_csi_lp_d3_dir,    	    
    /*
        camera imx334 iic configure interface
    */
    // inout wire       IO_imx334_scl,
    // inout wire       IO_imx334_sda,
	
    // output wire[3:0] O_led,
    /*
        camera imx334 24mhz clk
    */
	output wire      O_imx334_clk_24m,
    // output  [4:0]   row_err_cnt,
    //input           proc_en                 ,
    //input           resolution_sel          ,
    //output          [31:0]           gray_mean,
	
	input  wire		    I_clk_hs_p  ,
	input  wire	[3:0]	I_data_hs_p , 
	input  wire	[3:0]	I_data_lp_p ,
	input  wire	[3:0]	I_data_lp_n ,

	output			S_pll_lock,
    output          wire[3:0]	S_lane_error,
    output          S_hs_rx_clk         ,//synthesis keep  
    output  [127:0] O_ISP_tdata         ,//synthesis keep
    output		    O_ISP_tlast         ,//synthesis keep
    output		    O_ISP_tuser         ,//synthesis keep
    output	 		O_ISP_tvalid         //synthesis keep

);
endmodule
