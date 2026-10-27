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

    // 12 个通道（4 像素 × RGB）流水完全同构，折成单一 generate 循环（位序：通道 k = 第 k 字节）
    reg       I_tlast_r, I_tuser_r, I_tvalid_r;
    reg [9:0] v [0:11];        // 亮度偏移结果（10bit，位宽同原版）

    genvar i;
    generate for (i = 0; i < 12; i = i + 1) begin : g_ch
        wire [7:0] ch = I_tdata[8*i +: 8];

        // 亮度偏移按原版 32bit 表达式计算后截 10bit（参数可为负/超范围，语义不变）
        always @(posedge I_clk or negedge I_rst_n) begin
            if (!I_rst_n)      v[i] <= 0;
            else if (I_tvalid) v[i] <= ch + BRIGHTNESS_ADD - BRIGHTNESS_MINUS;
        end

        // 10bit 结果按原厂规则钳位：bit9 → 0，bit8 → 255，否则取低 8bit
        assign O_tdata[8*i +: 8] = v[i][9] ? 8'd0 : v[i][8] ? 8'd255 : v[i][7:0];
    end endgenerate

    // 控制信号同步 1 拍（原版 {I_tlast_r,I_tlast} 赋 1bit 只取 LSB，等价于直接打一拍）
    always @(posedge I_clk or negedge I_rst_n)
        if (!I_rst_n) {I_tlast_r, I_tuser_r, I_tvalid_r} <= 3'd0;
        else {I_tlast_r, I_tuser_r, I_tvalid_r} <= {I_tlast, I_tuser, I_tvalid};

    assign O_tlast  = I_tlast_r;
    assign O_tuser  = I_tuser_r;
    assign O_tvalid = I_tvalid_r;
    assign I_tready = O_tready;            // 直通，无背压

endmodule
