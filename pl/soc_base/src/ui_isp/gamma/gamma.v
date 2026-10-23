/*****************************************************************
 出处：南京维酷机器人科技（VLKUS）ui_isp/gamma/gamma.v（GB18030 原版 639 行）。
 2026-10 厂家 RTL 重构（用户批准的单配置特化）：本工程只用 GAMMA_10x=22（2.2 曲线，
 isp_top 写死），故删除 0.8/1.2/1.4/1.6/1.8/2.0/2.4/2.6 八套 LUT 与 10 选 1 多路选择，
 只保留 4 像素 × RGB = 12 个 lut_2_2 实例。端口行为与原版逐拍一致。
*****************************************************************/
module gamma (
    input  wire        I_clk,
    input  wire        I_rst_n,

    input  wire        I_tlast,
    input  wire        I_tuser,
    input  wire [95:0] I_tdata,
    input  wire        I_tvalid,
    output wire        I_tready,

    output wire        O_tlast,
    output wire        O_tuser,
    output wire [95:0] O_tdata,
    output wire        O_tvalid,
    input  wire        O_tready
);

  /*****************************************************************
    24bit × 4 像素 → RGB888（每像素 24bit = {R[7:0], G[7:0], B[7:0]}）
  *****************************************************************/
  wire [7:0] rgb_r [3:0];
  wire [7:0] rgb_g [3:0];
  wire [7:0] rgb_b [3:0];

  wire [11:0] lut_r [3:0];
  wire [11:0] lut_g [3:0];
  wire [11:0] lut_b [3:0];

  reg  [11:0] acc_r [3:0];
  reg  [11:0] acc_g [3:0];
  reg  [11:0] acc_b [3:0];

  reg  [20:0] x0_r [3:0];
  reg  [20:0] x0_g [3:0];
  reg  [20:0] x0_b [3:0];

  reg  [20:0] x1_r [3:0];
  reg  [20:0] x1_g [3:0];
  reg  [20:0] x1_b [3:0];

  reg  [7:0]  x2_r [3:0];
  reg  [7:0]  x2_g [3:0];
  reg  [7:0]  x2_b [3:0];

  reg  [3:0]  I_tlast_r;
  reg  [3:0]  I_tuser_r;
  reg  [3:0]  I_tvalid_r;

  genvar i;
  generate
    for (i = 0; i < 4; i = i + 1) begin : g_pixel
      assign rgb_r[i] = I_tdata[24*i + 16 +: 8];
      assign rgb_g[i] = I_tdata[24*i +  8 +: 8];
      assign rgb_b[i] = I_tdata[24*i      +: 8];

      lut_2_2 u_lut_r (
          /*input  [7:0] */ .I_LUT_2_2_data(rgb_r[i]),
          /*output [11:0]*/ .O_LUT_2_2_data(lut_r[i])
      );
      lut_2_2 u_lut_g (
          /*input  [7:0] */ .I_LUT_2_2_data(rgb_g[i]),
          /*output [11:0]*/ .O_LUT_2_2_data(lut_g[i])
      );
      lut_2_2 u_lut_b (
          /*input  [7:0] */ .I_LUT_2_2_data(rgb_b[i]),
          /*output [11:0]*/ .O_LUT_2_2_data(lut_b[i])
      );
    end
  endgenerate

  /*****************************************************************
    gamma 查表结果打一拍（原版 acc_rgb888_* 级）
  *****************************************************************/
  genvar p;
  generate
    for (p = 0; p < 4; p = p + 1) begin : g_pipe
      always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
          acc_r[p] <= 12'd0;
          acc_g[p] <= 12'd0;
          acc_b[p] <= 12'd0;
        end else begin
          acc_r[p] <= lut_r[p];
          acc_g[p] <= lut_g[p];
          acc_b[p] <= lut_b[p];
        end
      end

      // 反归一化 <<8，再减偏置 -2048；两级各打一拍（与原版 x0/x1 级一致）
      always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
          x0_r[p] <= 21'd0;
          x0_g[p] <= 21'd0;
          x0_b[p] <= 21'd0;
        end else if (I_tvalid_r[0]) begin
          x0_r[p] <= acc_r[p] << 8;
          x0_g[p] <= acc_g[p] << 8;
          x0_b[p] <= acc_b[p] << 8;
        end
      end

      always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
          x1_r[p] <= 21'd0;
          x1_g[p] <= 21'd0;
          x1_b[p] <= 21'd0;
        end else if (I_tvalid_r[1]) begin
          x1_r[p] <= x0_r[p] - 2048;
          x1_g[p] <= x0_g[p] - 2048;
          x1_b[p] <= x0_b[p] - 2048;
        end
      end

      // 饱和截断到 8bit（与原版 x2 级一致）
      always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
          x2_r[p] <= 8'd0;
          x2_g[p] <= 8'd0;
          x2_b[p] <= 8'd0;
        end else if (I_tvalid_r[2]) begin
          x2_r[p] <= (x1_r[p][20]) ? 8'd255 : x1_r[p][19:12];
          x2_g[p] <= (x1_g[p][20]) ? 8'd255 : x1_g[p][19:12];
          x2_b[p] <= (x1_b[p][20]) ? 8'd255 : x1_b[p][19:12];
        end
      end
    end
  endgenerate

  // tlast / tuser / tvalid 与数据同深度打 4 拍（复位值、移位顺序与原版一致）
  always @(posedge I_clk or negedge I_rst_n) begin
    if (!I_rst_n) {I_tlast_r, I_tuser_r, I_tvalid_r} <= 12'd0;
    else begin
      I_tlast_r  <= {I_tlast_r[2:0],  I_tlast};
      I_tuser_r  <= {I_tuser_r[2:0],  I_tuser};
      I_tvalid_r <= {I_tvalid_r[2:0], I_tvalid};
    end
  end

  assign O_tdata = {
      x2_r[3], x2_g[3], x2_b[3],
      x2_r[2], x2_g[2], x2_b[2],
      x2_r[1], x2_g[1], x2_b[1],
      x2_r[0], x2_g[0], x2_b[0]
  };
  assign O_tlast  = I_tlast_r[3];
  assign O_tuser  = I_tuser_r[3];
  assign O_tvalid = I_tvalid_r[3];
  assign I_tready = O_tready;            // 直通，无背压反压

endmodule
