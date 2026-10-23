`timescale 1ns/1ps
// isp_front 等价回归 testbench：ui_isp 前端 9 个厂家模块（golden 9a905cc vs 工作区重构版共用）。
// 每拍把全部实例的全部输出（含 x/z 态）写入 trace；激励固定种子、多帧 tuser/tlast 随机边界、
// valid/ready 随机背压、中途复位、数据极值；末段注入全 x 数据验证 x 传播与恢复。
// 覆盖要点：image_correction 先长行（越过 480 列边界）再短行（累积越过 1081 行边界）；
// image_cut 小尺寸实例走 h/v 计数器回绕；参数化模块各带 1~2 组非实际参数实例。
module tb_isp_front;
    `include "equiv_tb_common.vh"

    integer      seed = 32'h1357_9BDF;   // 固定种子：两侧激励必须逐拍一致

    reg clk = 1'b0;
    always #5 clk = ~clk;

    reg        rst_n    = 1'b0;          // 上电先复位 5 拍
    reg [15:0] cyc_stim = 16'd0;

    // ------------------------------------------------------------------
    // 公共 AXI4-Stream 激励（所有模块共用数据 / 控制，端口只读）
    // ------------------------------------------------------------------
    reg        vld    = 1'b0;
    reg        last   = 1'b0;
    reg        user   = 1'b0;
    reg        rdy    = 1'b0;
    reg [9:0]  dest10 = 10'd0;
    reg [39:0] d40    = 40'd0;
    reg [95:0] d96    = 96'd0;
    reg [127:0] d128  = 128'd0;

    // raw10_unpacket_4lane 独立激励（valid 包）
    reg        r10_v  = 1'b0;
    reg        r10_fs = 1'b0;
    reg        r10_fe = 1'b0;

    // image_correction 独立行/帧激励
    reg        cor_v  = 1'b0;
    reg        cor_fs = 1'b0;
    reg        cor_fe = 1'b0;

    // ------------------------------------------------------------------
    // 输出监视向量（每个实例的全部输出，位序固定）
    // ------------------------------------------------------------------
    wire        r10_o_fs, r10_o_fe, r10_o_v;
    wire [39:0] r10_o_d;
    wire [42:0] mon_raw = {r10_o_fs, r10_o_fe, r10_o_v, r10_o_d};

    wire        blc0_o_last, blc0_o_user, blc0_o_v, blc0_o_rdy;
    wire [9:0]  blc0_o_dest;
    wire [39:0] blc0_o_d;
    wire [53:0] mon_blc0 = {blc0_o_last, blc0_o_user, blc0_o_v, blc0_o_dest, blc0_o_d, blc0_o_rdy};

    wire        blc1_o_last, blc1_o_user, blc1_o_v, blc1_o_rdy;
    wire [9:0]  blc1_o_dest;
    wire [39:0] blc1_o_d;
    wire [53:0] mon_blc1 = {blc1_o_last, blc1_o_user, blc1_o_v, blc1_o_dest, blc1_o_d, blc1_o_rdy};

    wire        cor_o_last, cor_o_v, cor_o_user, cor_o_rdy;
    wire [9:0]  cor_o_dest;
    wire [39:0] cor_o_d;
    wire [53:0] mon_cor = {cor_o_last, cor_o_v, cor_o_user, cor_o_dest, cor_o_d, cor_o_rdy};

    wire        cor2_o_last, cor2_o_v, cor2_o_user, cor2_o_rdy;
    wire [7:0]  cor2_o_dest;
    wire [31:0] cor2_o_d;
    wire [43:0] mon_cor2 = {cor2_o_last, cor2_o_v, cor2_o_user, cor2_o_dest, cor2_o_d, cor2_o_rdy};

    wire        cut0_o_last, cut0_o_user, cut0_o_v, cut0_o_rdy;
    wire [95:0] cut0_o_d;
    wire [99:0] mon_cut0 = {cut0_o_last, cut0_o_user, cut0_o_v, cut0_o_d, cut0_o_rdy};

    wire        cut1_o_last, cut1_o_user, cut1_o_v, cut1_o_rdy;
    wire [95:0] cut1_o_d;
    wire [99:0] mon_cut1 = {cut1_o_last, cut1_o_user, cut1_o_v, cut1_o_d, cut1_o_rdy};

    wire        cut2_o_last, cut2_o_user, cut2_o_v, cut2_o_rdy;
    wire [39:0] cut2_o_d;
    wire [43:0] mon_cut2 = {cut2_o_last, cut2_o_user, cut2_o_v, cut2_o_d, cut2_o_rdy};

    wire        brt0_o_last, brt0_o_user, brt0_o_v, brt0_o_rdy;
    wire [95:0] brt0_o_d;
    wire [99:0] mon_brt0 = {brt0_o_last, brt0_o_user, brt0_o_v, brt0_o_d, brt0_o_rdy};

    wire        brt1_o_last, brt1_o_user, brt1_o_v, brt1_o_rdy;
    wire [95:0] brt1_o_d;
    wire [99:0] mon_brt1 = {brt1_o_last, brt1_o_user, brt1_o_v, brt1_o_d, brt1_o_rdy};

    wire        brt2_o_last, brt2_o_user, brt2_o_v, brt2_o_rdy;
    wire [95:0] brt2_o_d;
    wire [99:0] mon_brt2 = {brt2_o_last, brt2_o_user, brt2_o_v, brt2_o_d, brt2_o_rdy};

    wire        con0_o_last, con0_o_user, con0_o_v, con0_o_rdy;
    wire [95:0] con0_o_d;
    wire [99:0] mon_con0 = {con0_o_last, con0_o_user, con0_o_v, con0_o_d, con0_o_rdy};

    wire        con1_o_last, con1_o_user, con1_o_v, con1_o_rdy;
    wire [95:0] con1_o_d;
    wire [99:0] mon_con1 = {con1_o_last, con1_o_user, con1_o_v, con1_o_d, con1_o_rdy};

    wire        con2_o_last, con2_o_user, con2_o_v, con2_o_rdy;
    wire [95:0] con2_o_d;
    wire [99:0] mon_con2 = {con2_o_last, con2_o_user, con2_o_v, con2_o_d, con2_o_rdy};

    wire        sat0_o_last, sat0_o_user, sat0_o_v, sat0_o_rdy;
    wire [95:0] sat0_o_d;
    wire [99:0] mon_sat0 = {sat0_o_last, sat0_o_user, sat0_o_v, sat0_o_d, sat0_o_rdy};

    wire        sat1_o_last, sat1_o_user, sat1_o_v, sat1_o_rdy;
    wire [95:0] sat1_o_d;
    wire [99:0] mon_sat1 = {sat1_o_last, sat1_o_user, sat1_o_v, sat1_o_d, sat1_o_rdy};

    wire        sat2_o_last, sat2_o_user, sat2_o_v, sat2_o_rdy;
    wire [95:0] sat2_o_d;
    wire [99:0] mon_sat2 = {sat2_o_last, sat2_o_user, sat2_o_v, sat2_o_d, sat2_o_rdy};

    wire [95:0]  u128_96_o_d;
    wire [127:0] u96_128_o_d;

    // ------------------------------------------------------------------
    // DUT 实例：实际参数组 + 泛化参数组
    // ------------------------------------------------------------------
    raw10_unpacket_4lane u_raw10 (
        .I_clk(clk),
        .I_rst_n(rst_n),
        .I_csi_frame_start(r10_fs),
        .I_csi_frame_end(r10_fe),
        .I_csi_valid(r10_v),
        .I_csi_data(d40[31:0]),
        .O_raw10_frame_start(r10_o_fs),
        .O_raw10_frame_end(r10_o_fe),
        .O_raw10_valid(r10_o_v),
        .O_raw10_data(r10_o_d)
    );

    BLC #(.Black_level_offset_r0(50), .Black_level_offset_r1(50),
           .Black_level_offset_r2(50), .Black_level_offset_r3(50)) u_blc0 (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_tlast(last), .I_tuser(user), .I_tdata(d40), .I_tvalid(vld), .I_tdest(dest10),
        .I_tready(blc0_o_rdy),
        .O_tlast(blc0_o_last), .O_tuser(blc0_o_user), .O_tdata(blc0_o_d),
        .O_tvalid(blc0_o_v), .O_tdest(blc0_o_dest), .O_tready(rdy)
    );

    BLC #(.Black_level_offset_r0(0), .Black_level_offset_r1(1),
           .Black_level_offset_r2(511), .Black_level_offset_r3(1023)) u_blc1 (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_tlast(last), .I_tuser(user), .I_tdata(d40), .I_tvalid(vld), .I_tdest(dest10),
        .I_tready(blc1_o_rdy),
        .O_tlast(blc1_o_last), .O_tuser(blc1_o_user), .O_tdata(blc1_o_d),
        .O_tvalid(blc1_o_v), .O_tdest(blc1_o_dest), .O_tready(rdy)
    );

    image_correction #(.DATA_WIDTH(40), .TDEST_WIDTH(10)) u_cor (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_raw_data(d40), .I_raw_valid(cor_v),
        .I_raw_frame_start(cor_fs), .I_raw_frame_end(cor_fe),
        .O_raw_tdata(cor_o_d), .O_raw_tlast(cor_o_last), .O_raw_tdest(cor_o_dest),
        .O_raw_tvalid(cor_o_v), .O_raw_tuser(cor_o_user), .O_raw_tready(rdy)
    );

    image_correction #(.DATA_WIDTH(32), .TDEST_WIDTH(8)) u_cor2 (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_raw_data(d40[31:0]), .I_raw_valid(cor_v),
        .I_raw_frame_start(cor_fs), .I_raw_frame_end(cor_fe),
        .O_raw_tdata(cor2_o_d), .O_raw_tlast(cor2_o_last), .O_raw_tdest(cor2_o_dest),
        .O_raw_tvalid(cor2_o_v), .O_raw_tuser(cor2_o_user), .O_raw_tready(rdy)
    );

    image_cut #(.IMG_WIDTH(1920), .IMG_HEIGHT(1080), .DATA_WIDTH(96),
                .SKIP_ROWS_top(0), .SKIP_ROWS_bottom(0),
                .SKIP_COLS_left(1), .SKIP_COLS_right(0)) u_cut0 (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_tlast(last), .I_tuser(user), .I_tdata(d96), .I_tvalid(vld),
        .I_tready(cut0_o_rdy),
        .O_tlast(cut0_o_last), .O_tuser(cut0_o_user), .O_tdata(cut0_o_d),
        .O_tvalid(cut0_o_v), .O_tready(rdy)
    );

    image_cut #(.IMG_WIDTH(16), .IMG_HEIGHT(4), .DATA_WIDTH(96),
                .SKIP_ROWS_top(0), .SKIP_ROWS_bottom(2),
                .SKIP_COLS_left(1), .SKIP_COLS_right(0)) u_cut1 (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_tlast(last), .I_tuser(user), .I_tdata(d96), .I_tvalid(vld),
        .I_tready(cut1_o_rdy),
        .O_tlast(cut1_o_last), .O_tuser(cut1_o_user), .O_tdata(cut1_o_d),
        .O_tvalid(cut1_o_v), .O_tready(rdy)
    );

    image_cut #(.IMG_WIDTH(32), .IMG_HEIGHT(6), .DATA_WIDTH(40),
                .SKIP_ROWS_top(1), .SKIP_ROWS_bottom(1),
                .SKIP_COLS_left(2), .SKIP_COLS_right(1)) u_cut2 (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_tlast(last), .I_tuser(user), .I_tdata(d40), .I_tvalid(vld),
        .I_tready(cut2_o_rdy),
        .O_tlast(cut2_o_last), .O_tuser(cut2_o_user), .O_tdata(cut2_o_d),
        .O_tvalid(cut2_o_v), .O_tready(rdy)
    );

    Brightness_adjustment #(.BRIGHTNESS_ADD(5), .BRIGHTNESS_MINUS(0)) u_brt0 (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_tlast(last), .I_tuser(user), .I_tdata(d96), .I_tvalid(vld), .I_tready(brt0_o_rdy),
        .O_tlast(brt0_o_last), .O_tuser(brt0_o_user), .O_tdata(brt0_o_d),
        .O_tvalid(brt0_o_v), .O_tready(rdy)
    );

    Brightness_adjustment #(.BRIGHTNESS_ADD(1000), .BRIGHTNESS_MINUS(0)) u_brt1 (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_tlast(last), .I_tuser(user), .I_tdata(d96), .I_tvalid(vld), .I_tready(brt1_o_rdy),
        .O_tlast(brt1_o_last), .O_tuser(brt1_o_user), .O_tdata(brt1_o_d),
        .O_tvalid(brt1_o_v), .O_tready(rdy)
    );

    Brightness_adjustment #(.BRIGHTNESS_ADD(300), .BRIGHTNESS_MINUS(400)) u_brt2 (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_tlast(last), .I_tuser(user), .I_tdata(d96), .I_tvalid(vld), .I_tready(brt2_o_rdy),
        .O_tlast(brt2_o_last), .O_tuser(brt2_o_user), .O_tdata(brt2_o_d),
        .O_tvalid(brt2_o_v), .O_tready(rdy)
    );

    contrast_adj #(.contrast_level(140)) u_con0 (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_tlast(last), .I_tuser(user), .I_tdata(d96), .I_tvalid(vld), .I_tready(con0_o_rdy),
        .O_tlast(con0_o_last), .O_tuser(con0_o_user), .O_tdata(con0_o_d),
        .O_tvalid(con0_o_v), .O_tready(rdy)
    );

    contrast_adj #(.contrast_level(-100)) u_con1 (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_tlast(last), .I_tuser(user), .I_tdata(d96), .I_tvalid(vld), .I_tready(con1_o_rdy),
        .O_tlast(con1_o_last), .O_tuser(con1_o_user), .O_tdata(con1_o_d),
        .O_tvalid(con1_o_v), .O_tready(rdy)
    );

    contrast_adj #(.contrast_level(300)) u_con2 (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_tlast(last), .I_tuser(user), .I_tdata(d96), .I_tvalid(vld), .I_tready(con2_o_rdy),
        .O_tlast(con2_o_last), .O_tuser(con2_o_user), .O_tdata(con2_o_d),
        .O_tvalid(con2_o_v), .O_tready(rdy)
    );

    Saturation_adj #(.ADJUST_VAL(130)) u_sat0 (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_tlast(last), .I_tuser(user), .I_tdata(d96), .I_tvalid(vld), .I_tready(sat0_o_rdy),
        .O_tlast(sat0_o_last), .O_tuser(sat0_o_user), .O_tdata(sat0_o_d),
        .O_tvalid(sat0_o_v), .O_tready(rdy)
    );

    Saturation_adj #(.ADJUST_VAL(0)) u_sat1 (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_tlast(last), .I_tuser(user), .I_tdata(d96), .I_tvalid(vld), .I_tready(sat1_o_rdy),
        .O_tlast(sat1_o_last), .O_tuser(sat1_o_user), .O_tdata(sat1_o_d),
        .O_tvalid(sat1_o_v), .O_tready(rdy)
    );

    Saturation_adj #(.ADJUST_VAL(384)) u_sat2 (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_tlast(last), .I_tuser(user), .I_tdata(d96), .I_tvalid(vld), .I_tready(sat2_o_rdy),
        .O_tlast(sat2_o_last), .O_tuser(sat2_o_user), .O_tdata(sat2_o_d),
        .O_tvalid(sat2_o_v), .O_tready(rdy)
    );

    data128_96 u_128_96 (
        .I_tdata(d128), .O_tdata(u128_96_o_d)
    );

    data96_128 u_96_128 (
        .I_tdata(d96), .O_tdata(u96_128_o_d)
    );

    // ------------------------------------------------------------------
    // 激励生成（全部非阻塞赋值；DUT 在时钟沿采样上一拍的寄存器值）
    // ------------------------------------------------------------------
    always @(posedge clk) begin
        cyc_stim <= cyc_stim + 1'b1;

        // 复位序列：上电 5 拍 + 第 1005 拍 5 拍 + 第 23505 拍 2 拍
        if (cyc_stim == 16'd5)          rst_n <= 1'b1;
        else if (cyc_stim == 16'd1005)  rst_n <= 1'b0;
        else if (cyc_stim == 16'd1010)  rst_n <= 1'b1;
        else if (cyc_stim == 16'd23505) rst_n <= 1'b0;
        else if (cyc_stim == 16'd23507) rst_n <= 1'b1;

        // 通用控制 / 数据
        if (cyc_stim < 16'd20) begin
            vld <= 1'b0; last <= 1'b0; user <= 1'b0; rdy <= 1'b0;
            dest10 <= 10'd0; d40 <= 40'd0; d96 <= 96'd0; d128 <= 128'd0;
        end
        else if (cyc_stim < 16'd24000) begin
            vld    <= (($random(seed) & 32'h7) != 0);      // ~87.5% 有效
            last   <= (($random(seed) & 32'h3FF) == 0);    // ~0.1% 行尾
            user   <= (($random(seed) & 32'h7FF) == 0);    // ~0.05% 帧头
            rdy    <= (($random(seed) & 32'h1) != 0);      // 随机背压
            dest10 <= $random(seed);
            case (cyc_stim[2:0])
                3'd0: begin d40 <= 40'd0;      d96 <= 96'd0;                  d128 <= 128'd0; end
                3'd1: begin d40 <= {40{1'b1}}; d96 <= {96{1'b1}};             d128 <= {128{1'b1}}; end
                3'd2: begin d40 <= {5{8'h55}}; d96 <= {12{8'h55}};            d128 <= {16{8'h55}}; end
                3'd3: begin d40 <= {5{8'hAA}}; d96 <= {4{8'hFF,8'h00,8'hFF}}; d128 <= {8{8'hFF,8'h00,8'hFF,8'hFF}}; end
                3'd4: begin d40 <= {5{8'h01}}; d96 <= {4{8'h01,8'hFE,8'h80}}; d128 <= {8{8'h01,8'hFE,8'h80,8'h7F}}; end
                default: begin
                    d40  <= {$random(seed), $random(seed)};
                    d96  <= {$random(seed), $random(seed), $random(seed)};
                    d128 <= {$random(seed), $random(seed), $random(seed), $random(seed)};
                end
            endcase
        end
        else if (cyc_stim < 16'd24006) begin
            // x 注入窗：控制有效、数据全 x
            vld <= 1'b1; last <= 1'b0; user <= 1'b0; rdy <= 1'b1;
            dest10 <= 10'h3FF;
            d40 <= {40{1'bx}}; d96 <= {96{1'bx}}; d128 <= {128{1'bx}};
        end
        else if (cyc_stim < 16'd24080) begin
            // 恢复窗：连续有效 + 极值数据，冲掉流水线中的 x
            vld  <= 1'b1;
            last <= (cyc_stim == 16'd24020);
            user <= (cyc_stim == 16'd24020);
            rdy  <= 1'b1;
            dest10 <= 10'h2AA;
            d40 <= {40{1'b1}}; d96 <= {96{1'b1}}; d128 <= {128{1'b1}};
        end
        else begin
            vld    <= (($random(seed) & 32'h7) != 0);
            last   <= (($random(seed) & 32'h3FF) == 0);
            user   <= (($random(seed) & 32'h7FF) == 0);
            rdy    <= (($random(seed) & 32'h1) != 0);
            dest10 <= $random(seed);
            d40  <= {$random(seed), $random(seed)};
            d96  <= {$random(seed), $random(seed), $random(seed)};
            d128 <= {$random(seed), $random(seed), $random(seed), $random(seed)};
        end

        // raw10_unpacket_4lane：周期长 burst（>=5 拍）与随机混合，覆盖 5 态解包计数
        if (cyc_stim < 16'd20)
            r10_v <= 1'b0;
        else if (cyc_stim < 16'd24000)
            r10_v <= (((cyc_stim % 16'd97) < 16'd21) || (($random(seed) & 32'h1) != 0));
        else
            r10_v <= 1'b1;                                  // x 窗 / 恢复窗保持有效
        r10_fs <= (cyc_stim % 16'd4099) == 16'd0;
        r10_fe <= (cyc_stim % 16'd3079) == 16'd0;

        // image_correction：长行（越过 480 列边界）→ 短行（累积越过 1081 行边界）
        if (cyc_stim < 16'd20) begin
            cor_v  <= 1'b0;
            cor_fs <= 1'b0;
        end
        else if (cyc_stim < 16'd4400) begin
            cor_v  <= (((cyc_stim - 16'd20) % 16'd532) < 16'd520);
            cor_fs <= (cyc_stim == 16'd20) || (cyc_stim == 16'd600) || (cyc_stim == 16'd3000);
        end
        else if (cyc_stim < 16'd24000) begin
            cor_v  <= (((cyc_stim - 16'd4400) % 16'd14) < 16'd3);
            cor_fs <= (cyc_stim == 16'd22000);  // 越过 1081 行后打帧头，验证 v_cnt 复位恢复
        end
        else begin
            cor_v  <= 1'b1;                                 // x 窗 / 恢复窗保持有效
            cor_fs <= (cyc_stim == 16'd24020);
        end
        cor_fe <= (($random(seed) & 32'h1FF) == 0);         // 模块未使用，仍给随机
    end

    // ------------------------------------------------------------------
    // trace：每拍一行，自增 cycle + 全部实例全部输出（x/z 原样）
    // ------------------------------------------------------------------
    initial equiv_trace_open;

    always @(posedge clk) begin
        cyc <= cyc + 1;
        $fdisplay(fd, "%0d %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b",
                  cyc, mon_raw, mon_blc0, mon_blc1, mon_cor, mon_cor2,
                  mon_cut0, mon_cut1, mon_cut2,
                  mon_brt0, mon_brt1, mon_brt2, mon_con0, mon_con1, mon_con2,
                  mon_sat0, mon_sat1, mon_sat2, u128_96_o_d, u96_128_o_d);
    end

    initial begin
        repeat (24300) @(posedge clk);
        #1;
        equiv_finish(cyc);
    end

endmodule
