`timescale 1ns/1ps
// uiFDMA 等价回归：AXI4 master 的读写通道 glue。
// 两个被测实例：
//   dut_a = system_top 实际参数（ID 6 / ADDR 33 / DATA 64 / MAX_BURST 16）
//   dut_b = 模块默认参数（ID 3 / ADDR 32 / DATA 128 / MAX_BURST 64）
// 每个实例配一个随机 AXI slave（AWREADY/WREADY/ARREADY 随机延迟与长背压、
// BVALID 在 AW+WLAST 后随机延迟返回并回传 BID/BRESP、R 通道随机气泡/RLAST/RDATA），
// FDMA 侧随机请求、随机 wsize/rsize（含传输中途突变）、写/读 ready 随机背压、中途复位。
// trace 每拍打印两个实例的全部输出（x/z 原样）。
module axi_slave #(
    parameter integer ID_WIDTH   = 6,
    parameter integer ADDR_WIDTH = 32,
    parameter integer DATA_WIDTH = 64,
    parameter integer SEED       = 32'h1234_5678
) (
    input  wire                    clk,
    input  wire                    rstn,
    // master 输出（由 slave 观察）
    input  wire [ID_WIDTH-1:0]     awid,
    input  wire [ADDR_WIDTH-1:0]   awaddr,
    input  wire [7:0]              awlen,
    input  wire [2:0]              awsize,
    input  wire [1:0]              awburst,
    input  wire                    awvalid,
    input  wire [DATA_WIDTH-1:0]   wdata,
    input  wire [DATA_WIDTH/8-1:0] wstrb,
    input  wire                    wlast,
    input  wire                    wvalid,
    input  wire                    bready,
    input  wire [ID_WIDTH-1:0]     arid,
    input  wire [ADDR_WIDTH-1:0]   araddr,
    input  wire [7:0]              arlen,
    input  wire [2:0]              arsize,
    input  wire [1:0]              arburst,
    input  wire                    arvalid,
    input  wire                    rready,
    // slave 输出
    output reg                     awready,
    output reg                     wready,
    output reg  [ID_WIDTH-1:0]     bid,
    output reg  [1:0]              bresp,
    output reg                     bvalid,
    output reg                     arready,
    output reg  [ID_WIDTH-1:0]     rid,
    output reg  [DATA_WIDTH-1:0]   rdata,
    output reg  [1:0]              rresp,
    output reg                     rlast,
    output reg                     rvalid
);
    reg [31:0] rng = SEED;

    function [31:0] rnd;
        input integer dummy;
        begin
            rng = {rng[30:0], rng[31] ^ rng[21] ^ rng[1] ^ rng[0]};
            rnd = rng;
        end
    endfunction

    wire aw_hs     = awvalid & awready;
    wire w_hs      = wvalid  & wready;
    wire wlast_hs  = w_hs & wlast;
    wire ar_hs     = arvalid & arready;
    wire r_hs      = rvalid  & rready;

    // ---- 写地址/响应通道 ----
    reg [4:0] aw_stall;
    reg [4:0] w_stall;
    reg [3:0] bdelay;
    reg [3:0] aw_pending, w_pending;
    reg [ID_WIDTH-1:0] awid_r;

    // ---- 读数据通道 ----
    reg [4:0] ar_stall;
    reg [8:0] r_pending;   // 剩余 R beat 数（ARLEN 最大 255 -> 256）
    reg [3:0] r_gap;
    reg [ID_WIDTH-1:0] arid_r;

    always @(posedge clk) begin
        if (!rstn) begin
            awready   <= 1'b1;
            wready    <= 1'b1;
            bvalid    <= 1'b0;
            bid       <= {ID_WIDTH{1'b0}};
            bresp     <= 2'b00;
            aw_stall  <= 5'd0;
            w_stall   <= 5'd0;
            bdelay    <= 4'd0;
            aw_pending <= 4'd0;
            w_pending  <= 4'd0;
            awid_r    <= {ID_WIDTH{1'b0}};

            arready   <= 1'b1;
            rvalid    <= 1'b0;
            rlast     <= 1'b0;
            rid       <= {ID_WIDTH{1'b0}};
            rresp     <= 2'b00;
            rdata     <= {DATA_WIDTH{1'b0}};
            ar_stall  <= 5'd0;
            r_pending <= 9'd0;
            r_gap     <= 4'd0;
            arid_r    <= {ID_WIDTH{1'b0}};
        end else begin
            // AWREADY：常态 ~70% 高，随机长背压
            if (aw_stall != 5'd0) begin
                awready <= 1'b0;
                aw_stall <= aw_stall - 1'b1;
            end else if (rnd(0) % 9 == 0) begin
                aw_stall <= rnd(0) % 20 + 1;
                awready  <= 1'b0;
            end else begin
                awready <= (rnd(0) % 10 < 7);
            end
            // WREADY：常态 ~65% 高，随机长背压
            if (w_stall != 5'd0) begin
                wready <= 1'b0;
                w_stall <= w_stall - 1'b1;
            end else if (rnd(0) % 8 == 0) begin
                w_stall <= rnd(0) % 24 + 1;
                wready  <= 1'b0;
            end else begin
                wready <= (rnd(0) % 10 < 6);
            end

            if (aw_hs) begin
                awid_r <= awid;
                if (aw_pending != 4'hF) aw_pending <= aw_pending + 1'b1;
            end
            if (wlast_hs) begin
                if (w_pending != 4'hF) w_pending <= w_pending + 1'b1;
            end
            // B 响应：AW 与 WLAST 都到齐后随机延迟，BID 回传、BRESP 随机
            if (!bvalid) begin
                if (bdelay != 4'd0) begin
                    bdelay <= bdelay - 1'b1;
                end else if ((aw_pending != 4'd0) && (w_pending != 4'd0)) begin
                    bvalid    <= 1'b1;
                    bid       <= awid_r;
                    bresp     <= rnd(0);
                    aw_pending <= aw_pending - 1'b1;
                    w_pending  <= w_pending - 1'b1;
                end
            end else if (bready) begin
                bvalid <= 1'b0;
                bdelay <= rnd(0) % 5;
            end

            // ARREADY：常态 ~70% 高，随机长背压
            if (ar_stall != 5'd0) begin
                arready <= 1'b0;
                ar_stall <= ar_stall - 1'b1;
            end else if (rnd(0) % 11 == 0) begin
                ar_stall <= rnd(0) % 16 + 1;
                arready  <= 1'b0;
            end else begin
                arready <= (rnd(0) % 10 < 7);
            end

            // RDATA 每拍抖动（uiFDMA 每拍无条件寄存 M_AXI_RDATA）
            rdata <= {rnd(0), rnd(0), rnd(0), rnd(0)};

            if (ar_hs) begin
                r_pending <= {1'b0, arlen} + 1'b1;
                arid_r    <= arid;
                r_gap     <= rnd(0) % 6;
            end else if (r_hs) begin
                if (r_pending > 9'd1) begin
                    r_pending <= r_pending - 1'b1;
                    r_gap     <= (rnd(0) % 4 == 0) ? (rnd(0) % 4) : 4'd0;
                end else begin
                    r_pending <= 9'd0;
                end
            end

            // R 通道：按 ARLEN 发满，RLAST 落在最后一拍
            if ((r_pending != 9'd0) && (r_gap == 4'd0)) begin
                rvalid <= 1'b1;
                rlast  <= (r_pending == 9'd1);
                rid    <= arid_r;
                rresp  <= rnd(0);
            end else begin
                rvalid <= 1'b0;
                rlast  <= 1'b0;
                if (r_gap != 4'd0) r_gap <= r_gap - 1'b1;
            end
        end
    end
endmodule


module tb_uiFDMA;
    `include "equiv_tb_common.vh"

    reg clk = 0;
    reg aresetn = 0;
    always #5 clk = ~clk;

    // ---------------- 共享的 FDMA 侧激励 ----------------
    reg [32:0]  fdma_waddr;
    reg         fdma_wareq;
    reg [15:0]  fdma_wsize;
    reg [127:0] fdma_wdata;
    reg         fdma_wready;
    reg [32:0]  fdma_raddr;
    reg         fdma_rareq;
    reg [15:0]  fdma_rsize;
    reg         fdma_rready;

    // ---------------- DUT A：system_top 实际参数 ----------------
    wire        a_fdma_wbusy, a_fdma_wvalid, a_fdma_rbusy, a_fdma_rvalid;
    wire [63:0] a_fdma_rdata;
    wire [5:0]  a_awid, a_arid, a_wid, a_bid, a_rid;
    wire [32:0] a_awaddr, a_araddr;
    wire [7:0]  a_awlen, a_arlen;
    wire [2:0]  a_awsize, a_arsize, a_awprot, a_arprot;
    wire [1:0]  a_awburst, a_arburst, a_bresp, a_rresp;
    wire        a_awlock, a_arlock, a_awvalid, a_arvalid;
    wire [3:0]  a_awcache, a_arcache, a_awqos, a_arqos;
    wire [63:0] a_wdata, a_rdata;
    wire [7:0]  a_wstrb;
    wire        a_wlast, a_wvalid, a_bready, a_rready, a_rlast;
    wire        a_awready, a_wready, a_bvalid, a_arready, a_rvalid;

    uiFDMA #(
        .M_AXI_ID_WIDTH(6), .M_AXI_ID(0), .M_AXI_ADDR_WIDTH(33),
        .M_AXI_DATA_WIDTH(64), .M_AXI_MAX_BURST_LEN(16)
    ) dut_a (
        .I_fdma_waddr(fdma_waddr), .I_fdma_wareq(fdma_wareq), .I_fdma_wsize(fdma_wsize),
        .O_fdma_wbusy(a_fdma_wbusy),
        .I_fdma_wdata(fdma_wdata[63:0]), .O_fdma_wvalid(a_fdma_wvalid), .I_fdma_wready(fdma_wready),
        .I_fdma_raddr(fdma_raddr), .I_fdma_rareq(fdma_rareq), .I_fdma_rsize(fdma_rsize),
        .O_fdma_rbusy(a_fdma_rbusy),
        .O_fdma_rdata(a_fdma_rdata), .O_fdma_rvalid(a_fdma_rvalid), .I_fdma_rready(fdma_rready),
        .M_AXI_ACLK(clk), .M_AXI_ARESETN(aresetn),
        .M_AXI_AWID(a_awid), .M_AXI_AWADDR(a_awaddr), .M_AXI_AWLEN(a_awlen),
        .M_AXI_AWSIZE(a_awsize), .M_AXI_AWBURST(a_awburst), .M_AXI_AWLOCK(a_awlock),
        .M_AXI_AWCACHE(a_awcache), .M_AXI_AWPROT(a_awprot), .M_AXI_AWQOS(a_awqos),
        .M_AXI_AWVALID(a_awvalid), .M_AXI_AWREADY(a_awready),
        .M_AXI_WID(a_wid), .M_AXI_WDATA(a_wdata), .M_AXI_WSTRB(a_wstrb),
        .M_AXI_WLAST(a_wlast), .M_AXI_WVALID(a_wvalid), .M_AXI_WREADY(a_wready),
        .M_AXI_BID(a_bid), .M_AXI_BRESP(a_bresp), .M_AXI_BVALID(a_bvalid), .M_AXI_BREADY(a_bready),
        .M_AXI_ARID(a_arid), .M_AXI_ARADDR(a_araddr), .M_AXI_ARLEN(a_arlen),
        .M_AXI_ARSIZE(a_arsize), .M_AXI_ARBURST(a_arburst), .M_AXI_ARLOCK(a_arlock),
        .M_AXI_ARCACHE(a_arcache), .M_AXI_ARPROT(a_arprot), .M_AXI_ARQOS(a_arqos),
        .M_AXI_ARVALID(a_arvalid), .M_AXI_ARREADY(a_arready),
        .M_AXI_RID(a_rid), .M_AXI_RDATA(a_rdata), .M_AXI_RRESP(a_rresp),
        .M_AXI_RLAST(a_rlast), .M_AXI_RVALID(a_rvalid), .M_AXI_RREADY(a_rready)
    );

    axi_slave #(.ID_WIDTH(6), .ADDR_WIDTH(33), .DATA_WIDTH(64), .SEED(32'hA5A5_0001)) slave_a (
        .clk(clk), .rstn(aresetn),
        .awid(a_awid), .awaddr(a_awaddr), .awlen(a_awlen), .awsize(a_awsize),
        .awburst(a_awburst), .awvalid(a_awvalid),
        .wdata(a_wdata), .wstrb(a_wstrb), .wlast(a_wlast), .wvalid(a_wvalid), .bready(a_bready),
        .arid(a_arid), .araddr(a_araddr), .arlen(a_arlen), .arsize(a_arsize),
        .arburst(a_arburst), .arvalid(a_arvalid), .rready(a_rready),
        .awready(a_awready), .wready(a_wready), .bid(a_bid), .bresp(a_bresp), .bvalid(a_bvalid),
        .arready(a_arready), .rid(a_rid), .rdata(a_rdata), .rresp(a_rresp),
        .rlast(a_rlast), .rvalid(a_rvalid)
    );

    // ---------------- DUT B：模块默认参数 ----------------
    wire        b_fdma_wbusy, b_fdma_wvalid, b_fdma_rbusy, b_fdma_rvalid;
    wire [127:0] b_fdma_rdata;
    wire [2:0]  b_awid, b_arid, b_wid, b_bid, b_rid;
    wire [31:0] b_awaddr, b_araddr;
    wire [7:0]  b_awlen, b_arlen;
    wire [2:0]  b_awsize, b_arsize, b_awprot, b_arprot;
    wire [1:0]  b_awburst, b_arburst, b_bresp, b_rresp;
    wire        b_awlock, b_arlock, b_awvalid, b_arvalid;
    wire [3:0]  b_awcache, b_arcache, b_awqos, b_arqos;
    wire [127:0] b_wdata, b_rdata;
    wire [15:0] b_wstrb;
    wire        b_wlast, b_wvalid, b_bready, b_rready, b_rlast;
    wire        b_awready, b_wready, b_bvalid, b_arready, b_rvalid;

    uiFDMA #(
        .M_AXI_ID_WIDTH(3), .M_AXI_ID(0), .M_AXI_ADDR_WIDTH(32),
        .M_AXI_DATA_WIDTH(128), .M_AXI_MAX_BURST_LEN(64)
    ) dut_b (
        .I_fdma_waddr(fdma_waddr[31:0]), .I_fdma_wareq(fdma_wareq), .I_fdma_wsize(fdma_wsize),
        .O_fdma_wbusy(b_fdma_wbusy),
        .I_fdma_wdata(fdma_wdata), .O_fdma_wvalid(b_fdma_wvalid), .I_fdma_wready(fdma_wready),
        .I_fdma_raddr(fdma_raddr[31:0]), .I_fdma_rareq(fdma_rareq), .I_fdma_rsize(fdma_rsize),
        .O_fdma_rbusy(b_fdma_rbusy),
        .O_fdma_rdata(b_fdma_rdata), .O_fdma_rvalid(b_fdma_rvalid), .I_fdma_rready(fdma_rready),
        .M_AXI_ACLK(clk), .M_AXI_ARESETN(aresetn),
        .M_AXI_AWID(b_awid), .M_AXI_AWADDR(b_awaddr), .M_AXI_AWLEN(b_awlen),
        .M_AXI_AWSIZE(b_awsize), .M_AXI_AWBURST(b_awburst), .M_AXI_AWLOCK(b_awlock),
        .M_AXI_AWCACHE(b_awcache), .M_AXI_AWPROT(b_awprot), .M_AXI_AWQOS(b_awqos),
        .M_AXI_AWVALID(b_awvalid), .M_AXI_AWREADY(b_awready),
        .M_AXI_WID(b_wid), .M_AXI_WDATA(b_wdata), .M_AXI_WSTRB(b_wstrb),
        .M_AXI_WLAST(b_wlast), .M_AXI_WVALID(b_wvalid), .M_AXI_WREADY(b_wready),
        .M_AXI_BID(b_bid), .M_AXI_BRESP(b_bresp), .M_AXI_BVALID(b_bvalid), .M_AXI_BREADY(b_bready),
        .M_AXI_ARID(b_arid), .M_AXI_ARADDR(b_araddr), .M_AXI_ARLEN(b_arlen),
        .M_AXI_ARSIZE(b_arsize), .M_AXI_ARBURST(b_arburst), .M_AXI_ARLOCK(b_arlock),
        .M_AXI_ARCACHE(b_arcache), .M_AXI_ARPROT(b_arprot), .M_AXI_ARQOS(b_arqos),
        .M_AXI_ARVALID(b_arvalid), .M_AXI_ARREADY(b_arready),
        .M_AXI_RID(b_rid), .M_AXI_RDATA(b_rdata), .M_AXI_RRESP(b_rresp),
        .M_AXI_RLAST(b_rlast), .M_AXI_RVALID(b_rvalid), .M_AXI_RREADY(b_rready)
    );

    axi_slave #(.ID_WIDTH(3), .ADDR_WIDTH(32), .DATA_WIDTH(128), .SEED(32'h5A5A_0002)) slave_b (
        .clk(clk), .rstn(aresetn),
        .awid(b_awid), .awaddr(b_awaddr), .awlen(b_awlen), .awsize(b_awsize),
        .awburst(b_awburst), .awvalid(b_awvalid),
        .wdata(b_wdata), .wstrb(b_wstrb), .wlast(b_wlast), .wvalid(b_wvalid), .bready(b_bready),
        .arid(b_arid), .araddr(b_araddr), .arlen(b_arlen), .arsize(b_arsize),
        .arburst(b_arburst), .arvalid(b_arvalid), .rready(b_rready),
        .awready(b_awready), .wready(b_wready), .bid(b_bid), .bresp(b_bresp), .bvalid(b_bvalid),
        .arready(b_arready), .rid(b_rid), .rdata(b_rdata), .rresp(b_rresp),
        .rlast(b_rlast), .rvalid(b_rvalid)
    );

    // ---------------- trace：每拍两个实例的全部输出 ----------------
    always @(negedge clk) begin
        cyc <= cyc + 1;
        $fdisplay(fd, "%0d A %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b",
            cyc, a_fdma_wbusy, a_fdma_wvalid, a_fdma_rbusy, a_fdma_rdata, a_fdma_rvalid,
            a_awid, a_awaddr, a_awlen, a_awsize, a_awburst, a_awlock, a_awcache, a_awprot,
            a_awqos, a_awvalid,
            a_wid, a_wdata, a_wstrb, a_wlast, a_wvalid, a_bready,
            a_arid, a_araddr, a_arlen, a_arsize, a_arburst, a_arlock, a_arcache, a_arprot,
            a_arqos, a_arvalid, a_rready);
        $fdisplay(fd, "%0d B %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b",
            cyc, b_fdma_wbusy, b_fdma_wvalid, b_fdma_rbusy, b_fdma_rdata, b_fdma_rvalid,
            b_awid, b_awaddr, b_awlen, b_awsize, b_awburst, b_awlock, b_awcache, b_awprot,
            b_awqos, b_awvalid,
            b_wid, b_wdata, b_wstrb, b_wlast, b_wvalid, b_bready,
            b_arid, b_araddr, b_arlen, b_arsize, b_arburst, b_arlock, b_arcache, b_arprot,
            b_arqos, b_arvalid, b_rready);
    end

    // ---------------- FDMA 侧随机激励 + 复位 ----------------
    reg [31:0] rng = 32'hDEAD_BEEF;
    function [31:0] rnd;
        input integer dummy;
        begin
            rng = {rng[30:0], rng[31] ^ rng[21] ^ rng[1] ^ rng[0]};
            rnd = rng;
        end
    endfunction

    // 覆盖：小尺寸、恰好等于 MAX_BURST、略超、以及多次拆分的大尺寸
    function [15:0] pick_size;
        input [31:0] r;
        begin
            case (r % 24)
                0: pick_size = 16'd1;   1: pick_size = 16'd2;   2: pick_size = 16'd3;
                3: pick_size = 16'd4;   4: pick_size = 16'd5;   5: pick_size = 16'd7;
                6: pick_size = 16'd8;   7: pick_size = 16'd15;  8: pick_size = 16'd16;
                9: pick_size = 16'd17; 10: pick_size = 16'd20; 11: pick_size = 16'd31;
               12: pick_size = 16'd32; 13: pick_size = 16'd33; 14: pick_size = 16'd40;
               15: pick_size = 16'd48; 16: pick_size = 16'd63; 17: pick_size = 16'd64;
               18: pick_size = 16'd65; 19: pick_size = 16'd80; 20: pick_size = 16'd100;
               21: pick_size = 16'd128; 22: pick_size = 16'd129; 23: pick_size = 16'd200;
            endcase
        end
    endfunction

    integer rst_hold = 0;
    integer rst_gap  = 900;
    integer wstall = 0;
    integer rstall = 0;

    always @(negedge clk) begin
        // ---- 周期性复位（含传输中途） ----
        if (rst_hold > 0) begin
            aresetn  <= 1'b0;
            rst_hold <= rst_hold - 1;
            if (rst_hold == 1) rst_gap <= rnd(0) % 1600 + 900;
        end else if (rst_gap == 0) begin
            aresetn  <= 1'b0;
            rst_hold <= rnd(0) % 5 + 1;
        end else begin
            aresetn  <= 1'b1;
            rst_gap  <= rst_gap - 1;
        end

        // ---- 写通道 FDMA 请求：宽度随机、偶发长请求、偶发中途改 wsize/waddr ----
        fdma_wdata <= {rnd(0), rnd(0), rnd(0), rnd(0)};
        if (fdma_wareq) begin
            if (rnd(0) % 3 == 0) fdma_wareq <= 1'b0;
        end else if (rnd(0) % 5 == 0) begin
            fdma_wareq <= 1'b1;
            fdma_waddr <= {rnd(0), rnd(0)};
            fdma_wsize <= pick_size(rnd(0));
        end
        if (fdma_wareq && (rnd(0) % 23 == 0)) fdma_wsize <= pick_size(rnd(0));
        if (rnd(0) % 29 == 0) fdma_waddr <= {rnd(0), rnd(0)};

        // ---- 写 ready 背压 ----
        if (wstall > 0) begin
            fdma_wready <= 1'b0;
            wstall <= wstall - 1;
        end else if (rnd(0) % 11 == 0) begin
            wstall <= rnd(0) % 14 + 1;
            fdma_wready <= 1'b0;
        end else begin
            fdma_wready <= (rnd(0) % 10 < 8);
        end

        // ---- 读通道 FDMA 请求 ----
        if (fdma_rareq) begin
            if (rnd(0) % 3 == 0) fdma_rareq <= 1'b0;
        end else if (rnd(0) % 6 == 0) begin
            fdma_rareq <= 1'b1;
            fdma_raddr <= {rnd(0), rnd(0)};
            fdma_rsize <= pick_size(rnd(0));
        end
        if (fdma_rareq && (rnd(0) % 19 == 0)) fdma_rsize <= pick_size(rnd(0));
        if (rnd(0) % 31 == 0) fdma_raddr <= {rnd(0), rnd(0)};

        // ---- 读 ready 背压 ----
        if (rstall > 0) begin
            fdma_rready <= 1'b0;
            rstall <= rstall - 1;
        end else if (rnd(0) % 13 == 0) begin
            rstall <= rnd(0) % 12 + 1;
            fdma_rready <= 1'b0;
        end else begin
            fdma_rready <= (rnd(0) % 10 < 8);
        end
    end

    initial begin
        equiv_trace_open;
        // 复位期间输入也保持随机（覆盖复位下的输入无关性）
        fdma_waddr = 0;  fdma_wareq = 0; fdma_wsize = 0; fdma_wdata = 0; fdma_wready = 0;
        fdma_raddr = 0; fdma_rareq = 0; fdma_rsize = 0; fdma_rready = 0;
        aresetn = 0;
        repeat (8) @(negedge clk);
        aresetn = 1;
        repeat (15000) @(negedge clk);
        equiv_finish(cyc);
    end
endmodule
