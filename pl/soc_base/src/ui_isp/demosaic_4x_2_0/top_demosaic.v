// 出处：Milianke uisrc ISP demo（ui_isp/demosaic_4x_2_0/top_demosaic.v, XiaoQingquan 2025/01, v1.1）
// demosaic 顶层：3x3 行窗口缓存 + 双线性插值；本文件只做连线，无逻辑。
module demosaic #(
    parameter IMG_HEIGHT          = 1080,    // 图像高度
    parameter IMG_WIDTH           = 1920,    // 图像宽度
    parameter data_complete_delay = 50,      // 每行数据完成到读出的延迟（cycle）
    parameter BAYER_MODE          = "BGGR"   // Bayer 排列：RGGB/BGGR/GBRG/GRBG
) (
    input               I_clk   ,
    input               I_rst_n ,

    input  [39:0]       axi4s_video_tdata ,   // AXI4-Stream 视频数据
    input  [9:0]        axi4s_video_tdest ,
    input               axi4s_video_tlast ,
    input               axi4s_video_tvalid,
    input               axi4s_video_tuser ,
    output              axi4s_video_tready,

    output              O_tlast ,
    output              O_tuser ,
    output [127:0]      O_tdata ,
    output              O_tvalid,
    input               O_tready
);

    wire        d_tlast , d_tuser , d_tvalid , d_tready;
    wire        bayer_ypos;
    wire [95:0] matrix_last_line, matrix_cur_line, matrix_next_line;

    raw_matrix_3x3_buffer #(
        .IMG_HEIGHT         (IMG_HEIGHT         ),
        .IMG_WIDTH          (IMG_WIDTH          ),
        .data_complete_delay(data_complete_delay)
    ) raw_matrix_3x3_buffer_d (
        .I_clk              (I_clk              ),
        .I_rst_n            (I_rst_n            ),
        .axi4s_video_tdata  (axi4s_video_tdata  ),
        .axi4s_video_tdest  (axi4s_video_tdest  ),
        .axi4s_video_tlast  (axi4s_video_tlast  ),
        .axi4s_video_tvalid (axi4s_video_tvalid ),
        .axi4s_video_tuser  (axi4s_video_tuser  ),
        .axi4s_video_tready (axi4s_video_tready ),
        .O_tlast            (d_tlast            ),
        .O_tuser            (d_tuser            ),
        .O_tvalid           (d_tvalid           ),
        .O_tready           (d_tready           ),
        .bayer_ypos         (bayer_ypos         ),
        .matrix_last_line   (matrix_last_line   ),
        .matrix_cur_line    (matrix_cur_line    ),
        .matrix_next_line   (matrix_next_line   )
    );

    bilinear_interpolation #(
        .BAYER_MODE(BAYER_MODE)
    ) bilinear_interpolation_d (
        .I_clk            (I_clk            ),
        .I_rst_n          (I_rst_n          ),
        .I_tlast          (d_tlast          ),
        .I_tuser          (d_tuser          ),
        .I_tvalid         (d_tvalid         ),
        .I_tready         (d_tready         ),
        .bayer_ypos       (bayer_ypos       ),
        .matrix_last_line (matrix_last_line ),
        .matrix_cur_line  (matrix_cur_line  ),
        .matrix_next_line (matrix_next_line ),
        .O_tlast          (O_tlast          ),
        .O_tuser          (O_tuser          ),
        .O_tdata          (O_tdata          ),
        .O_tvalid         (O_tvalid         ),
        .O_tready         (O_tready         )
    );

endmodule
