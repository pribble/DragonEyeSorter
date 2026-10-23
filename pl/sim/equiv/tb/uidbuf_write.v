`timescale 1ns/1ps
// uidbuf_write 等价回归：帧写主控（帧起始捕获 / 帧写状态机 / wfifo 读写 / 中断与完成 / 复位）。
// 内部加密模块用真实 wfifo.v（明文 RTL，两侧同一份，见 tests/uidbuf_write.f）。
// 三个实例：
//   u_a = system_top 实际参数（VIDEO=1 / AXI_DATA 64 / ADDR 33 / BUFDEPTH 4096 /
//         W_DATA 128 / XDIV 1 / XSIZE 480 / XSTRIDE 480 / YSIZE 1080 / BUFSIZE 6）
//   u_b = 参数化小尺寸（VIDEO=0 / XDIV 2 / stride≠xsize / YSIZE 8 / 窄地址）
//   u_c = ENABLE_WRITE=0（禁用分支）
// FDMA 侧 busy/wvalid 由 TB 建模（busy 交替驱动状态机走完整个帧），W 侧随机写数据与
// 随机 VS 帧同步，PS 侧 camera_start/camera_clear 闭环 + 随机干扰，运行中随机复位。
// trace 在 ui_clk 与 W_clk 两个时钟沿都采样（tag U/W），逐拍打印全部输出。
module tb_uidbuf_write;
    integer fd;
    integer ucyc = 0;
    integer wcyc = 0;
    reg [1023:0] tf;

    reg ui_clk = 0;
    reg wclk   = 0;
    always #3 ui_clk = ~ui_clk;   // ~166 MHz（与 pll_clk_150 同量级）
    always #5 wclk   = ~wclk;     // 100 MHz（相机写时钟）

    reg rstn = 0;

    // ---------------- 共享用户侧写激励（相机侧） ----------------
    reg         wren  = 0;
    reg [127:0] wdata = 0;
    reg         wfs   = 0;
    // ---------------- 共享 FDMA 侧回读模型 ----------------
    reg busy_a  = 0;
    reg wvld_a  = 0;
    reg busy_b  = 0;
    reg wvld_b  = 0;
    // ---------------- PS 侧控制 ----------------
    reg cam_start = 0;
    reg cam_clear = 0;

    // ================= 实例 A：system_top 实际参数 =================
    wire [7:0]  a_sync_cnt;
    wire        a_full;
    wire [32:0] a_waddr;
    wire        a_wareq;
    wire [15:0] a_wsize;
    wire [63:0] a_wdata;
    wire        a_wready;
    wire [7:0]  a_wbuf;
    wire        a_wirq;
    wire        a_done;
    wire        a_start_tgl;

    uidbuf_write #(
        .VIDEO_ENABLE(1), .ENABLE_WRITE(1), .ENABLE_READ(1),
        .AXI_DATA_WIDTH(64), .AXI_ADDR_WIDTH(33),
        .W_BUFDEPTH(1024*4), .W_DATAWIDTH(128), .W_DSIZEBITS(25),
        .W_XSIZE(480), .W_XSTRIDE(480), .W_YSIZE(1080), .W_XDIV(1)
    ) u_a (
        .W_BASEADDR(33'h1_2345_6000), .W_BUFSIZE(16'd6),
        .I_ui_clk(ui_clk), .I_ui_rstn(rstn),
        .I_W_clk(wclk), .I_W_FS(wfs), .I_W_wren(wren), .I_W_data(wdata),
        .O_W_sync_cnt(a_sync_cnt), .I_W_buf(a_sync_cnt), .O_W_full(a_full),
        .O_fdma_waddr(a_waddr), .O_fdma_wareq(a_wareq), .O_fdma_wsize(a_wsize),
        .I_fdma_wbusy(busy_a), .O_fdma_wdata(a_wdata), .I_fdma_wvalid(wvld_a),
        .O_fdma_wready(a_wready), .O_fmda_wbuf(a_wbuf), .O_fdma_wirq(a_wirq),
        .camera_start(cam_start), .camera_done(a_done),
        .camera_clear_intr(cam_clear), .O_W_start_tgl(a_start_tgl)
    );

    // ================= 实例 B：参数化小尺寸（VIDEO_ENABLE=0） =================
    wire [7:0]  b_sync_cnt;
    wire        b_full;
    wire [23:0] b_waddr;
    wire        b_wareq;
    wire [15:0] b_wsize;
    wire [63:0] b_wdata;
    wire        b_wready;
    wire [7:0]  b_wbuf;
    wire        b_wirq;
    wire        b_done;
    wire        b_start_tgl;

    uidbuf_write #(
        .VIDEO_ENABLE(0), .ENABLE_WRITE(1), .ENABLE_READ(1),
        .AXI_DATA_WIDTH(64), .AXI_ADDR_WIDTH(24),
        .W_BUFDEPTH(64), .W_DATAWIDTH(32), .W_DSIZEBITS(16),
        .W_XSIZE(8), .W_XSTRIDE(12), .W_YSIZE(8), .W_XDIV(2)
    ) u_b (
        .W_BASEADDR(24'h80_0000), .W_BUFSIZE(16'd3),
        .I_ui_clk(ui_clk), .I_ui_rstn(rstn),
        .I_W_clk(wclk), .I_W_FS(wfs), .I_W_wren(wren), .I_W_data(wdata[31:0]),
        .O_W_sync_cnt(b_sync_cnt), .I_W_buf(b_sync_cnt), .O_W_full(b_full),
        .O_fdma_waddr(b_waddr), .O_fdma_wareq(b_wareq), .O_fdma_wsize(b_wsize),
        .I_fdma_wbusy(busy_b), .O_fdma_wdata(b_wdata), .I_fdma_wvalid(wvld_b),
        .O_fdma_wready(b_wready), .O_fmda_wbuf(b_wbuf), .O_fdma_wirq(b_wirq),
        .camera_start(cam_start), .camera_done(b_done),
        .camera_clear_intr(cam_clear), .O_W_start_tgl(b_start_tgl)
    );

    // ================= 实例 C：ENABLE_WRITE=0 =================
    wire [7:0]  c_sync_cnt;
    wire        c_full;
    wire [31:0] c_waddr;
    wire        c_wareq;
    wire [15:0] c_wsize;
    wire [63:0] c_wdata;
    wire        c_wready;
    wire [7:0]  c_wbuf;
    wire        c_wirq;
    wire        c_done;
    wire        c_start_tgl;

    uidbuf_write #(
        .VIDEO_ENABLE(1), .ENABLE_WRITE(0), .ENABLE_READ(1),
        .AXI_DATA_WIDTH(64), .AXI_ADDR_WIDTH(32),
        .W_BUFDEPTH(256), .W_DATAWIDTH(64), .W_DSIZEBITS(24),
        .W_XSIZE(32), .W_XSTRIDE(32), .W_YSIZE(4), .W_XDIV(1)
    ) u_c (
        .W_BASEADDR(32'h1000), .W_BUFSIZE(16'd4),
        .I_ui_clk(ui_clk), .I_ui_rstn(rstn),
        .I_W_clk(wclk), .I_W_FS(wfs), .I_W_wren(wren), .I_W_data(wdata[63:0]),
        .O_W_sync_cnt(c_sync_cnt), .I_W_buf(c_sync_cnt), .O_W_full(c_full),
        .O_fdma_waddr(c_waddr), .O_fdma_wareq(c_wareq), .O_fdma_wsize(c_wsize),
        .I_fdma_wbusy(busy_a), .O_fdma_wdata(c_wdata), .I_fdma_wvalid(wvld_a),
        .O_fdma_wready(c_wready), .O_fmda_wbuf(c_wbuf), .O_fdma_wirq(c_wirq),
        .camera_start(cam_start), .camera_done(c_done),
        .camera_clear_intr(cam_clear), .O_W_start_tgl(c_start_tgl)
    );

    // ---------------- trace：ui_clk / W_clk 两个时钟沿都采样，tag 区分 ----------------
    always @(posedge ui_clk) begin
        ucyc <= ucyc + 1;
        $fdisplay(fd, "%0d U %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b",
            ucyc,
            a_sync_cnt, a_full, a_waddr, a_wareq, a_wsize, a_wdata, a_wready,
            a_wbuf, a_wirq, a_done, a_start_tgl,
            b_sync_cnt, b_full, b_waddr, b_wareq, b_wsize, b_wdata, b_wready,
            b_wbuf, b_wirq, b_done, b_start_tgl,
            c_sync_cnt, c_full, c_waddr, c_wareq, c_wsize, c_wdata, c_wready,
            c_wbuf, c_wirq, c_done, c_start_tgl);
    end

    always @(posedge wclk) begin
        wcyc <= wcyc + 1;
        $fdisplay(fd, "%0d W %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b %b",
            wcyc,
            a_sync_cnt, a_full, a_waddr, a_wareq, a_wsize, a_wdata, a_wready,
            a_wbuf, a_wirq, a_done, a_start_tgl,
            b_sync_cnt, b_full, b_waddr, b_wareq, b_wsize, b_wdata, b_wready,
            b_wbuf, b_wirq, b_done, b_start_tgl,
            c_sync_cnt, c_full, c_waddr, c_wareq, c_wsize, c_wdata, c_wready,
            c_wbuf, c_wirq, c_done, c_start_tgl);
    end

    // ---------------- 确定性 LFSR ----------------
    reg [31:0] rng = 32'h0BAD_F00D;
    function [31:0] rnd;
        input integer dummy;
        begin
            rng = {rng[30:0], rng[31] ^ rng[21] ^ rng[1] ^ rng[0]};
            rnd = rng;
        end
    endfunction

    // ---------------- 相机写侧：随机 wren / 数据 / VS 帧同步 ----------------
    integer fs_gap = 120;
    integer wren_gap = 0;
    integer rst_gap = 4200;
    integer rst_hold = 0;
    // 灌满/排空阶段：长时间只写不读，把 FIFO 顶到 full/afull，再恢复读排空
    reg flood = 1'b0;
    integer ph = 0;

    always @(posedge ui_clk) begin
        ph <= ph + 1;
        flood <= (ph > 2500) && (ph < 16000);
    end

    always @(posedge wclk) begin
        wdata <= {rnd(0), rnd(0), rnd(0), rnd(0)};
        // 写使能：成片突发 + 随机空闲；灌满阶段连续写
        if (flood) begin
            wren <= 1'b1;
        end else if (wren_gap > 0) begin
            wren_gap <= wren_gap - 1;
            wren <= 1'b0;
        end else if (rnd(0) % 6 == 0) begin
            wren_gap <= rnd(0) % 60;
            wren <= 1'b0;
        end else begin
            wren <= (rnd(0) % 10 < 8);
        end
        // VS：正常帧间隔脉冲 + 偶发 1 拍毛刺
        if (fs_gap > 0) begin
            fs_gap <= fs_gap - 1;
            wfs <= 1'b0;
        end else if (rnd(0) % 9 == 0) begin
            // 1 拍毛刺
            wfs <= 1'b1;
            fs_gap <= 1;
        end else begin
            wfs <= 1'b1;
            fs_gap <= rnd(0) % 420 + 90;
        end
    end

    // ---------------- FDMA 侧回读模型 ----------------
    // A：busy 每拍交替，让 S_DATA1/S_DATA2 以最快速度走完每个 burst；
    // B：busy 随机保持，覆盖各种进入/退出时序。wvalid 随机气泡读 FIFO。
    integer bhold_a = 0, bhold_b = 0, wgap_a = 0, wgap_b = 0;

    always @(posedge ui_clk) begin
        if (!rstn) begin
            busy_a <= 1'b0; wvld_a <= 1'b0; bhold_a <= 0; wgap_a <= 0;
            busy_b <= 1'b0; wvld_b <= 1'b0; bhold_b <= 0; wgap_b <= 0;
        end else begin
            // 灌满阶段：实例 A 的 FDMA 不启动新传输（busy=0），写侧把 FIFO 灌满
            if (flood) busy_a <= 1'b0;
            else        busy_a <= ~busy_a;
            if (bhold_b > 0) begin
                bhold_b <= bhold_b - 1;
            end else if (rnd(0) % 5 == 0) begin
                busy_b <= ~busy_b;
                bhold_b <= rnd(0) % 3;
            end

            if (flood) begin
                wvld_a <= 1'b0;
                wgap_a <= 0;
            end else if (wgap_a > 0) begin
                wgap_a <= wgap_a - 1;
                wvld_a <= 1'b0;
            end else if (rnd(0) % 7 == 0) begin
                wgap_a <= rnd(0) % 5;
                wvld_a <= 1'b0;
            end else begin
                wvld_a <= (rnd(0) % 10 < 7);
            end

            if (flood) begin
                wvld_b <= 1'b0;
                wgap_b <= 0;
            end else if (wgap_b > 0) begin
                wgap_b <= wgap_b - 1;
                wvld_b <= 1'b0;
            end else if (rnd(0) % 9 == 0) begin
                wgap_b <= rnd(0) % 7;
                wvld_b <= 1'b0;
            end else begin
                wvld_b <= (rnd(0) % 10 < 6);
            end
        end
    end

    // ---------------- PS 闭环：camera_done -> clear -> start；外加随机干扰 ----------------
    reg a_done_d = 1'b0;
    integer clear_left = 0;
    integer start_dly = 0;
    integer cam_rnd = 0;

    always @(posedge ui_clk) begin
        if (!rstn) begin
            cam_clear <= 1'b0; cam_start <= 1'b0;
            a_done_d <= 1'b0; clear_left <= 0; start_dly <= 0; cam_rnd <= 0;
        end else begin
            a_done_d <= a_done;
            // 帧完成上升沿：2 拍后给 clear 脉冲
            if (a_done & ~a_done_d) begin
                clear_left <= 2;
                start_dly  <= 12 + (rnd(0) % 40);
            end
            if (clear_left > 0) begin
                cam_clear <= 1'b1;
                clear_left <= clear_left - 1;
            end else if (rnd(0) % 300 == 0) begin
                cam_clear <= 1'b1;   // 随机干扰脉冲
            end else begin
                cam_clear <= 1'b0;
            end
            if (start_dly > 0) begin
                start_dly <= start_dly - 1;
                cam_start <= 1'b0;
            end else if (rnd(0) % 500 == 0) begin
                cam_start <= 1'b1;
            end else begin
                cam_start <= 1'b0;
            end
            // 不定期强拉 start（覆盖重复 start / 空闲期）
            if (rnd(0) % 900 == 0) cam_start <= 1'b1;
        end
    end

    // ---------------- 运行中随机复位（异步，作用到两个时钟域） ----------------
    always @(posedge ui_clk) begin
        if (rst_hold > 0) begin
            rstn <= 1'b0;
            rst_hold <= rst_hold - 1;
            if (rst_hold == 1) rst_gap <= rnd(0) % 2600 + 2600;
        end else if (rst_gap == 0) begin
            rstn <= 1'b0;
            rst_hold <= rnd(0) % 4 + 1;
        end else begin
            rstn <= 1'b1;
            rst_gap <= rst_gap - 1;
        end
    end

    initial begin
        if (!$value$plusargs("TRACE=%s", tf)) tf = "trace.txt";
        fd = $fopen(tf, "w");
        if (fd == 0) begin $display("cannot open trace"); $finish; end
        rstn = 0;
        cam_start = 0;
        cam_clear = 0;
        repeat (10) @(posedge ui_clk);
        repeat (24000) @(posedge ui_clk);
        $display("EQUIV_CYCLES %0d", ucyc);
        $display("EQUIV_DONE");
        $fclose(fd);
        $finish;
    end
endmodule
