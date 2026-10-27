/*******************************MILIANKE*******************************
*Company : MiLianKe Electronic Technology Co., Ltd.   Description : FDMA <-> AXI4 FULL 读写通道 glue
*2026 两轮零行为重构：删旧版本注释、未用 axi_rlast/rburst_cnt、合并同步链与同域 always。
*  M_AXI_WID 原版即未驱动（保持 z），M_AXI_BID/RID/RRESP/RLAST 输入未用。
*  本工程只用写通道（system_top 未接 AXI 读通道，I_fdma_rareq/raddr/rsize 恒 0）；读通道为保持
*  逐拍等价与端口契约原样保留（两个等价 tb 都覆盖读通道，删它须同改 tb）。
*********************************************************************/
module uiFDMA #
(
parameter  integer         M_AXI_ID_WIDTH			= 3		    , //ID,demo中没用到
parameter  integer         M_AXI_ID			        = 0		    , //ID,demo中没用到
parameter  integer         M_AXI_ADDR_WIDTH			= 32		,//内存地址位宽
parameter  integer         M_AXI_DATA_WIDTH			= 128		,//AXI总线的数据位宽
parameter  integer		  M_AXI_MAX_BURST_LEN       = 64         //AXI总线的burst 大小
)
(
input  wire [M_AXI_ADDR_WIDTH-1:0] I_fdma_waddr,   input  wire I_fdma_wareq,
input  wire [15:0]                 I_fdma_wsize,   output     O_fdma_wbusy,
input  wire [M_AXI_DATA_WIDTH-1:0] I_fdma_wdata,   output wire O_fdma_wvalid,
input  wire                        I_fdma_wready,
input  wire [M_AXI_ADDR_WIDTH-1:0] I_fdma_raddr,   input  wire I_fdma_rareq,
input  wire [15:0]                 I_fdma_rsize,   output     O_fdma_rbusy,
output wire [M_AXI_DATA_WIDTH-1:0] O_fdma_rdata,   output wire O_fdma_rvalid,
input  wire                        I_fdma_rready,
input  wire                          M_AXI_ACLK,      input  wire M_AXI_ARESETN,
output wire [M_AXI_ID_WIDTH-1:0]     M_AXI_AWID,      output wire [M_AXI_ADDR_WIDTH-1:0] M_AXI_AWADDR,
output wire [7:0]                    M_AXI_AWLEN,     output wire [2:0] M_AXI_AWSIZE,
output wire [1:0]                    M_AXI_AWBURST,   output wire M_AXI_AWLOCK,
output wire [3:0]                    M_AXI_AWCACHE,   output wire [2:0] M_AXI_AWPROT,
output wire [3:0]                    M_AXI_AWQOS,     output wire M_AXI_AWVALID,
input  wire                          M_AXI_AWREADY,
output wire [M_AXI_ID_WIDTH-1:0]     M_AXI_WID,       // 厂家原版未驱动（保持 z）
output wire [M_AXI_DATA_WIDTH-1:0]   M_AXI_WDATA,     output wire [M_AXI_DATA_WIDTH/8-1:0] M_AXI_WSTRB,
output wire                          M_AXI_WLAST,     output wire M_AXI_WVALID,
input  wire                          M_AXI_WREADY,
input  wire [M_AXI_ID_WIDTH-1:0]     M_AXI_BID,       // 未用
input  wire [1:0]                    M_AXI_BRESP,     // 未用
input  wire                          M_AXI_BVALID,    // 未用
output wire                          M_AXI_BREADY,
output wire [M_AXI_ID_WIDTH-1:0]     M_AXI_ARID,      output wire [M_AXI_ADDR_WIDTH-1:0] M_AXI_ARADDR,
output wire [7:0]                    M_AXI_ARLEN,     output wire [2:0] M_AXI_ARSIZE,
output wire [1:0]                    M_AXI_ARBURST,   output wire M_AXI_ARLOCK,
output wire [3:0]                    M_AXI_ARCACHE,   output wire [2:0] M_AXI_ARPROT,
output wire [3:0]                    M_AXI_ARQOS,     output wire M_AXI_ARVALID,
input  wire                          M_AXI_ARREADY,
input  wire [M_AXI_ID_WIDTH-1:0]     M_AXI_RID,       // 未用
input  wire [M_AXI_DATA_WIDTH-1:0]   M_AXI_RDATA,
input  wire [1:0]                    M_AXI_RRESP,     // 未用
input  wire                          M_AXI_RLAST,     // 未用
input  wire                          M_AXI_RVALID,
output wire                          M_AXI_RREADY
	);

