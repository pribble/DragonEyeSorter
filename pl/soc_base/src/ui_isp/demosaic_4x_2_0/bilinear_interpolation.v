// 出处：Milianke uisrc ISP demo（ui_isp/demosaic_4x_2_0/bilinear_interpolation.v,
//       XiaoQingquan 2025/08, v2.0）。本版按统一双线性公式 + Bayer 模式色表折叠重写，逐拍行为等价：
// 先用与模式无关的 3x3 窗口几何算 4 个「角色和」(中心/水平/垂直/对角)，再按 BAYER_MODE + bayer_ypos
// 的色表映射到 R/G/B，替代原码 4 份重复通道逻辑。
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
    wire [9:0]  role [0:3][0:3];   // [角色 C/H/V/D][像素位 0..3]，10bit（4 抽头最大 1020）

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
            assign role[0][gj] = {2'b0, w[1][gj+1]};
            assign role[1][gj] = {1'b0, w[1][gj]} + {1'b0, w[1][gj+2]};
            assign role[2][gj] = {1'b0, w[0][gj+1]} + {1'b0, w[2][gj+1]};
            assign role[3][gj] = ({1'b0, w[0][gj]} + {1'b0, w[0][gj+2]})
                              + ({1'b0, w[2][gj]} + {1'b0, w[2][gj+2]});
        end
    endgenerate

    reg [119:0] mp;     // 每像素位 30bit = {B, G, R}，各 10bit
    reg [119:0] mean;   // 每像素位 30bit = {B, G, R}，各 10bit（值域 <=255，与原码 9bit 均值等效）

    // 第 1 级：角色和打拍（q 布局：每个像素位 40bit = {D, V, H, C}，各 10bit）；
    // 同拍推进 bayer_ypos 与 tlast/tuser/tvalid 移位的 4 个原独立 always 合并（全非阻塞、同复位）。
    reg [159:0] q;
    reg bayer_ypos_r0, bayer_ypos_r1;
    reg [4:0] I_tlast_r, I_tuser_r, I_tvalid_r;
    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            q <= 160'd0;
            bayer_ypos_r0 <= 1'b0;
            bayer_ypos_r1 <= 1'b0;
            I_tlast_r  <= 5'd0;
            I_tuser_r  <= 5'd0;
            I_tvalid_r <= 5'd0;
        end else begin
            if (I_tvalid)
                q <= {role[3][3], role[2][3], role[1][3], role[0][3],
                      role[3][2], role[2][2], role[1][2], role[0][2],
                      role[3][1], role[2][1], role[1][1], role[0][1],
                      role[3][0], role[2][0], role[1][0], role[0][0]};
            // bayer_ypos 打拍：第 2 级用 r0 选色表，第 3 级用 r1 选均值移位
            bayer_ypos_r0 <= bayer_ypos;
            bayer_ypos_r1 <= bayer_ypos_r0;
            // tlast/tuser/tvalid 延迟 5 拍与数据对齐
            I_tlast_r  <= {I_tlast_r[3:0],  I_tlast};
            I_tuser_r  <= {I_tuser_r[3:0],  I_tuser};
            I_tvalid_r <= {I_tvalid_r[3:0], I_tvalid};
        end
    end

    // 第 2/3 级：按 Bayer 模式取色表（角色和 -> R/G/B），再按抽头数移位求均值。
    // 未列出的 BAYER_MODE（非法参数）不生成任何逻辑 -> 与厂家原码一致输出 x。
    generate
        if (BAYER_MODE == "RGGB") begin : g_rggb
            wire [119:0] mp_p0 = {
                q[140 +: 10], q[120 +: 10], q[130 +: 10], q[110 +: 10],
                (q[90 +: 10] + q[100 +: 10]), q[80 +: 10], q[60 +: 10], q[40 +: 10],
                q[50 +: 10], q[30 +: 10], (q[10 +: 10] + q[20 +: 10]), q[0 +: 10]
            };
            wire [119:0] mp_p1 = {
                q[120 +: 10], (q[130 +: 10] + q[140 +: 10]), q[150 +: 10], q[90 +: 10],
                q[80 +: 10], q[100 +: 10], q[40 +: 10], (q[50 +: 10] + q[60 +: 10]),
                q[70 +: 10], q[10 +: 10], q[0 +: 10], q[20 +: 10]
            };
            always @(posedge I_clk or negedge I_rst_n) begin
                if (!I_rst_n) begin
                    mp <= 120'd0;
                    mean <= 120'd0;
                end else begin
                    if (I_tvalid_r[0]) mp <= bayer_ypos_r0 ? mp_p1 : mp_p0;
                    if (I_tvalid_r[1]) mean <= bayer_ypos_r1 ? {
                        mp[110 +: 10] >> 0, mp[100 +: 10] >> 2, mp[90 +: 10] >> 2,
                        mp[80 +: 10] >> 1, mp[70 +: 10] >> 0, mp[60 +: 10] >> 1,
                        mp[50 +: 10] >> 0, mp[40 +: 10] >> 2, mp[30 +: 10] >> 2,
                        mp[20 +: 10] >> 1, mp[10 +: 10] >> 0, mp[0 +: 10] >> 1
                    } : {
                        mp[110 +: 10] >> 1, mp[100 +: 10] >> 0, mp[90 +: 10] >> 1,
                        mp[80 +: 10] >> 2, mp[70 +: 10] >> 2, mp[60 +: 10] >> 0,
                        mp[50 +: 10] >> 1, mp[40 +: 10] >> 0, mp[30 +: 10] >> 1,
                        mp[20 +: 10] >> 2, mp[10 +: 10] >> 2, mp[0 +: 10] >> 0
                    };
                end
            end
        end
        else if (BAYER_MODE == "BGGR") begin : g_bggr
            wire [119:0] mp_p0 = {
                q[130 +: 10], q[120 +: 10], q[140 +: 10], q[80 +: 10],
                (q[90 +: 10] + q[100 +: 10]), q[110 +: 10], q[50 +: 10], q[40 +: 10],
                q[60 +: 10], q[0 +: 10], (q[10 +: 10] + q[20 +: 10]), q[30 +: 10]
            };
            wire [119:0] mp_p1 = {
                q[150 +: 10], (q[130 +: 10] + q[140 +: 10]), q[120 +: 10], q[100 +: 10],
                q[80 +: 10], q[90 +: 10], q[70 +: 10], (q[50 +: 10] + q[60 +: 10]),
                q[40 +: 10], q[20 +: 10], q[0 +: 10], q[10 +: 10]
            };
            always @(posedge I_clk or negedge I_rst_n) begin
                if (!I_rst_n) begin
                    mp <= 120'd0;
                    mean <= 120'd0;
                end else begin
                    if (I_tvalid_r[0]) mp <= bayer_ypos_r0 ? mp_p1 : mp_p0;
                    if (I_tvalid_r[1]) mean <= bayer_ypos_r1 ? {
                        mp[110 +: 10] >> 2, mp[100 +: 10] >> 2, mp[90 +: 10] >> 0,
                        mp[80 +: 10] >> 1, mp[70 +: 10] >> 0, mp[60 +: 10] >> 1,
                        mp[50 +: 10] >> 2, mp[40 +: 10] >> 2, mp[30 +: 10] >> 0,
                        mp[20 +: 10] >> 1, mp[10 +: 10] >> 0, mp[0 +: 10] >> 1
                    } : {
                        mp[110 +: 10] >> 1, mp[100 +: 10] >> 0, mp[90 +: 10] >> 1,
                        mp[80 +: 10] >> 0, mp[70 +: 10] >> 2, mp[60 +: 10] >> 2,
                        mp[50 +: 10] >> 1, mp[40 +: 10] >> 0, mp[30 +: 10] >> 1,
                        mp[20 +: 10] >> 0, mp[10 +: 10] >> 2, mp[0 +: 10] >> 2
                    };
                end
            end
        end
        else if (BAYER_MODE == "GBRG") begin : g_gbrg
            wire [119:0] mp_p0 = {
                q[120 +: 10], (q[130 +: 10] + q[140 +: 10]), q[150 +: 10], q[90 +: 10],
                q[80 +: 10], q[100 +: 10], q[40 +: 10], (q[50 +: 10] + q[60 +: 10]),
                q[70 +: 10], q[10 +: 10], q[0 +: 10], q[20 +: 10]
            };
            wire [119:0] mp_p1 = {
                q[140 +: 10], q[120 +: 10], q[130 +: 10], q[110 +: 10],
                (q[90 +: 10] + q[100 +: 10]), q[80 +: 10], q[60 +: 10], q[40 +: 10],
                q[50 +: 10], q[30 +: 10], (q[10 +: 10] + q[20 +: 10]), q[0 +: 10]
            };
            always @(posedge I_clk or negedge I_rst_n) begin
                if (!I_rst_n) begin
                    mp <= 120'd0;
                    mean <= 120'd0;
                end else begin
                    if (I_tvalid_r[0]) mp <= bayer_ypos_r0 ? mp_p1 : mp_p0;
                    if (I_tvalid_r[1]) mean <= bayer_ypos_r1 ? {
                        mp[110 +: 10] >> 1, mp[100 +: 10] >> 0, mp[90 +: 10] >> 1,
                        mp[80 +: 10] >> 2, mp[70 +: 10] >> 2, mp[60 +: 10] >> 0,
                        mp[50 +: 10] >> 1, mp[40 +: 10] >> 0, mp[30 +: 10] >> 1,
                        mp[20 +: 10] >> 2, mp[10 +: 10] >> 2, mp[0 +: 10] >> 0
                    } : {
                        mp[110 +: 10] >> 0, mp[100 +: 10] >> 2, mp[90 +: 10] >> 2,
                        mp[80 +: 10] >> 1, mp[70 +: 10] >> 0, mp[60 +: 10] >> 1,
                        mp[50 +: 10] >> 0, mp[40 +: 10] >> 2, mp[30 +: 10] >> 2,
                        mp[20 +: 10] >> 1, mp[10 +: 10] >> 0, mp[0 +: 10] >> 1
                    };
                end
            end
        end
        else if (BAYER_MODE == "GRBG") begin : g_grbg
            wire [119:0] mp_p0 = {
                q[150 +: 10], (q[130 +: 10] + q[140 +: 10]), q[120 +: 10], q[100 +: 10],
                q[80 +: 10], q[90 +: 10], q[70 +: 10], (q[50 +: 10] + q[60 +: 10]),
                q[40 +: 10], q[20 +: 10], q[0 +: 10], q[10 +: 10]
            };
            wire [119:0] mp_p1 = {
                q[130 +: 10], q[120 +: 10], q[140 +: 10], q[80 +: 10],
                (q[90 +: 10] + q[100 +: 10]), q[110 +: 10], q[50 +: 10], q[40 +: 10],
                q[60 +: 10], q[0 +: 10], (q[10 +: 10] + q[20 +: 10]), q[30 +: 10]
            };
            always @(posedge I_clk or negedge I_rst_n) begin
                if (!I_rst_n) begin
                    mp <= 120'd0;
                    mean <= 120'd0;
                end else begin
                    if (I_tvalid_r[0]) mp <= bayer_ypos_r0 ? mp_p1 : mp_p0;
                    if (I_tvalid_r[1]) mean <= bayer_ypos_r1 ? {
                        mp[110 +: 10] >> 1, mp[100 +: 10] >> 0, mp[90 +: 10] >> 1,
                        mp[80 +: 10] >> 0, mp[70 +: 10] >> 2, mp[60 +: 10] >> 2,
                        mp[50 +: 10] >> 1, mp[40 +: 10] >> 0, mp[30 +: 10] >> 1,
                        mp[20 +: 10] >> 0, mp[10 +: 10] >> 2, mp[0 +: 10] >> 2
                    } : {
                        mp[110 +: 10] >> 2, mp[100 +: 10] >> 2, mp[90 +: 10] >> 0,
                        mp[80 +: 10] >> 1, mp[70 +: 10] >> 0, mp[60 +: 10] >> 1,
                        mp[50 +: 10] >> 2, mp[40 +: 10] >> 2, mp[30 +: 10] >> 0,
                        mp[20 +: 10] >> 1, mp[10 +: 10] >> 0, mp[0 +: 10] >> 1
                    };
                end
            end
        end
    endgenerate

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
