// 出处：Milianke uisrc ISP demo（ui_isp/demosaic_4x_2_0/raw_matrix_3x3_buffer.v, XiaoQingquan 2025/01, v1.1）
// RAW10 转 32bit 像素组 -> 行整合（zhenghe）-> 两行缓冲 -> 3x3 窗口 + 图像边界像素复制。
module raw_matrix_3x3_buffer #(
    parameter IMG_HEIGHT          = 1080,   // 图像高度
    parameter IMG_WIDTH           = 1920,   // 图像宽度
    parameter data_complete_delay = 50
) (
    input  wire        I_clk, I_rst_n,
    input  wire [39:0] axi4s_video_tdata,
    input  wire [ 9:0] axi4s_video_tdest,
    input  wire        axi4s_video_tlast, axi4s_video_tvalid, axi4s_video_tuser,
    output wire        axi4s_video_tready,
    output wire        O_tlast, O_tuser, O_tvalid,
    input  wire        O_tready,
    output wire        bayer_ypos,           // Bayer 行位置（窗口中心行的行奇偶）
    output wire [95:0] matrix_last_line,     // 上一行 3x3 窗口
    output wire [95:0] matrix_cur_line,      // 当前行 3x3 窗口
    output wire [95:0] matrix_next_line      // 下一行 3x3 窗口
);

    localparam N            = 3;                   // 3x3 窗口
    localparam DATA_WIDTH   = 32;                  // 每拍 4 个 RAW10 像素
    localparam IMG_WIDTH_4x = (IMG_WIDTH >> 2);    // 每行的 4 像素组数

    wire        zhenghe_tlast, zhenghe_tuser, zhenghe_tvalid;
    wire [31:0] zhenghe_tdata;
    wire        raw_tlast, raw_tuser, raw_tvalid;
    wire [31:0] raw_data;

    // 3 行数据（每行 32bit 数据 + tuser 位）
    wire                  valid [0:N-1];
    wire [DATA_WIDTH:0]   data  [0:N-1];
    wire [32:0]           data_d2 = data[2];
    reg  [32:0]           data_d0_d, data_d0, data_d1;
    reg                   valid_d0, valid_d1;

    reg  [95:0] matrix [0:N-1];          // 0=上一行, 1=当前行, 2=下一行
    reg  [13:0] h_cnt;                   // 行内像素组计数
    reg  [13:0] v_cnt, v_cnt_d;          // 行计数（v_cnt_d 打一拍）
    reg         ch_valid;                // 窗口数据有效
    reg  [2:0]  tlast_pipe, tuser_pipe;  // tlast/tuser 三级对齐

    assign data[0]  = {zhenghe_tuser, zhenghe_tdata};
    assign valid[0] = zhenghe_tvalid;
    assign axi4s_video_tready = O_tready;
    assign matrix_last_line = matrix[0];
    assign matrix_cur_line  = matrix[1];
    assign matrix_next_line = matrix[2];
    assign bayer_ypos = !v_cnt_d[0] & ch_valid;

    mipi_to_raw_converter mipi_to_raw_converter_r0 (
        .I_clk(I_clk), .I_rst_n(I_rst_n),
        .I_raw_tdata(axi4s_video_tdata), .I_raw_tdest(axi4s_video_tdest),
        .I_raw_tlast(axi4s_video_tlast), .I_raw_tvalid(axi4s_video_tvalid),
        .I_raw_tuser(axi4s_video_tuser),
        .I_raw_tready(),                    // 输出恒可接收，不需反压
        .O_raw_tdata(raw_data), .O_raw_tlast(raw_tlast), .O_raw_tuser(raw_tuser),
        .O_raw_tvalid(raw_tvalid), .O_raw_tready(1'b1)
    );

    // 行整合：按行写 BRAM，延迟 data_complete_delay 后整行读出
    zhenghe #(
        .IMG_WIDTH(IMG_WIDTH), .IMG_HEIGHT(IMG_HEIGHT),
        .DATA_WIDTH(32), .data_complete_delay(data_complete_delay)
    ) zhenghe_d (
        .I_clk(I_clk), .I_rst_n(I_rst_n),
        .I_tlast(raw_tlast), .I_tuser(raw_tuser), .I_tdata(raw_data), .I_tvalid(raw_tvalid),
        .O_tlast(zhenghe_tlast), .O_tuser(zhenghe_tuser),
        .O_tdata(zhenghe_tdata), .O_tvalid(zhenghe_tvalid)
    );

    // 行缓冲：data[0] 延迟 2 拍、data[1] 延迟 1 拍后与 data[2] 对齐
    genvar i;
    generate
        for (i = 1; i < N; i = i + 1) begin : two_line_buffer
            line_buffer_demosaic #(
                .IMG_HEIGHT(IMG_HEIGHT), .IMG_WIDTH(IMG_WIDTH)
            ) line_buffer_demosaic_r1 (
                .I_clk(I_clk), .I_rst_n(I_rst_n),
                .I_tuser(data[i-1][32] & valid[i-1]), .I_valid(valid[i-1]), .I_data(data[i-1]),
                .O_valid(valid[i]), .O_data(data[i])
            );
        end
    endgenerate

    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            data_d0_d <= 33'd0; data_d0 <= 33'd0; data_d1 <= 33'd0;
            valid_d0  <= 1'b0;  valid_d1 <= 1'b0;
        end else begin
            valid_d0  <= valid[0];
            valid_d1  <= valid_d0;
            data_d0_d <= data[0];
            data_d0   <= data_d0_d;
            data_d1   <= data[1];
        end
    end

    // 3x3 窗口：每行 6 个像素（窗口列 -1..+4），行/列边界按复制处理。
    // 三行各自左移 96bit 窗口：h_cnt==0 复制中心像素；h_cnt==末列 复制末像素；否则左移进新像素。
    // 各行的源数据：下一行=当前拍 data_d0（v_cnt>=1 时），当前行=data_d1（v_cnt>=1），上一行=data_d2（v_cnt>=2）；
    // v_cnt==0/1 时按图像上边界复制（见 src 选择）。
    wire [32:0] src_last = (v_cnt == 0) ? data_d0 : (v_cnt == 1) ? data_d1 : data_d2;
    wire [32:0] src_cur  = (v_cnt == 0) ? data_d0 : data_d1;
    wire [32:0] src_next = data_d0;

    generate
        for (i = 0; i < N; i = i + 1) begin : g_matrix
            wire [32:0] src = (i == 0) ? src_last : (i == 1) ? src_cur : src_next;
            always @(posedge I_clk or negedge I_rst_n) begin
                if (!I_rst_n)              matrix[i] <= 96'd0;
                else if (valid_d1) begin
                    if (h_cnt == 0)        matrix[i] <= {24'd0, src[31:24], src[31:0], src[31:0]};
                    else if (h_cnt == IMG_WIDTH_4x - 1)
                                           matrix[i] <= {matrix[i][63:0], matrix[i][7:0], 24'd0};
                    else                   matrix[i] <= {matrix[i][63:0], src[31:0]};
                end
                else                       matrix[i] <= 96'd0;
            end
        end
    endgenerate

    // 行/列计数器（窗口数据有效时推进）与行号/窗口有效打拍（合并为同一 always，全非阻塞）
    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            h_cnt <= 14'd0; v_cnt <= 14'd0; ch_valid <= 1'b0; v_cnt_d <= 14'd0;
        end else begin
            if (zhenghe_tuser) begin
                h_cnt <= 14'd0;
                v_cnt <= 14'd0;
            end else begin
                h_cnt <= valid_d1 ? ((h_cnt == IMG_WIDTH_4x - 1) ? 14'd0 : h_cnt + 1'b1) : h_cnt;
                v_cnt <= (valid_d1 && (h_cnt == IMG_WIDTH_4x - 1))
                         ? ((v_cnt == IMG_HEIGHT - 1) ? 14'd0 : v_cnt + 1'b1) : v_cnt;
            end
            ch_valid <= valid_d1;
            v_cnt_d  <= v_cnt;
        end
    end

    // tlast/tuser 三级对齐后输出
    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            tlast_pipe <= 3'd0;
            tuser_pipe <= 3'd0;
        end else begin
            tlast_pipe <= {tlast_pipe[1:0], zhenghe_tlast};
            tuser_pipe <= {tuser_pipe[1:0], zhenghe_tuser};
        end
    end

    assign O_tuser  = tuser_pipe[2];
    assign O_tlast  = tlast_pipe[2];
    assign O_tvalid = ch_valid;

endmodule
