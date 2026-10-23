/*****************************************************************
 出处：南京维酷机器人科技（VLKUS）ui_isp/gamma/gamma.v（GB18030 原版 639 行）。
 2026-10 厂家 RTL 重构（用户批准的单配置特化）：本工程只用 GAMMA_10x=22（2.2 曲线，
 isp_top 写死），故删除 0.8/1.2/1.4/1.6/1.8/2.0/2.4/2.6 八套 LUT 与 10 选 1 多路选择，
 只保留 4 像素 × RGB = 12 个 lut_2_2 实例。端口行为与原版逐拍一致。
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

  // 24bit × 4 像素 → RGB888（每像素 24bit = {R[7:0], G[7:0], B[7:0]}）
  wire [7:0] rgb_r [3:0];
  wire [7:0] rgb_g [3:0];
  wire [7:0] rgb_b [3:0];
  wire [11:0] lut_r [3:0];
  wire [11:0] lut_g [3:0];
  wire [11:0] lut_b [3:0];

  reg [3:0]  I_tlast_r, I_tuser_r, I_tvalid_r;
  reg [11:0] acc_r [3:0];
  reg [11:0] acc_g [3:0];
  reg [11:0] acc_b [3:0];
  reg [20:0] x0_r [3:0];
  reg [20:0] x0_g [3:0];
  reg [20:0] x0_b [3:0];
  reg [20:0] x1_r [3:0];
  reg [20:0] x1_g [3:0];
  reg [20:0] x1_b [3:0];
  reg [7:0]  x2_r [3:0];
  reg [7:0]  x2_g [3:0];
  reg [7:0]  x2_b [3:0];

  genvar i;
  generate
    for (i = 0; i < 4; i = i + 1) begin : g_pixel
      assign rgb_r[i] = I_tdata[24*i + 16 +: 8];
      assign rgb_g[i] = I_tdata[24*i +  8 +: 8];
      assign rgb_b[i] = I_tdata[24*i      +: 8];

      lut_2_2 u_lut_r (.I_LUT_2_2_data(rgb_r[i]), .O_LUT_2_2_data(lut_r[i]));
      lut_2_2 u_lut_g (.I_LUT_2_2_data(rgb_g[i]), .O_LUT_2_2_data(lut_g[i]));
      lut_2_2 u_lut_b (.I_LUT_2_2_data(rgb_b[i]), .O_LUT_2_2_data(lut_b[i]));
    end
  endgenerate

  // 4 像素流水（原 4 个独立 always，同复位/同时钟、全非阻塞，合并后逐拍等价）：
  //   acc 级查表打拍 → x0 反归一化 <<8 → x1 减偏置 -2048 → x2 饱和截断到 8bit
  generate
    for (i = 0; i < 4; i = i + 1) begin : g_pipe
      always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
          {acc_r[i], acc_g[i], acc_b[i]} <= 0;
          {x0_r[i], x0_g[i], x0_b[i]} <= 0;
          {x1_r[i], x1_g[i], x1_b[i]} <= 0;
          {x2_r[i], x2_g[i], x2_b[i]} <= 0;
        end else begin
          acc_r[i] <= lut_r[i];
          acc_g[i] <= lut_g[i];
          acc_b[i] <= lut_b[i];
          if (I_tvalid_r[0]) begin
            x0_r[i] <= acc_r[i] << 8;
            x0_g[i] <= acc_g[i] << 8;
            x0_b[i] <= acc_b[i] << 8;
          end
          if (I_tvalid_r[1]) begin
            x1_r[i] <= x0_r[i] - 2048;
            x1_g[i] <= x0_g[i] - 2048;
            x1_b[i] <= x0_b[i] - 2048;
          end
          if (I_tvalid_r[2]) begin
            x2_r[i] <= (x1_r[i][20]) ? 8'd255 : x1_r[i][19:12];
            x2_g[i] <= (x1_g[i][20]) ? 8'd255 : x1_g[i][19:12];
            x2_b[i] <= (x1_b[i][20]) ? 8'd255 : x1_b[i][19:12];
          end
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

  assign O_tdata = {x2_r[3], x2_g[3], x2_b[3], x2_r[2], x2_g[2], x2_b[2],
                    x2_r[1], x2_g[1], x2_b[1], x2_r[0], x2_g[0], x2_b[0]};
  assign O_tlast  = I_tlast_r[3];
  assign O_tuser  = I_tuser_r[3];
  assign O_tvalid = I_tvalid_r[3];
  assign I_tready = O_tready;            // 直通，无背压反压

endmodule
