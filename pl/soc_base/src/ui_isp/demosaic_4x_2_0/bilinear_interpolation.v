// 出处：Milianke uisrc ISP demo（ui_isp/demosaic_4x_2_0/bilinear_interpolation.v,
//       XiaoQingquan 2025/08, v2.0）。本版按统一双线性公式 + Bayer 模式色表折叠重写，逐拍行为等价：
// 先用与模式无关的 3x3 窗口几何算 4 个「角色和」(中心 C / 水平 H / 垂直 V / 对角 D)，
// 再按 BAYER_MODE + bayer_ypos 的色表映射到 R/G/B。四种 Bayer 模式只有两套色表
// （RGGB ≡ GBRG、BGGR ≡ GRBG，行奇偶 bayer_ypos 在两者间互换），故折叠成 P/Q/R/S 四张
// 常量拼接 + 一个受 BAYER_MODE 常量选择的通道；未列出的模式不生成逻辑（输出 x，同原版）。
module bilinear_interpolation #(
    parameter BAYER_MODE = "BGGR"   // Bayer 排列：RGGB/BGGR/GBRG/GRBG
) (
    input  wire        I_clk, I_rst_n,
    input  wire        I_tlast, I_tuser, I_tvalid,
    output wire        I_tready,
    input  wire        bayer_ypos,           // 窗口中心行行奇偶
    input  wire [95:0] matrix_last_line,
    input  wire [95:0] matrix_cur_line,
    input  wire [95:0] matrix_next_line,
    output wire        O_tlast, O_tuser,
    output wire [127:0] O_tdata,
    output wire        O_tvalid,
    input  wire        O_tready
);

    // 3x3 窗口字节：行 0/1/2 = 上一行/当前行/下一行；窗口列 0..5 = 相对当前 4 像素组的 -1..+4
    wire [95:0] line [0:2];
    wire [7:0]  w    [0:2][0:5];
    // 角色和（位宽取精确最大值：C 1 抽头 8bit、H/V 2 抽头 9bit、D 4 抽头 10bit）
    wire [7:0]  role_c [0:3];      // [角色][像素位 0..3]
    wire [8:0]  role_h [0:3];
    wire [8:0]  role_v [0:3];
    wire [9:0]  role_d [0:3];

    assign line[0] = matrix_last_line;
    assign line[1] = matrix_cur_line;
    assign line[2] = matrix_next_line;

    genvar gl, gw, gj;
    generate
        for (gl = 0; gl < 3; gl = gl + 1) begin : g_line
            for (gw = 0; gw < 6; gw = gw + 1) begin : g_word
                assign w[gl][gw] = line[gl][71 - 8*gw -: 8];
            end
        end
        for (gj = 0; gj < 4; gj = gj + 1) begin : g_role
            // 8bit 相加先零扩展到 9bit，避免截断；对角 4 抽头扩到 10bit
            assign role_c[gj] = w[1][gj+1];
            assign role_h[gj] = {1'b0, w[1][gj]}   + {1'b0, w[1][gj+2]};
            assign role_v[gj] = {1'b0, w[0][gj+1]} + {1'b0, w[2][gj+1]};
            assign role_d[gj] = ({1'b0, w[0][gj]} + {1'b0, w[0][gj+2]})
                              + ({1'b0, w[2][gj]} + {1'b0, w[2][gj+2]});
        end
    endgenerate

    // 第 1 级：每像素 36bit = {D(10), V(9), H(9), C(8)}（原版统一 10bit，高位恒 0；按实际位宽存，逐位等价）
    reg [143:0] q;
    reg [119:0] mp;        // 第 2 级：每像素 30bit = {B, G, R}，各 10bit
    reg [119:0] mean;      // 第 3 级：同上（值域 <=255，与 9bit 均值等效）
    reg bayer_ypos_r0, bayer_ypos_r1;
    reg [4:0] I_tlast_r, I_tuser_r, I_tvalid_r;

    // 第 2 级色表输入的「角色和」= 第 1 级寄存器 q 的 4 角色 × 4 像素（色表取的是打拍后的值）
    wire [7:0]  qr_c [0:3];        // [角色][像素位 0..3]
    wire [8:0]  qr_h [0:3], qr_v [0:3];
    wire [9:0]  qr_d [0:3];
    genvar gk;
    generate for (gk = 0; gk < 4; gk = gk + 1) begin : g_qrole
        assign qr_c[gk] = q[36*gk      +:  8];
        assign qr_h[gk] = q[36*gk +  8 +:  9];
        assign qr_v[gk] = q[36*gk + 17 +:  9];
        assign qr_d[gk] = q[36*gk + 26 +: 10];
    end endgenerate

    // 第 2 级色表（未打拍）：每像素 30bit {B,G,R}、各取一个角色和或其两两和；
    // P/Q 给 RGGB/GBRG，R/S 给 BGGR/GRBG（Q/S 是行奇偶 1 那张表）
    // 各角色按原版 10bit 字段零扩展（q 里 C/H/V 高位恒 0），字段布局与位偏移与原版完全一致
    wire [119:0] P = {{2'b0,qr_v[3]}, {2'b0,qr_c[3]}, {1'b0,qr_h[3]}, qr_d[2], ({1'b0,qr_h[2]}+{1'b0,qr_v[2]}), {2'b0,qr_c[2]},
                      {1'b0,qr_v[1]}, {2'b0,qr_c[1]}, {1'b0,qr_h[1]}, qr_d[0], ({1'b0,qr_h[0]}+{1'b0,qr_v[0]}), {2'b0,qr_c[0]}};
    wire [119:0] Q = {{2'b0,qr_c[3]}, ({1'b0,qr_h[3]}+{1'b0,qr_v[3]}), qr_d[3], {1'b0,qr_h[2]}, {2'b0,qr_c[2]}, {1'b0,qr_v[2]},
                      {2'b0,qr_c[1]}, ({1'b0,qr_h[1]}+{1'b0,qr_v[1]}), qr_d[1], {1'b0,qr_h[0]}, {2'b0,qr_c[0]}, {1'b0,qr_v[0]}};
    wire [119:0] R = {{1'b0,qr_h[3]}, {2'b0,qr_c[3]}, {1'b0,qr_v[3]}, {2'b0,qr_c[2]}, ({1'b0,qr_h[2]}+{1'b0,qr_v[2]}), qr_d[2],
                      {1'b0,qr_h[1]}, {2'b0,qr_c[1]}, {1'b0,qr_v[1]}, {2'b0,qr_c[0]}, ({1'b0,qr_h[0]}+{1'b0,qr_v[0]}), qr_d[0]};
    wire [119:0] S = {qr_d[3], ({1'b0,qr_h[3]}+{1'b0,qr_v[3]}), {2'b0,qr_c[3]}, {1'b0,qr_v[2]}, {2'b0,qr_c[2]}, {1'b0,qr_h[2]},
                      qr_d[1], ({1'b0,qr_h[1]}+{1'b0,qr_v[1]}), {2'b0,qr_c[1]}, {1'b0,qr_v[0]}, {2'b0,qr_c[0]}, {1'b0,qr_h[0]}};

    // 第 3 级均值：按抽头数右移（C 1 抽头 >>0、H/V 2 抽头 >>1、D 与 H+V 4 抽头 >>2）
    wire [119:0] NP = {mp[110+:10]>>1, mp[100+:10],  mp[90+:10]>>1, mp[80+:10]>>2, mp[70+:10]>>2, mp[60+:10],
                       mp[50+:10]>>1,  mp[40+:10],   mp[30+:10]>>1, mp[20+:10]>>2, mp[10+:10]>>2, mp[0+:10]};
    wire [119:0] NQ = {mp[110+:10],  mp[100+:10]>>2, mp[90+:10]>>2, mp[80+:10]>>1, mp[70+:10],   mp[60+:10]>>1,
                       mp[50+:10],   mp[40+:10]>>2,  mp[30+:10]>>2, mp[20+:10]>>1, mp[10+:10],   mp[0+:10]>>1};
    wire [119:0] NR = {mp[110+:10]>>1, mp[100+:10],  mp[90+:10]>>1, mp[80+:10],   mp[70+:10]>>2, mp[60+:10]>>2,
                       mp[50+:10]>>1,  mp[40+:10],   mp[30+:10]>>1, mp[20+:10],   mp[10+:10]>>2, mp[0+:10]>>2};
    wire [119:0] NS = {mp[110+:10]>>2, mp[100+:10]>>2, mp[90+:10], mp[80+:10]>>1, mp[70+:10],   mp[60+:10]>>1,
                       mp[50+:10]>>2,  mp[40+:10]>>2,  mp[30+:10], mp[20+:10]>>1, mp[10+:10],   mp[0+:10]>>1};

    // BAYER_MODE 选表：RGGB/GBRG 用 P/Q，BGGR/GRBG 用 R/S；GBRG/GRBG 的 bayer_ypos 极性相反
    // （0/1 表对调）。只用 generate-if 常量分支赋值（与厂家原版同一写法，TD 6.2.1 已验证可解析），
    // 非法模式不驱动这些选择线（对应地第 2/3 级 always 也不生成，输出保持 x）。
    wire [119:0] mp0_sel, mp1_sel, mn0_sel, mn1_sel;
    generate
        if (BAYER_MODE == "RGGB") begin : g_rggb
            assign {mp0_sel, mp1_sel, mn0_sel, mn1_sel} = {P, Q, NP, NQ};
        end else if (BAYER_MODE == "GBRG") begin : g_gbrg
            assign {mp0_sel, mp1_sel, mn0_sel, mn1_sel} = {Q, P, NQ, NP};
        end else if (BAYER_MODE == "BGGR") begin : g_bggr
            assign {mp0_sel, mp1_sel, mn0_sel, mn1_sel} = {R, S, NR, NS};
        end else if (BAYER_MODE == "GRBG") begin : g_grbg
            assign {mp0_sel, mp1_sel, mn0_sel, mn1_sel} = {S, R, NS, NR};
        end
    endgenerate

    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            {q, bayer_ypos_r0, bayer_ypos_r1, I_tlast_r, I_tuser_r, I_tvalid_r} <= 0;
        end else begin
            if (I_tvalid)
                q <= {role_d[3], role_v[3], role_h[3], role_c[3],
                      role_d[2], role_v[2], role_h[2], role_c[2],
                      role_d[1], role_v[1], role_h[1], role_c[1],
                      role_d[0], role_v[0], role_h[0], role_c[0]};
            // bayer_ypos 打拍：第 2 级用 r0 选色表，第 3 级用 r1 选均值移位
            bayer_ypos_r0 <= bayer_ypos;
            bayer_ypos_r1 <= bayer_ypos_r0;
            // tlast/tuser/tvalid 延迟 5 拍与数据对齐
            I_tlast_r  <= {I_tlast_r[3:0],  I_tlast};
            I_tuser_r  <= {I_tuser_r[3:0],  I_tuser};
            I_tvalid_r <= {I_tvalid_r[3:0], I_tvalid};
        end
    end

    // 第 2/3 级寄存器：非法 BAYER_MODE 不生成这段逻辑（mp/mean 保持 x，同原版）
    generate if (BAYER_MODE == "RGGB" || BAYER_MODE == "BGGR" ||
                 BAYER_MODE == "GBRG" || BAYER_MODE == "GRBG") begin : g_bayer
        always @(posedge I_clk or negedge I_rst_n) begin
            if (!I_rst_n) begin
                mp <= 120'd0;
                mean <= 120'd0;
            end else begin
                if (I_tvalid_r[0]) mp   <= bayer_ypos_r0 ? mp1_sel : mp0_sel;
                if (I_tvalid_r[1]) mean <= bayer_ypos_r1 ? mn1_sel : mn0_sel;
            end
        end
    end endgenerate

    // 第 4/5 级：饱和到 8bit 拼 128bit（每个 32bit 像素 = {8'b0, R, G, B}），再打一拍输出
    reg [127:0] rgb_d, rgb_q;
    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            rgb_d <= 128'd0;
            rgb_q <= 128'd0;
        end else begin
            if (I_tvalid_r[2])
                rgb_d <= {
                    8'd0, (mean[98] ? 8'd255 : mean[97 -: 8]), (mean[108] ? 8'd255 : mean[107 -: 8]), (mean[118] ? 8'd255 : mean[117 -: 8]),
                    8'd0, (mean[68] ? 8'd255 : mean[67 -: 8]), (mean[78] ? 8'd255 : mean[77 -: 8]), (mean[88] ? 8'd255 : mean[87 -: 8]),
                    8'd0, (mean[38] ? 8'd255 : mean[37 -: 8]), (mean[48] ? 8'd255 : mean[47 -: 8]), (mean[58] ? 8'd255 : mean[57 -: 8]),
                    8'd0, (mean[8] ? 8'd255 : mean[7 -: 8]), (mean[18] ? 8'd255 : mean[17 -: 8]), (mean[28] ? 8'd255 : mean[27 -: 8])
                };
            if (I_tvalid_r[3]) rgb_q <= rgb_d;
        end
    end

    assign O_tlast  = I_tlast_r[4];
    assign O_tuser  = I_tuser_r[4];
    assign O_tvalid = I_tvalid_r[4];
    assign O_tdata  = rgb_q;
    assign I_tready = O_tready;

endmodule
