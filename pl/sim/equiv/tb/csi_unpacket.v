`timescale 1ns/1ps
// csi_unpacket 等价回归：帧开始 / 帧结束 / RAW10 长包头、valid 空档与背靠背、
// 中途复位、随机与极值数据。逐拍 trace 全部输出（x/z 原样）。
module tb_csi_unpacket;
    integer fd;
    integer cyc = 0;
    reg [1023:0] tf;

    reg        clk      = 0;
    reg        rst_n    = 0;
    reg        hs_valid = 0;
    reg [31:0] hs_data  = 0;

    wire        csi_frame_start;
    wire        csi_frame_end;
    wire        csi_valid;
    wire [31:0] csi_data;

    csi_unpacket dut (
        .I_clk             (clk),
        .I_rst_n           (rst_n),
        .I_hs_valid        (hs_valid),
        .I_hs_data         (hs_data),
        .O_csi_frame_start (csi_frame_start),
        .O_csi_frame_end   (csi_frame_end),
        .O_csi_valid       (csi_valid),
        .O_csi_data        (csi_data)
    );

    always #5 clk = ~clk;

    initial begin
        if (!$value$plusargs("TRACE=%s", tf)) tf = "trace.txt";
        fd = $fopen(tf, "w");
        if (fd == 0) begin
            $display("cannot open trace");
            $finish;
        end
    end

    always @(posedge clk) begin
        #1;
        cyc = cyc + 1;
        $fdisplay(fd, "%0d %b %b %b %h",
                  cyc, csi_frame_start, csi_frame_end, csi_valid, csi_data);
    end

    // 送一个 32bit word：valid 保持 hold 拍，然后拉低 gap 拍
    task send;
        input [31:0]  w;
        input integer hold;
        input integer gap;
        integer k;
        begin
            @(negedge clk);
            hs_valid = 1'b1;
            hs_data  = w;
            for (k = 1; k < hold; k = k + 1) @(negedge clk);
            @(negedge clk);
            hs_valid = 1'b0;
            hs_data  = 32'hxxxxxxxx;
            for (k = 0; k < gap; k = k + 1) @(negedge clk);
        end
    endtask

    integer i;
    reg [31:0] rnd;
    integer seed = 32'h5A5A1234;

    initial begin
        rst_n    = 1'b0;
        hs_valid = 1'b0;
        hs_data  = 32'h0000_0000;
        repeat (4) @(negedge clk);
        rst_n = 1'b1;
        repeat (2) @(negedge clk);

        // 1) 帧开始 + 背靠背 RAW10 长包头（length=16 -> S_data_length=4）
        send(32'h00_12_34_56, 1, 1);
        send(32'h2B_10_00_00, 1, 0);
        send(32'hDEAD_BEEF,   1, 0);
        send(32'h1122_3344,   1, 0);
        send(32'h5566_7788,   1, 0);
        send(32'h99AA_BBCC,   1, 0);
        // 2) 帧结束
        send(32'h01_00_00_00, 1, 3);
        // 3) 长包头 length=0（S_data_length=0 边界）
        send(32'h2B_00_00_00, 1, 1);
        send(32'hCAFE_F00D,   1, 1);
        // 4) 长包头 length 极值 0xFFFF -> 0x3FFF
        send(32'h2B_FF_FF_00, 1, 1);
        // 5) valid 连续保持 12 拍（无新上升沿，只有首个 head）
        send(32'h2B_00_10_00, 12, 2);
        // 6) 随机 60 个 word，随机 hold / gap
        for (i = 0; i < 60; i = i + 1) begin
            rnd = $random(seed);
            send(rnd, 1 + (i % 3), i % 4);
        end
        // 7) 中途复位（S_data_en 有异步复位）
        send(32'h2B_00_20_00, 3, 0);
        @(negedge clk);
        rst_n = 1'b0;
        repeat (2) @(negedge clk);
        rst_n = 1'b1;
        send(32'h00_AA_BB_CC, 1, 1);
        send(32'h2B_00_08_00, 2, 1);
        send(32'h2B_00_04_00, 2, 1);
        send(32'h01_55_66_77, 1, 2);
        // 8) 极值 / x 数据
        send(32'hFF_FF_FF_FF, 2, 1);
        send(32'h2B_FF_FF_FF, 2, 1);
        send(32'h00_00_00_00, 1, 1);
        send(32'h01_FF_FF_FF, 1, 1);
        send(32'hxxxx_xxxx, 1, 1);
        repeat (20) @(negedge clk);

        $display("EQUIV_CYCLES %0d", cyc);
        $display("EQUIV_DONE");
        $fclose(fd);
        $finish;
    end
endmodule