function integer clogb2 (input integer bit_depth);
begin
	 for(clogb2=0; bit_depth>0; clogb2=clogb2+1)
	 bit_depth = bit_depth >> 1;
end
endfunction

localparam AXI_BYTES          = M_AXI_DATA_WIDTH/8;
localparam MAX_BURST_LEN_SIZE = clogb2(M_AXI_MAX_BURST_LEN - 1);
localparam BURST_LEN_W        = clogb2(M_AXI_MAX_BURST_LEN);
localparam BURST_BYTES_W      = clogb2(M_AXI_MAX_BURST_LEN * AXI_BYTES);

reg [M_AXI_ADDR_WIDTH-1:0] axi_awaddr = 0;
reg axi_awvalid = 1'b0, axi_wvalid = 1'b0;
reg [BURST_LEN_W-1:0] wburst_len = 1, wburst_cnt = 0;
reg [15:0] wfdma_cnt = 0;
reg axi_wstart_locked = 1'b0;
reg w_enable = 1'b0, aw_enable = 1'b0;
reg fdma_wstart_locked = 1'b0;
reg fdma_wend_enable = 1'b0, fdma_wend_all = 1'b0;
reg next_req = 1'b0, wburst_len_req = 1'b0;
reg [15:0] fdma_wleft_cnt = 16'd0;
reg axi_wstart_locked_r1 = 1'b0, axi_wstart_locked_r2 = 1'b0;
// 读通道寄存器（提前声明：TD 6.2.1 不接受先使用后声明）
reg axi_rstart_locked = 1'b0, axi_rstart_locked_r1 = 1'b0, axi_rstart_locked_r2 = 1'b0;
wire [BURST_BYTES_W-1:0] axi_wburst_size = wburst_len * AXI_BYTES;
wire w_next      = (M_AXI_WVALID & M_AXI_WREADY);
wire fdma_wstart = (fdma_wstart_locked == 1'b0 && I_fdma_wareq == 1'b1);
wire fdma_wend   = w_next && (fdma_wleft_cnt == 1);
wire wend_ack    = fdma_wend_enable & next_req;
wire axi_wlast   = (w_next == 1'b1) && (wburst_cnt == M_AXI_AWLEN);

assign M_AXI_AWID = M_AXI_ID;  assign M_AXI_AWADDR = axi_awaddr;  assign M_AXI_AWLEN = wburst_len - 1;
assign M_AXI_AWSIZE = clogb2(AXI_BYTES-1);  assign M_AXI_AWBURST = 2'b01;  assign M_AXI_AWLOCK = 1'b0;
assign M_AXI_AWCACHE = 4'b0010;  assign M_AXI_AWPROT = 3'h0;  assign M_AXI_AWQOS = 4'h0;
assign M_AXI_AWVALID = axi_awvalid;  assign M_AXI_WDATA = I_fdma_wdata;
assign M_AXI_WSTRB = {(AXI_BYTES){1'b1}};  assign M_AXI_WLAST = axi_wlast;
assign M_AXI_WVALID = axi_wvalid & I_fdma_wready;  assign M_AXI_BREADY = 1'b1;

assign O_fdma_wvalid    = (w_next && (~axi_wlast)) || ((axi_wstart_locked_r1 == 1'b1) && axi_wstart_locked_r2 == 1'b0);
assign O_fdma_wbusy     = fdma_wstart_locked;

always @(posedge M_AXI_ACLK) begin
    if(M_AXI_ARESETN == 1'b0 || fdma_wend_all == 1'b1) fdma_wstart_locked <= 1'b0;
    else if(fdma_wstart)                               fdma_wstart_locked <= 1'b1;
    if(fdma_wstart)          axi_awaddr <= I_fdma_waddr;
    else if(next_req == 1'b1) axi_awaddr <= axi_awaddr + axi_wburst_size;
    axi_wstart_locked_r1 <= axi_wstart_locked;
    axi_wstart_locked_r2 <= axi_wstart_locked_r1;
    axi_rstart_locked_r1 <= axi_rstart_locked;
    axi_rstart_locked_r2 <= axi_rstart_locked_r1;
    aw_enable <= (w_enable & aw_enable) ? 1'b0 : ((axi_awvalid & M_AXI_AWREADY) ? 1'b1 : aw_enable);
    w_enable  <= (w_enable & aw_enable) ? 1'b0 : (axi_wlast ? 1'b1 : w_enable);
    next_req  <= (w_enable & aw_enable);
    fdma_wend_all    <= wend_ack;
    fdma_wend_enable <= wend_ack ? 1'b0 : (fdma_wend ? 1'b1 : fdma_wend_enable);
    if((fdma_wstart_locked == 1'b1 && (~fdma_wend_all)) && axi_wstart_locked == 1'b0) axi_wstart_locked <= 1'b1;
    else if(next_req == 1'b1 || fdma_wstart == 1'b1)                                   axi_wstart_locked <= 1'b0;
    if((axi_wstart_locked_r1 == 1'b1) && axi_wstart_locked_r2 == 1'b0) axi_awvalid <= 1'b1;
    else if((axi_wstart_locked == 1'b1 && M_AXI_AWREADY == 1'b1) || axi_wstart_locked == 1'b0) axi_awvalid <= 1'b0;
    if((axi_wstart_locked_r1 == 1'b1) && axi_wstart_locked_r2 == 1'b0) axi_wvalid <= 1'b1;
    else if(axi_wlast == 1'b1 || axi_wstart_locked == 1'b0)            axi_wvalid <= 1'b0;
    if(axi_wstart_locked == 1'b0) wburst_cnt <= 8'd0;
    else if(w_next)               wburst_cnt <= wburst_cnt + 1'b1;
    wburst_len_req <= fdma_wstart | next_req;
    if(fdma_wstart)begin
        wfdma_cnt      <= 16'd0;
        fdma_wleft_cnt <= I_fdma_wsize;
    end
    else if(w_next)begin
        wfdma_cnt      <= wfdma_cnt + 1'b1;
        fdma_wleft_cnt <= (I_fdma_wsize - 1'b1) - wfdma_cnt;
    end
    if(M_AXI_ARESETN == 1'b0) wburst_len <= 1;
    else if(wburst_len_req)begin
        if(fdma_wleft_cnt[15:MAX_BURST_LEN_SIZE] > 0) wburst_len <= M_AXI_MAX_BURST_LEN;
        else                                         wburst_len <= fdma_wleft_cnt[MAX_BURST_LEN_SIZE-1:0];
    end
end

// ================= AXI4 读通道（本工程不可达，见文件头） =================
reg [M_AXI_ADDR_WIDTH-1:0] axi_araddr = 0;      //AXI4 读地址
reg axi_arvalid = 1'b0, axi_rready = 1'b0;      //读地址有效 / 读准备
reg [BURST_LEN_W-1:0] rburst_len = 1;           //当前 axi 读 burst 长度（拍）
reg [15:0] rfdma_cnt = 0;                       //本次 FDMA 读已传拍数
reg [15:0] fdma_rleft_cnt = 16'd0;              //本次 FDMA 剩余拍数
reg [15:0] fdma_rleft_cnt_ar = 16'd0;           //burst 长度规划剩余值
reg rburst_len_req = 1'b0;                      //burst 长度重算请求
reg fdma_rstart_locked = 1'b0;                  //FDMA 读传输进行中
reg out_ar_valid_enable;                        //地址自动递增使能
wire [BURST_BYTES_W-1:0] axi_rburst_size = rburst_len * AXI_BYTES;     //axi burst 地址增量
wire r_next      = (M_AXI_RVALID & M_AXI_RREADY);                      //AXI 读数据传输有效
wire fdma_rstart = (fdma_rstart_locked == 1'b0 && I_fdma_rareq == 1'b1);
wire fdma_rend   = r_next && (fdma_rleft_cnt == 1);
wire ar_ack      = M_AXI_ARREADY && axi_arvalid && out_ar_valid_enable;

reg [M_AXI_DATA_WIDTH-1:0] rdata_fdma;
reg                        valid_fdma;

assign M_AXI_ARID = M_AXI_ID;  assign M_AXI_ARADDR = axi_araddr;  assign M_AXI_ARLEN = rburst_len - 1;
assign M_AXI_ARSIZE = clogb2(AXI_BYTES-1);  assign M_AXI_ARBURST = 2'b01;  assign M_AXI_ARLOCK = 1'b0;
assign M_AXI_ARCACHE = 4'b0010;  assign M_AXI_ARPROT = 3'h0;  assign M_AXI_ARQOS = 4'h0;
assign M_AXI_ARVALID = axi_arvalid;  assign M_AXI_RREADY = axi_rready && I_fdma_rready;

assign O_fdma_rdata     = rdata_fdma;
assign O_fdma_rvalid    = valid_fdma;
assign O_fdma_rbusy     = fdma_rstart_locked;

always @(posedge M_AXI_ACLK) begin
    rdata_fdma <= M_AXI_RDATA;
    valid_fdma <= r_next;
    if(M_AXI_ARESETN == 1'b0 || fdma_rend == 1'b1) fdma_rstart_locked <= 1'b0;
    else if(fdma_rstart)                           fdma_rstart_locked <= 1'b1;
    if(fdma_rstart == 1'b1) axi_araddr <= I_fdma_raddr;
    else if(ar_ack)         axi_araddr <= axi_araddr + axi_rburst_size;
    if((fdma_rstart_locked == 1'b1) && axi_rstart_locked == 1'b0) axi_rstart_locked <= 1'b1;
    else if(fdma_rstart == 1'b1 || ar_ack)                        axi_rstart_locked <= 1'b0;
    if((axi_rstart_locked_r1 == 1'b1) && axi_rstart_locked_r2 == 1'b0) axi_arvalid <= 1'b1;
    else if((axi_rstart_locked == 1'b1 && M_AXI_ARREADY == 1'b1) || axi_rstart_locked == 1'b0) axi_arvalid <= 1'b0;
    if(fdma_rstart_locked & (~fdma_rend)) axi_rready <= 1'b1;
    else                                  axi_rready <= 1'b0;
    rburst_len_req <= fdma_rstart | ar_ack;
    if(fdma_rstart)begin
        rfdma_cnt      <= 16'd0;
        fdma_rleft_cnt <= I_fdma_rsize;
    end
    else if(r_next)begin
        rfdma_cnt      <= rfdma_cnt + 1'b1;
        fdma_rleft_cnt <= (I_fdma_rsize - 1'b1) - rfdma_cnt;
    end
    if(M_AXI_ARESETN == 1'b0)begin
        rburst_len <= 1;
        out_ar_valid_enable <= 0;
    end
    else if(rburst_len_req)begin
        if(fdma_rleft_cnt_ar[15:MAX_BURST_LEN_SIZE] > 0)begin
            rburst_len <= M_AXI_MAX_BURST_LEN;
            out_ar_valid_enable <= 1;
            if(fdma_rleft_cnt_ar == M_AXI_MAX_BURST_LEN)begin
                rburst_len <= 0;
                out_ar_valid_enable <= 0;
            end
        end
        else begin
            rburst_len <= fdma_rleft_cnt_ar[MAX_BURST_LEN_SIZE-1:0];
            out_ar_valid_enable <= 0;
        end
    end
    if(fdma_rstart)                           fdma_rleft_cnt_ar <= I_fdma_rsize;
    else if(M_AXI_ARREADY && axi_arvalid)     fdma_rleft_cnt_ar <= fdma_rleft_cnt_ar - rburst_len;
end

endmodule
