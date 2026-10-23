/*****************************************************************
Company : MiLianKe Electronic Technology Co., Ltd.
WebSite:https://www.milianke.com
TechWeb:https://www.uisrc.com
tmall-shop:https://milianke.tmall.com
jd-shop:https://milianke.jd.com
taobao-shop: https://milianke.taobao.com
Description: 
The reference demo provided by Milianke is only used for learning. 
We cannot ensure that the demo itself is free of bugs, so users 
should be responsible for the technical problems and consequences
caused by the use of their own products.
@Author      :   XiaoQingquan 
@Time        :   2025/01 
version:     :   
@Modified    :   DragonEyeSorter 2026-10-24: source-level trim, cycle-exact (see pl/sim/equiv/tb/awb.v).
@Description :   
*****************************************************************/
module signal_delay #(
    parameter IMG_HEIGHT = 1080,
    parameter IMG_WIDTH  = 1920,
    parameter DATA_WIDTH = 96,
    parameter DELAY_CYCLE = 50
) (
    input                        I_clk,
    input                        I_rst_n,
    input                        I_tuser,  //synthesis keep 
    input  wire                  I_valid,  //synthesis keep 
    input  wire [DATA_WIDTH-1:0] I_data,   //synthesis keep 
    output wire                  O_valid,  //synthesis keep 
    output wire [DATA_WIDTH-1:0] O_data ,  //synthesis keep 
    output wire                  O_tuser   //synthesis keep 
);

    localparam IMG_WIDTH_4x = (IMG_WIDTH >> 2);      // beats per line
    localparam HCNT_W = $clog2(IMG_WIDTH_4x + 1);    // counter widths from parameters (was fixed 14 bits)
    localparam VCNT_W = $clog2(IMG_HEIGHT + 1);      // $clog2: Verilog-2005, TD 6.2.1 ok

    reg  [HCNT_W-1:0] h_cnt, rd_cnt;   // beat index in a line / in the read burst
    reg  [VCNT_W-1:0] v_cnt;           // line index
    reg               rd_valid;        // read burst active
    reg  [10:0] addra;  //synthesis keep 
    reg  [10:0] addrb;  //synthesis keep 
    reg  [DATA_WIDTH-1:0] I_data_r;
    reg                   I_valid_r;
    reg                   rd_valid_d;

    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            I_valid_r  <= 1'b0;
            I_data_r   <= 0;
            rd_valid_d <= 1'b0;
        end else begin
            I_valid_r  <= I_valid;
            I_data_r   <= I_data;
            rd_valid_d <= rd_valid;
        end
    end

    // beat/line counters advance on valid beats only, cleared at frame start
    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n || I_tuser) begin
            h_cnt <= 0;
            v_cnt <= 0;
        end else begin
            h_cnt <= I_valid_r ? ((h_cnt == IMG_WIDTH_4x - 1) ? 0 : h_cnt + 1'b1) : h_cnt;
            v_cnt <= (I_valid_r && (h_cnt == IMG_WIDTH_4x - 1)) ? ((v_cnt == IMG_HEIGHT - 1) ? 0 : v_cnt + 1'b1) : v_cnt;
        end
    end

    // read burst starts at h_cnt == DELAY_CYCLE-3 for IMG_WIDTH_4x beats; the four regs share one clear
    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n || I_tuser) begin
            rd_valid <= 1'b0;
            rd_cnt   <= 0;
            addra    <= 0;
            addrb    <= 0;
        end else begin
            addra  <= I_valid_r ? ((addra == IMG_WIDTH_4x - 1) ? 0 : addra + 1'b1) : addra;
            addrb  <= rd_valid ? ((addrb == IMG_WIDTH_4x - 1) ? 0 : addrb + 1'b1) : addrb;
            rd_cnt <= (rd_cnt == IMG_WIDTH_4x) ? 0 : (rd_valid ? rd_cnt + 1'b1 : rd_cnt);
            if (h_cnt == DELAY_CYCLE - 3) rd_valid <= 1'b1;
            else if (rd_cnt == IMG_WIDTH_4x - 1) rd_valid <= 1'b0;
        end
    end

    assign O_valid = rd_valid_d;
    assign O_tuser = (v_cnt == 0) && (rd_cnt == 1);

    blk_mem_gen_awb_delay_signal blk_mem_gen_awb_delay_signal_d (
      .clka (I_clk   ),  .wea (I_valid_r),  .addra(addra),  .dia(I_data_r),
      .clkb (I_clk   ),  .addrb(addrb   ),  .dob  (O_data )
    );

endmodule
