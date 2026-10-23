// 出处：MiLianKe（米联客，www.uisrc.com）ISP 参考设计；重构版，接口与逐拍行为不变。
module contrast_adj #(
    parameter  contrast_level   = 160 //对比度调节因子，定点数表示，128对应1.0
)
(
    input                   I_clk  ,
    input                   I_rst_n,

    input                   I_tlast  ,
    input                   I_tuser  ,
    input  [95:0]           I_tdata  ,
    input                   I_tvalid ,
    output                  I_tready ,

    output                  O_tlast  ,
    output                  O_tuser  ,
    output [95:0]           O_tdata  ,
    output                  O_tvalid ,
    input                   O_tready
);

    reg [2:0]  I_tlast_r, I_tuser_r, I_tvalid_r;
    reg [9:0]  R_d[3:0], G_d[3:0], B_d[3:0];
    wire [7:0] R[3:0], G[3:0], B[3:0];

    // 4 像素通道折叠：三级流水（中心化 → 乘系数右移 7 → 加回 128）
    genvar i;
    generate for (i = 0; i < 4; i = i + 1) begin: g_pixel
        wire [7:0] r = I_tdata[24*i + 16 +: 8];
        wire [7:0] g = I_tdata[24*i +  8 +: 8];
        wire [7:0] b = I_tdata[24*i      +: 8];

        reg [15:0] sh_r, sh_g, sh_b;   // 中心化结果（16bit 回绕，位宽同原版）
        reg [15:0] tp_r, tp_g, tp_b;   // 乘对比度右移结果

        // 第 1 级：减 128 中心化
        always @(posedge I_clk or negedge I_rst_n) begin
            if (!I_rst_n) begin
                sh_r <= 16'd0;
                sh_g <= 16'd0;
                sh_b <= 16'd0;
            end else if (I_tvalid) begin
                sh_r <= {8'd0, r} - 16'd128;
                sh_g <= {8'd0, g} - 16'd128;
                sh_b <= {8'd0, b} - 16'd128;
            end
        end

        // 第 2 级：乘 Q0.7 对比度因子后右移 7（表达式宽度/符号与原版逐字一致）
        always @(posedge I_clk or negedge I_rst_n) begin
            if (!I_rst_n) begin
                tp_r <= 16'd0;
                tp_g <= 16'd0;
                tp_b <= 16'd0;
            end else if (I_tvalid_r[0]) begin
                tp_r <= (sh_r * contrast_level) >> 7;
                tp_g <= (sh_g * contrast_level) >> 7;
                tp_b <= (sh_b * contrast_level) >> 7;
            end
        end

        // 第 3 级：加回 128（16bit 求和后截 10bit）
        always @(posedge I_clk or negedge I_rst_n) begin
            if (!I_rst_n) begin
                R_d[i] <= 10'd0;
                G_d[i] <= 10'd0;
                B_d[i] <= 10'd0;
            end else if (I_tvalid_r[1]) begin
                R_d[i] <= tp_r + 16'd128;
                G_d[i] <= tp_g + 16'd128;
                B_d[i] <= tp_b + 16'd128;
            end
        end

        // 10bit 结果按原厂规则钳位：bit9 → 0，bit8 → 255，否则取低 8bit
        assign R[i] = (R_d[i][9]) ? 8'd0 : (R_d[i][8]) ? 8'd255 : R_d[i][7:0];
        assign G[i] = (G_d[i][9]) ? 8'd0 : (G_d[i][8]) ? 8'd255 : G_d[i][7:0];
        assign B[i] = (B_d[i][9]) ? 8'd0 : (B_d[i][8]) ? 8'd255 : B_d[i][7:0];
    end endgenerate

    // 控制信号同步 2 拍（数据路径 3 级）
    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) {I_tlast_r, I_tuser_r, I_tvalid_r} <= 9'd0;
        else begin
            I_tlast_r  <= {I_tlast_r[1:0],  I_tlast};
            I_tuser_r  <= {I_tuser_r[1:0],  I_tuser};
            I_tvalid_r <= {I_tvalid_r[1:0], I_tvalid};
        end
    end

    assign O_tlast  = I_tlast_r[2];
    assign O_tuser  = I_tuser_r[2];
    assign O_tvalid = I_tvalid_r[2];
    assign O_tdata  = {R[3], G[3], B[3],
                       R[2], G[2], B[2],
                       R[1], G[1], B[1],
                       R[0], G[0], B[0]};
    assign I_tready = O_tready;

endmodule
