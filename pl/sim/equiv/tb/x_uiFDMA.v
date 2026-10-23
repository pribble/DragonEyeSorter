`timescale 1ns/1ps
// x_uiFDMA — uiFDMA.v 对抗性等价回归（verifier-platform 独立验证）
// 参数取 system_top 实际例化值（ID=6 / ADDR=33 / DATA=64 / MAX_BURST=16）。
// 覆盖：写/读通道握手（随机 AW/W/AR/R ready 延迟、随机 BVALID 延迟、BID/BRESP/RRESP 抖动）、
//       多种传输长度（1/2/15/16/17/32/64/255/256/257，跨 burst 拆分与自动地址递增）、
//       连续多次传输（busy 沿）、中途复位/短复位、FDMA 侧 ready/wvalid 背压。
// 逐拍 trace DUT 全部输出（含 AXI 主口全部信号与 x 态）。
module tb_x_uiFDMA;
    localparam IDW = 6, ADW = 33, DAW = 64;

    integer fd;
    integer cyc = 0;
    reg [1023:0] tf;

    reg aclk = 0;
    always #5 aclk = ~aclk;

    reg aresetn = 0;

    // FDMA 用户侧（TB 驱动）
    reg  [ADW-1:0]  f_waddr = 0;
    reg             f_wareq = 0;
    reg  [15:0]     f_wsize = 0;
    reg  [DAW-1:0]  f_wdata = 0;
    reg             f_wvalid = 0;
    reg             f_wready = 0;
    reg  [ADW-1:0]  f_raddr = 0;
    reg             f_rareq = 0;
    reg  [15:0]     f_rsize = 0;
    reg             f_rready = 0;

    // DUT 输出（FDMA 用户侧）
    wire            o_wbusy, o_wvalid, o_rbusy, o_rvalid;
    wire [DAW-1:0]  o_rdata;

    // AXI 主口：DUT 输出
    wire [IDW-1:0]  m_awid, m_wid, m_arid;
    wire [ADW-1:0]  m_awaddr, m_araddr;
    wire [7:0]      m_awlen, m_arlen;
    wire [2:0]      m_awsize, m_arsize;
    wire [1:0]      m_awburst, m_arburst;
    wire            m_awlock, m_arlock, m_awvalid, m_wlast, m_wvalid, m_bready, m_arvalid, m_rready;
    wire [3:0]      m_awcache, m_arcache, m_awqos, m_arqos;
    wire [2:0]      m_awprot, m_arprot;
    wire [DAW-1:0]  m_wdata;
    wire [DAW/8-1:0] m_wstrb;

    // AXI 从机模型（TB 驱动）
    reg             m_awready = 0, m_wready = 0, m_arready = 0;
    reg             m_bvalid = 0, m_rvalid = 0, m_rlast = 0;
    reg [IDW-1:0]   m_bid = 0, m_rid = 0;
    reg [1:0]       m_bresp = 0, m_rresp = 0;
    reg [DAW-1:0]   m_rdata = 0;

    uiFDMA #(
        .M_AXI_ID_WIDTH(IDW), .M_AXI_ADDR_WIDTH(ADW),
        .M_AXI_DATA_WIDTH(DAW), .M_AXI_MAX_BURST_LEN(16)
    ) dut (
        .I_fdma_waddr(f_waddr), .I_fdma_wareq(f_wareq), .I_fdma_wsize(f_wsize),
        .O_fdma_wbusy(o_wbusy),
        .I_fdma_wdata(f_wdata), .O_fdma_wvalid(o_wvalid), .I_fdma_wready(f_wready),
        .I_fdma_raddr(f_raddr), .I_fdma_rareq(f_rareq), .I_fdma_rsize(f_rsize),
        .O_fdma_rbusy(o_rbusy),
        .O_fdma_rdata(o_rdata), .O_fdma_rvalid(o_rvalid), .I_fdma_rready(f_rready),
        .M_AXI_ACLK(aclk), .M_AXI_ARESETN(aresetn),
        .M_AXI_AWID(m_awid), .M_AXI_AWADDR(m_awaddr), .M_AXI_AWLEN(m_awlen),
        .M_AXI_AWSIZE(m_awsize), .M_AXI_AWBURST(m_awburst), .M_AXI_AWLOCK(m_awlock),
        .M_AXI_AWCACHE(m_awcache), .M_AXI_AWPROT(m_awprot), .M_AXI_AWQOS(m_awqos),
        .M_AXI_AWVALID(m_awvalid), .M_AXI_AWREADY(m_awready),
        .M_AXI_WID(m_wid), .M_AXI_WDATA(m_wdata), .M_AXI_WSTRB(m_wstrb),
        .M_AXI_WLAST(m_wlast), .M_AXI_WVALID(m_wvalid), .M_AXI_WREADY(m_wready),
        .M_AXI_BID(m_bid), .M_AXI_BRESP(m_bresp), .M_AXI_BVALID(m_bvalid), .M_AXI_BREADY(m_bready),
        .M_AXI_ARID(m_arid), .M_AXI_ARADDR(m_araddr), .M_AXI_ARLEN(m_arlen),
        .M_AXI_ARSIZE(m_arsize), .M_AXI_ARBURST(m_arburst), .M_AXI_ARLOCK(m_arlock),
        .M_AXI_ARCACHE(m_arcache), .M_AXI_ARPROT(m_arprot), .M_AXI_ARQOS(m_arqos),
        .M_AXI_ARVALID(m_arvalid), .M_AXI_ARREADY(m_arready),
        .M_AXI_RID(m_rid), .M_AXI_RDATA(m_rdata), .M_AXI_RRESP(m_rresp),
        .M_AXI_RLAST(m_rlast), .M_AXI_RVALID(m_rvalid), .M_AXI_RREADY(m_rready)
    );

    // ---------------- AXI 从机模型（随机 ready/BVALID 延迟） ----------------
    reg [7:0]  w_remaining = 8'd0, r_remaining = 8'd0;
    reg [2:0]  aw_delay = 3'd0, ar_delay = 3'd0, b_delay = 3'd0;
    reg [31:0] lfsr  = 32'hA5A5_1234;
    reg [31:0] lfsr2 = 32'hC0FF_EE01;

    always @(posedge aclk) begin
        lfsr  <= {lfsr[30:0],  lfsr[31]  ^ lfsr[21]  ^ lfsr[1]  ^ lfsr[0]};
        lfsr2 <= {lfsr2[30:0], lfsr2[31] ^ lfsr2[28] ^ lfsr2[2] ^ lfsr2[0]};
        if (!aresetn) begin
            m_awready <= 0; m_wready <= 0; m_arready <= 0;
            m_bvalid <= 0; m_rvalid <= 0; m_rlast <= 0;
            w_remaining <= 0; r_remaining <= 0;
            aw_delay <= 0; ar_delay <= 0; b_delay <= 0;
        end else begin
            // ---- 写地址通道：AWREADY 随机延迟 0~3 拍 ----
            if (aw_delay > 0) begin
                aw_delay <= aw_delay - 1; m_awready <= 1;
            end else if (m_awvalid && m_awready) begin
                aw_delay <= {1'b0, lfsr[2:1]};
                m_awready <= 0;
                w_remaining <= m_awlen + 1'b1;
            end else begin
                m_awready <= lfsr[0];
            end
            // ---- 写数据通道：WREADY 随机 ----
            if (w_remaining > 0) begin
                m_wready <= (lfsr2[3:2] != 2'b00);
                if (m_wvalid && m_wready) begin
                    w_remaining <= w_remaining - 1'b1;
                    if (m_wlast) begin
                        w_remaining <= 0;
                        b_delay <= {1'b0, lfsr[5:3]};
                    end
                end
            end else begin
                m_wready <= 0;
            end
            // ---- 写响应：随机延迟后拉 BVALID，BREADY 后撤销 ----
            if (b_delay > 0) begin
                b_delay <= b_delay - 1;
            end else if (w_remaining == 0 && !m_bvalid && (lfsr2[6:4] == 3'b000)) begin
                m_bvalid <= 1;
                m_bid    <= lfsr[IDW+3:4];
                m_bresp  <= lfsr2[8:7];
            end else if (m_bvalid && m_bready) begin
                m_bvalid <= 0;
            end
            // ---- 读地址通道：ARREADY 随机延迟 ----
            if (ar_delay > 0) begin
                ar_delay <= ar_delay - 1; m_arready <= 1;
            end else if (m_arvalid && m_arready) begin
                ar_delay <= {1'b0, lfsr2[10:9]};
                m_arready <= 0;
                r_remaining <= m_arlen + 1'b1;
            end else begin
                m_arready <= lfsr2[11];
            end
            // ---- 读数据：随机 RVALID，RLAST 在最后一拍 ----
            if (r_remaining > 0) begin
                m_rvalid <= (lfsr[10:9] != 2'b00);
                m_rid    <= lfsr[IDW+7:8];
                m_rdata  <= {lfsr2[15:0], lfsr[31:16], lfsr2[31:16], r_remaining[7:0]};
                m_rresp  <= lfsr2[13:12];
                m_rlast  <= (r_remaining == 1);
                if (m_rvalid && m_rready) begin
                    r_remaining <= r_remaining - 1'b1;
                end
            end else begin
                m_rvalid <= 0;
                m_rlast  <= 0;
            end
        end
    end

    // ---------------- FDMA 侧激励 ----------------
    task wait_cycles;
        input integer n;
        integer i;
        begin
            for (i = 0; i < n; i = i + 1) @(posedge aclk);
        end
    endtask

    task do_write;
        input [ADW-1:0] addr;
        input [15:0]    size;
        begin
            @(negedge aclk);
            f_waddr = addr; f_wsize = size; f_wareq = 1'b1;
            @(negedge aclk);
            f_wareq = 1'b0;
            wait_cycles(2);
            while (o_wbusy !== 1'b1) @(posedge aclk);
            while (o_wbusy !== 1'b0) @(posedge aclk);
            wait_cycles(3);
        end
    endtask

    task do_read;
        input [ADW-1:0] addr;
        input [15:0]    size;
        begin
            @(negedge aclk);
            f_raddr = addr; f_rsize = size; f_rareq = 1'b1;
            @(negedge aclk);
            f_rareq = 1'b0;
            wait_cycles(2);
            while (o_rbusy !== 1'b1) @(posedge aclk);
            while (o_rbusy !== 1'b0) @(posedge aclk);
            wait_cycles(3);
        end
    endtask

    // 随机背压：wdata / wvalid / wready / rready 每拍变化
    always @(posedge aclk) begin
        f_wdata  <= {lfsr[15:0], lfsr2[31:16], lfsr[31:16], lfsr2[15:0]};
        f_wvalid <= lfsr[12];
        f_wready <= lfsr[13];
        f_rready <= lfsr2[14];
    end

    initial begin
        if (!$value$plusargs("TRACE=%s", tf)) tf = "trace.txt";
        fd = $fopen(tf, "w");
        if (fd == 0) begin $display("cannot open trace"); $finish; end
    end

    // 逐拍写全部输出（两行/拍，避免格式串过长）
    always @(negedge aclk) begin
        cyc <= cyc + 1;
        $fdisplay(fd, "%0d A %b %b %b %b %h %h %h %h %h %b %b %h %h %h %b", cyc,
                  o_wbusy, o_wvalid, o_rbusy, o_rvalid, o_rdata,
                  m_awid, m_awaddr, m_awlen, m_awsize, m_awburst, m_awlock,
                  m_awcache, m_awprot, m_awqos, m_awvalid);
        $fdisplay(fd, "%0d B %h %h %h %b %b %b %h %h %b %h %h %h %h %b %b %h %h %h %b %b %b %b",
                  cyc, m_wid, m_wdata, m_wstrb, m_wlast, m_wvalid, m_bready,
                  m_bid, m_bresp, m_bvalid,
                  m_arid, m_araddr, m_arlen, m_arsize, m_arburst, m_arlock, m_arcache,
                  m_arprot, m_arqos, m_arvalid, m_rready, m_rlast, m_rvalid, m_rid);
        if (cyc == 12000) begin
            $display("EQUIV_CYCLES %0d", cyc);
            $display("EQUIV_DONE");
            $fclose(fd);
            $finish;
        end
    end

    initial begin
        aresetn = 0;
        repeat (5) @(posedge aclk);
        aresetn = 1;
        wait_cycles(20);
        do_write(33'h0_0000_0000, 16'd1);
        do_write(33'h0_0000_0100, 16'd16);
        do_write(33'h0_0000_0200, 16'd17);
        do_write(33'h0_0000_0300, 16'd32);
        do_write(33'h0_0000_0400, 16'd256);
        do_write(33'h0_0000_0500, 16'd257);
        aresetn = 0; wait_cycles(3); aresetn = 1; wait_cycles(10);   // 中途复位
        do_write(33'h0_0000_0600, 16'd15);
        do_read (33'h0_0000_0000, 16'd1);
        do_read (33'h0_0000_0200, 16'd16);
        do_read (33'h0_0000_0300, 16'd17);
        do_read (33'h0_0000_0400, 16'd64);
        aresetn = 0; wait_cycles(2); aresetn = 1; wait_cycles(8);     // 短复位
        do_read (33'h0_0000_0500, 16'd32);
        do_write(33'h0_0000_0700, 16'd255);
        do_write(33'h0_0000_0800, 16'd2);
        repeat (3000) @(posedge aclk);
    end
endmodule
