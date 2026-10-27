// 出处：Milianke uisrc ISP demo；本文件只做 11 级 ISP 链的例化与连线（BLC → demosaic → data128_96
// → awb → image_cut → gamma → Saturation → contrast → Brightness → data96_128），端口与原版一致。
module isp_top (
    input  wire         axi4s_video_aclk,
    input  wire         I_rst_n,
    input  wire         I_tlast,
    input  wire         I_tuser,
    input  wire [39:0]  I_tdata,
    input  wire         I_tvalid,
    input  wire [9:0]   I_tdest,
    input  wire         O_tready,
    output wire [127:0] O_tdata,
    output wire         O_tlast,
    output wire         O_tuser,
    output wire         O_tvalid,
    output wire         I_tready
);

  wire         BLC_O_tlast, BLC_O_tuser, BLC_O_tvalid, BLC_O_tready;  //synthesis keep
  wire [ 39:0] BLC_O_tdata;  //synthesis keep
  wire [  9:0] BLC_O_tdest;  //synthesis keep
  wire         m_aixs_tvalid, m_aixs_tuser, m_aixs_tlast, m_aixs_tready;  //synthesis keep
  wire [127:0] m_aixs_tdata;  //synthesis keep
  wire [ 95:0] m_aixs_tdata_96;
  wire         awb_O_tlast, awb_O_tuser, awb_O_tvalid, awb_O_tready;  //synthesis keep
  wire [ 95:0] awb_O_tdata;  //synthesis keep
  wire         gamma_O_tlast, gamma_O_tuser, gamma_O_tvalid, gamma_O_tready;  //synthesis keep
  wire [ 95:0] gamma_O_tdata;  //synthesis keep
  wire         cut_O_tlast, cut_O_tuser, cut_O_tvalid, cut_O_tready;
  wire [ 95:0] cut_O_tdata;
  wire         contrast_O_tlast, contrast_O_tuser, contrast_O_tvalid, contrast_O_tready;
  wire [ 95:0] contrast_O_tdata;
  wire         Saturation_O_tlast, Saturation_O_tuser, Saturation_O_tvalid, Saturation_O_tready;
  wire [ 95:0] Saturation_O_tdata;
  wire         Brightness_O_tlast, Brightness_O_tuser, Brightness_O_tvalid, Brightness_O_tready;
  wire [ 95:0] Brightness_O_tdata;
  wire [127:0] Brightness_O_tdata_128;  //synthesis keep

  assign Brightness_O_tready = O_tready;
  assign O_tdata  = Brightness_O_tdata_128;
  assign O_tlast  = Brightness_O_tlast;
  // 安路行间距问题导致后续 tuser 不再同步，下游把它近似为帧同步 vs，直接用最初始的 I_tuser
  assign O_tuser  = I_tuser;
  assign O_tvalid = Brightness_O_tvalid;

  BLC #(
      .Black_level_offset_r0(50), .Black_level_offset_r1(50),
      .Black_level_offset_r2(50), .Black_level_offset_r3(50)
  ) u_BLC (
      .I_clk(axi4s_video_aclk), .I_rst_n(I_rst_n),
      .I_tlast(I_tlast), .I_tuser(I_tuser), .I_tdata(I_tdata),
      .I_tvalid(I_tvalid), .I_tdest(I_tdest), .I_tready(),
      .O_tlast(BLC_O_tlast), .O_tuser(BLC_O_tuser), .O_tdata(BLC_O_tdata),
      .O_tvalid(BLC_O_tvalid), .O_tdest(BLC_O_tdest), .O_tready(BLC_O_tready)
  );

  demosaic #(
      .IMG_HEIGHT(1080), .IMG_WIDTH(1920),    // 图像高度 / 宽度
      .data_complete_delay(50), .BAYER_MODE("RGGB")
  ) u_demosaic (
      .I_clk(axi4s_video_aclk), .I_rst_n(I_rst_n),
      .axi4s_video_tdata(BLC_O_tdata), .axi4s_video_tdest(BLC_O_tdest),
      .axi4s_video_tlast(BLC_O_tlast), .axi4s_video_tvalid(BLC_O_tvalid),
      .axi4s_video_tuser(BLC_O_tuser), .axi4s_video_tready(BLC_O_tready),
      .O_tlast(m_aixs_tlast), .O_tuser(m_aixs_tuser), .O_tdata(m_aixs_tdata),
      .O_tvalid(m_aixs_tvalid), .O_tready(m_aixs_tready)
  );

  data128_96 u_data128_96 (.I_tdata(m_aixs_tdata), .O_tdata(m_aixs_tdata_96));

  awb #(.IMG_HEIGHT(1080), .IMG_WIDTH(1920)) u_awb (
      .I_clk(axi4s_video_aclk), .I_rst_n(I_rst_n),
      .I_tlast(m_aixs_tlast), .I_tuser(m_aixs_tuser), .I_tdata(m_aixs_tdata_96),
      .I_tvalid(m_aixs_tvalid), .I_tready(m_aixs_tready),
      .O_tlast(awb_O_tlast), .O_tuser(awb_O_tuser), .O_tdata(awb_O_tdata),
      .O_tvalid(awb_O_tvalid), .O_tready(awb_O_tready)
  );

  image_cut #(
      .IMG_WIDTH(1920), .IMG_HEIGHT(1080), .DATA_WIDTH(96),
      .SKIP_ROWS_top(0), .SKIP_ROWS_bottom(0), .SKIP_COLS_left(1), .SKIP_COLS_right(0)
  ) u_image_cut (
      .I_clk(axi4s_video_aclk), .I_rst_n(I_rst_n),
      .I_tlast(awb_O_tlast), .I_tuser(awb_O_tuser), .I_tdata(awb_O_tdata),
      .I_tvalid(awb_O_tvalid), .I_tready(awb_O_tready),
      .O_tlast(cut_O_tlast), .O_tuser(cut_O_tuser), .O_tdata(cut_O_tdata),
      .O_tvalid(cut_O_tvalid), .O_tready(cut_O_tready)
  );

  gamma u_gamma (
      .I_clk(axi4s_video_aclk), .I_rst_n(I_rst_n),
      .I_tlast(cut_O_tlast), .I_tuser(cut_O_tuser), .I_tdata(cut_O_tdata),
      .I_tvalid(cut_O_tvalid), .I_tready(cut_O_tready),
      .O_tlast(gamma_O_tlast), .O_tuser(gamma_O_tuser), .O_tdata(gamma_O_tdata),
      .O_tvalid(gamma_O_tvalid), .O_tready(gamma_O_tready)
  );

  Saturation_adj #(.ADJUST_VAL(130)) u_Saturation_adj (
      .I_clk(axi4s_video_aclk), .I_rst_n(I_rst_n),
      .I_tlast(gamma_O_tlast), .I_tuser(gamma_O_tuser), .I_tdata(gamma_O_tdata),
      .I_tvalid(gamma_O_tvalid), .I_tready(gamma_O_tready),
      .O_tlast(Saturation_O_tlast), .O_tuser(Saturation_O_tuser), .O_tdata(Saturation_O_tdata),
      .O_tvalid(Saturation_O_tvalid), .O_tready(Saturation_O_tready)
  );

  contrast_adj #(
      .contrast_level(140)  //对比度调节因子，定点数表示，128对应1.0
  ) u_contrast_adj (
      .I_clk(axi4s_video_aclk), .I_rst_n(I_rst_n),
      .I_tlast(Saturation_O_tlast), .I_tuser(Saturation_O_tuser), .I_tdata(Saturation_O_tdata),
      .I_tvalid(Saturation_O_tvalid), .I_tready(Saturation_O_tready),
      .O_tlast(contrast_O_tlast), .O_tuser(contrast_O_tuser), .O_tdata(contrast_O_tdata),
      .O_tvalid(contrast_O_tvalid), .O_tready(contrast_O_tready)
  );

  Brightness_adjustment #(.BRIGHTNESS_ADD(5), .BRIGHTNESS_MINUS(0)) u_Brightness_adjustment (
      .I_clk(axi4s_video_aclk), .I_rst_n(I_rst_n),
      .I_tlast(contrast_O_tlast), .I_tuser(contrast_O_tuser), .I_tdata(contrast_O_tdata),
      .I_tvalid(contrast_O_tvalid), .I_tready(contrast_O_tready),
      .O_tlast(Brightness_O_tlast), .O_tuser(Brightness_O_tuser), .O_tdata(Brightness_O_tdata),
      .O_tvalid(Brightness_O_tvalid), .O_tready(Brightness_O_tready)
  );

  data96_128 u_data96_128 (.I_tdata(Brightness_O_tdata), .O_tdata(Brightness_O_tdata_128));

endmodule
