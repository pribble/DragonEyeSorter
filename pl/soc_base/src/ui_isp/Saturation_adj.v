// 出处：MiLianKe（米联客，www.uisrc.com）ISP 参考设计；重构版，接口与逐拍行为不变。
// 饱和度：Y = 0.299R + 0.587G + 0.114B（10bit 定点），R' = -Y*v + R*(1+v)，v ∈ [-1,1]
`timescale 1ns / 1ps
module Saturation_adj #(
    parameter [8:0] ADJUST_VAL = 128
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

    // 原版固定系数（保持参数名/类型/默认值不变，可被 defparam 覆盖）
    parameter C0 = 9'd306;    // 0.299*1024
    parameter C1 = 10'd601;   // 0.587*1024
    parameter C2 = 7'd117;    // 0.114*1024

    // 增益常量：RGB_C = (1+v)*255 截 9bit；v 的符号与绝对值按原版规则从 ADJUST_VAL 取
    wire [8:0] RGB_C    = 255 + ADJUST_VAL;
    wire       Y_C_sign = ADJUST_VAL[8];
    wire [7:0] Y_C_abs  = Y_C_sign ? (~ADJUST_VAL[7:0] + 1) : ADJUST_VAL[7:0];

    reg [3:0] I_tlast_r, I_tuser_r, I_tvalid_r;

    // 10bit 结果按原厂规则钳位（保留三目写法以维持 x 语义）
    function [7:0] clip8(input [18:0] v);
        clip8 = v[18] ? 8'd0 : (v[17:16] > 0) ? 8'd255 : v[15:8];
    endfunction

    // 4 像素通道折叠；RGB 三通道位宽不同（17/18/15bit），保留原版位宽避免改变溢出与 x 传播
    genvar i;
    generate for (i = 0; i < 4; i = i + 1) begin : g_pixel
        wire [7:0] r = I_tdata[24*i + 16 +: 8];
        wire [7:0] g = I_tdata[24*i +  8 +: 8];
        wire [7:0] b = I_tdata[24*i      +: 8];

        reg [16:0] y_r, r2, g2, b2;              // 第1级 Y 贡献 / 第3级 像素*(255+v)
        reg [17:0] y_g;
        reg [14:0] y_b;
        reg [15:0] Y_m;                          // Y*|v|：Y<=255、|v|<=255 → 最大 65025（16bit）
        reg [7:0]  r0, g0, b0, Y, r1, g1, b1, Rn, Gn, Bn;

        // Y_C_sign / Y_C_abs 只由参数决定（常量的「寄存器」），且每次使用前都已被同一拍流水加载过，
        // 故直接常量传播，省 4×(1+8)=36 个 FF 与其加载逻辑（逐拍值与流水数据一致）
        wire [17:0] Y_w   = y_r + y_g + y_b;
        wire [18:0] Y_m_s = Y_C_sign ? {2'b0, Y_m} : (~{3'b0, Y_m} + 1);
        wire [18:0] R_sum = Y_m_s + r2;
        wire [18:0] G_sum = Y_m_s + g2;
        wire [18:0] B_sum = Y_m_s + b2;

        // 4 级流水（乘系数/打拍 → 取 Y 高 8bit/打拍 → 乘增益 → 输出钳位）；
        // 原为 4 个独立 always，同为非阻塞 + 同复位/同时钟，合并后逐拍等价
        always @(posedge I_clk or negedge I_rst_n) begin
            if (!I_rst_n) begin
                {y_r, y_g, y_b, r0, g0, b0} <= 0;
                {Y, r1, g1, b1} <= 0;
                {Y_m, r2, g2, b2} <= 0;
                {Rn, Gn, Bn} <= 0;
            end else begin
                if (I_tvalid) begin
                    y_r <= r * C0;  y_g <= g * C1;  y_b <= b * C2;
                    r0 <= r;  g0 <= g;  b0 <= b;
                end
                if (I_tvalid_r[0]) begin
                    Y <= Y_w[17:10];  r1 <= r0;  g1 <= g0;  b1 <= b0;
                end
                if (I_tvalid_r[1]) begin
                    Y_m <= Y * Y_C_abs;  r2 <= r1 * RGB_C;  g2 <= g1 * RGB_C;  b2 <= b1 * RGB_C;
                end
                if (I_tvalid_r[2]) begin
                    Rn <= clip8(R_sum);  Gn <= clip8(G_sum);  Bn <= clip8(B_sum);
                end
            end
        end

        assign O_tdata[24*i + 16 +: 8] = Rn;
        assign O_tdata[24*i +  8 +: 8] = Gn;
        assign O_tdata[24*i      +: 8] = Bn;
    end endgenerate

    // 控制信号同步 3 拍（数据路径延迟 4 拍）
    always @(posedge I_clk or negedge I_rst_n)
        if (!I_rst_n) {I_tlast_r, I_tuser_r, I_tvalid_r} <= 12'd0;
        else {I_tlast_r, I_tuser_r, I_tvalid_r} <= {{I_tlast_r[2:0],  I_tlast},
                                                    {I_tuser_r[2:0],  I_tuser},
                                                    {I_tvalid_r[2:0], I_tvalid}};

    assign O_tlast  = I_tlast_r[3];
    assign O_tuser  = I_tuser_r[3];
    assign O_tvalid = I_tvalid_r[3];
    assign I_tready = O_tready;

endmodule
