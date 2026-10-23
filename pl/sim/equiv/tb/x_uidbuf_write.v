`timescale 1ns/1ps
// x_uidbuf_write — uidbuf_write.v 对抗性等价回归（verifier-platform 独立验证）
// 依赖真实 fs_cap.v + 真实 wfifo.v（wfifo 是明文生成 IP，未在重构范围内；两侧共用同一份，
// 比行为桩更接近真实 CDC FIFO 语义）。
// 实例：
//   A = system_top 实际参数（W_YSIZE=1080 大帧，只看长窗口内部分行为）
//   B = 缩小参数（8×4 小帧 + W_XDIV=1）→ 可在短仿真内跑完整帧状态机（S_RST→DATA1→DATA2→FRAME_DONE→S_IDLE）
//   C = ENABLE_WRITE=0（generate else 分支，输出全 0）
// 覆盖：FS 同步（fs_cap）、camera_start/clear_intr 握手、FDMA busy 往返、FIFO 水位触发 burst、
//       写指针回绕/O_W_sync_cnt 回绕、irq 60 拍窗口、中途复位与复位时输入抖动。
// 逐拍 trace 三个实例全部输出。
module tb_x_uidbuf_write;
    integer fd;
    integer cyc = 0;
    reg [1023:0] tf;

    reg ui_clk = 0;
    always #5 ui_clk = ~ui_clk;          // 100MHz AXI/寄存器时钟
    reg wclk_a = 0;
    always #15 wclk_a = ~wclk_a;         // 33MHz 写域时钟（异步）
    reg wclk_b = 0;
    always #13 wclk_b = ~wclk_b;

    // 复位
    reg ui_rstn = 0;
    reg w_rst_a = 0, w_rst_b = 0;

    // ---------------- 实例 A：system_top 实际参数 ----------------
    reg         a_wfs = 0;
    reg         a_wren = 0;
    reg  [127:0] a_wdata = 0;
    wire [7:0]  a_sync;
    wire        a_full;
    wire [32:0] a_waddr;
    wire        a_wareq;
    wire [15:0] a_wsize;
    wire [63:0] a_wdata_o;
    wire        a_wready;
    wire [7:0]  a_wbuf_o;
    wire        a_wirq;
    wire        a_cam_done;
    wire        a_start_tgl;
    reg         a_cam_start = 0, a_clear_intr = 0;
    wire [7:0]  a_wbuf_sync;
    reg         a_fdma_busy = 0, a_fdma_wvalid = 0;
    integer     a_busy_cnt = 0;

    uidbuf_write #(
        .VIDEO_ENABLE(1), .ENABLE_WRITE(1), .ENABLE_READ(1),
        .AXI_DATA_WIDTH(64), .AXI_ADDR_WIDTH(33),
        .W_BUFDEPTH(4096), .W_DATAWIDTH(128), .W_DSIZEBITS(25),
        .W_XDIV(1), .W_XSIZE(480), .W_XSTRIDE(480), .W_YSIZE(1080)
    ) dut_a (
        .W_BASEADDR({1'b0, 32'h1000_0000}), .W_BUFSIZE(16'd6),
        .I_ui_clk(ui_clk), .I_ui_rstn(ui_rstn),
        .I_W_clk(wclk_a), .I_W_FS(a_wfs), .I_W_wren(a_wren), .I_W_data(a_wdata),
        .O_W_sync_cnt(a_sync), .I_W_buf(a_wbuf_sync), .O_W_full(a_full),
        .O_fdma_waddr(a_waddr), .O_fdma_wareq(a_wareq), .O_fdma_wsize(a_wsize),
        .I_fdma_wbusy(a_fdma_busy), .O_fdma_wdata(a_wdata_o), .I_fdma_wvalid(a_fdma_wvalid),
        .O_fdma_wready(a_wready), .O_fmda_wbuf(a_wbuf_o), .O_fdma_wirq(a_wirq),
        .camera_start(a_cam_start), .camera_done(a_cam_done), .camera_clear_intr(a_clear_intr),
        .O_W_start_tgl(a_start_tgl)
    );
    assign a_wbuf_sync = a_sync;   // 与 system_top 一致：sync_cnt 回环到 I_W_buf

    // ---------------- 实例 B：缩小参数，能跑完整帧 ----------------
    reg         b_wren = 0;
    reg  [31:0] b_wdata = 0;
    wire [7:0]  b_sync;
    wire        b_full;
    wire [15:0] b_waddr;
    wire        b_wareq;
    wire [15:0] b_wsize;
    wire [31:0] b_wdata_o;
    wire        b_wready;
    wire [7:0]  b_wbuf_o;
    wire        b_wirq;
    wire        b_cam_done;
    wire        b_start_tgl;
    reg         b_cam_start = 0, b_clear_intr = 0;
    wire [7:0]  b_wbuf_sync;
    reg         b_fdma_busy = 0, b_fdma_wvalid = 0;
    integer     b_busy_cnt = 0;
    reg [7:0]  a_wfs_cnt = 8'd0;
    reg [7:0]  b_fs_raw = 0;

    uidbuf_write #(
        .VIDEO_ENABLE(1), .ENABLE_WRITE(1), .ENABLE_READ(1),
        .AXI_DATA_WIDTH(32), .AXI_ADDR_WIDTH(16),
        .W_BUFDEPTH(64), .W_DATAWIDTH(32), .W_DSIZEBITS(12),
        .W_XDIV(1), .W_XSIZE(8), .W_XSTRIDE(8), .W_YSIZE(4)
    ) dut_b (
        .W_BASEADDR(16'h4000), .W_BUFSIZE(16'd3),
        .I_ui_clk(ui_clk), .I_ui_rstn(ui_rstn),
        .I_W_clk(wclk_b), .I_W_FS(b_fs_raw[0]), .I_W_wren(b_wren), .I_W_data(b_wdata),
        .O_W_sync_cnt(b_sync), .I_W_buf(b_wbuf_sync), .O_W_full(b_full),
        .O_fdma_waddr(b_waddr), .O_fdma_wareq(b_wareq), .O_fdma_wsize(b_wsize),
        .I_fdma_wbusy(b_fdma_busy), .O_fdma_wdata(b_wdata_o), .I_fdma_wvalid(b_fdma_wvalid),
        .O_fdma_wready(b_wready), .O_fmda_wbuf(b_wbuf_o), .O_fdma_wirq(b_wirq),
        .camera_start(b_cam_start), .camera_done(b_cam_done), .camera_clear_intr(b_clear_intr),
        .O_W_start_tgl(b_start_tgl)
    );
    assign b_wbuf_sync = b_sync;

    // ---------------- 实例 C：ENABLE_WRITE=0 分支 ----------------
    wire [31:0] c_waddr, c_wdata_o;
    wire        c_wareq, c_wready, c_wirq, c_full, c_cam_done, c_start_tgl;
    wire [15:0] c_wsize;
    wire [7:0]  c_sync, c_wbuf_o;

    uidbuf_write #(
        .VIDEO_ENABLE(1), .ENABLE_WRITE(0), .ENABLE_READ(1),
        .AXI_DATA_WIDTH(32), .AXI_ADDR_WIDTH(16),
        .W_BUFDEPTH(64), .W_DATAWIDTH(32),
        .W_XSIZE(8), .W_XSTRIDE(8), .W_YSIZE(4)
    ) dut_c (
        .W_BASEADDR(16'h8000), .W_BUFSIZE(16'd2),
        .I_ui_clk(ui_clk), .I_ui_rstn(ui_rstn),
        .I_W_clk(wclk_b), .I_W_FS(1'b1), .I_W_wren(1'b0), .I_W_data(32'd0),
        .O_W_sync_cnt(c_sync), .I_W_buf(c_sync), .O_W_full(c_full),
        .O_fdma_waddr(c_waddr), .O_fdma_wareq(c_wareq), .O_fdma_wsize(c_wsize),
        .I_fdma_wbusy(1'b0), .O_fdma_wdata(c_wdata_o), .I_fdma_wvalid(1'b0),
        .O_fdma_wready(c_wready), .O_fmda_wbuf(c_wbuf_o), .O_fdma_wirq(c_wirq),
        .camera_start(1'b0), .camera_done(c_cam_done), .camera_clear_intr(1'b0),
        .O_W_start_tgl(c_start_tgl)
    );

    // ---------------- 随机源 ----------------
    reg [31:0] lfsr = 32'h5A5A_0F0F;
    reg [31:0] lfsr2 = 32'h3C3C_F00F;

    always @(posedge ui_clk) begin
        lfsr  <= {lfsr[30:0],  lfsr[31]  ^ lfsr[21]  ^ lfsr[1]  ^ lfsr[0]};
        lfsr2 <= {lfsr2[30:0], lfsr2[31] ^ lfsr2[28] ^ lfsr2[2] ^ lfsr2[0]};
        if (!ui_rstn) begin
            a_fdma_busy <= 0; a_busy_cnt <= 0; a_fdma_wvalid <= 0;
            b_fdma_busy <= 0; b_busy_cnt <= 0; b_fdma_wvalid <= 0;
            a_cam_start <= 0; b_cam_start <= 0;
            a_clear_intr <= 0; b_clear_intr <= 0;
        end else begin
            // FDMA busy 模型：看到 wareq 后 busy 保持随机 3~18 拍
            if (a_wareq && !a_fdma_busy) begin
                a_fdma_busy <= 1; a_busy_cnt <= 3 + lfsr[3:0];
            end else if (a_fdma_busy) begin
                if (a_busy_cnt == 0) a_fdma_busy <= 0;
                else a_busy_cnt <= a_busy_cnt - 1;
            end
            if (b_wareq && !b_fdma_busy) begin
                b_fdma_busy <= 1; b_busy_cnt <= 3 + lfsr2[3:0];
            end else if (b_fdma_busy) begin
                if (b_busy_cnt == 0) b_fdma_busy <= 0;
                else b_busy_cnt <= b_busy_cnt - 1;
            end
            a_fdma_wvalid <= lfsr[7];
            b_fdma_wvalid <= lfsr2[7];
            a_cam_start   <= (lfsr[12:8] == 5'b00000);
            b_cam_start   <= (lfsr2[12:8] == 5'b00000);
            a_clear_intr  <= a_cam_done & lfsr[13];
            b_clear_intr  <= b_cam_done & lfsr2[13];
        end
    end

    // 写域激励
    always @(posedge wclk_a) begin
        a_wfs_cnt <= (a_wfs_cnt >= 8'd199) ? 8'd0 : a_wfs_cnt + 1'b1;
        if (!w_rst_a) begin
            a_wren <= 0; a_wdata <= 0; a_wfs <= 0;
        end else begin
            a_wren  <= (lfsr[17:15] != 3'b000);           // ~87% 写入
            a_wdata <= {lfsr2[31:0], lfsr[31:0], lfsr2[31:16], lfsr[15:0]};
            a_wfs   <= (a_wfs_cnt == 8'd0) ? 1'b1 : (a_wfs_cnt < 8'd3);
        end
    end
    always @(posedge wclk_b) begin
        if (!w_rst_b) begin
            b_wren <= 0; b_wdata <= 0; b_fs_raw <= 0;
        end else begin
            b_wren  <= (lfsr[20:17] != 4'b0000);
            b_wdata <= {lfsr2[15:0], lfsr[31:16]};
            b_fs_raw <= (b_fs_raw == 8'd0) ? 8'd1 : (b_fs_raw << 1);   // 周期性 FS 脉冲
        end
    end

    initial begin
        if (!$value$plusargs("TRACE=%s", tf)) tf = "trace.txt";
        fd = $fopen(tf, "w");
        if (fd == 0) begin $display("cannot open trace"); $finish; end
    end

    always @(negedge ui_clk) begin
        cyc <= cyc + 1;
        $fdisplay(fd, "%0d A %h %b %h %b %h %h %b %h %b %b %b", cyc,
                  a_sync, a_full, a_waddr, a_wareq, a_wsize, a_wdata_o, a_wready,
                  a_wbuf_o, a_wirq, a_cam_done, a_start_tgl);
        $fdisplay(fd, "%0d B %h %b %h %b %h %h %b %h %b %b %b", cyc,
                  b_sync, b_full, b_waddr, b_wareq, b_wsize, b_wdata_o, b_wready,
                  b_wbuf_o, b_wirq, b_cam_done, b_start_tgl);
        $fdisplay(fd, "%0d C %h %b %h %b %h %h %b %h %b %b", cyc,
                  c_sync, c_full, c_waddr, c_wareq, c_wsize, c_wdata_o, c_wready,
                  c_wbuf_o, c_wirq, c_cam_done);
        if (cyc == 40000) begin
            $display("EQUIV_CYCLES %0d", cyc);
            $display("EQUIV_DONE");
            $fclose(fd);
            $finish;
        end
    end

    initial begin
        ui_rstn = 0; w_rst_a = 0; w_rst_b = 0;
        repeat (5) @(posedge ui_clk);
        ui_rstn = 1; w_rst_a = 1; w_rst_b = 1;
        repeat (9000) @(posedge ui_clk);
        ui_rstn = 0; repeat (4) @(posedge ui_clk); ui_rstn = 1;   // 中途复位
        w_rst_a = 0; repeat (3) @(posedge wclk_a); w_rst_a = 1;
        repeat (12000) @(posedge ui_clk);
        ui_rstn = 0; repeat (1) @(posedge ui_clk); ui_rstn = 1;   // 1 拍复位
        repeat (15000) @(posedge ui_clk);
    end
endmodule
