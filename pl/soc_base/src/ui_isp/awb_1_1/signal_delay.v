/*****************************************************************
 Company : MiLianKe Electronic Technology Co., Ltd. （ui_isp/awb_1_1/signal_delay.v）
 50 拍延迟线（BRAM）：把像素流对齐到 awb 逐位除法器的延迟；按 DELAY_CYCLE 参数在行内滞后启动读突发。
 2026-10 重构：行/列计数位宽由参数推导；合并同复位非阻塞 always、精简文件头，逐拍等价。
*****************************************************************/
module signal_delay #(
    parameter IMG_HEIGHT  = 1080,
    parameter IMG_WIDTH   = 1920,
    parameter DATA_WIDTH  = 96,
    parameter DELAY_CYCLE = 50
) (
    input  wire                  I_clk,
    input  wire                  I_rst_n,
    input  wire                  I_tuser,  //synthesis keep
    input  wire                  I_valid,  //synthesis keep
    input  wire [DATA_WIDTH-1:0] I_data,   //synthesis keep
    output wire                  O_valid,  //synthesis keep
    output wire [DATA_WIDTH-1:0] O_data,   //synthesis keep
    output wire                  O_tuser   //synthesis keep
);

    localparam IMG_WIDTH_4x = (IMG_WIDTH >> 2);      // beats per line
    localparam HCNT_W = $clog2(IMG_WIDTH_4x + 1);    // counter widths from parameters
    localparam VCNT_W = $clog2(IMG_HEIGHT + 1);      // $clog2: Verilog-2005, TD 6.2.1 ok

    reg  [HCNT_W-1:0]     h_cnt, rd_cnt;  // beat index in a line / in the read burst
    reg  [VCNT_W-1:0]     v_cnt;          // line index
    reg                   rd_valid;       // read burst active
    reg                   rd_valid_d;
    reg  [HCNT_W-1:0]     addrb;          //synthesis keep（写地址 addra 与 h_cnt 逐拍同轨迹，已删，见下）
    reg  [DATA_WIDTH-1:0] I_data_r;
    reg                   I_valid_r;

    // 写地址：原 addra 与 h_cnt 的复位/递增/回绕条件逐字相同（同一 always、同 tuser 清零、同 I_valid_r 门控、
    // 同在 IMG_WIDTH_4x-1 回绕），故二者逐拍恒等，直接复用 h_cnt；BRAM 11bit 地址口零扩展（HCNT_W<=11 即
    // IMG_WIDTH<=8192，本工程/全部 TB 均 1920）。addrb 的回绕常数 IMG_WIDTH_4x-1 也 < 2^HCNT_W，等宽收窄。
    wire [10:0] bram_addra = h_cnt;
    wire [10:0] bram_addrb = addrb;

    // 原 3 个 always（输入打拍 / 行列计数 / 读突发与地址）同复位、同时钟、全非阻塞，合并后逐拍等价。
    // beat/line counters advance on valid beats only, cleared at frame start；
    // read burst starts at h_cnt == DELAY_CYCLE-3 for IMG_WIDTH_4x beats
    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            I_valid_r <= 1'b0;  I_data_r <= 0;  rd_valid_d <= 1'b0;
            h_cnt <= 0;         v_cnt <= 0;
            rd_valid <= 1'b0;   rd_cnt <= 0;  addrb <= 0;
        end else begin
            I_valid_r  <= I_valid;
            I_data_r   <= I_data;
            rd_valid_d <= rd_valid;
            if (I_tuser) begin
                h_cnt <= 0;
                v_cnt <= 0;
            end else begin
                h_cnt <= I_valid_r ? ((h_cnt == IMG_WIDTH_4x - 1) ? 0 : h_cnt + 1'b1) : h_cnt;
                v_cnt <= (I_valid_r && (h_cnt == IMG_WIDTH_4x - 1))
                         ? ((v_cnt == IMG_HEIGHT - 1) ? 0 : v_cnt + 1'b1) : v_cnt;
            end
            if (I_tuser) begin
                rd_valid <= 1'b0;  rd_cnt <= 0;  addrb <= 0;
            end else begin
                addrb  <= rd_valid ? ((addrb == IMG_WIDTH_4x - 1) ? 0 : addrb + 1'b1) : addrb;
                rd_cnt <= (rd_cnt == IMG_WIDTH_4x) ? 0 : (rd_valid ? rd_cnt + 1'b1 : rd_cnt);
                if (h_cnt == DELAY_CYCLE - 3)         rd_valid <= 1'b1;
                else if (rd_cnt == IMG_WIDTH_4x - 1)  rd_valid <= 1'b0;
            end
        end
    end

    assign O_valid = rd_valid_d;
    assign O_tuser = (v_cnt == 0) && (rd_cnt == 1);

    blk_mem_gen_awb_delay_signal blk_mem_gen_awb_delay_signal_d (
      .clka(I_clk), .wea(I_valid_r), .addra(bram_addra), .dia(I_data_r),
      .clkb(I_clk), .addrb(bram_addrb), .dob(O_data)
    );

endmodule
