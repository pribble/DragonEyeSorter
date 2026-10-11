// des_vision.v — PL image path between the ISP output and the frame writer (赛题领域 3: 简单级「单路 1080P 图像 PL
// 采集；硬件灰度滤波」, 中级「PL 硬件边缘检测、尺寸粗测量」).
//
// Stream: 128-bit beats of 4 pixels (BGRA, B in the low byte of each 32-bit pixel), WIDTH/4 beats per row. Rows are
// counted from the frame-start edge of tuser exactly like the frame writer does, so coordinates are the ones the PS
// sees in the DMA buffer. No stalls (the frame writer has no back-pressure). MODE 0 is a 1-clock register bypass
// (the camera data are not carried through the filter pipeline: v6 first try with a 128-bit delay line did not route,
// 90 % slices); the other modes come out of the pipeline after 6 clocks with alpha = 0. The output source switches
// on the frame-start clock itself (decided by the new mode), so every frame start reaches the writer exactly once. The 3x3 filters are split over three pipeline stages: the ISP clock is the MIPI byte
// clock (up to 156 MHz) and TD does not report that domain (fan-out 0 in the timing summary).
//   gray     Y = (77 R + 150 G + 29 B) >> 8 (BT.601)
//   window   3x3 from two row buffers (one ERAM, WIDTH/4 x 64 bit): Gaussian [1 2 1; 2 4 2; 1 2 1] / 16,
//            Sobel |Gx| + |Gy| (0..2040)
//   output   MODE 0 camera, 1 gray, 2 Gaussian, 3 binary (Gaussian >= THR), 4 Sobel (saturated to 255),
//            5 edges (Sobel >= ETHR). A 3x3 result is produced when the window's lower-right pixel arrives, so it
//            lands one pixel left and one row up of its centre: no extra line of delay, frame timing unchanged.
//            At the left image edge the left neighbour is the pixel itself; the row above row 0 is the previous
//            frame's last row.
// Statistics on the binary image inside the ROI [X0..X1] x [Y0..Y1] (or the whole frame with FULL), published at
// every frame start: bright AREA, bounding box, SUMX / SUMY (centroid = SUM / AREA), EDGES, CYCLES (pixel clocks
// per frame). The PS writes the ROI of the part it measures -> PL coarse size next to the PS sub-pixel result.
//
// Registers (byte offsets in the 0x300 window, register clock):
//   0x00 CTRL   RW [2:0] MODE, [8] FULL
//   0x04 THR    RW [7:0] binary threshold, [26:16] edge threshold
//   0x08 ROI_X  RW [10:0] X0, [26:16] X1 (pixels)
//   0x0C ROI_Y  RW [10:0] Y0, [26:16] Y1 (rows)
//   0x10 APPLY  W  any write: the settings above take effect at the next frame start (no tearing)
//   0x14 ID     R  0x56495331 'VIS1'
//   0x20 SEQ    R  frames with published results
//   0x24 AREA   R  bright pixels in the ROI
//   0x28 BBOX_X R  [10:0] min x, [26:16] max x (min > max: nothing bright)
//   0x2C BBOX_Y R  [10:0] min y, [26:16] max y
//   0x30 SUMX   R  sum of x over the bright pixels
//   0x34 SUMY   R  sum of y
//   0x38 EDGES  R  edge pixels in the ROI
//   0x3C CYCLES R  pixel clocks between the last two frame starts (pixel clock = CYCLES x frame rate)
//   0x40 ACTIVE R  settings in use: [2:0] MODE, [8] FULL, [23:16] THR
module des_vision #(
    parameter integer WIDTH  = 1920,
    parameter integer HEIGHT = 1080
) (
    // register side
    input  wire         clk,
    input  wire         wr,
    input  wire [7:0]   waddr,
    input  wire [31:0]  wdata,
    input  wire [7:0]   raddr,
    output reg  [31:0]  rdata,
    // stream side (ISP clock)
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
    localparam integer BEATS = WIDTH / 4;

    // ======================= register side =======================
    reg [2:0]  r_mode = 3'd0;
    reg        r_full = 1'b1;
    reg [7:0]  r_thr = 8'd128;
    reg [10:0] r_ethr = 11'd200;
    reg [10:0] r_x0 = 11'd0, r_x1 = WIDTH - 1, r_y0 = 11'd0, r_y1 = HEIGHT - 1;
    reg        r_apply = 1'b0;
    always @(posedge clk) begin
        if (wr) begin
            case (waddr[7:2])
                6'h00: begin r_mode <= wdata[2:0]; r_full <= wdata[8]; end
                6'h01: begin r_thr <= wdata[7:0]; r_ethr <= wdata[26:16]; end
                6'h02: begin r_x0 <= wdata[10:0]; r_x1 <= wdata[26:16]; end
                6'h03: begin r_y0 <= wdata[10:0]; r_y1 <= wdata[26:16]; end
                6'h04: r_apply <= ~r_apply;
                default: ;
            endcase
        end
    end

    // ======================= pixel side =======================
    // settings: r_* are stable when the APPLY toggle arrives (written before it) -> copied to pending, then to
    // active at the next frame start
    reg [2:0]  ap_sync = 3'd0;
    reg        ap_seen = 1'b0;
    reg [2:0]  p_mode = 3'd0, a_mode = 3'd0;
    reg        p_full = 1'b1, a_full = 1'b1;
    reg [7:0]  p_thr = 8'd128, a_thr = 8'd128;
    reg [10:0] p_ethr = 11'd200, a_ethr = 11'd200;
    reg [10:0] p_x0 = 11'd0, p_x1 = WIDTH - 1, p_y0 = 11'd0, p_y1 = HEIGHT - 1;
    reg [10:0] a_x0 = 11'd0, a_x1 = WIDTH - 1, a_y0 = 11'd0, a_y1 = HEIGHT - 1;

    reg        tuser_d = 1'b0;
    wire       sof = s_tuser & ~tuser_d;
    reg [8:0]  bx = 9'd0;               // beat within the row (WIDTH <= 2048)
    reg [10:0] row = 11'd0;
    wire [8:0]  cur_bx  = sof ? 9'd0 : bx;
    wire [10:0] cur_row = sof ? 11'd0 : row;
    // output source: decided by the NEW mode on the frame-start clock itself, so that frame's start pulse is passed
    // exactly once (bypass: now; filters: when it comes out of the pipeline)
    wire bypass = sof ? (p_mode == 3'd0 || p_mode > 3'd5) : (a_mode == 3'd0 || a_mode > 3'd5);

    // stage 0 -> 1: gray of the 4 pixels, row-buffer read
    function [7:0] gray_of(input [31:0] p);
        reg [16:0] s;
        begin
            s = 17'd77 * p[23:16] + 17'd150 * p[15:8] + 17'd29 * p[7:0];
            gray_of = s[15:8];
        end
    endfunction

    reg [63:0]  lb [0:BEATS-1];         // {row y-2, row y-1} x 4 pixels per beat
    reg [63:0]  lb_rd = 64'd0;
    reg [31:0]  g1 = 32'd0;             // gray of the current beat (row y)
    reg [8:0]   bx1 = 9'd0;
    reg [10:0]  row1 = 11'd0;
    reg         v1 = 1'b0, u1 = 1'b0, l1 = 1'b0;

    // stage 1 -> 2: six window columns (top / mid / bottom)
    reg [7:0]   pv_t2 = 8'd0, pv_t3 = 8'd0, pv_m2 = 8'd0, pv_m3 = 8'd0, pv_b2 = 8'd0, pv_b3 = 8'd0;
    reg [47:0]  c2_t = 48'd0, c2_m = 48'd0, c2_b = 48'd0;   // column k in [8k+7:8k], k = 0..5
    reg [8:0]   bx2 = 9'd0;
    reg [10:0]  row2 = 11'd0;
    reg         v2 = 1'b0, u2 = 1'b0, l2 = 1'b0;

    wire [31:0] cur_t = lb_rd[63:32], cur_m = lb_rd[31:0], cur_b = g1;
    wire        lb_we = v1;

    // stage 2a: vertical 1-2-1 column sums S[k] (k = 0..5), horizontal 1-2-1 sums of the top / bottom rows per pixel
    reg [59:0]  sa_s = 60'd0;               // S[k] = t + 2m + b, 10 bits each
    reg [39:0]  sa_th = 40'd0, sa_bh = 40'd0;
    reg [31:0]  sa_c = 32'd0;               // centre pixels (column j+1 of the middle row)
    reg [8:0]   bxa = 9'd0;
    reg [10:0]  rowa = 11'd0;
    reg         va = 1'b0, ua = 1'b0, la = 1'b0;
    // stage 2b: Gaussian sum (S[j] + 2 S[j+1] + S[j+2]), Gx = S[j+2] - S[j], Gy = bottom - top
    reg [47:0]  sb_g = 48'd0;               // 12 bits each
    reg [47:0]  sb_gx = 48'd0, sb_gy = 48'd0;   // signed 12 bits each
    reg [31:0]  sb_c = 32'd0;
    reg [8:0]   bxb = 9'd0;
    reg [10:0]  rowb = 11'd0;
    reg         vb = 1'b0, ub = 1'b0, lb_ = 1'b0;

    // stage 2c -> 3: filter results per output pixel j (centre = column j+1 of the window)
    reg [31:0]  gc3 = 32'd0, ga3 = 32'd0;   // centre gray, Gaussian
    reg [43:0]  mg3 = 44'd0;                // Sobel magnitude, 11 bits per pixel
    reg [8:0]   bx3 = 9'd0;
    reg [10:0]  row3 = 11'd0;
    reg         v3 = 1'b0, u3 = 1'b0, l3 = 1'b0;

    // stage 3 -> 4: statistics inputs
    reg [3:0]   sel4 = 4'd0, esel4 = 4'd0;
    reg [43:0]  c4 = 44'd0;                 // x of the 4 output pixels, 11 bits each
    reg [10:0]  r4 = 11'd0;

    // accumulators and published results
    reg [20:0]  acc_area = 21'd0, acc_edges = 21'd0;
    reg [31:0]  acc_sx = 32'd0, acc_sy = 32'd0, cyc = 32'd0;
    reg [10:0]  acc_x0 = 11'h7FF, acc_x1 = 11'd0, acc_y0 = 11'h7FF, acc_y1 = 11'd0;
    reg [20:0]  res_area = 21'd0, res_edges = 21'd0;
    reg [31:0]  res_sx = 32'd0, res_sy = 32'd0, res_cyc = 32'd0;
    reg [10:0]  res_x0 = 11'h7FF, res_x1 = 11'd0, res_y0 = 11'h7FF, res_y1 = 11'd0;
    reg         res_tgl = 1'b0;

    integer j, k;
    reg [7:0]  t0, t1, t2, m1, b0, b1, b2;
    reg [9:0]  s0, s1, s2;
    reg signed [11:0] gx, gy;
    reg [10:0] ax, ay;
    reg [11:0] mag;
    reg [7:0]  ov;
    reg [10:0] cx;
    reg        inroi;
    reg [2:0]  n_sel;
    reg [12:0] sx_beat;
    reg [10:0] bmin, bmax;

    always @(posedge pclk) begin
        // ---- settings
        ap_sync <= {ap_sync[1:0], r_apply};
        if (ap_sync[2] != ap_seen) begin
            ap_seen <= ap_sync[2];
            p_mode <= r_mode; p_full <= r_full; p_thr <= r_thr; p_ethr <= r_ethr;
            p_x0 <= r_x0; p_x1 <= r_x1; p_y0 <= r_y0; p_y1 <= r_y1;
        end

        // ---- frame start: publish the previous frame, take the pending settings
        tuser_d <= s_tuser;
        cyc <= cyc + 1'b1;
        if (sof) begin
            res_area <= acc_area; res_edges <= acc_edges; res_sx <= acc_sx; res_sy <= acc_sy; res_cyc <= cyc;
            res_x0 <= acc_x0; res_x1 <= acc_x1; res_y0 <= acc_y0; res_y1 <= acc_y1;
            res_tgl <= ~res_tgl;
            cyc <= 32'd1;
            a_mode <= p_mode; a_full <= p_full; a_thr <= p_thr; a_ethr <= p_ethr;
            a_x0 <= p_x0; a_x1 <= p_x1; a_y0 <= p_y0; a_y1 <= p_y1;
        end

        // ---- stage 0: position, gray, row-buffer read
        if (s_tvalid) begin
            bx  <= (cur_bx == BEATS - 1) ? 9'd0 : cur_bx + 1'b1;
            row <= (cur_bx == BEATS - 1) ? cur_row + 1'b1 : cur_row;
            lb_rd <= lb[cur_bx];
        end else if (sof) begin
            bx <= 9'd0;
            row <= 11'd0;
        end
        g1   <= {gray_of(s_tdata[127:96]), gray_of(s_tdata[95:64]), gray_of(s_tdata[63:32]), gray_of(s_tdata[31:0])};
        bx1  <= cur_bx;
        row1 <= cur_row;
        v1   <= s_tvalid;
        u1   <= s_tuser;
        l1   <= s_tlast;

        // ---- stage 1: row buffer write-back, window columns
        if (lb_we) lb[bx1] <= {cur_m, cur_b};
        if (v1) begin
            // columns 0, 1 = pixels 2, 3 of the previous beat (the current pixel 0 at a row start)
            c2_t <= {cur_t, (bx1 == 9'd0) ? {cur_t[7:0], cur_t[7:0]} : {pv_t3, pv_t2}};
            c2_m <= {cur_m, (bx1 == 9'd0) ? {cur_m[7:0], cur_m[7:0]} : {pv_m3, pv_m2}};
            c2_b <= {cur_b, (bx1 == 9'd0) ? {cur_b[7:0], cur_b[7:0]} : {pv_b3, pv_b2}};
            pv_t2 <= cur_t[23:16]; pv_t3 <= cur_t[31:24];
            pv_m2 <= cur_m[23:16]; pv_m3 <= cur_m[31:24];
            pv_b2 <= cur_b[23:16]; pv_b3 <= cur_b[31:24];
        end
        bx2 <= bx1; row2 <= row1; v2 <= v1; u2 <= u1; l2 <= l1;

        // ---- stage 2a: 1-2-1 sums (column sums over the three rows, row sums of the top / bottom rows)
        for (k = 0; k < 6; k = k + 1)
            sa_s[10*k +: 10] <= c2_t[8*k +: 8] + {c2_m[8*k +: 8], 1'b0} + c2_b[8*k +: 8];
        for (j = 0; j < 4; j = j + 1) begin
            t0 = c2_t[8*j +: 8]; t1 = c2_t[8*(j+1) +: 8]; t2 = c2_t[8*(j+2) +: 8];
            b0 = c2_b[8*j +: 8]; b1 = c2_b[8*(j+1) +: 8]; b2 = c2_b[8*(j+2) +: 8];
            sa_th[10*j +: 10] <= t0 + {t1, 1'b0} + t2;
            sa_bh[10*j +: 10] <= b0 + {b1, 1'b0} + b2;
            sa_c[8*j +: 8]    <= c2_m[8*(j+1) +: 8];
        end
        bxa <= bx2; rowa <= row2; va <= v2; ua <= u2; la <= l2;

        // ---- stage 2b: Gaussian sum, Gx, Gy
        for (j = 0; j < 4; j = j + 1) begin
            s0 = sa_s[10*j +: 10]; s1 = sa_s[10*(j+1) +: 10]; s2 = sa_s[10*(j+2) +: 10];
            sb_g[12*j +: 12]  <= s0 + {s1, 1'b0} + s2;
            sb_gx[12*j +: 12] <= $signed({2'd0, s2}) - $signed({2'd0, s0});
            sb_gy[12*j +: 12] <= $signed({2'd0, sa_bh[10*j +: 10]}) - $signed({2'd0, sa_th[10*j +: 10]});
        end
        sb_c <= sa_c;
        bxb <= bxa; rowb <= rowa; vb <= va; ub <= ua; lb_ <= la;

        // ---- stage 2c: |Gx| + |Gy|, Gaussian / 16
        for (j = 0; j < 4; j = j + 1) begin
            gx = sb_gx[12*j +: 12];
            gy = sb_gy[12*j +: 12];
            ax = gx[11] ? -gx : gx;
            ay = gy[11] ? -gy : gy;
            mag = ax + ay;
            gc3[8*j +: 8]   <= sb_c[8*j +: 8];
            ga3[8*j +: 8]   <= sb_g[12*j + 4 +: 8];
            mg3[11*j +: 11] <= mag[10:0];
        end
        bx3 <= bxb; row3 <= rowb; v3 <= vb; u3 <= ub; l3 <= lb_;

        // ---- stage 3: output (camera bypass or filtered pixels), statistics inputs
        m_tvalid <= bypass ? s_tvalid : v3;
        m_tuser  <= bypass ? s_tuser  : u3;
        m_tlast  <= bypass ? s_tlast  : l3;
        for (j = 0; j < 4; j = j + 1) begin
            case (a_mode)
                3'd1: ov = gc3[8*j +: 8];
                3'd2: ov = ga3[8*j +: 8];
                3'd3: ov = (ga3[8*j +: 8] >= a_thr) ? 8'hFF : 8'h00;
                3'd4: ov = (mg3[11*j +: 11] > 11'd255) ? 8'hFF : mg3[11*j + 7 -: 8];
                3'd5: ov = (mg3[11*j +: 11] >= a_ethr) ? 8'hFF : 8'h00;
                default: ov = 8'd0;
            endcase
            m_tdata[32*j +: 32] <= bypass ? s_tdata[32*j +: 32] : {8'd0, ov, ov, ov};
            cx = {bx3, 2'b00} + j - 1;                      // x of the result (one left of the window's last column)
            inroi = a_full || (cx >= a_x0 && cx <= a_x1 && row3 - 1'b1 >= a_y0 && row3 - 1'b1 <= a_y1);
            inroi = inroi && !(bx3 == 9'd0 && j == 0) && row3 != 11'd0;   // x = -1 / y = -1 do not exist
            sel4[j]  <= v3 && inroi && (ga3[8*j +: 8] >= a_thr);
            esel4[j] <= v3 && inroi && (mg3[11*j +: 11] >= a_ethr);
            c4[11*j +: 11] <= cx;
        end
        r4 <= row3 - 1'b1;

        // ---- stage 4: accumulate (cleared at frame start)
        n_sel = sel4[0] + sel4[1] + sel4[2] + sel4[3];
        sx_beat = (sel4[0] ? c4[10:0] : 13'd0) + (sel4[1] ? c4[21:11] : 13'd0) + (sel4[2] ? c4[32:22] : 13'd0)
                + (sel4[3] ? c4[43:33] : 13'd0);
        bmin = sel4[0] ? c4[10:0] : sel4[1] ? c4[21:11] : sel4[2] ? c4[32:22] : c4[43:33];
        bmax = sel4[3] ? c4[43:33] : sel4[2] ? c4[32:22] : sel4[1] ? c4[21:11] : c4[10:0];
        if (sof) begin
            acc_area <= 21'd0; acc_edges <= 21'd0; acc_sx <= 32'd0; acc_sy <= 32'd0;
            acc_x0 <= 11'h7FF; acc_x1 <= 11'd0; acc_y0 <= 11'h7FF; acc_y1 <= 11'd0;
        end else begin
            acc_area  <= acc_area + n_sel;
            acc_edges <= acc_edges + esel4[0] + esel4[1] + esel4[2] + esel4[3];
            acc_sx    <= acc_sx + sx_beat;
            acc_sy    <= acc_sy + n_sel * r4;
            if (n_sel != 3'd0) begin
                if (bmin < acc_x0) acc_x0 <= bmin;
                if (bmax > acc_x1) acc_x1 <= bmax;
                if (r4 < acc_y0) acc_y0 <= r4;
                if (r4 > acc_y1) acc_y1 <= r4;
            end
        end
    end

    // ======================= results to the register side =======================
    reg [2:0]  res_sync = 3'd0;
    reg [31:0] o_seq = 32'd0, o_area = 32'd0, o_bx = 32'd0, o_by = 32'd0, o_sx = 32'd0, o_sy = 32'd0;
    reg [31:0] o_edges = 32'd0, o_cyc = 32'd0, o_active = 32'd0;
    always @(posedge clk) begin
        res_sync <= {res_sync[1:0], res_tgl};
        if (res_sync[2] != res_sync[1]) begin   // res_* are stable for a whole frame after the toggle
            o_seq   <= o_seq + 1'b1;
            o_area  <= {11'd0, res_area};
            o_bx    <= {5'd0, res_x1, 5'd0, res_x0};
            o_by    <= {5'd0, res_y1, 5'd0, res_y0};
            o_sx    <= res_sx;
            o_sy    <= res_sy;
            o_edges <= {11'd0, res_edges};
            o_cyc   <= res_cyc;
            o_active <= {8'd0, a_thr, 7'd0, a_full, 5'd0, a_mode};
        end
        case (raddr[7:2])
            6'h00: rdata <= {23'd0, r_full, 5'd0, r_mode};
            6'h01: rdata <= {5'd0, r_ethr, 8'd0, r_thr};
            6'h02: rdata <= {5'd0, r_x1, 5'd0, r_x0};
            6'h03: rdata <= {5'd0, r_y1, 5'd0, r_y0};
            6'h05: rdata <= 32'h56495331;
            6'h08: rdata <= o_seq;
            6'h09: rdata <= o_area;
            6'h0A: rdata <= o_bx;
            6'h0B: rdata <= o_by;
            6'h0C: rdata <= o_sx;
            6'h0D: rdata <= o_sy;
            6'h0E: rdata <= o_edges;
            6'h0F: rdata <= o_cyc;
            6'h10: rdata <= o_active;
            default: rdata <= 32'hDEADBEEF;
        endcase
    end
endmodule
