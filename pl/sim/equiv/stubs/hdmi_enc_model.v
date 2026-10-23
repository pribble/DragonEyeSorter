// 行为桩（仅 pl/sim/equiv 等价回归用；golden 与工作区构建共用同一份）：
//   hdmi_tx_controller_wrapper — 全加密 TMDS 编码/时序控制器（uihdmi_tx 内一份）。
// 假设：输出是像素域输入的寄存函数，复位后确定、可复现；不模拟真实 TMDS 编码算法。
// 端口名/方向/位宽取自 uihdmi_tx.v 的例化（加密文件无明文模块头）。
// 注意：每个通道用不同的输入位/计数器位，便于逐拍发现输入打拍/连线变化。

module hdmi_tx_controller_wrapper (
    input wire       I_pixel_clk,
    input wire       I_rst,

    input wire[23:0] I_video_in_data,
    input wire       I_video_in_vs,
    input wire       I_video_in_hs,
    input wire       I_video_in_de,

    output reg[9:0]  O_ch0_tmds_code_data,
    output reg[9:0]  O_ch1_tmds_code_data,
    output reg[9:0]  O_ch2_tmds_code_data,
    output reg[9:0]  O_clk_tmds_code_data
);
    reg [15:0] r_cnt;

    initial begin
        r_cnt                = 16'h0000;
        O_ch0_tmds_code_data = 10'h2AA;
        O_ch1_tmds_code_data = 10'h155;
        O_ch2_tmds_code_data = 10'h2AA;
        O_clk_tmds_code_data = 10'h3C3;
    end

    always @(posedge I_pixel_clk or posedge I_rst) begin
        if (I_rst) begin
            r_cnt                 <= 16'h0000;
            O_ch0_tmds_code_data  <= 10'h2AA;
            O_ch1_tmds_code_data  <= 10'h155;
            O_ch2_tmds_code_data  <= 10'h2AA;
            O_clk_tmds_code_data  <= 10'h3C3;
        end
        else begin
            r_cnt                <= r_cnt + 1'b1;
            O_ch0_tmds_code_data <= {I_video_in_de, I_video_in_data[7:0], I_video_in_hs};
            O_ch1_tmds_code_data <= {I_video_in_vs, I_video_in_data[15:8], r_cnt[0]};
            O_ch2_tmds_code_data <= {I_video_in_de ^ I_video_in_vs,
                                     I_video_in_data[23:16], r_cnt[1]};
            O_clk_tmds_code_data <= {2'b01, I_video_in_data[7:0] ^ r_cnt[7:0]};
        end
    end
endmodule
