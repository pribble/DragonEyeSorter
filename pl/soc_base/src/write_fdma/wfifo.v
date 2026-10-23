/************************************************************\
 **  Anlogic IP Generator 生成文件（原 wfifo.v，TD 5.6.68885，2023-03-28）
 **  Copyright (c) 2012-2023 Anlogic Inc. All Right Reserved.
 **  异步 FIFO：格雷码指针跨时钟域同步 + RAM 推断（wfifo / fifo_cross_domain_addr_process_al_wfifo / ram_infer_wfifo）
 **
 **  2026 PL refactor：行为逐拍不变；删除生成器注释/排版死码、合并重复 always、
 **  按参数常量折叠，收紧重复声明。端口名/方向/位宽/顺序与参数名/默认值均未改；
 **  fehdl keep / force_ram 注释保留（TD 综合约束用）。
\************************************************************/
`timescale 1ns/1ps

module wfifo #(parameter DATA_WIDTH_W = 32,
        parameter DATA_WIDTH_R = 256,
        parameter ADDR_WIDTH_W = 11,
        parameter ADDR_WIDTH_R = 8,
        parameter AL_FULL_NUM = 2045,
        parameter AL_EMPTY_NUM = 2,
        parameter SHOW_AHEAD_EN = 1'b1,
        parameter OUTREG_EN = "NOREG") (
    //SHOWAHEAD mode enable
    //OUTREG, NOREG
    //independent clock mode,fixed as asynchronous reset
    input rst,  //asynchronous port,active hight
    input clkw,  //write clock
    input clkr,  //read clock
    input we,  //write enable,active hight
    input [(DATA_WIDTH_W - 1):0] di,  //write data
    input re,  //read enable,active hight
    output [(DATA_WIDTH_R - 1):0] dout,  //read data
    output valid,  //read data valid flag
    output reg full_flag,  //fifo full flag
    output empty_flag,  //fifo empty flag
    output reg afull,  //fifo almost full flag
    output aempty,  //fifo almost empty flag
    output [(ADDR_WIDTH_W - 1):0] wrusedw,  //stored data number in fifo
    output [(ADDR_WIDTH_R - 1):0] rdusedw //available data number for read
        ) ;

    localparam FULL_NUM = ({ADDR_WIDTH_W{1'b1}} - 1) ;

    reg asy_w_rst0, asy_w_rst1 ;
    reg asy_r_rst0, asy_r_rst1 ;
    wire rd_rst, wr_rst ;
    wire [(ADDR_WIDTH_R - 1):0] rd_to_wr_addr ; // 读指针同步到写时钟域
    wire [(ADDR_WIDTH_W - 1):0] wr_to_rd_addr ; // 写指针同步到读时钟域
    reg [(ADDR_WIDTH_W - 1):0] wr_addr ;
    reg [(ADDR_WIDTH_R - 1):0] rd_addr ;
    wire wr_en_s, rd_en_s ;
    reg [(ADDR_WIDTH_W - 1):0] shift_rdaddr ;
    reg [(ADDR_WIDTH_R - 1):0] shift_wraddr ;
    wire [(ADDR_WIDTH_W - 1):0] wr_addr_diff ; // 写时钟域内已写入数量
    wire [(ADDR_WIDTH_R - 1):0] rd_addr_diff ; // 读时钟域内可读数量
    reg empty_flag_r1, empty_flag_r2 ;
    reg re_r1, re_r2 ;

    // =============================================
    // 复位：异步置位、同步释放（写/读时钟域各一组）
    // =============================================
    always @(posedge clkw or posedge rst)
        if (rst) begin
            asy_w_rst0 <= 1'b1 ;
            asy_w_rst1 <= 1'b1 ;
        end
        else begin
            asy_w_rst0 <= 1'b0 ;
            asy_w_rst1 <= asy_w_rst0 ;
        end

    always @(posedge clkr or posedge rst)
        if (rst) begin
            asy_r_rst0 <= 1'b1 ;
            asy_r_rst1 <= 1'b1 ;
        end
        else begin
            asy_r_rst0 <= 1'b0 ;
            asy_r_rst1 <= asy_r_rst0 ;
        end

    assign rd_rst = asy_r_rst1 ;
    assign wr_rst = asy_w_rst1 ;

    // =============================================
    // 地址产生：满/空时分别禁止写/读
    // =============================================
    assign wr_en_s = ((!full_flag) & we) ;
    assign rd_en_s = ((!empty_flag) & re) ;

    always @(posedge clkw or posedge wr_rst)
        if (wr_rst == 1'b1) wr_addr <= 'b0 ;
        else if (wr_en_s)   wr_addr <= (wr_addr + 1) ;

    always @(posedge clkr or posedge rd_rst)
        if (rd_rst == 1'b1) rd_addr <= 'b0 ;
        else if (rd_en_s)   rd_addr <= (rd_addr + 1) ;

    // 两侧指针宽度不同（读写数据位宽比），换算到各自域
    always @(*)
        if (ADDR_WIDTH_R >= ADDR_WIDTH_W) begin
            shift_rdaddr = (rd_to_wr_addr >> (ADDR_WIDTH_R - ADDR_WIDTH_W)) ;
            shift_wraddr = (wr_to_rd_addr << (ADDR_WIDTH_R - ADDR_WIDTH_W)) ;
        end
        else begin
            shift_rdaddr = (rd_to_wr_addr << (ADDR_WIDTH_W - ADDR_WIDTH_R)) ;
            shift_wraddr = (wr_to_rd_addr >> (ADDR_WIDTH_W - ADDR_WIDTH_R)) ;
        end

    // two's complement format
    assign wr_addr_diff = (wr_addr - shift_rdaddr) ;
    assign rd_addr_diff = (shift_wraddr - rd_addr) ;

    // =============================================
    // 状态标志与计数
    // =============================================
    always @(posedge clkw or posedge wr_rst)
        if (wr_rst == 1'b1) full_flag <= 1'b0 ;
        else if (wr_addr_diff >= FULL_NUM) full_flag <= 1'b1 ;
        else full_flag <= 1'b0 ;

    always @(posedge clkw or posedge wr_rst)
        if (wr_rst == 1'b1) afull <= 1'b0 ;
        else if (wr_addr_diff >= AL_FULL_NUM) afull <= 1'b1 ;
        else afull <= 1'b0 ;

    assign empty_flag = ((rd_addr_diff == 5'b0) ? 1'b1 : 1'b0) ;
    assign aempty = ((rd_addr_diff <= AL_EMPTY_NUM) ? 1'b1 : 1'b0) ;
    assign wrusedw = wr_addr_diff ;
    assign rdusedw = rd_addr_diff ;

    // empty_flag / rd_en_s 各延迟 2 拍，用于读数据 valid 与 OUTREG 出数
    always @(posedge clkr or posedge rd_rst)
        if (rd_rst == 1'b1) begin
            empty_flag_r1 <= 1'b1 ;
            empty_flag_r2 <= 1'b1 ;
        end
        else begin
            empty_flag_r1 <= empty_flag ;
            empty_flag_r2 <= empty_flag_r1 ;
        end

    always @(posedge clkr or posedge rd_rst)
        if (rd_rst == 1'b1) begin
            re_r1 <= 1'b0 ;
            re_r2 <= 1'b0 ;
        end
        else begin
            re_r1 <= rd_en_s ;
            re_r2 <= re_r1 ;
        end

    // 读数据 valid：SHOW_AHEAD / OUTREG 三种组合（参数常量，无分支延迟差异）
    assign valid = (SHOW_AHEAD_EN && (OUTREG_EN == "NOREG")) ? (!empty_flag) :
                   ((!SHOW_AHEAD_EN && (OUTREG_EN == "OUTREG")) ? (re_r2 & (!empty_flag_r2)) :
                                                                  (re_r1 & (!empty_flag_r1))) ;

    // =============================================
    // 实例：双向格雷码指针同步 + 双端口 RAM
    // =============================================
    fifo_cross_domain_addr_process_al_wfifo #(.ADDR_WIDTH(ADDR_WIDTH_R)) rd_to_wr_cross_inst (
        .primary_asreset_i(rd_rst), .secondary_asreset_i(wr_rst),
        .primary_clk_i(clkr), .secondary_clk_i(clkw),
        .primary_addr_i(rd_addr), .secondary_addr_o(rd_to_wr_addr)) ;

    fifo_cross_domain_addr_process_al_wfifo #(.ADDR_WIDTH(ADDR_WIDTH_W)) wr_to_rd_cross_inst (
        .primary_asreset_i(wr_rst), .secondary_asreset_i(rd_rst),
        .primary_clk_i(clkw), .secondary_clk_i(clkr),
        .primary_addr_i(wr_addr), .secondary_addr_o(wr_to_rd_addr)) ;

    ram_infer_wfifo #(.DATAWIDTH_A(DATA_WIDTH_W),
            .ADDRWIDTH_A(ADDR_WIDTH_W),
            .DATAWIDTH_B(DATA_WIDTH_R),
            .ADDRWIDTH_B(ADDR_WIDTH_R),
            .MODE("SDP"),
            .REGMODE_B(OUTREG_EN)) ram_inst (
        .clka(clkw), .rsta(wr_rst), .cea(1'b1), .ocea(1'b0), .wea(wr_en_s),
        .dia(di), .addra(wr_addr),
        .clkb(clkr), .rstb(rd_rst), .ceb((SHOW_AHEAD_EN | rd_en_s)), .oceb(1'b1),
        .web(1'b0), .addrb((rd_addr + (SHOW_AHEAD_EN & rd_en_s))),
        .dob(dout)) ;
endmodule


// ===========================================================================
// 格雷码指针跨时钟域同步：primary 域地址打格雷码寄存，secondary 域两级同步后转回二进制
// ===========================================================================
module fifo_cross_domain_addr_process_al_wfifo #(parameter ADDR_WIDTH = 9) (
    input primary_asreset_i,
    input secondary_asreset_i,
    input primary_clk_i,
    input secondary_clk_i,
    input [(ADDR_WIDTH - 1):0] primary_addr_i,
    output reg [(ADDR_WIDTH - 1):0] secondary_addr_o) ;

    wire [(ADDR_WIDTH - 1):0] primary_addr_gray ;
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

    assign primary_addr_gray = bin2gray(primary_addr_i) ;

    // primary 域：格雷码打拍
    always @(posedge primary_clk_i or posedge primary_asreset_i)
        if (primary_asreset_i == 1'b1) primary_addr_gray_reg <= 0 ;
        else primary_addr_gray_reg <= primary_addr_gray ;

    // secondary 域：两级同步 + 转回二进制（同一时钟/复位，合并为一个 always）
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


// ===========================================================================
// RAM 推断：A 口写、B 口读；支持 SP/SDP/DP 三种模式与字节写/输出寄存/复位模式
// ===========================================================================
module ram_infer_wfifo (clka,
        rsta,
        cea,
        ocea,
        wea,
        dia,
        addra,
        doa,
        clkb,
        rstb,
        ceb,
        oceb,
        web,
        dib,
        addrb,
        dob) ;
    //parameter
    parameter MODE = "SDP" ; //SP, SDP, DP
    parameter BYTE_SIZE = 8 ; //8, 9, 10
    parameter INIT_FILE = "" ; //memory initialization file which can be $readmemh, generated by software from .mif
    parameter INIT_VALUE = 0 ;
    parameter USE_BYTE_WEA = 0 ; //0,1, use Byte Writes or not
    parameter DATAWIDTH_A = 32 ; //A WIDTH
    parameter WEA_WIDTH = ((USE_BYTE_WEA == 0) ? 1 : (DATAWIDTH_A / BYTE_SIZE)) ; //wea port width
    parameter REGMODE_A = "NOREG" ; //OUTREG, NOREG
    parameter RESETMODE_A = "ASYNC" ; //SYNC, ASYNC, ARSR
    parameter INITVAL_A = {DATAWIDTH_A{1'b0}} ; //A initial value or reset value
    parameter WRITEMODE_A = "NORMAL" ; //NORMAL, WRITETHROUGH, READBEFOREWRITE
    parameter ADDRWIDTH_A = 5 ; //A ADDR WIDTH
    parameter DATADEPTH_A = (2 ** ADDRWIDTH_A) ; //A DEPTH
    parameter SSROVERCE_A = "ENABLE" ; //ENABLE, DISABLE
    parameter USE_BYTE_WEB = USE_BYTE_WEA ; //0,1, use Byte Writes or not
    parameter DATAWIDTH_B = DATAWIDTH_A ; //B WIDTH
    parameter WEB_WIDTH = ((USE_BYTE_WEB == 0) ? 1 : (DATAWIDTH_B / BYTE_SIZE)) ; //web port width
    parameter REGMODE_B = "NOREG" ; //OUTREG, NOREG
    parameter RESETMODE_B = "ASYNC" ; //SYNC, ASYNC, ARSR
    parameter INITVAL_B = {DATAWIDTH_B{1'b0}} ; //B initial value or reset value
    parameter WRITEMODE_B = "NORMAL" ; //NORMAL, WRITETHROUGH, READBEFOREWRITE
    parameter ADDRWIDTH_B = ADDRWIDTH_A ; //B ADDR WIDTH
    parameter DATADEPTH_B = (2 ** ADDRWIDTH_B) ; //A DEPTH
    parameter SSROVERCE_B = "ENABLE" ; //ENABLE, DISABLEi
    //input
    input clka, rsta, cea, ocea ;
    input [(WEA_WIDTH - 1):0] wea ;
    input [(DATAWIDTH_A - 1):0] dia ;
    input [(ADDRWIDTH_A - 1):0] addra ;
    input clkb, rstb, ceb, oceb ;
    input [(WEB_WIDTH - 1):0] web ;
    input [(DATAWIDTH_B - 1):0] dib ;
    input [(ADDRWIDTH_B - 1):0] addrb ;
    //output
    output [(DATAWIDTH_A - 1):0] doa ;
    output [(DATAWIDTH_B - 1):0] dob ;

    localparam MIN_WIDTH = ((DATAWIDTH_A > DATAWIDTH_B) ? DATAWIDTH_B : DATAWIDTH_A) ;
    localparam MAX_DEPTH = ((DATADEPTH_A > DATADEPTH_B) ? DATADEPTH_A : DATADEPTH_B) ;
    localparam WIDTHRATIO_A = (DATAWIDTH_A / MIN_WIDTH) ;
    localparam WIDTHRATIO_B = (DATAWIDTH_B / MIN_WIDTH) ;

    reg [(MIN_WIDTH - 1):0] memory [(MAX_DEPTH - 1):0] ;  /* fehdl force_ram=1, ram_style="bram" */
    reg [(DATAWIDTH_A - 1):0] doa_tmp = INITVAL_A ;
    reg [(DATAWIDTH_B - 1):0] dob_tmp = INITVAL_B ;
    reg [(DATAWIDTH_A - 1):0] doa_tmp2 = INITVAL_A ;
    reg [(DATAWIDTH_B - 1):0] dob_tmp2 = INITVAL_B ;

    // =============================================
    // 写端口
    // =============================================
    always @(posedge clka)
        if (cea) write_a(addra, dia, wea) ;

    always @(posedge clkb)
        if (ceb && (MODE == "DP")) write_b(addrb, dib, web) ;

    // =============================================
    // A 口复位：SYNC 同步复位 / ASYNC 异步复位 / ARSR 异步复位同步释放
    // =============================================
    reg rsta_p1 = 1, rsta_p2 = 1 ;
    wire rsta_p3 = ((rsta_p2 | rsta_p1) | rsta) ;
    always @(posedge clka or posedge rsta)
        if (rsta) rsta_p1 <= 1 ;
        else rsta_p1 <= 0 ;
    always @(negedge clka) rsta_p2 <= rsta_p1 ;

    wire sync_rsta = ((RESETMODE_A == "SYNC") && rsta) ;
    wire async_rsta = ((RESETMODE_A == "ASYNC") ? rsta : ((RESETMODE_A == "ARSR") ? rsta_p3 : 0)) ;
    wire ssroverce_a = ((SSROVERCE_A == "ENABLE") && sync_rsta) ;
    wire ceoverssr_a = ((SSROVERCE_A == "DISABLE") && sync_rsta) ;

    // A 口出数寄存（REGMODE_A="OUTREG" 时先经 doa_tmp2）
    always @(posedge clka or posedge async_rsta)
        if (async_rsta) doa_tmp2 <= INITVAL_A ;
        else if (ssroverce_a) doa_tmp2 <= INITVAL_A ;
        else if (ocea) doa_tmp2 <= (ceoverssr_a ? INITVAL_A : doa_tmp) ;

    // A 口读数据：SP/DP 模式读，NOREG 直接输出
    always @(posedge clka or posedge async_rsta)
        if (async_rsta) doa_tmp <= INITVAL_A ;
        else if (sync_rsta) doa_tmp <= INITVAL_A ;
        else if (cea && ((MODE == "SP") || (MODE == "DP"))) read_a(addra, dia, wea) ;

    // =============================================
    // B 口复位
    // =============================================
    reg rstb_p1 = 1, rstb_p2 = 1 ;
    wire rstb_p3 = ((rstb_p1 | rstb_p2) | rstb) ;
    always @(posedge clkb or posedge rstb)
        if (rstb) rstb_p1 <= 1 ;
        else rstb_p1 <= 0 ;
    always @(negedge clkb) rstb_p2 <= rstb_p1 ;

    wire sync_rstb = ((RESETMODE_B == "SYNC") && rstb) ;
    wire async_rstb = ((RESETMODE_B == "ASYNC") ? rstb : ((RESETMODE_B == "ARSR") ? rstb_p3 : 0)) ;
    wire ssroverce_b = ((SSROVERCE_B == "ENABLE") && sync_rstb) ;
    wire ceoverssr_b = ((SSROVERCE_B == "DISABLE") && sync_rstb) ;

    always @(posedge clkb or posedge async_rstb)
        if (async_rstb) dob_tmp2 <= INITVAL_B ;
        else if (ssroverce_b) dob_tmp2 <= INITVAL_B ;
        else if (oceb) dob_tmp2 <= (ceoverssr_b ? INITVAL_B : dob_tmp) ;

    // B 口读数据：SDP 只读，DP 读写
    always @(posedge clkb or posedge async_rstb)
        if (async_rstb) dob_tmp <= INITVAL_B ;
        else if (sync_rstb) dob_tmp <= INITVAL_B ;
        else if (ceb) begin
            if (MODE == "DP") read_b(addrb, dib, web) ;
            else if (MODE == "SDP") read_b(addrb, dib, 1'b0) ;
        end

    assign doa = ((REGMODE_A == "OUTREG") ? doa_tmp2 : doa_tmp) ;
    assign dob = ((REGMODE_B == "OUTREG") ? dob_tmp2 : dob_tmp) ;

    // =============================================
    // 读写任务：按 MIN_WIDTH 拆分位宽；字节写按 BYTE_SIZE 分片
    // =============================================
    task write_a ;
        input [(ADDRWIDTH_A - 1):0] addr ;
        input [(DATAWIDTH_A - 1):0] data ;
        input [(WEA_WIDTH - 1):0] we ;
        integer i, j ;
        begin
            if (USE_BYTE_WEA != 0) begin
                for (i = 0 ; i < WIDTHRATIO_A ; i = i + 1)
                    for (j = 0 ; j < (WEA_WIDTH / WIDTHRATIO_A) ; j = j + 1)
                        if (we[(((i * WEA_WIDTH) / WIDTHRATIO_A) + j)])
                            memory[((addr * WIDTHRATIO_A) + i)][(j * BYTE_SIZE) +: BYTE_SIZE]
                                <= data[((i * MIN_WIDTH) + (j * BYTE_SIZE)) +: BYTE_SIZE] ;
            end
            else if (we[0]) begin
                for (i = 0 ; i < WIDTHRATIO_A ; i = i + 1)
                    memory[((addr * WIDTHRATIO_A) + i)] <= data[(i * MIN_WIDTH) +: MIN_WIDTH] ;
            end
        end
    endtask

    task write_b ;
        input [(ADDRWIDTH_B - 1):0] addr ;
        input [(DATAWIDTH_B - 1):0] data ;
        input [(WEB_WIDTH - 1):0] we ;
        integer i, j ;
        begin
            if (USE_BYTE_WEB != 0) begin
                for (i = 0 ; i < WIDTHRATIO_B ; i = i + 1)
                    for (j = 0 ; j < (WEB_WIDTH / WIDTHRATIO_B) ; j = j + 1)
                        if (we[(((i * WEB_WIDTH) / WIDTHRATIO_B) + j)])
                            memory[((addr * WIDTHRATIO_B) + i)][(j * BYTE_SIZE) +: BYTE_SIZE]
                                <= data[((i * MIN_WIDTH) + (j * BYTE_SIZE)) +: BYTE_SIZE] ;
            end
            else if (we[0]) begin
                for (i = 0 ; i < WIDTHRATIO_B ; i = i + 1)
                    memory[((addr * WIDTHRATIO_B) + i)] <= data[(i * MIN_WIDTH) +: MIN_WIDTH] ;
            end
        end
    endtask

    task read_a ;
        input [(ADDRWIDTH_A - 1):0] addr ;
        input [(DATAWIDTH_A - 1):0] data ;
        input [(WEA_WIDTH - 1):0] we ;
        integer i, j ;
        begin
            if (USE_BYTE_WEA != 0) begin
                if (WRITEMODE_A == "NORMAL") begin
                    for (i = 0 ; i < WIDTHRATIO_A ; i = i + 1)
                        for (j = 0 ; j < (WEA_WIDTH / WIDTHRATIO_A) ; j = j + 1)
                            if (!we[(((i * WEA_WIDTH) / WIDTHRATIO_A) + j)])
                                doa_tmp[((i * MIN_WIDTH) + (j * BYTE_SIZE)) +: BYTE_SIZE]
                                    <= memory[((addr * WIDTHRATIO_A) + i)][(j * BYTE_SIZE) +: BYTE_SIZE] ;
                end
                else if (WRITEMODE_A == "WRITETHROUGH") begin
                    for (i = 0 ; i < WIDTHRATIO_A ; i = i + 1)
                        for (j = 0 ; j < (WEA_WIDTH / WIDTHRATIO_A) ; j = j + 1)
                            if (we[(((i * WEA_WIDTH) / WIDTHRATIO_A) + j)])
                                doa_tmp[((i * MIN_WIDTH) + (j * BYTE_SIZE)) +: BYTE_SIZE]
                                    <= data[((i * MIN_WIDTH) + (j * BYTE_SIZE)) +: BYTE_SIZE] ;
                            else
                                doa_tmp[((i * MIN_WIDTH) + (j * BYTE_SIZE)) +: BYTE_SIZE]
                                    <= memory[((addr * WIDTHRATIO_A) + i)][(j * BYTE_SIZE) +: BYTE_SIZE] ;
                end
                else begin
                    for (i = 0 ; i < WIDTHRATIO_A ; i = i + 1)
                        doa_tmp[(MIN_WIDTH * i) +: MIN_WIDTH] <= memory[((addr * WIDTHRATIO_A) + i)] ;
                end
            end
            else begin
                if (WRITEMODE_A == "NORMAL") begin
                    if (!we[0])
                        for (i = 0 ; i < WIDTHRATIO_A ; i = i + 1)
                            doa_tmp[(MIN_WIDTH * i) +: MIN_WIDTH] <= memory[((addr * WIDTHRATIO_A) + i)] ;
                end
                else if (WRITEMODE_A == "WRITETHROUGH") begin
                    if (we[0]) doa_tmp <= data ;
                    else
                        for (i = 0 ; i < WIDTHRATIO_A ; i = i + 1)
                            doa_tmp[(MIN_WIDTH * i) +: MIN_WIDTH] <= memory[((addr * WIDTHRATIO_A) + i)] ;
                end
                else begin
                    for (i = 0 ; i < WIDTHRATIO_A ; i = i + 1)
                        doa_tmp[(MIN_WIDTH * i) +: MIN_WIDTH] <= memory[((addr * WIDTHRATIO_A) + i)] ;
                end
            end
        end
    endtask

    task read_b ;
        input [(ADDRWIDTH_B - 1):0] addr ;
        input [(DATAWIDTH_B - 1):0] data ;
        input [(WEB_WIDTH - 1):0] we ;
        integer i, j ;
        begin
            if (USE_BYTE_WEB != 0) begin
                if (WRITEMODE_B == "NORMAL") begin
                    for (i = 0 ; i < WIDTHRATIO_B ; i = i + 1)
                        for (j = 0 ; j < (WEB_WIDTH / WIDTHRATIO_B) ; j = j + 1)
                            if (!we[(((i * WEB_WIDTH) / WIDTHRATIO_B) + j)])
                                dob_tmp[((i * MIN_WIDTH) + (j * BYTE_SIZE)) +: BYTE_SIZE]
                                    <= memory[((addr * WIDTHRATIO_B) + i)][(j * BYTE_SIZE) +: BYTE_SIZE] ;
                end
                else if (WRITEMODE_B == "WRITETHROUGH") begin
                    for (i = 0 ; i < WIDTHRATIO_B ; i = i + 1)
                        for (j = 0 ; j < (WEB_WIDTH / WIDTHRATIO_B) ; j = j + 1)
                            if (we[(((i * WEB_WIDTH) / WIDTHRATIO_B) + j)])
                                dob_tmp[((i * MIN_WIDTH) + (j * BYTE_SIZE)) +: BYTE_SIZE]
                                    <= data[((i * MIN_WIDTH) + (j * BYTE_SIZE)) +: BYTE_SIZE] ;
                            else
                                dob_tmp[((i * MIN_WIDTH) + (j * BYTE_SIZE)) +: BYTE_SIZE]
                                    <= memory[((addr * WIDTHRATIO_B) + i)][(j * BYTE_SIZE) +: BYTE_SIZE] ;
                end
                else begin
                    for (i = 0 ; i < WIDTHRATIO_B ; i = i + 1)
                        dob_tmp[(MIN_WIDTH * i) +: MIN_WIDTH] <= memory[((addr * WIDTHRATIO_B) + i)] ;
                end
            end
            else begin
                if (WRITEMODE_B == "NORMAL") begin
                    if (!we[0])
                        for (i = 0 ; i < WIDTHRATIO_B ; i = i + 1)
                            dob_tmp[(MIN_WIDTH * i) +: MIN_WIDTH] <= memory[((addr * WIDTHRATIO_B) + i)] ;
                end
                else if (WRITEMODE_B == "WRITETHROUGH") begin
                    if (we[0]) dob_tmp <= data ;
                    else
                        for (i = 0 ; i < WIDTHRATIO_B ; i = i + 1)
                            dob_tmp[(MIN_WIDTH * i) +: MIN_WIDTH] <= memory[((addr * WIDTHRATIO_B) + i)] ;
                end
                else begin
                    for (i = 0 ; i < WIDTHRATIO_B ; i = i + 1)
                        dob_tmp[(MIN_WIDTH * i) +: MIN_WIDTH] <= memory[((addr * WIDTHRATIO_B) + i)] ;
                end
            end
        end
    endtask
endmodule
