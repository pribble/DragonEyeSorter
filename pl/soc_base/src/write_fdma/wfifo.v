/************************************************************\
 **  Anlogic IP Generator 生成文件（原 wfifo.v，TD 5.6.68885，2023-03-28）
 **  异步 FIFO：格雷码指针跨时钟域同步 + RAM 推断（wfifo / fifo_cross_domain_addr_process_al_wfifo /
 **  ram_infer_wfifo）。2026 两轮零行为重构：删生成器注释与排版死码、合并同域 always、按参数常量折叠；
 **  端口/参数名、方向、位宽、顺序与默认值全保留。wfifo 公开参数仍通用（tb_wfifo 4 组配置 +
 **  uidbuf_write 实例都要逐拍等价）；私有 helper 只支持本工程可达配置（MODE="SDP"、无字节写、
 **  异步复位，REGMODE_B 仍有 OUTREG/NOREG）。fehdl keep / force_ram 是 TD 综合约束，勿删。
\************************************************************/
`timescale 1ns/1ps

module wfifo #(parameter DATA_WIDTH_W = 32,
        parameter DATA_WIDTH_R = 256,
        parameter ADDR_WIDTH_W = 11,
        parameter ADDR_WIDTH_R = 8,
        parameter AL_FULL_NUM = 2045,
        parameter AL_EMPTY_NUM = 2,
        parameter SHOW_AHEAD_EN = 1'b1,
        parameter OUTREG_EN = "NOREG")(
    input rst, input clkw, input clkr, input we,   // 异步复位（高有效）/ 写时钟 / 读时钟 / 写使能
    input [(DATA_WIDTH_W - 1):0] di,               // 写数据
    input re,                                      // 读使能
    output [(DATA_WIDTH_R - 1):0] dout,            // 读数据
    output valid,                                  // 读数据有效
    output reg full_flag, output empty_flag, output reg afull, output aempty,  // 满 / 空 / 几乎满 / 几乎空
    output [(ADDR_WIDTH_W - 1):0] wrusedw,         // 写域已写入数量
    output [(ADDR_WIDTH_R - 1):0] rdusedw          // 读域可读数量
        ) ;

    localparam FULL_NUM = ({ADDR_WIDTH_W{1'b1}} - 1) ;

    reg asy_w_rst0, asy_w_rst1, asy_r_rst0, asy_r_rst1 ;
    wire rd_rst = asy_r_rst1, wr_rst = asy_w_rst1 ;
    wire [(ADDR_WIDTH_R - 1):0] rd_to_wr_addr ;  // 读指针同步到写时钟域
    wire [(ADDR_WIDTH_W - 1):0] wr_to_rd_addr ;  // 写指针同步到读时钟域
    reg [(ADDR_WIDTH_W - 1):0] wr_addr ;
    reg [(ADDR_WIDTH_R - 1):0] rd_addr ;
    wire wr_en_s, rd_en_s ;
    // 两侧指针宽度不同（读写数据位宽比），换算到各自域（位宽 / 截断与原版逐字一致）
    wire [(ADDR_WIDTH_W - 1):0] shift_rdaddr = (ADDR_WIDTH_R >= ADDR_WIDTH_W)
        ? (rd_to_wr_addr >> (ADDR_WIDTH_R - ADDR_WIDTH_W)) : (rd_to_wr_addr << (ADDR_WIDTH_W - ADDR_WIDTH_R)) ;
    wire [(ADDR_WIDTH_R - 1):0] shift_wraddr = (ADDR_WIDTH_R >= ADDR_WIDTH_W)
        ? (wr_to_rd_addr << (ADDR_WIDTH_R - ADDR_WIDTH_W)) : (wr_to_rd_addr >> (ADDR_WIDTH_W - ADDR_WIDTH_R)) ;
    wire [(ADDR_WIDTH_W - 1):0] wr_addr_diff = (wr_addr - shift_rdaddr) ;  // 写时钟域内已写入数量
    wire [(ADDR_WIDTH_R - 1):0] rd_addr_diff = (shift_wraddr - rd_addr) ;  // 读时钟域内可读数量
    reg empty_flag_r1, empty_flag_r2, re_r1, re_r2 ;

    // 复位：异步置位、同步释放（写 / 读时钟域各一组）
    always @(posedge clkw or posedge rst)
        if (rst) {asy_w_rst1, asy_w_rst0} <= 2'b11 ;
        else     {asy_w_rst1, asy_w_rst0} <= {asy_w_rst0, 1'b0} ;
    always @(posedge clkr or posedge rst)
        if (rst) {asy_r_rst1, asy_r_rst0} <= 2'b11 ;
        else     {asy_r_rst1, asy_r_rst0} <= {asy_r_rst0, 1'b0} ;

    assign wr_en_s = ((!full_flag) & we) ;
    assign rd_en_s = ((!empty_flag) & re) ;
    always @(posedge clkw or posedge wr_rst)
        if (wr_rst == 1'b1) wr_addr <= 'b0 ;
        else if (wr_en_s)   wr_addr <= (wr_addr + 1) ;
    always @(posedge clkr or posedge rd_rst)
        if (rd_rst == 1'b1) rd_addr <= 'b0 ;
        else if (rd_en_s)   rd_addr <= (rd_addr + 1) ;

    always @(posedge clkw or posedge wr_rst)
        if (wr_rst == 1'b1) begin
            full_flag <= 1'b0 ;
            afull     <= 1'b0 ;
        end
        else begin
            full_flag <= (wr_addr_diff >= FULL_NUM) ;
            afull     <= (wr_addr_diff >= AL_FULL_NUM) ;
        end
    assign empty_flag = ((rd_addr_diff == 5'b0) ? 1'b1 : 1'b0) ;
    assign aempty     = ((rd_addr_diff <= AL_EMPTY_NUM) ? 1'b1 : 1'b0) ;
    assign wrusedw    = wr_addr_diff ;
    assign rdusedw    = rd_addr_diff ;

    // empty_flag / rd_en_s 各延迟 2 拍，用于读数据 valid 与 OUTREG 出数
    always @(posedge clkr or posedge rd_rst)
        if (rd_rst == 1'b1) begin
            empty_flag_r1 <= 1'b1 ;
            empty_flag_r2 <= 1'b1 ;
            re_r1 <= 1'b0 ;
            re_r2 <= 1'b0 ;
        end
        else begin
            empty_flag_r1 <= empty_flag ;
            empty_flag_r2 <= empty_flag_r1 ;
            re_r1 <= rd_en_s ;
            re_r2 <= re_r1 ;
        end

    // 读数据 valid：SHOW_AHEAD / OUTREG 三种组合（参数常量，无分支延迟差异）
    assign valid = (SHOW_AHEAD_EN && (OUTREG_EN == "NOREG")) ? (!empty_flag) :
                   ((!SHOW_AHEAD_EN && (OUTREG_EN == "OUTREG")) ? (re_r2 & (!empty_flag_r2)) :
                                                                  (re_r1 & (!empty_flag_r1))) ;

    fifo_cross_domain_addr_process_al_wfifo #(.ADDR_WIDTH(ADDR_WIDTH_R)) rd_to_wr_cross_inst (
        .primary_asreset_i(rd_rst), .secondary_asreset_i(wr_rst),
        .primary_clk_i(clkr), .secondary_clk_i(clkw),
        .primary_addr_i(rd_addr), .secondary_addr_o(rd_to_wr_addr)) ;
    fifo_cross_domain_addr_process_al_wfifo #(.ADDR_WIDTH(ADDR_WIDTH_W)) wr_to_rd_cross_inst (
        .primary_asreset_i(wr_rst), .secondary_asreset_i(rd_rst),
        .primary_clk_i(clkw), .secondary_clk_i(clkr),
        .primary_addr_i(wr_addr), .secondary_addr_o(wr_to_rd_addr)) ;
    ram_infer_wfifo #(.DATAWIDTH_A(DATA_WIDTH_W), .ADDRWIDTH_A(ADDR_WIDTH_W),
            .DATAWIDTH_B(DATA_WIDTH_R), .ADDRWIDTH_B(ADDR_WIDTH_R),
            .MODE("SDP"), .REGMODE_B(OUTREG_EN)) ram_inst (
        .clka(clkw), .rsta(wr_rst), .cea(1'b1), .ocea(1'b0), .wea(wr_en_s),
        .dia(di), .addra(wr_addr),
        .clkb(clkr), .rstb(rd_rst), .ceb((SHOW_AHEAD_EN | rd_en_s)), .oceb(1'b1),
        .web(1'b0), .addrb((rd_addr + (SHOW_AHEAD_EN & rd_en_s))),
        .dob(dout)) ;
endmodule


module fifo_cross_domain_addr_process_al_wfifo #(parameter ADDR_WIDTH = 9) (
    input primary_asreset_i, input secondary_asreset_i, input primary_clk_i, input secondary_clk_i,
    input [(ADDR_WIDTH - 1):0] primary_addr_i,
    output reg [(ADDR_WIDTH - 1):0] secondary_addr_o) ;

    wire [(ADDR_WIDTH - 1):0] primary_addr_gray = bin2gray(primary_addr_i) ;
    reg [(ADDR_WIDTH - 1):0] primary_addr_gray_reg ;  /* fehdl keep="true" */
    reg [(ADDR_WIDTH - 1):0] sync_r1 ;  /* fehdl keep="true" */
    reg [(ADDR_WIDTH - 1):0] primary_addr_gray_sync ;

    function [(ADDR_WIDTH - 1):0] gray2bin ;
        input [(ADDR_WIDTH - 1):0] gray ;
        integer j ;
        begin
            gray2bin[(ADDR_WIDTH - 1)] = gray[(ADDR_WIDTH - 1)] ;
            for (j = (ADDR_WIDTH - 1) ; (j > 0) ; j = (j - 1))
                gray2bin[(j - 1)] = (gray2bin[j] ^ gray[(j - 1)]) ;
        end
    endfunction

    function [(ADDR_WIDTH - 1):0] bin2gray ;
        input [(ADDR_WIDTH - 1):0] bin ;
        integer j ;
        begin
            bin2gray[(ADDR_WIDTH - 1)] = bin[(ADDR_WIDTH - 1)] ;
            for (j = (ADDR_WIDTH - 1) ; (j > 0) ; j = (j - 1))
                bin2gray[(j - 1)] = (bin[j] ^ bin[(j - 1)]) ;
        end
    endfunction

    always @(posedge primary_clk_i or posedge primary_asreset_i)
        if (primary_asreset_i == 1'b1) primary_addr_gray_reg <= 0 ;
        else                           primary_addr_gray_reg <= primary_addr_gray ;

    always @(posedge secondary_clk_i or posedge secondary_asreset_i)
        if (secondary_asreset_i == 1'b1) begin
            sync_r1 <= 0 ;
            primary_addr_gray_sync <= 0 ;
            secondary_addr_o <= 0 ;
        end
        else begin
            sync_r1 <= primary_addr_gray_reg ;
            primary_addr_gray_sync <= sync_r1 ;
            secondary_addr_o <= gray2bin(primary_addr_gray_sync) ;
        end
endmodule


module ram_infer_wfifo (clka, rsta, cea, ocea, wea, dia, addra, doa,
        clkb, rstb, ceb, oceb, web, dib, addrb, dob) ;
    parameter MODE = "SDP" ; // SP, SDP, DP
    parameter BYTE_SIZE = 8 ; // 8, 9, 10
    parameter INIT_FILE = "" ;
    parameter INIT_VALUE = 0 ;
    parameter USE_BYTE_WEA = 0 ; // 只支持 0
    parameter DATAWIDTH_A = 32 ;
    parameter WEA_WIDTH = ((USE_BYTE_WEA == 0) ? 1 : (DATAWIDTH_A / BYTE_SIZE)) ;
    parameter REGMODE_A = "NOREG" ; // 只实现 NOREG
    parameter RESETMODE_A = "ASYNC" ; // 只实现 ASYNC
    parameter INITVAL_A = {DATAWIDTH_A{1'b0}} ;
    parameter WRITEMODE_A = "NORMAL" ; // 只实现 NORMAL
    parameter ADDRWIDTH_A = 5 ;
    parameter DATADEPTH_A = (2 ** ADDRWIDTH_A) ;
    parameter SSROVERCE_A = "ENABLE" ; // 只实现 ENABLE
    parameter USE_BYTE_WEB = USE_BYTE_WEA ; // 只支持 0
    parameter DATAWIDTH_B = DATAWIDTH_A ;
    parameter WEB_WIDTH = ((USE_BYTE_WEB == 0) ? 1 : (DATAWIDTH_B / BYTE_SIZE)) ;
    parameter REGMODE_B = "NOREG" ; // 支持 OUTREG / NOREG
    parameter RESETMODE_B = "ASYNC" ; // 只实现 ASYNC
    parameter INITVAL_B = {DATAWIDTH_B{1'b0}} ;
    parameter WRITEMODE_B = "NORMAL" ; // 只实现 NORMAL
    parameter ADDRWIDTH_B = ADDRWIDTH_A ;
    parameter DATADEPTH_B = (2 ** ADDRWIDTH_B) ;
    parameter SSROVERCE_B = "ENABLE" ; // 只实现 ENABLE
    input clka, rsta, cea, ocea ;
    input [(WEA_WIDTH - 1):0] wea ;
    input [(DATAWIDTH_A - 1):0] dia ;
    input [(ADDRWIDTH_A - 1):0] addra ;
    input clkb, rstb, ceb, oceb ;
    input [(WEB_WIDTH - 1):0] web ;
    input [(DATAWIDTH_B - 1):0] dib ;
    input [(ADDRWIDTH_B - 1):0] addrb ;
    output [(DATAWIDTH_A - 1):0] doa ;
    output [(DATAWIDTH_B - 1):0] dob ;

    localparam MIN_WIDTH = ((DATAWIDTH_A > DATAWIDTH_B) ? DATAWIDTH_B : DATAWIDTH_A) ;
    localparam MAX_DEPTH = ((DATADEPTH_A > DATADEPTH_B) ? DATADEPTH_A : DATADEPTH_B) ;
    localparam WIDTHRATIO_A = (DATAWIDTH_A / MIN_WIDTH) ;
    localparam WIDTHRATIO_B = (DATAWIDTH_B / MIN_WIDTH) ;

    reg [(MIN_WIDTH - 1):0] memory [(MAX_DEPTH - 1):0] ;  /* fehdl force_ram=1, ram_style="bram" */
    reg [(DATAWIDTH_B - 1):0] dob_tmp = INITVAL_B ;
    reg [(DATAWIDTH_B - 1):0] dob_tmp2 = INITVAL_B ;

    assign doa = INITVAL_A ;   // A 口只写，读数据恒为复位值（wfifo 未连接 doa）

    always @(posedge clka)
        if (cea) write_a(addra, dia, wea) ;
    always @(posedge clkb or posedge rstb)
        if (rstb) dob_tmp2 <= INITVAL_B ;
        else if (oceb) dob_tmp2 <= dob_tmp ;
    always @(posedge clkb or posedge rstb)
        if (rstb) dob_tmp <= INITVAL_B ;
        else if (ceb) read_b(addrb, dib, 1'b0) ;
    assign dob = ((REGMODE_B == "OUTREG") ? dob_tmp2 : dob_tmp) ;

    task write_a ;
        input [(ADDRWIDTH_A - 1):0] addr ;
        input [(DATAWIDTH_A - 1):0] data ;
        input [(WEA_WIDTH - 1):0] we ;
        integer i ;
        begin
            if (we[0])
                for (i = 0 ; i < WIDTHRATIO_A ; i = i + 1)
                    memory[((addr * WIDTHRATIO_A) + i)] <= data[(i * MIN_WIDTH) +: MIN_WIDTH] ;
        end
    endtask

    task read_b ;
        input [(ADDRWIDTH_B - 1):0] addr ;
        input [(DATAWIDTH_B - 1):0] data ;
        input [(WEB_WIDTH - 1):0] we ;
        integer i ;
        begin
            for (i = 0 ; i < WIDTHRATIO_B ; i = i + 1)
                dob_tmp[(MIN_WIDTH * i) +: MIN_WIDTH] <= memory[((addr * WIDTHRATIO_B) + i)] ;
        end
    endtask
endmodule
