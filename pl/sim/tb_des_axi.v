`timescale 1ns/1ps
// AXI-Lite test of the fvdma_ctrl window split: vendor regs at 0x0000-0x0FFF, des_regs at 0x1000-0x1FFF
module tb_des_axi;
    reg clk = 0, rstn = 0;
    always #6.667 clk = ~clk;

    reg  [15:0] awaddr = 0, araddr = 0;
    reg         awvalid = 0, wvalid = 0, bready = 0, arvalid = 0, rready = 0;
    reg  [31:0] wdata = 0;
    wire        awready, wready, bvalid, arready, rvalid;
    wire [1:0]  bresp, rresp;
    wire [31:0] rdata, camera_base_addr;
    wire        camera_start, camera_clear;
    wire        des_clk, des_wr, led_ovr, led_val;
    wire [11:0] des_waddr, des_raddr;
    wire [31:0] des_wdata, des_rdata;

    fvdma_ctrl_300e9b49f7b5 #(.C_S_AXI_DATA_WIDTH(32), .C_S_AXI_ADDR_WIDTH(16)) dut (
        .camera_base_addr(camera_base_addr), .camera_start(camera_start), .camera_clear(camera_clear),
        .des_clk(des_clk), .des_wr(des_wr), .des_waddr(des_waddr), .des_wdata(des_wdata),
        .des_raddr(des_raddr), .des_rdata(des_rdata),
        .s_axi_aclk(clk), .s_axi_aresetn(rstn),
        .s_axi_awaddr(awaddr), .s_axi_awprot(3'd0), .s_axi_awvalid(awvalid), .s_axi_awready(awready),
        .s_axi_wdata(wdata), .s_axi_wstrb(4'hF), .s_axi_wvalid(wvalid), .s_axi_wready(wready),
        .s_axi_bresp(bresp), .s_axi_bvalid(bvalid), .s_axi_bready(bready),
        .s_axi_araddr(araddr), .s_axi_arprot(3'd0), .s_axi_arvalid(arvalid), .s_axi_arready(arready),
        .s_axi_rdata(rdata), .s_axi_rresp(rresp), .s_axi_rvalid(rvalid), .s_axi_rready(rready)
    );

    reg sof_toggle = 0, cap_start_toggle = 0, cap_done_toggle = 0;
    reg [31:0] pos = 0, enc = 0;
    wire [31:0] ts;
    des_regs regs (
        .clk(des_clk), .wr(des_wr), .waddr(des_waddr), .wdata(des_wdata),
        .raddr(des_raddr), .rdata(des_rdata), .sof_toggle(sof_toggle),
        .cap_start_toggle(cap_start_toggle), .cap_done_toggle(cap_done_toggle), .pos(pos), .enc(enc), .ts(ts),
        .led_ovr(led_ovr), .led_val(led_val)
    );

    integer errors = 0;
    integer wr_pulses = 0;
    always @(posedge clk) if (des_wr) wr_pulses = wr_pulses + 1;

    task axi_write(input [15:0] a, input [31:0] d, input split);
        begin
            @(posedge clk);
            awaddr <= a; awvalid <= 1; wdata <= d; wvalid <= !split; bready <= 1;
            @(posedge clk);
            while (!(awvalid && awready)) @(posedge clk);
            awvalid <= 0;
            if (split) begin
                @(posedge clk); @(posedge clk);
                wvalid <= 1;
                @(posedge clk);
            end
            wvalid <= 0;
            while (!bvalid) @(posedge clk);
            @(posedge clk); bready <= 0;
        end
    endtask

    task axi_read(input [15:0] a, output [31:0] d);
        begin
            @(posedge clk);
            araddr <= a; arvalid <= 1; rready <= 1;
            @(posedge clk);
            while (!(arvalid && arready)) @(posedge clk);
            arvalid <= 0;
            while (!rvalid) @(posedge clk);
            d = rdata;
            @(posedge clk); rready <= 0;
        end
    endtask

    task check(input [255:0] what, input [31:0] got, input [31:0] exp);
        begin
            if (got !== exp) begin errors = errors + 1; $display("FAIL %0s: got %h exp %h", what, got, exp); end
            else $display("ok   %0s = %h", what, got);
        end
    endtask

    reg [31:0] r, r2;
    initial begin
        repeat (5) @(posedge clk); rstn <= 1; repeat (3) @(posedge clk);

        axi_write(16'h0000, 32'h12345678, 0);
        check("vendor camera_base_addr after write 0x0000", camera_base_addr, 32'h12345678);

        axi_write(16'h1008, 32'hCAFEBABE, 0);
        check("camera_base_addr untouched by 0x1008 write", camera_base_addr, 32'h12345678);
        check("one des_wr pulse per write", wr_pulses, 1);

        axi_read(16'h1000, r);  check("DES_ID @0x1000", r, 32'h44455301);
        axi_read(16'h1004, r);  check("DES_VERSION @0x1004", r, 32'd2);
        axi_read(16'h1008, r);  check("scratch @0x1008", r, 32'hCAFEBABE);
        axi_read(16'h0000, r);  check("vendor slv_reg0 @0x0000", r, 32'h12345678);

        axi_read(16'h100C, r); repeat (10) @(posedge clk); axi_read(16'h100C, r2);
        check("free counter advances", (r2 > r) ? 32'd1 : 32'd0, 32'd1);

        axi_write(16'h1010, 32'h3, 0);
        check("led_ovr", {31'd0, led_ovr}, 1);
        check("led_val", {31'd0, led_val}, 1);

        axi_write(16'h1008, 32'h0BADF00D, 1);
        axi_read(16'h1008, r);  check("scratch via split AW/W write", r, 32'h0BADF00D);
        check("camera_base_addr still untouched", camera_base_addr, 32'h12345678);
        check("des_wr pulses total", wr_pulses, 3);

        axi_write(16'h0004, 32'h1, 0);
        check("vendor camera_start pulse register path", {31'd0, camera_start}, 1);

        // camera start-of-frame counter: 3 toggles 1000 cycles apart
        repeat (3) begin sof_toggle = ~sof_toggle; repeat (1000) @(posedge clk); end
        axi_read(16'h1014, r);  check("FRAME_CNT", r, 3);
        axi_read(16'h101C, r);  check("FRAME_PERIOD ~1000 cycles", (r >= 998 && r <= 1002) ? 1 : 0, 1);

        // capture latch: writer start -> done; values must only change on done
        pos = 100; enc = -7; cap_start_toggle = ~cap_start_toggle; repeat (500) @(posedge clk);
        pos = 150; enc = -77; cap_done_toggle = ~cap_done_toggle;   repeat (10) @(posedge clk);
        axi_read(16'h1020, r);  check("CAP_SEQ after 1st done", r, 1);
        axi_read(16'h1024, r);  check("CAP_FCNT = FRAME_CNT at writer start", r, 3);
        axi_read(16'h1030, r);  check("CAP_POS0 at start", r, 100);
        axi_read(16'h1034, r);  check("CAP_POS1 at done", r, 150);
        axi_read(16'h1038, r);  check("CAP_ENC0 at start (v16)", r, -7);
        axi_read(16'h103C, r);  check("CAP_ENC1 at done (v16)", r, -77);
        axi_read(16'h1028, r2); axi_read(16'h102C, r);
        check("CAP_T1 - CAP_T0 ~500 cycles", (r - r2 >= 498 && r - r2 <= 502) ? 1 : 0, 1);
        pos = 200; enc = 40; cap_start_toggle = ~cap_start_toggle; repeat (50) @(posedge clk);
        axi_read(16'h1030, r);  check("CAP_POS0 stable while next frame is written", r, 100);
        axi_read(16'h1038, r);  check("CAP_ENC0 stable while next frame is written", r, -7);
        pos = 260; enc = 90; cap_done_toggle = ~cap_done_toggle;   repeat (10) @(posedge clk);
        axi_read(16'h1020, r);  check("CAP_SEQ after 2nd done", r, 2);
        axi_read(16'h1030, r);  check("CAP_POS0 of 2nd frame", r, 200);
        axi_read(16'h1034, r);  check("CAP_POS1 of 2nd frame", r, 260);
        axi_read(16'h1038, r);  check("CAP_ENC0 of 2nd frame", r, 40);
        axi_read(16'h103C, r);  check("CAP_ENC1 of 2nd frame", r, 90);

        if (errors == 0) $display("ALL TESTS PASSED"); else $display("%0d TESTS FAILED", errors);
        $finish;
    end
    initial begin #200000; $display("TIMEOUT"); $finish; end
endmodule
