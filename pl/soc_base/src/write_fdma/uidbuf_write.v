`timescale 1ns / 1ns
/*******************************MILIANKE*******************************
*Company : MiLianKe Electronic Technology Co., Ltd.
*Description : 摄像头帧写 DDR 主控：VS 帧同步 -> FIFO 缓冲 -> FDMA 分块写
*2026 两轮零行为重构：删未用 write_cnt / W_wren_ri / wirq 冗余信号与注释掉的旧参数，
*  状态机 / 地址步进 / 中断语义原样保留。W_rbusy 是厂家原版未驱动 wire（W_REQ 因此含 x），
*  为保持与 golden 逐位等价原样保留；TD 综合按 0 处理（既有时序行为不变）。
*  ENABLE_READ / I_W_buf 厂家原版即未使用，保留只为端口契约。
*********************************************************************/
module uidbuf_write #(
parameter  integer  VIDEO_ENABLE   = 1,   //使能视频帧支持功能
parameter  integer  ENABLE_WRITE   = 1,   //使能写通道
parameter  integer  ENABLE_READ    = 1,   //使能读通道（厂家原版未使用）
parameter  integer  AXI_DATA_WIDTH = 128, //AXI总线数据位宽
parameter  integer  AXI_ADDR_WIDTH = 32,  //AXI总线地址位宽
parameter  integer  W_BUFDEPTH     = 2048,//写通道 AXI FIFO 缓存大小
parameter  integer  W_DATAWIDTH    = 32,  //写通道 AXI 数据位宽
parameter  integer  W_DSIZEBITS    = 24,  //写通道缓存数据增量地址位宽
parameter  integer  W_XSIZE        = 1920,//X 方向数据大小（每次 FDMA 传输长度）
parameter  integer  W_XSTRIDE      = 1920,//X 方向 Stride
parameter  integer  W_YSIZE        = 1080,//Y 方向值（XSIZE 传输次数）
parameter  integer  W_XDIV         = 2    //X 方向拆分为 XDIV 次传输以省 FIFO
)
(
input  wire [AXI_ADDR_WIDTH-1:0] W_BASEADDR,
input  wire [15:0]               W_BUFSIZE,
input  wire                      I_ui_clk,  //和 FDMA AXI 总线时钟一致
input  wire                      I_ui_rstn, //和 FDMA AXI 复位一致
input  wire                      I_W_clk,   //用户写数据接口时钟
input  wire                      I_W_FS,    //用户写数据同步信号（非视频帧一般置 1）
input  wire                      I_W_wren,  //用户写数据使能
input  wire [W_DATAWIDTH-1:0]    I_W_data,  //用户写数据
output reg  [7:0]                O_W_sync_cnt = 0,  //写通道 BUF 帧同步输出
input  wire [7:0]                I_W_buf,           //写通道 BUF 帧同步输入（未使用）
output wire                      O_W_full,
output wire [AXI_ADDR_WIDTH-1:0] O_fdma_waddr,  //FDMA 写通道地址
output wire                      O_fdma_wareq,  //FDMA 写通道请求
output wire [15:0]               O_fdma_wsize,  //一次 FDMA 的传输大小
input  wire                      I_fdma_wbusy,  //FDMA 正在写 AXI
output wire [AXI_DATA_WIDTH-1:0] O_fdma_wdata,  //FDMA 写数据
input  wire                      I_fdma_wvalid, //FDMA 写有效
output wire                      O_fdma_wready, //FDMA 写准备好
output reg  [7:0]                O_fmda_wbuf = 0,  //FDMA 写帧缓存号
output wire                      O_fdma_wirq,   //FDMA 一次写传输完成中断
input  wire                      camera_start,
output wire                      camera_done,
input  wire                      camera_clear_intr,
output reg                       O_W_start_tgl = 0  //每帧写开始翻转一次
);

function integer clog2;
  input integer value;
  begin
    for (clog2=0; value>0; clog2=clog2+1) value = value>>1;
  end
endfunction

localparam S_IDLE       = 3'd0;
localparam S_RST        = 3'd1;
localparam S_DATA1      = 3'd2;
localparam S_DATA2      = 3'd3;
localparam S_FRAME_DONE = 3'd4;

generate  if(ENABLE_WRITE == 1)begin : FDMA_WRITE_ENABLE

localparam WFIFO_DEPTH            = W_BUFDEPTH;                                   //写通道FIFO深度
localparam W_WR_DATA_COUNT_WIDTH  = clog2(WFIFO_DEPTH);                           //FIFO写侧计数位宽
localparam W_RD_DATA_COUNT_WIDTH  = clog2(WFIFO_DEPTH*W_DATAWIDTH/AXI_DATA_WIDTH); //FIFO读侧计数位宽
wire [15:0] WYBUF_SIZE       = (W_BUFSIZE - 1'b1);                        //写通道帧缓存数量-1
localparam WY_BURST_TIMES    = (W_YSIZE*W_XDIV);                          //一帧需要的 FDMA burst 次数
localparam FDMA_WX_BURST     = (W_XSIZE*W_DATAWIDTH/AXI_DATA_WIDTH)/W_XDIV;//一次 FDMA burst 拍数
localparam WX_BURST_ADDR_INC = (W_XSIZE*(W_DATAWIDTH/8))/W_XDIV;          //一次 burst 后地址增量
localparam WX_LAST_ADDR_INC  = (W_XSTRIDE-W_XSIZE)*(W_DATAWIDTH/8) + WX_BURST_ADDR_INC; //行末地址增量

reg                            O_fdma_wareq_r = 1'b0;
reg                            W_FIFO_Rst = 0;
wire                           W_FS;
reg [2:0]                      W_MS = 0;
reg [W_DSIZEBITS-1:0]          W_addr = 0;
reg [clog2(WY_BURST_TIMES)-1:0] W_bcnt = 0;      //burst 计数（0..WY_BURST_TIMES-1，位宽收窄）
wire [W_RD_DATA_COUNT_WIDTH-1:0] W_rcnt;
reg                            W_REQ = 0;
reg [5:0]                      wirq_dly_cnt = 0;
reg [clog2(W_XDIV)-1:0]        wdiv_cnt = 0;     //XDIV 分次计数（位宽收窄）
reg [6:0]                      wrst_cnt = 0;     //0..100（位宽收窄）
reg [7:0]                      O_fmda_wbufn;
reg [2:0]                      W_MS_r = 0;

assign O_fdma_wready = 1'b1;
assign O_fdma_wsize  = FDMA_WX_BURST;
assign O_fdma_wirq   = (wirq_dly_cnt > 0);
assign O_fdma_wareq  = O_fdma_wareq_r;
assign O_fdma_waddr  = W_BASEADDR + W_addr; //缓存号由高位地址实现（FPGA 内做乘法代价高）

always @(posedge I_ui_clk) W_MS_r <= W_MS;

always @(posedge I_ui_clk) begin
    if(I_ui_rstn == 1'b0)begin
        wirq_dly_cnt <= 6'd0;
        O_fmda_wbuf  <= 0;
    end
    else if((W_MS_r == S_DATA2) && (W_MS == S_IDLE))begin
        wirq_dly_cnt <= 60;
        O_fmda_wbuf  <= O_fmda_wbufn;
    end
    else if(wirq_dly_cnt > 0) wirq_dly_cnt <= wirq_dly_cnt - 1'b1;
end

fs_cap #(.VIDEO_ENABLE(VIDEO_ENABLE)) fs_cap_W0 (
    .I_clk(I_ui_clk), .I_rstn(I_ui_rstn), .I_vs(I_W_FS), .O_fs_cap(W_FS));

reg [7:0] W0_Fbuf;                    //当前可写帧缓存号
reg camera_done_d1, camera_done_d2;
always @(posedge I_ui_clk) camera_done_d2 <= camera_done_d1;   //无复位（与 golden 一致）
assign camera_done = camera_done_d2;

reg camera_work_enable = 0;
always @(posedge I_ui_clk) begin
    if(!I_ui_rstn)        camera_work_enable <= 1'b0;
    else if(camera_start) camera_work_enable <= 1'b1;
    else if(camera_done)  camera_work_enable <= 1'b0;
end

always @(posedge I_ui_clk) begin
    if(!I_ui_rstn)begin
        W_MS <= S_IDLE;  W_FIFO_Rst <= 0;  W_addr <= 0;  O_W_sync_cnt <= 0;
        W_bcnt <= 0;  wrst_cnt <= 0;  wdiv_cnt <= 0;  O_fmda_wbufn <= 0;
        O_fdma_wareq_r <= 1'd0;  camera_done_d1 <= 0;
    end
    else case(W_MS)
        S_IDLE:begin
          camera_done_d1 <= 0;
          if(camera_work_enable)begin
            W_addr <= 0;  W_bcnt <= 0;  wrst_cnt <= 0;  wdiv_cnt <= 0;
            if(W_FS)begin //帧同步，对于非视频数据一般常量为1
                W_MS          <= S_RST;
                O_W_start_tgl <= ~O_W_start_tgl;
                O_W_sync_cnt  <= (O_W_sync_cnt < WYBUF_SIZE) ? (O_W_sync_cnt + 1'b1) : 8'd0;
            end
          end
        end
        S_RST:begin //视频模式复位数据FIFO；非视频模式直接跳过
           O_fmda_wbufn <= W0_Fbuf;
           wrst_cnt     <= wrst_cnt + 1'b1;
           if((VIDEO_ENABLE == 1) && (wrst_cnt < 40))        W_FIFO_Rst <= 1;
           else if((VIDEO_ENABLE == 1) && (wrst_cnt < 100))  W_FIFO_Rst <= 0;
           else if(O_fdma_wirq == 1'b0)                      W_MS <= S_DATA1;
        end
        S_DATA1:begin //发送写FDMA请求
          if(I_fdma_wbusy == 1'b0 && W_REQ)  O_fdma_wareq_r <= 1'b1;
          else if(I_fdma_wbusy == 1'b1)begin
             O_fdma_wareq_r <= 1'b0;
             W_MS <= S_DATA2;
          end
        end
        S_DATA2:begin //等待FDMA完成后推进地址与计数
            if(I_fdma_wbusy == 1'b0)begin
                if(W_bcnt == WY_BURST_TIMES - 1'b1) W_MS <= S_FRAME_DONE; //一帧传输完毕
                else begin
                    if(wdiv_cnt < W_XDIV - 1'b1)begin //XDIV 分次传输时先走 burst 增量
                        W_addr   <= W_addr + WX_BURST_ADDR_INC;
                        wdiv_cnt <= wdiv_cnt + 1'b1;
                    end
                    else begin                        //最后一次按 stride 计算行末地址增量
                        W_addr   <= W_addr + WX_LAST_ADDR_INC;
                        wdiv_cnt <= 0;
                    end
                    W_bcnt <= W_bcnt + 1'b1;
                    W_MS   <= S_DATA1;
                end
            end
        end
        S_FRAME_DONE:begin //帧完成，等 PS 清中断后切下一帧缓存
            camera_done_d1 <= 1;
            if(camera_clear_intr) begin
                W_MS <= S_IDLE;
                //W0_Fbuf 无复位（上电为 x），if/else 不能写成三目：x 时 if 取 else 支
                if(W0_Fbuf < WYBUF_SIZE) W0_Fbuf <= W0_Fbuf + 1'b1;
                else                     W0_Fbuf <= 8'd0;
            end
        end
        default: W_MS <= S_IDLE;
      endcase
end

//W_rbusy 为厂家原版未驱动 wire：保持悬空以维持 golden 的 x 语义（见文件头注）
wire W_rbusy;
always@(posedge I_ui_clk)
     W_REQ <= (W_rcnt > FDMA_WX_BURST - 2) && (~W_rbusy);

wfifo #(
.DATA_WIDTH_W(W_DATAWIDTH), .DATA_WIDTH_R(AXI_DATA_WIDTH),
.ADDR_WIDTH_W(W_WR_DATA_COUNT_WIDTH), .ADDR_WIDTH_R(W_RD_DATA_COUNT_WIDTH),
.AL_FULL_NUM(WFIFO_DEPTH-2), .AL_EMPTY_NUM(2),
.SHOW_AHEAD_EN(1'b0), .OUTREG_EN ("NOREG")
)
u_wfifo(
.rst((I_ui_rstn == 1'b0) || (W_FIFO_Rst == 1'b1)), //asynchronous port,active hight
.clkw(I_W_clk),   //write clock
.clkr(I_ui_clk),  //read clock
.we(I_W_wren),    //write enable,active hight
.di(I_W_data),    //write data
.re(I_fdma_wvalid),//read enable,active hight
.dout(O_fdma_wdata),//read data
.full_flag(O_W_full),//fifo full flag
.rdusedw(W_rcnt) //available data number for read
);
end
else begin : FDMA_WRITE_DISABLE
assign O_fdma_waddr  = 0;
assign O_fdma_wareq  = 0;
assign O_fdma_wsize  = 0;
assign O_fdma_wdata  = 0;
assign O_fdma_wready = 0;
assign O_fdma_wirq   = 0;
assign O_W_full      = 0;
end
endgenerate

endmodule
