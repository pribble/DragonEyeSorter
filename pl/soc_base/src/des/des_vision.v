// des_vision.v — PL image path between ISP output and the frame writer: gray / Gaussian / Sobel / binary + ROI
// statistics (published each SOF). 寄存器 0x300 的偏移 / 位域 / 复位值以 docs/PL设计.md 为准. 128-bit beats of
`include "des_common.vh"
module des_vision #(
    parameter integer WIDTH = 1920, HEIGHT = 1080, LANES = 4
) (
    input  wire         clk, input  wire         wr,
    input  wire [7:0]   waddr, input  wire [31:0]  wdata,
    input  wire [7:0]   raddr, output reg  [31:0]  rdata,
    input  wire         pclk, input  wire [127:0] s_tdata,
    input  wire         s_tvalid, input  wire         s_tuser,
    input  wire         s_tlast, output reg  [127:0] m_tdata,
    output reg          m_tvalid, output reg          m_tuser,
    output reg          m_tlast
);
    localparam BEATS = WIDTH / 4, L = LANES, STEP = 4 / LANES;
    localparam XW = $clog2(WIDTH), YW = $clog2(HEIGHT), BTW = $clog2(BEATS), NW = $clog2(L + 1), SXW = XW + $clog2(L);
    localparam [3:0] L4 = LANES;
    localparam F_MODE = 0, F_FULL = 3, F_THR = 4, F_ETHR = 12, F_X0 = F_ETHR + 11, F_X1 = F_X0 + XW,
               F_Y0 = F_X1 + XW, F_Y1 = F_Y0 + YW, CFG_W = F_Y1 + YW;
    localparam [YW-1:0] Y1D = HEIGHT - 1; localparam [XW-1:0] X1D = WIDTH - 1;
    localparam [CFG_W-1:0] CFG_DEF = {Y1D, {YW{1'b0}}, X1D, {XW{1'b0}}, 11'd200, 8'd128, 1'b1, 3'd0};
    reg [CFG_W-1:0] r_cfg = CFG_DEF, p_cfg = CFG_DEF, a_cfg = CFG_DEF; reg r_apply = 1'b0;
    always @(posedge clk) begin
        if (wr) case (waddr[7:2])
            6'h00: begin r_cfg[F_MODE +: 3] <= wdata[2:0]; r_cfg[F_FULL] <= wdata[8]; end
            6'h01: begin r_cfg[F_THR +: 8] <= wdata[7:0]; r_cfg[F_ETHR +: 11] <= wdata[26:16]; end
            6'h02: begin r_cfg[F_X0 +: XW] <= wdata[XW-1:0]; r_cfg[F_X1 +: XW] <= wdata[16 +: XW]; end
            6'h03: begin r_cfg[F_Y0 +: YW] <= wdata[YW-1:0]; r_cfg[F_Y1 +: YW] <= wdata[16 +: YW]; end
            6'h04: r_apply <= ~r_apply;
        endcase
    end
    wire ap_s1, ap_s2; reg ap_seen = 1'b0;
    des_togsync u_ap (.clk(pclk), .tin(r_apply), .s1(ap_s1), .s2(ap_s2));
    always @(posedge pclk) if (ap_s2 != ap_seen) begin ap_seen <= ap_s2; p_cfg <= r_cfg; end
    wire [2:0] a_mode = a_cfg[F_MODE +: 3]; wire a_full = a_cfg[F_FULL];
    wire [7:0] a_thr = a_cfg[F_THR +: 8]; wire [10:0] a_ethr = a_cfg[F_ETHR +: 11];
    reg tuser_d = 1'b0; wire sof = s_tuser & ~tuser_d; reg [BTW-1:0] bx = 0; reg [YW-1:0] row = 0;
    wire [BTW-1:0] cur_bx = sof ? 0 : bx; wire [YW-1:0] cur_row = sof ? 0 : row;
    wire [2:0] mode_now = sof ? p_cfg[F_MODE +: 3] : a_mode;
    wire bypass = mode_now == 3'd0 || mode_now > 3'd5;
    reg [5*BTW-1:0] bxq = 0; reg [6*YW-1:0] rowq = 0; reg [4:0] vsr = 0, usr = 0, lsr = 0;
    wire [BTW-1:0] bx1 = bxq[0 +: BTW], bx3 = bxq[4*BTW +: BTW];
    wire [YW-1:0] row3 = rowq[4*YW +: YW], row6 = rowq[5*YW +: YW];
    wire v1 = vsr[0], v3 = vsr[4], u3 = usr[4], l3 = lsr[4];
    function [7:0] gray_of(input [23:0] p);
        reg [15:0] s;
        begin s = 77 * p[23:16] + 150 * p[15:8] + 29 * p[7:0]; gray_of = s[15:8]; end
    endfunction
    function [7:0] out_pixel(input [2:0] mode, input [7:0] g, input [7:0] ga,
                             input [10:0] mg, input [7:0] thr, input [10:0] ethr);
        case (mode)
            3'd1: out_pixel = g;   3'd2: out_pixel = ga;
            3'd3: out_pixel = ga >= thr ? 8'hFF : 8'h00;
            3'd4: out_pixel = mg > 11'd255 ? 8'hFF : mg[7:0];
            3'd5: out_pixel = mg >= ethr ? 8'hFF : 8'h00;
            default: out_pixel = 8'd0;
        endcase
    endfunction
    reg [16*L-1:0] lb [0:BEATS-1];
    reg [16*L-1:0] lb_rd = 0; reg [8*L-1:0] g1 = 0;
    wire [24*L-1:0] rd = {lb_rd[16*L-1:8*L], lb_rd[8*L-1:0], g1};
    localparam ROWW = 8 * (L + 2); reg [3*ROWW-1:0] win = 0; reg [47:0] pv = 0; reg [10*(L+2)-1:0] vs = 0;
    reg [20*L-1:0] hs = 0; reg [8*L-1:0] gm = 0, ga4 = 0, gm4 = 0;
    reg [10*L-1:0] ax4 = 0, ay4 = 0; reg [8*L-1:0] gc3 = 0, ga3 = 0; reg [11*L-1:0] mg3 = 0;
    reg [L-1:0] sel4 = 0, esel4 = 0; reg [XW-1:0] xb = 0;
    integer i, j, k; reg [11:0] gs; reg signed [11:0] gx, gy;
    reg [XW-1:0] cx, bmin, bmax; reg [SXW-1:0] sx_beat; reg [NW-1:0] n_sel, n_edge;
    reg [YW-1:0] ry, r; reg inroi, rok; reg [8*L-1:0] ovl = 0; reg res_tgl = 1'b0;
    reg [20:0] acc_area = 0, acc_edges = 0, res_area = 0, res_edges = 0;
    reg [31:0] acc_sx = 0, acc_sy = 0, cyc = 0, res_sx = 0, res_sy = 0, res_cyc = 0;
    reg [XW-1:0] acc_x0 = {XW{1'b1}}, acc_x1 = 0, res_x0 = {XW{1'b1}}, res_x1 = 0;
    reg [YW-1:0] acc_y0 = {YW{1'b1}}, acc_y1 = 0, res_y0 = {YW{1'b1}}, res_y1 = 0;
    always @(posedge pclk) begin
        tuser_d <= s_tuser; cyc <= cyc + 1'b1;
        if (s_tvalid) begin
            bx <= (cur_bx == BEATS - 1) ? 0 : cur_bx + 1'b1;
            row <= (cur_bx == BEATS - 1) ? cur_row + 1'b1 : cur_row;
            lb_rd <= lb[cur_bx];
        end else if (sof) begin bx <= 0; row <= 0; end
        for (j = 0; j < L; j = j + 1) g1[8*j +: 8] <= gray_of(s_tdata[32*STEP*j +: 24]);
        bxq <= {bxq[4*BTW-1:0], cur_bx}; rowq <= {rowq[5*YW-1:0], cur_row};
        vsr <= {vsr[3:0], s_tvalid}; usr <= {usr[3:0], s_tuser}; lsr <= {lsr[3:0], s_tlast};
        if (v1) begin
            lb[bx1] <= rd[16*L-1:0];
            for (i = 0; i < 3; i = i + 1) begin
                win[ROWW*i +: ROWW] <= {rd[8*L*i +: 8*L],
                                        (bx1 == 0) ? {rd[8*L*i +: 8], rd[8*L*i +: 8]} : pv[16*i +: 16]};
                pv[16*i +: 16] <= rd[8*L*i + 8*L - 16 +: 16];
            end
        end
        for (k = 0; k < L + 2; k = k + 1)
            vs[10*k +: 10] <= win[8*k +: 8] + {win[ROWW + 8*k +: 8], 1'b0} + win[2*ROWW + 8*k +: 8];
        for (i = 0; i < 2; i = i + 1) for (j = 0; j < L; j = j + 1)
            hs[10*L*i + 10*j +: 10] <= win[2*ROWW*i + 8*j +: 8] + {win[2*ROWW*i + 8*(j+1) +: 8], 1'b0}
                                      + win[2*ROWW*i + 8*(j+2) +: 8];
        for (j = 0; j < L; j = j + 1) gm[8*j +: 8] <= win[ROWW + 8*(j+1) +: 8];
        for (j = 0; j < L; j = j + 1) begin
            gx = $signed({2'd0, vs[10*(j+2) +: 10]}) - $signed({2'd0, vs[10*j +: 10]});
            gy = $signed({2'd0, hs[10*L + 10*j +: 10]}) - $signed({2'd0, hs[10*j +: 10]});
            ax4[10*j +: 10] <= gx[11] ? -gx : gx; ay4[10*j +: 10] <= gy[11] ? -gy : gy;
            gs = vs[10*j +: 10] + {vs[10*(j+1) +: 10], 1'b0} + vs[10*(j+2) +: 10];
            ga4[8*j +: 8] <= gs[11:4];
        end
        gm4 <= gm;
        for (j = 0; j < L; j = j + 1) mg3[11*j +: 11] <= ax4[10*j +: 10] + ay4[10*j +: 10];
        gc3 <= gm4; ga3 <= ga4;
        m_tvalid <= bypass ? s_tvalid : v3; m_tuser <= bypass ? s_tuser : u3; m_tlast <= bypass ? s_tlast : l3;
        ry = row3 - 1'b1;
        rok = row3 != 0 && (a_full || (ry >= a_cfg[F_Y0 +: YW] && ry <= a_cfg[F_Y1 +: YW]));
        for (j = 0; j < L; j = j + 1) begin
            ovl[8*j +: 8] = out_pixel(a_mode, gc3[8*j +: 8], ga3[8*j +: 8], mg3[11*j +: 11], a_thr, a_ethr);
            cx = {bx3, 2'b00} + STEP * j - STEP;
            inroi = rok && (bx3 != 0 || j != 0)
                    && (a_full || (cx >= a_cfg[F_X0 +: XW] && cx <= a_cfg[F_X1 +: XW]));
            sel4[j] <= v3 && inroi && (ga3[8*j +: 8] >= a_thr);
            esel4[j] <= v3 && inroi && (mg3[11*j +: 11] >= a_ethr);
        end
        for (k = 0; k < 4; k = k + 1)
            m_tdata[32*k +: 32] <= bypass ? s_tdata[32*k +: 32]
                : {8'd0, ovl[8*(k/STEP) +: 8], ovl[8*(k/STEP) +: 8], ovl[8*(k/STEP) +: 8]};
        xb <= {bx3, 2'b00};
        n_sel = 0; n_edge = 0; sx_beat = 0; bmin = {XW{1'b1}}; bmax = 0; r = row6 - 1'b1;
        for (j = 0; j < L; j = j + 1) if (sel4[j]) begin
            cx = xb + STEP * j - STEP; n_sel = n_sel + 1'b1; sx_beat = sx_beat + cx;
            if (cx < bmin) bmin = cx; if (cx > bmax) bmax = cx;
        end
        for (j = 0; j < L; j = j + 1) n_edge = n_edge + esel4[j];
        if (sof) begin
            res_area <= acc_area; res_edges <= acc_edges; res_sx <= acc_sx; res_sy <= acc_sy;
            res_cyc <= cyc; res_x0 <= acc_x0; res_x1 <= acc_x1; res_y0 <= acc_y0; res_y1 <= acc_y1;
            res_tgl <= ~res_tgl; cyc <= 32'd1; a_cfg <= p_cfg;
            acc_area <= 0; acc_edges <= 0; acc_sx <= 0; acc_sy <= 0; acc_x0 <= {XW{1'b1}}; acc_x1 <= 0; acc_y0 <= {YW{1'b1}}; acc_y1 <= 0;
        end else begin
            acc_area <= acc_area + n_sel; acc_edges <= acc_edges + n_edge; acc_sx <= acc_sx + sx_beat;
            acc_sy <= acc_sy + n_sel * r;
            if (n_sel != 0) begin
                if (bmin < acc_x0) acc_x0 <= bmin; if (bmax > acc_x1) acc_x1 <= bmax;
                if (r < acc_y0) acc_y0 <= r; if (r > acc_y1) acc_y1 <= r;
            end
        end
    end
    wire rs_s1, rs_s2;
    des_togsync u_rs (.clk(clk), .tin(res_tgl), .s1(rs_s1), .s2(rs_s2));
    reg [31:0] o_seq = 0, o_area = 0, o_bx = 0, o_by = 0, o_sx = 0, o_sy = 0, o_edges = 0, o_cyc = 0, o_active = 0;
    always @(posedge clk) begin
        if (rs_s2 != rs_s1) begin
            o_seq <= o_seq + 1'b1; o_area <= {11'd0, res_area};
            o_bx <= {{(16-XW){1'b0}}, res_x1, {(16-XW){1'b0}}, res_x0};
            o_by <= {{(16-YW){1'b0}}, res_y1, {(16-YW){1'b0}}, res_y0};
            o_sx <= res_sx; o_sy <= res_sy; o_edges <= {11'd0, res_edges}; o_cyc <= res_cyc;
            o_active <= {4'd0, L4, a_thr, 7'd0, a_full, 5'd0, a_mode};
        end
    end
    always @* begin
        case (raddr[7:2])
            6'h00: rdata = {23'd0, r_cfg[F_FULL], 5'd0, r_cfg[F_MODE +: 3]};
            6'h01: rdata = {5'd0, r_cfg[F_ETHR +: 11], 8'd0, r_cfg[F_THR +: 8]};
            6'h02: rdata = {{(16-XW){1'b0}}, r_cfg[F_X1 +: XW], {(16-XW){1'b0}}, r_cfg[F_X0 +: XW]};
            6'h03: rdata = {{(16-YW){1'b0}}, r_cfg[F_Y1 +: YW], {(16-YW){1'b0}}, r_cfg[F_Y0 +: YW]};
            6'h05: rdata = 32'h56495331;
            6'h08: rdata = o_seq;  6'h09: rdata = o_area;  6'h0A: rdata = o_bx;    6'h0B: rdata = o_by;
            6'h0C: rdata = o_sx;   6'h0D: rdata = o_sy;    6'h0E: rdata = o_edges; 6'h0F: rdata = o_cyc;
            6'h10: rdata = o_active;
            default: rdata = 32'hDEADBEEF;
        endcase
    end
endmodule
