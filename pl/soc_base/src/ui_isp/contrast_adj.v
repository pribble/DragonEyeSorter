// 出处：MiLianKe（米联客，www.uisrc.com）ISP 参考设计；重构版，接口与逐拍行为不变。
module contrast_adj #(
    parameter contrast_level = 160   //对比度调节因子，定点数表示，128对应1.0
) (
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

    // 12 个通道（4 像素 × RGB）流水完全同构，折成单一 generate 循环（位序：通道 k = 第 k 字节）
    reg [2:0]  I_tlast_r, I_tuser_r, I_tvalid_r;
    reg [15:0] sh [0:11];      // 第 1 级：中心化结果（16bit 回绕，位宽同原版）
    reg [15:0] tp [0:11];      // 第 2 级：乘对比度右移结果
    reg [7:0]  d  [0:11];      // 第 3 级：加回 128 后按原厂规则钳位的 8bit 结果（钳位前与原 10bit 截断逐位等价）

    genvar i;
    generate for (i = 0; i < 12; i = i + 1) begin : g_ch
        wire [7:0]  ch    = I_tdata[8*i +: 8];
        wire [15:0] d_sum = tp[i] + 16'd128;

        // 三级流水（中心化 → 乘系数右移 7 → 加回 128 → 钳位），各级门控与原版一致；
        // 原版第 3 级存 10bit 截断结果、输出口组合钳位，本版把钳位前移到寄存器入口（同拍输出，省 2bit/通道 FF）
        always @(posedge I_clk or negedge I_rst_n) begin
            if (!I_rst_n) begin
                sh[i] <= 0; tp[i] <= 0; d[i] <= 0;
            end else begin
                if (I_tvalid)      sh[i] <= {8'd0, ch} - 16'd128;
                if (I_tvalid_r[0]) tp[i] <= (sh[i] * contrast_level) >> 7;
                if (I_tvalid_r[1]) d[i]  <= d_sum[9] ? 8'd0 : d_sum[8] ? 8'd255 : d_sum[7:0];
            end
        end

        assign O_tdata[8*i +: 8] = d[i];
    end endgenerate

    // 控制信号同步 3 拍（数据路径 3 级）
    always @(posedge I_clk or negedge I_rst_n)
        if (!I_rst_n) {I_tlast_r, I_tuser_r, I_tvalid_r} <= 9'd0;
        else {I_tlast_r, I_tuser_r, I_tvalid_r} <= {{I_tlast_r[1:0],  I_tlast},
                                                    {I_tuser_r[1:0],  I_tuser},
                                                    {I_tvalid_r[1:0], I_tvalid}};

    assign O_tlast  = I_tlast_r[2];
    assign O_tuser  = I_tuser_r[2];
    assign O_tvalid = I_tvalid_r[2];
    assign I_tready = O_tready;            // 直通，无背压

endmodule
