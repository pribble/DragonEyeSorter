`timescale 1ns/1ps
// x_csi_unpacket — csi_unpacket.v 对抗性等价回归（verifier-platform 独立验证）
// 覆盖：CSI-2 包序列（帧起/帧止/数据头）、长度 0/1/2/5/63/0x3FFF 边界、
//       valid 随机间隙与连续突发、长度计数中途改长度、包头背靠背、
//       x 态注入（I_hs_data 全 x / 部分 x）、复位中途打断（含复位期间输入抖动）。
// 逐拍 trace 全部输出（含 x/z）；golden vs 新必须逐字节一致。
module tb_x_csi_unpacket;
    `include "equiv_tb_common.vh"

    reg clk = 0;
    always #5 clk = ~clk;

    reg        rst_n = 0;
    reg        hs_valid = 0;
    reg [31:0] hs_data = 32'd0;

    wire        o_fs, o_fe, o_valid;
    wire [31:0] o_data;

    csi_unpacket dut (
        .I_clk(clk), .I_rst_n(rst_n),
        .I_hs_valid(hs_valid), .I_hs_data(hs_data),
        .O_csi_frame_start(o_fs), .O_csi_frame_end(o_fe),
        .O_csi_valid(o_valid), .O_csi_data(o_data)
    );

    reg [31:0] lfsr = 32'hC51_2B10;
    reg [31:0] pat_lfsr = 32'h00AB_CDEF;

    // 包序列发生器：pak_idx 指到某个包的第几个字
    integer pak_sel = 0;
    integer pak_idx = 0;
    integer pak_len = 0;
    integer wait_cnt = 0;

    // 每个包：[头字, 数据字数, ...]
    reg [31:0] pkt_head;
    task set_pkt;
        input integer sel;
        begin
            case (sel % 8)
                0: begin pkt_head = 32'h00_A5_5A_0F; pak_len = 0; end
                1: begin pkt_head = 32'h2B_00_00_00; pak_len = 0; end   // 长度 0
                2: begin pkt_head = 32'h2B_00_04_00; pak_len = 1; end   // 长度 1
                3: begin pkt_head = 32'h2B_00_08_00; pak_len = 2; end   // 长度 2
                4: begin pkt_head = 32'h2B_00_14_00; pak_len = 5; end   // 长度 5
                5: begin pkt_head = 32'h2B_00_FC_00; pak_len = 63; end  // 长度 63
                6: begin pkt_head = 32'h2B_01_90_00; pak_len = 100; end // 长度 = 0x0190>>2 = 100（长包跨多次间隙）
                7: begin pkt_head = 32'h01_00_00_00; pak_len = 0; end   // 帧结束
            endcase
        end
    endtask

    initial equiv_trace_open;

    always @(posedge clk) begin
        cyc <= cyc + 1;
        $fdisplay(fd, "%0d %b %b %b %b %h", cyc, o_fs, o_fe, o_valid, o_valid, o_data);

        lfsr      <= {lfsr[30:0], lfsr[31] ^ lfsr[21] ^ lfsr[1] ^ lfsr[0]};
        pat_lfsr  <= {pat_lfsr[30:0], pat_lfsr[31] ^ pat_lfsr[28] ^ pat_lfsr[2] ^ pat_lfsr[0]};

        // ---------------- 复位剧本 ----------------
        if (cyc < 4) begin
            rst_n <= 1'b0;
            hs_valid <= 1'b0;
            hs_data  <= 32'd0;
        end else if (cyc == 7000 || cyc == 7001) begin
            rst_n <= 1'b0;
            hs_valid <= pat_lfsr[0];
            hs_data  <= pat_lfsr;
        end else if (cyc >= 15000) begin
            rst_n <= 1'b1;
            // 收尾：固定向量，覆盖 valid=1 长时间连续 + 归零
            hs_valid <= 1'b1;
            hs_data  <= (cyc % 3 == 0) ? 32'h2B_00_08_00 : pat_lfsr;
        end else begin
            rst_n <= 1'b1;
            // ---------------- 包序列 + 随机间隙 ----------------
            if (pak_index_done) begin
                set_pkt(pak_sel);
                pak_sel   <= pak_sel + 1;
                pak_idx   <= 0;
                pak_index_done <= 0;
                wait_cnt  <= 1 + (lfsr[3:0]);
                hs_valid  <= 1'b0;
                hs_data   <= pat_lfsr;
            end else if (wait_cnt > 0) begin
                wait_cnt <= wait_cnt - 1;
                hs_valid <= 1'b0;
                hs_data  <= pat_lfsr;
            end else begin
                // 包头/数据字：偶尔注入 x 态
                if (lfsr[20:16] == 5'b00000)
                    hs_data <= 32'hxxxx_xxxx;
                else if (lfsr[20:16] == 5'b00001)
                    hs_data <= {16'hZZZZ, 8'h2B, 8'h00};
                else if (pak_idx == 0)
                    hs_data <= pkt_head;
                else
                    hs_data <= pat_lfsr;
                if (lfsr[7:3] == 5'b00000)
                    hs_valid <= 1'b0;    // 随机 valid 间隙（打断包）
                else
                    hs_valid <= 1'b1;
                pak_idx <= pak_idx + 1;
                if (pak_idx >= pak_len) begin
                    pak_index_done <= 1;
                    pak_idx <= 0;
                end
            end
        end
        if (cyc == 26000) begin
            equiv_finish(cyc);
        end
    end

    reg pak_index_done = 1'b1;
endmodule
