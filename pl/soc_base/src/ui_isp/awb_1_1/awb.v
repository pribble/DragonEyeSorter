/*****************************************************************
 Company : MiLianKe Electronic Technology Co., Ltd. （ui_isp/awb_1_1/awb.v）
 自动白平衡：帧内统计 R/G/B 和 → 逐位除法器算 G/R、G/B 增益 → 4 像素 12bit 增益流水。
 DragonEyeSorter 2026-10-11: 两个 50 级流水线除法器 IP 换成 awb_seq_div（背靠背、每 49 时钟一个商，
 ~4k LUT / 5k FF 更少；增益不再每帧复位，帧首若干拍与厂家 1.0 的差异已由 pl/sim/tb_awb.v 覆盖）。
 2026-10 重构（2026-10-24）：源码级精简，逐拍等价（pl/sim/equiv/tb/awb.v）。
*****************************************************************/
module awb #(
    parameter IMG_HEIGHT = 1080,
    parameter IMG_WIDTH  = 1920
) (
    input  wire        I_clk, I_rst_n,
    input  wire        I_tlast,       // unused (port kept); O_tlast is derived from valid
    input  wire        I_tuser,
    input  wire [95:0] I_tdata,
    input  wire        I_tvalid,
    output wire        I_tready,
    output wire        O_tlast, O_tuser,
    output wire [95:0] O_tdata,
    output wire        O_tvalid,
    input  wire        O_tready
);

    // 输入打拍：统计用 d0，像素增益路径经 50 拍延迟线对齐除法器延迟
    reg  [95:0] I_tdata_d0, I_tdata_d1;
    reg         I_tvalid_r0, I_tvalid_r1;

    // 帧统计：只累加每拍 4 像素中的 pixel0；R+G+B >= 700 视为亮像素跳过。
    // rgb_sum 原是单独寄存器，与此刻 I_tdata_d0 同拍和，故组合求和等价。
    wire [7:0]  pix_r0 = I_tdata_d0[16 +: 8];
    wire [7:0]  pix_g0 = I_tdata_d0[ 8 +: 8];
    wire [7:0]  pix_b0 = I_tdata_d0[ 0 +: 8];
    wire [9:0]  rgb_sum = pix_r0 + pix_g0 + pix_b0;
    wire        pix_ok  = I_tvalid_r0 && (rgb_sum < 10'd700);

    reg  [31:0] sum_r,   sum_g,   sum_b;      // frame sum latched by flag_frame_r, used by the dividers
    reg  [31:0] sum_r_d, sum_g_d, sum_b_d;    // in-frame sum, cleared while frame_cnt==4
    reg  [2:0]  frame_cnt;                    // 0..4, one step per I_tuser
    reg  [1:0]  flag_frame;                   // {flag_frame_d, flag_frame}; pulse = flag_frame & ~flag_frame_d
    wire        flag_frame_r = flag_frame[0] && !flag_frame[1];

    // 逐位除法器：G/R（dout_R）与 G/B（dout_B），Q16.16；start 常 1，忙时忽略
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

    // 增益寄存器：复位 1.0，商溢出时钳到 0xFFFFF（厂家每帧复位；2026-10-11 起帧间保持、每 49 时钟更新）
    reg [19:0] in_r_r1, in_b_r1;

    // 50 拍延迟线输出与输出流水
    wire [95:0] data_d0;
    wire        valid_d0;
    wire        I_tuser_r0;
    reg         valid_d1, valid_d2;
    reg         I_tuser_r1, I_tuser_r2;
    reg  [95:0] data_d1;

    // 原 4 个 always（输入打拍 / 帧统计 / 增益更新 / 输出流水）同复位、同时钟、全非阻塞，合并后逐拍等价
    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            {I_tdata_d0, I_tdata_d1} <= 0;
            I_tvalid_r0 <= 1'b0;
            I_tvalid_r1 <= 1'b0;
            sum_r <= 1;  sum_g <= 1;  sum_b <= 1;
            sum_r_d <= 1; sum_g_d <= 1; sum_b_d <= 1;
            frame_cnt <= 0;
            flag_frame <= 0;
            in_r_r1 <= 20'h10000;
            in_b_r1 <= 20'h10000;
            valid_d1 <= 1'b0;  valid_d2 <= 1'b0;
            I_tuser_r1 <= 1'b0;  I_tuser_r2 <= 1'b0;
            data_d1 <= 0;
        end else begin
            I_tdata_d0  <= I_tdata;
            I_tdata_d1  <= I_tdata_d0;
            I_tvalid_r0 <= I_tvalid;
            I_tvalid_r1 <= I_tvalid_r0;

            frame_cnt  <= (frame_cnt == 4) ? 0 : (I_tuser ? frame_cnt + 1'b1 : frame_cnt);
            flag_frame <= {flag_frame[0], (frame_cnt == 1)};
            // latch the frozen frame sum on the flag_frame rising edge, accumulate otherwise
            sum_r <= flag_frame_r ? sum_r_d : (pix_ok ? sum_r + pix_r0 : sum_r);
            sum_g <= flag_frame_r ? sum_g_d : (pix_ok ? sum_g + pix_g0 : sum_g);
            sum_b <= flag_frame_r ? sum_b_d : (pix_ok ? sum_b + pix_b0 : sum_b);
            sum_r_d <= (frame_cnt == 4) ? 32'd1 : (pix_ok ? sum_r_d + pix_r0 : sum_r_d);
            sum_g_d <= (frame_cnt == 4) ? 32'd1 : (pix_ok ? sum_g_d + pix_g0 : sum_g_d);
            sum_b_d <= (frame_cnt == 4) ? 32'd1 : (pix_ok ? sum_b_d + pix_b0 : sum_b_d);

            if (done) begin
                in_r_r1 <= |dout_R[47:20] ? 20'hfffff : dout_R[19:0];
                in_b_r1 <= |dout_B[47:20] ? 20'hfffff : dout_B[19:0];
            end

            valid_d1   <= valid_d0;
            valid_d2   <= valid_d1;
            I_tuser_r1 <= I_tuser_r0;
            I_tuser_r2 <= I_tuser_r1;
            data_d1    <= data_d0;
        end
    end

    signal_delay #(
        .IMG_HEIGHT(IMG_HEIGHT), .IMG_WIDTH(IMG_WIDTH),
        .DATA_WIDTH(96), .DELAY_CYCLE(50)
    ) signal_delay_d (
        .I_clk(I_clk), .I_rst_n(I_rst_n),
        .I_tuser(I_tuser),        //synthesis keep
        .I_valid(I_tvalid_r1),    //synthesis keep
        .I_data (I_tdata_d1 ),    //synthesis keep
        .O_valid(valid_d0   ),    //synthesis keep
        .O_data (data_d0    ),    //synthesis keep
        .O_tuser(I_tuser_r0 )
    );

    // 4 像素 × RGB：R/B 乘 Q16.16 增益，只取乘积 [27:16]（12bit）：[27:24]!=0 → 255，否则 [23:16]；
    // 与原版 28bit 寄存器逐位一致。G 直通。
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
