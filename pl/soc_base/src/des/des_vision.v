// des_vision.v — PL image path between ISP output and frame writer (赛题领域3: 灰度滤波/边缘检测/尺寸粗测).
// Stream 128-bit beats of 4 BGRA pixels, WIDTH/4 beats/row; rows counted from tuser like the frame writer,
// so coordinates match the PS DMA buffer. No back-pressure. MODE0 1-clock bypass, others 6 clocks, alpha=0.
// 3x3 results land one pixel left and one row up of the centre (no extra line delay). Left edge reuses itself,
// row above 0 is the previous frame's last row. Filters split over 3 pipeline stages for the MIPI byte clock.
//   gray Y=(77R+150G+29B)>>8; window from two row buffers (ERAM WIDTH/4x64b): Gaussian/16, Sobel |Gx|+|Gy|.
//   modes 0 camera/1 gray/2 Gauss/3 binary(>=THR)/4 Sobel(sat)/5 edges(>=ETHR); settings take effect next SOF.
// Statistics on binary image in ROI (or FULL) published each SOF: AREA/BBOX/SUMX/SUMY/EDGES/CYCLES.
// Registers 0x300 window: 0x00 CTRL[2:0]MODE[8]FULL / 0x04 THR[7:0]BIN[26:16]EDGE / 0x08 ROIX[10:0]X0[26:16]X1 /
// 0x0C ROIY / 0x10 APPLY(W next SOF) / 0x14 ID 0x56495331 / 0x20 SEQ / 0x24 AREA / 0x28 BBOXX / 0x2C BBOXY /
// 0x30 SUMX / 0x34 SUMY / 0x38 EDGES / 0x3C CYCLES / 0x40 ACTIVE[2:0]MODE[8]FULL[23:16]THR[27:24]LANES.
// LANES=4 every pixel; =2 pixels 0,2 only (half logic): result shown on 2 pixels, window steps 2px, PS scales
// AREA/EDGES by 4/LANES using ACTIVE LANES.
module des_vision #(
    parameter integer WIDTH  = 1920,
    parameter integer HEIGHT = 1080,
    parameter integer LANES  = 4
) (
    input  wire         clk,
    input  wire         wr,
    input  wire [7:0]   waddr,
    input  wire [31:0]  wdata,
    input  wire [7:0]   raddr,
    output reg  [31:0]  rdata,
    input  wire         pclk,
    input  wire [127:0] s_tdata,
    input  wire         s_tvalid,
    input  wire         s_tuser,
    input  wire         s_tlast,
    output reg  [127:0] m_tdata,
    output reg          m_tvalid,
    output reg          m_tuser,
    output reg          m_tlast
);
    localparam BEATS = WIDTH / 4;
    localparam L = LANES, STEP = 4 / LANES;
    localparam XW = $clog2(WIDTH), YW = $clog2(HEIGHT), BTW = $clog2(BEATS);
    localparam NW = $clog2(L + 1), SXW = XW + $clog2(L);
    localparam [3:0] L4 = LANES;

    reg [2:0] r_mode = 3'd0; reg r_full = 1'b1;
    reg [7:0] r_thr = 8'd128; reg [10:0] r_ethr = 11'd200;
    reg [XW-1:0] r_x0 = 0, r_x1 = WIDTH - 1; reg [YW-1:0] r_y0 = 0, r_y1 = HEIGHT - 1;
    reg r_apply = 1'b0;
    always @(posedge clk) begin
        if (wr) case (waddr[7:2])
            6'h00: begin r_mode <= wdata[2:0]; r_full <= wdata[8]; end
            6'h01: begin r_thr <= wdata[7:0]; r_ethr <= wdata[26:16]; end
            6'h02: begin r_x0 <= wdata[XW-1:0]; r_x1 <= wdata[16+XW-1:16]; end
            6'h03: begin r_y0 <= wdata[YW-1:0]; r_y1 <= wdata[16+YW-1:16]; end
            6'h04: r_apply <= ~r_apply;
        endcase
    end

    reg [2:0] ap_sync = 3'd0; reg ap_seen = 1'b0;
    reg [2:0] p_mode = 3'd0, a_mode = 3'd0; reg p_full = 1'b1, a_full = 1'b1;
    reg [7:0] p_thr = 8'd128, a_thr = 8'd128; reg [10:0] p_ethr = 11'd200, a_ethr = 11'd200;
    reg [XW-1:0] p_x0 = 0, p_x1 = WIDTH - 1, a_x0 = 0, a_x1 = WIDTH - 1;
    reg [YW-1:0] p_y0 = 0, p_y1 = HEIGHT - 1, a_y0 = 0, a_y1 = HEIGHT - 1;

    reg tuser_d = 1'b0;
    wire sof = s_tuser & ~tuser_d;
    reg [BTW-1:0] bx = 0; reg [YW-1:0] row = 0;
    wire [BTW-1:0] cur_bx = sof ? 0 : bx;
    wire [YW-1:0] cur_row = sof ? 0 : row;
    wire bypass = sof ? (p_mode == 3'd0 || p_mode > 3'd5) : (a_mode == 3'd0 || a_mode > 3'd5);

    function [7:0] gray_of(input [31:0] p);
        reg [16:0] s;
        begin s = 77 * p[23:16] + 150 * p[15:8] + 29 * p[7:0]; gray_of = s[15:8]; end
    endfunction

    reg [16*L-1:0] lb [0:BEATS-1];
    reg [16*L-1:0] lb_rd = 0; reg [8*L-1:0] g1 = 0;
    reg [BTW-1:0] bx1 = 0, bx2 = 0, bxa = 0, bxb = 0, bx3 = 0;
    reg [YW-1:0] row1 = 0, row2 = 0, rowa = 0, rowb = 0, row3 = 0, r4 = 0;
    reg v1 = 0, u1 = 0, l1 = 0, v2 = 0, u2 = 0, l2 = 0, va = 0, ua = 0, la = 0;
    reg vb = 0, ub = 0, lb_ = 0, v3 = 0, u3 = 0, l3 = 0;
    reg [7:0] pv_t2 = 0, pv_t3 = 0, pv_m2 = 0, pv_m3 = 0, pv_b2 = 0, pv_b3 = 0;
    reg [8*(L+2)-1:0] c2_t = 0, c2_m = 0, c2_b = 0;
    wire [8*L-1:0] cur_t = lb_rd[16*L-1:8*L], cur_m = lb_rd[8*L-1:0], cur_b = g1;
    wire lb_we = v1;

    reg [10*(L+2)-1:0] st_s = 0;
    reg [10*L-1:0] st_th = 0, st_bh = 0;
    reg [8*L-1:0] st_c = 0, sb_c = 0;
    reg [12*L-1:0] sb_g = 0, sb_gx = 0, sb_gy = 0;
    reg [8*L-1:0] gc3 = 0, ga3 = 0; reg [11*L-1:0] mg3 = 0;

    reg [L-1:0] sel4 = 0, esel4 = 0;
    reg [XW*L-1:0] c4 = 0;

    reg [20:0] acc_area = 0, acc_edges = 0, res_area = 0, res_edges = 0;
    reg [31:0] acc_sx = 0, acc_sy = 0, cyc = 0, res_sx = 0, res_sy = 0, res_cyc = 0;
    reg [XW-1:0] acc_x0 = {XW{1'b1}}, acc_x1 = 0, res_x0 = {XW{1'b1}}, res_x1 = 0;
    reg [YW-1:0] acc_y0 = {YW{1'b1}}, acc_y1 = 0, res_y0 = {YW{1'b1}}, res_y1 = 0;
    reg res_tgl = 1'b0;

    integer j, k;
    reg [7:0] t0, t1, t2, b0, b1, b2, ov;
    reg [9:0] s0, s1, s2;
    reg signed [11:0] gx, gy; reg [10:0] ax, ay; reg [11:0] mag;
    reg [8*L-1:0] ovl = 0;
    reg [NW-1:0] n_sel, n_edge;
    reg [SXW-1:0] sx_beat;
    reg [XW-1:0] cx, bmin, bmax;
    reg inroi;

    always @(posedge pclk) begin
        ap_sync <= {ap_sync[1:0], r_apply};
        if (ap_sync[2] != ap_seen) begin
            ap_seen <= ap_sync[2];
            p_mode <= r_mode; p_full <= r_full; p_thr <= r_thr; p_ethr <= r_ethr;
            p_x0 <= r_x0; p_x1 <= r_x1; p_y0 <= r_y0; p_y1 <= r_y1;
        end
        tuser_d <= s_tuser;
        cyc <= cyc + 1'b1;
        if (sof) begin
            res_area <= acc_area; res_edges <= acc_edges; res_sx <= acc_sx; res_sy <= acc_sy; res_cyc <= cyc;
            res_x0 <= acc_x0; res_x1 <= acc_x1; res_y0 <= acc_y0; res_y1 <= acc_y1;
            res_tgl <= ~res_tgl; cyc <= 32'd1;
            a_mode <= p_mode; a_full <= p_full; a_thr <= p_thr; a_ethr <= p_ethr;
            a_x0 <= p_x0; a_x1 <= p_x1; a_y0 <= p_y0; a_y1 <= p_y1;
        end

        if (s_tvalid) begin
            bx <= (cur_bx == BEATS - 1) ? 0 : cur_bx + 1'b1;
            row <= (cur_bx == BEATS - 1) ? cur_row + 1'b1 : cur_row;
            lb_rd <= lb[cur_bx];
        end else if (sof) begin bx <= 0; row <= 0; end
        for (j = 0; j < L; j = j + 1) g1[8*j +: 8] <= gray_of(s_tdata[32*STEP*j +: 32]);
        bx1 <= cur_bx; row1 <= cur_row; v1 <= s_tvalid; u1 <= s_tuser; l1 <= s_tlast;

        if (lb_we) lb[bx1] <= {cur_m, cur_b};
        if (v1) begin
            c2_t <= {cur_t, (bx1 == 0) ? {cur_t[7:0], cur_t[7:0]} : {pv_t3, pv_t2}};
            c2_m <= {cur_m, (bx1 == 0) ? {cur_m[7:0], cur_m[7:0]} : {pv_m3, pv_m2}};
            c2_b <= {cur_b, (bx1 == 0) ? {cur_b[7:0], cur_b[7:0]} : {pv_b3, pv_b2}};
            pv_t2 <= cur_t[8*(L-2) +: 8]; pv_t3 <= cur_t[8*(L-1) +: 8];
            pv_m2 <= cur_m[8*(L-2) +: 8]; pv_m3 <= cur_m[8*(L-1) +: 8];
            pv_b2 <= cur_b[8*(L-2) +: 8]; pv_b3 <= cur_b[8*(L-1) +: 8];
        end
        bx2 <= bx1; row2 <= row1; v2 <= v1; u2 <= u1; l2 <= l1;

        for (k = 0; k < L + 2; k = k + 1)
            st_s[10*k +: 10] <= c2_t[8*k +: 8] + {c2_m[8*k +: 8], 1'b0} + c2_b[8*k +: 8];
        for (j = 0; j < L; j = j + 1) begin
            t0 = c2_t[8*j +: 8]; t1 = c2_t[8*(j+1) +: 8]; t2 = c2_t[8*(j+2) +: 8];
            b0 = c2_b[8*j +: 8]; b1 = c2_b[8*(j+1) +: 8]; b2 = c2_b[8*(j+2) +: 8];
            st_th[10*j +: 10] <= t0 + {t1, 1'b0} + t2;
            st_bh[10*j +: 10] <= b0 + {b1, 1'b0} + b2;
            st_c[8*j +: 8] <= c2_m[8*(j+1) +: 8];
        end
        bxa <= bx2; rowa <= row2; va <= v2; ua <= u2; la <= l2;

        for (j = 0; j < L; j = j + 1) begin
            s0 = st_s[10*j +: 10]; s1 = st_s[10*(j+1) +: 10]; s2 = st_s[10*(j+2) +: 10];
            sb_g[12*j +: 12] <= s0 + {s1, 1'b0} + s2;
            sb_gx[12*j +: 12] <= $signed({2'd0, s2}) - $signed({2'd0, s0});
            sb_gy[12*j +: 12] <= $signed({2'd0, st_bh[10*j +: 10]}) - $signed({2'd0, st_th[10*j +: 10]});
        end
        sb_c <= st_c;
        bxb <= bxa; rowb <= rowa; vb <= va; ub <= ua; lb_ <= la;

        for (j = 0; j < L; j = j + 1) begin
            gx = sb_gx[12*j +: 12]; gy = sb_gy[12*j +: 12];
            ax = gx[11] ? -gx : gx; ay = gy[11] ? -gy : gy; mag = ax + ay;
            gc3[8*j +: 8] <= sb_c[8*j +: 8];
            ga3[8*j +: 8] <= sb_g[12*j + 4 +: 8];
            mg3[11*j +: 11] <= mag[10:0];
        end
        bx3 <= bxb; row3 <= rowb; v3 <= vb; u3 <= ub; l3 <= lb_;

        m_tvalid <= bypass ? s_tvalid : v3;
        m_tuser <= bypass ? s_tuser : u3;
        m_tlast <= bypass ? s_tlast : l3;
        for (j = 0; j < L; j = j + 1) begin
            case (a_mode)
                3'd1: ov = gc3[8*j +: 8];
                3'd2: ov = ga3[8*j +: 8];
                3'd3: ov = (ga3[8*j +: 8] >= a_thr) ? 8'hFF : 8'h00;
                3'd4: ov = (mg3[11*j +: 11] > 11'd255) ? 8'hFF : mg3[11*j + 7 -: 8];
                3'd5: ov = (mg3[11*j +: 11] >= a_ethr) ? 8'hFF : 8'h00;
                default: ov = 8'd0;
            endcase
            ovl[8*j +: 8] = ov;
            cx = {bx3, 2'b00} + STEP * j - STEP;
            inroi = a_full || (cx >= a_x0 && cx <= a_x1 && row3 - 1'b1 >= a_y0 && row3 - 1'b1 <= a_y1);
            inroi = inroi && !(bx3 == 0 && j == 0) && row3 != 0;
            sel4[j] <= v3 && inroi && (ga3[8*j +: 8] >= a_thr);
            esel4[j] <= v3 && inroi && (mg3[11*j +: 11] >= a_ethr);
            c4[XW*j +: XW] <= cx;
        end
        for (k = 0; k < 4; k = k + 1)
            m_tdata[32*k +: 32] <= bypass ? s_tdata[32*k +: 32]
                                          : {8'd0, ovl[8*(k/STEP) +: 8], ovl[8*(k/STEP) +: 8], ovl[8*(k/STEP) +: 8]};
        r4 <= row3 - 1'b1;

        n_sel = 0; n_edge = 0; sx_beat = 0; bmin = {XW{1'b1}}; bmax = 0;
        for (j = 0; j < L; j = j + 1) if (sel4[j]) begin
            n_sel = n_sel + 1'b1; sx_beat = sx_beat + c4[XW*j +: XW];
            if (c4[XW*j +: XW] < bmin) bmin = c4[XW*j +: XW];
            if (c4[XW*j +: XW] > bmax) bmax = c4[XW*j +: XW];
        end
        for (j = 0; j < L; j = j + 1) n_edge = n_edge + esel4[j];
        if (sof) begin
            acc_area <= 0; acc_edges <= 0; acc_sx <= 0; acc_sy <= 0;
            acc_x0 <= {XW{1'b1}}; acc_x1 <= 0; acc_y0 <= {YW{1'b1}}; acc_y1 <= 0;
        end else begin
            acc_area <= acc_area + n_sel; acc_edges <= acc_edges + n_edge;
            acc_sx <= acc_sx + sx_beat; acc_sy <= acc_sy + n_sel * r4;
            if (n_sel != 0) begin
                if (bmin < acc_x0) acc_x0 <= bmin;
                if (bmax > acc_x1) acc_x1 <= bmax;
                if (r4 < acc_y0) acc_y0 <= r4;
                if (r4 > acc_y1) acc_y1 <= r4;
            end
        end
    end

    reg [2:0] res_sync = 3'd0;
    reg [31:0] o_seq = 0, o_area = 0, o_bx = 0, o_by = 0, o_sx = 0, o_sy = 0, o_edges = 0, o_cyc = 0, o_active = 0;
    always @(posedge clk) begin
        res_sync <= {res_sync[1:0], res_tgl};
        if (res_sync[2] != res_sync[1]) begin
            o_seq <= o_seq + 1'b1;
            o_area <= {11'd0, res_area};
            o_bx <= {{(16-XW){1'b0}}, res_x1, {(16-XW){1'b0}}, res_x0};
            o_by <= {{(16-YW){1'b0}}, res_y1, {(16-YW){1'b0}}, res_y0};
            o_sx <= res_sx; o_sy <= res_sy; o_edges <= {11'd0, res_edges}; o_cyc <= res_cyc;
            o_active <= {4'd0, L4, a_thr, 7'd0, a_full, 5'd0, a_mode};
        end
    end

    always @* begin
        case (raddr[7:2])
            6'h00: rdata = {23'd0, r_full, 5'd0, r_mode};
            6'h01: rdata = {5'd0, r_ethr, 8'd0, r_thr};
            6'h02: rdata = {{(16-XW){1'b0}}, r_x1, {(16-XW){1'b0}}, r_x0};
            6'h03: rdata = {{(16-YW){1'b0}}, r_y1, {(16-YW){1'b0}}, r_y0};
            6'h05: rdata = 32'h56495331;
            6'h08: rdata = o_seq; 6'h09: rdata = o_area; 6'h0A: rdata = o_bx; 6'h0B: rdata = o_by;
            6'h0C: rdata = o_sx; 6'h0D: rdata = o_sy; 6'h0E: rdata = o_edges; 6'h0F: rdata = o_cyc;
            6'h10: rdata = o_active;
            default: rdata = 32'hDEADBEEF;
        endcase
    end
endmodule
