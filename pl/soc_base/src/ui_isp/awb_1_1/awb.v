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
@Time        :   2024/10 
version:     :   
@Modified    :   DragonEyeSorter 2026-10-11: the two 50-stage pipelined divider IPs (a new
                 quotient every clock) replaced by awb_seq_div running back to back (a new quotient
                 every 49 clocks). The colour sums are large and change slowly, so the gains differ
                 from the vendor's by at most a step or two; ~4k LUT / 5k FF less (v6 did not route).
                 The gains are no longer reset to 1.0 at every frame start (the pipeline used to
                 recompute them within 50 clocks; the vendor's first beats of a frame ran at 1.0).
                 DragonEyeSorter 2026-10-24: source-level trim, cycle-exact (pl/sim/equiv/tb/awb.v).
@Description :   在原基础上删去了一些赘余的代码，并且优化了时序，加入BRAM IP核，
                  让从除法器出来的每一个数据都可以对上相应的像素数据进行增益，使
                  得它的适用行更加广泛了。
*****************************************************************/
module awb  #(   
    parameter IMG_HEIGHT = 1080,
    parameter IMG_WIDTH  = 1920
) 
(
    input                   I_clk  ,
    input                   I_rst_n,


    input                   I_tlast  ,   // unused (port kept); O_tlast is derived from valid
    input                   I_tuser  ,
    input [95:0]            I_tdata  ,
    input                   I_tvalid , 
    output                  I_tready ,

    output                  O_tlast  ,
    output                  O_tuser  ,
    output [95:0]           O_tdata  ,
    output                  O_tvalid ,
    input                   O_tready
);

/*****************************************************************
    input registers: data/valid delayed 2 cycles
    (rgb statistics use d0, the gain pixel path uses d1 through the 50-clock delay line)
*****************************************************************/
    reg  [95:0] I_tdata_d0, I_tdata_d1;
    reg         I_tvalid_r0, I_tvalid_r1;

    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            I_tdata_d0  <= 0;
            I_tdata_d1  <= 0;
            I_tvalid_r0 <= 1'b0;
            I_tvalid_r1 <= 1'b0;
        end else begin
            I_tdata_d0  <= I_tdata;
            I_tdata_d1  <= I_tdata_d0;
            I_tvalid_r0 <= I_tvalid;
            I_tvalid_r1 <= I_tvalid_r0;
        end
    end

