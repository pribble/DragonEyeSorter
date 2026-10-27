/*****************************************************************
 出处：南京维酷机器人科技（VLKUS）ui_isp/gamma/gamma.v（GB18030 原版 639 行）。
 2026-10 厂家 RTL 重构（用户批准的单配置特化）：本工程只用 GAMMA_10x=22（2.2 曲线，
 isp_top 写死），故删除其余 8 套曲线与其 10 选 1 多路选择，只保留 12 个 lut_2_2 实例。
 12 个通道（4 像素 × RGB）流水完全同构，本轮折成单一 generate 循环：查表打拍 →
 x0 = acc<<8 → x1 = x0-2048 → x2 饱和截 8bit。端口 / 逐拍行为 / 位宽与原版一致
 （lut_2_2 的 I_clk / I_rst_n 原版即不接，保持不变）。
*****************************************************************/
module gamma (
    input  wire        I_clk, I_rst_n,
    input  wire        I_tlast, I_tuser,
    input  wire [95:0] I_tdata,
    input  wire        I_tvalid,
    output wire        I_tready,
    output wire        O_tlast, O_tuser,
    output wire [95:0] O_tdata,
    output wire        O_tvalid,
    input  wire        O_tready
);

  // 通道 k = I_tdata / O_tdata 的第 k 个字节（低→高 = 像素0.B … 像素3.R），输入输出同序无需重排
  wire [7:0]  ch  [0:11];
  wire [11:0] lut [0:11];
  reg  [11:0] acc [0:11];
  reg  [20:0] x0  [0:11], x1 [0:11];
  reg  [7:0]  x2  [0:11];
  reg  [3:0]  I_tlast_r, I_tuser_r, I_tvalid_r;

  genvar i;
  generate for (i = 0; i < 12; i = i + 1) begin : g_ch
    assign ch[i] = I_tdata[8*i +: 8];
    lut_2_2 u_lut (.I_LUT_2_2_data(ch[i]), .O_LUT_2_2_data(lut[i]));

    // 4 级流水，各级门控 / 复位值 / 位宽同原版
    always @(posedge I_clk or negedge I_rst_n) begin
      if (!I_rst_n) begin
        acc[i] <= 0; x0[i] <= 0; x1[i] <= 0; x2[i] <= 0;
      end else begin
        acc[i] <= lut[i];
        if (I_tvalid_r[0]) x0[i] <= acc[i] << 8;
        if (I_tvalid_r[1]) x1[i] <= x0[i] - 2048;
        if (I_tvalid_r[2]) x2[i] <= x1[i][20] ? 8'd255 : x1[i][19:12];
      end
    end

    assign O_tdata[8*i +: 8] = x2[i];
  end endgenerate

  // tlast / tuser / tvalid 与数据同深度打 4 拍（复位值、移位顺序同原版）
  always @(posedge I_clk or negedge I_rst_n)
    if (!I_rst_n) {I_tlast_r, I_tuser_r, I_tvalid_r} <= 12'd0;
    else {I_tlast_r, I_tuser_r, I_tvalid_r} <= {{I_tlast_r[2:0],  I_tlast},
                                                {I_tuser_r[2:0],  I_tuser},
                                                {I_tvalid_r[2:0], I_tvalid}};

  assign O_tlast  = I_tlast_r[3];
  assign O_tuser  = I_tuser_r[3];
  assign O_tvalid = I_tvalid_r[3];
  assign I_tready = O_tready;            // 直通，无背压

endmodule
