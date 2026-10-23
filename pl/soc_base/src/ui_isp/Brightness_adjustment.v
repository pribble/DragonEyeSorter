// 出处：MiLianKe（米联客，www.uisrc.com）ISP 参考设计；重构版，接口与逐拍行为不变。
module Brightness_adjustment #(
    parameter BRIGHTNESS_ADD   = 20,
    parameter BRIGHTNESS_MINUS = 0
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

    reg       I_tlast_r, I_tuser_r, I_tvalid_r;
    reg [9:0] R_d[3:0], G_d[3:0], B_d[3:0];
    wire [7:0] R[3:0], G[3:0], B[3:0];

    // 4 像素通道折叠：每像素 RGB888 四路同构，仅数据切片不同
    genvar i;
    generate for (i = 0; i < 4; i = i + 1) begin : g_pixel
        wire [7:0] r = I_tdata[24*i + 16 +: 8];
        wire [7:0] g = I_tdata[24*i +  8 +: 8];
        wire [7:0] b = I_tdata[24*i      +: 8];

        // 亮度偏移按原版 32bit 表达式计算后截 10bit（参数可为负/超范围，语义保持不变）
        always @(posedge I_clk or negedge I_rst_n) begin
            if (!I_rst_n) {R_d[i], G_d[i], B_d[i]} <= 0;
            else if (I_tvalid) begin
                R_d[i] <= r + BRIGHTNESS_ADD - BRIGHTNESS_MINUS;
                G_d[i] <= g + BRIGHTNESS_ADD - BRIGHTNESS_MINUS;
                B_d[i] <= b + BRIGHTNESS_ADD - BRIGHTNESS_MINUS;
            end
        end

        // 10bit 结果按原厂规则钳位：bit9 → 0，bit8 → 255，否则取低 8bit
        assign R[i] = (R_d[i][9]) ? 8'd0 : (R_d[i][8]) ? 8'd255 : R_d[i][7:0];
        assign G[i] = (G_d[i][9]) ? 8'd0 : (G_d[i][8]) ? 8'd255 : G_d[i][7:0];
        assign B[i] = (B_d[i][9]) ? 8'd0 : (B_d[i][8]) ? 8'd255 : B_d[i][7:0];
    end endgenerate

    // 控制信号同步 1 拍：原版 {I_tlast_r,I_tlast} 赋给 1bit 只取 LSB，等价于直接打一拍
    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) {I_tlast_r, I_tuser_r, I_tvalid_r} <= 3'd0;
        else begin
            I_tlast_r  <= I_tlast;
            I_tuser_r  <= I_tuser;
            I_tvalid_r <= I_tvalid;
        end
    end

    assign O_tlast  = I_tlast_r;
    assign O_tuser  = I_tuser_r;
    assign O_tvalid = I_tvalid_r;
    assign O_tdata  = {R[3], G[3], B[3], R[2], G[2], B[2],
                       R[1], G[1], B[1], R[0], G[0], B[0]};
    assign I_tready = O_tready;

endmodule