/*****************************************************************
    frame statistics: only pixel0 of each 4-pixel beat is accumulated;
    pixel0 with R+G+B >= 700 is treated as a bright pixel and skipped.
    rgb_sum used to be registered separately, but it was the sum of the
    same beat now held in I_tdata_d0, so the combinational sum is equivalent.
*****************************************************************/
    wire [7:0] pix_r0 = I_tdata_d0[16+:8];
    wire [7:0] pix_g0 = I_tdata_d0[ 8+:8];
    wire [7:0] pix_b0 = I_tdata_d0[ 0+:8];
    wire [9:0] rgb_sum = pix_r0 + pix_g0 + pix_b0;
    wire       pix_ok  = I_tvalid_r0 && (rgb_sum < 10'd700);

    reg  [31:0] sum_r,   sum_g,   sum_b;      // frame sum latched by flag_frame_r, used by the dividers
    reg  [31:0] sum_r_d, sum_g_d, sum_b_d;    // in-frame sum, cleared while frame_cnt==4
    reg  [2:0]  frame_cnt;                    // 0..4, one step per I_tuser
    reg  [1:0]  flag_frame;                   // {flag_frame_d, flag_frame}; pulse = flag_frame & ~flag_frame_d
    wire        flag_frame_r = flag_frame[0] && !flag_frame[1];

    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            sum_r <= 1;  sum_g <= 1;  sum_b <= 1;
            sum_r_d <= 1; sum_g_d <= 1; sum_b_d <= 1;
            frame_cnt <= 0;
            flag_frame <= 0;
        end else begin
            frame_cnt  <= (frame_cnt == 4) ? 0 : (I_tuser ? frame_cnt + 1'b1 : frame_cnt);
            flag_frame <= {flag_frame[0], (frame_cnt == 1)};
            // latch the frozen frame sum on the flag_frame rising edge, accumulate otherwise
            sum_r <= flag_frame_r ? sum_r_d : (pix_ok ? sum_r + pix_r0 : sum_r);
            sum_g <= flag_frame_r ? sum_g_d : (pix_ok ? sum_g + pix_g0 : sum_g);
            sum_b <= flag_frame_r ? sum_b_d : (pix_ok ? sum_b + pix_b0 : sum_b);
            sum_r_d <= (frame_cnt == 4) ? 32'd1 : (pix_ok ? sum_r_d + pix_r0 : sum_r_d);
            sum_g_d <= (frame_cnt == 4) ? 32'd1 : (pix_ok ? sum_g_d + pix_g0 : sum_g_d);
            sum_b_d <= (frame_cnt == 4) ? 32'd1 : (pix_ok ? sum_b_d + pix_b0 : sum_b_d);
        end
    end

/*****************************************************************
    gain = G/R (dout_R) and G/B (dout_B), Q16.16.  The two awb_seq_div
    run back to back with start tied high: a new quotient every 49 clocks.
*****************************************************************/
    wire [47:0] dout_R;
    wire [47:0] dout_B;
    wire        done;

    awb_seq_div div_gen_G_R_r1 (     // divides back to back: start is ignored while busy
        .clk(I_clk), .rst(!I_rst_n), .start(1'b1),
        .denominator(sum_r), .numerator({sum_g,16'd0}), .quotient(dout_R), .done(done)
    );

    awb_seq_div divide_G_B_r1 (
        .clk(I_clk), .rst(!I_rst_n), .start(1'b1),
        .denominator(sum_b), .numerator({sum_g,16'd0}), .quotient(dout_B), .done()
    );

    // gain registers: reset to 1.0, clamped to 0xFFFFF when the quotient overflows
    // (the vendor reset them at every frame start; kept between frames since 2026-10-11, updated every 49 clocks)
    reg [19:0] in_r_r1;
    reg [19:0] in_b_r1;

    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            in_r_r1 <= 20'h10000;
            in_b_r1 <= 20'h10000;
        end else if (done) begin
            in_r_r1 <= |dout_R[47:20] ? 20'hfffff : dout_R[19:0];
            in_b_r1 <= |dout_B[47:20] ? 20'hfffff : dout_B[19:0];
        end
    end

/*****************************************************************
    50-clock delay line: aligns the pixel stream with the divider latency
*****************************************************************/
    wire [95:0] data_d0;
    wire        valid_d0;
    wire        I_tuser_r0;

    signal_delay #(
        .IMG_HEIGHT   (IMG_HEIGHT),
        .IMG_WIDTH    (IMG_WIDTH ),
        .DATA_WIDTH   (96  ),
        .DELAY_CYCLE  (50)
    )signal_delay_d (
        .I_clk  (I_clk  ),
        .I_rst_n(I_rst_n),
        .I_tuser(I_tuser),        //synthesis keep
        .I_valid(I_tvalid_r1),    //synthesis keep
        .I_data (I_tdata_d1 ),    //synthesis keep
        .O_valid(valid_d0   ),    //synthesis keep
        .O_data (data_d0    ),    //synthesis keep
        .O_tuser(I_tuser_r0 )
    );

/*****************************************************************
    output pipeline: valid/tuser one more stage (the gain result is registered on valid_d1)
*****************************************************************/
    reg         valid_d1, valid_d2;
    reg         I_tuser_r1, I_tuser_r2;
    reg  [95:0] data_d1;

    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            valid_d1 <= 1'b0;
            valid_d2 <= 1'b0;
            I_tuser_r1 <= 1'b0;
            I_tuser_r2 <= 1'b0;
            data_d1 <= 0;
        end else begin
            valid_d1 <= valid_d0;
            valid_d2 <= valid_d1;
            I_tuser_r1 <= I_tuser_r0;
            I_tuser_r2 <= I_tuser_r1;
            data_d1 <= data_d0;
        end
    end

/*****************************************************************
    4 pixels x RGB: R/B multiplied by the Q16.16 gain.  Only bits
    [27:16] of the product are kept (12 bits): [27:24] != 0 -> 255,
    otherwise [23:16], identical to the original 28-bit registers.
    G is passed through unchanged.
*****************************************************************/
    reg  [11:0] new_r [0:3];
    reg  [11:0] new_b [0:3];
    reg  [7:0]  new_g [0:3];
    wire [95:0] out_pix;

    genvar i;
    generate for (i = 0; i < 4; i = i + 1) begin : g_pixel
        wire [7:0]  px_r = data_d1[24*i + 16 +: 8];
        wire [7:0]  px_g = data_d1[24*i +  8 +: 8];
        wire [7:0]  px_b = data_d1[24*i      +: 8];
        wire [27:0] prod_r = in_r_r1 * px_r;   // 28-bit product, only [27:16] is used
        wire [27:0] prod_b = in_b_r1 * px_b;

        always @(posedge I_clk or negedge I_rst_n) begin
            if (!I_rst_n) begin
                new_r[i] <= 0;
                new_b[i] <= 0;
                new_g[i] <= 0;
            end else if (valid_d1) begin
                new_r[i] <= prod_r[27:16];
                new_b[i] <= prod_b[27:16];
                new_g[i] <= px_g;
            end
        end

        assign out_pix[24*i +: 24] = {new_r[i][11:8] ? 8'hff : new_r[i][7:0],
                                      new_g[i],
                                      new_b[i][11:8] ? 8'hff : new_b[i][7:0]};
    end endgenerate

    assign O_tlast  = valid_d2 && !valid_d1;
    assign O_tuser  = I_tuser_r2;
    assign O_tvalid = valid_d2;
    assign O_tdata  = O_tvalid ? out_pix : 96'd0;
    assign I_tready = O_tready;

endmodule
