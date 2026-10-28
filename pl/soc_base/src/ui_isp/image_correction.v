// 出处：MiLianKe（米联客，www.uisrc.com）ISP 参考设计；重构版，接口与逐拍行为不变。
module image_correction #(
    parameter DATA_WIDTH  = 40,
    parameter TDEST_WIDTH = 10
) (
    input  wire                    I_clk, I_rst_n,
    input  wire [DATA_WIDTH-1:0]   I_raw_data,
    input  wire                    I_raw_valid,
    input  wire                    I_raw_frame_start,
    input  wire                    I_raw_frame_end,
    output wire [DATA_WIDTH-1:0]   O_raw_tdata,
    output wire                    O_raw_tlast,
    output reg  [TDEST_WIDTH-1:0]  O_raw_tdest,
    output wire                    O_raw_tvalid,
    output wire                    O_raw_tuser,
    input  wire                    O_raw_tready
);
    reg                    I_raw_tvalid_d;
    reg [DATA_WIDTH-1:0]   I_raw_tdata_r;
    reg [16:0]             cnt;            // 行间空闲计数（位宽/回绕同原版）
    // 行列计数：所有使用处只和常量 0/1/480/481/1081 比较，故行/列计数在越过 1080 行 / 480 列后
    // 「饱和保持」与「继续递增」对外部输出完全一致（所有比较结果都已是常量），据此收窄到 $clog2 位宽。
    reg [8:0]              h_cnt;          // 0..481（饱和）
    reg [10:0]             v_cnt;          // 0..1081（饱和）
    wire                   V_valid, H_valid, tuser, last;

    assign V_valid = (v_cnt > 0) && (v_cnt < 1081);
    assign H_valid = (h_cnt < 481) && (h_cnt > 0);
    assign tuser   = (h_cnt == 1) && (v_cnt == 1) && I_raw_tvalid_d;
    assign last    = (cnt == 10);

    // 原 3 个 always（输入打拍 / 行间空闲计数 / 行列计数）同复位、同时钟、全非阻塞，合并后逐拍等价
    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            I_raw_tvalid_d <= 1'b0;
            I_raw_tdata_r  <= {DATA_WIDTH{1'b0}};
            cnt <= 17'd0;  h_cnt <= 9'd0;  v_cnt <= 11'd0;
        end else begin
            I_raw_tvalid_d <= I_raw_valid;
            I_raw_tdata_r  <= I_raw_data;
            // 行间空闲计数：valid 清 0，否则加 1；保留 else if(!x) 写法以维持 x 输入时保持的语义
            if (I_raw_valid)        cnt <= 17'd0;
            else if (!I_raw_valid)  cnt <= cnt + 17'd1;
            // 列/行计数：帧头与行尾清零，行尾使 v_cnt 加 1（越界后饱和保持，见上）
            if (I_raw_frame_start)     begin h_cnt <= 9'd0; v_cnt <= 11'd0; end
            else if (last)             begin h_cnt <= 9'd0; v_cnt <= (v_cnt == 11'd1081) ? 11'd1081 : v_cnt + 11'd1; end
            else if (I_raw_tvalid_d)   h_cnt <= (h_cnt == 9'd481) ? 9'd481 : h_cnt + 9'd1;
        end
    end

    assign O_raw_tdata  = V_valid && H_valid ? I_raw_tdata_r : {DATA_WIDTH{1'b0}};
    assign O_raw_tvalid = H_valid && V_valid && I_raw_tvalid_d;
    assign O_raw_tlast  = V_valid && (h_cnt == 480) && I_raw_tvalid_d;
    assign O_raw_tuser  = tuser;
    // O_raw_tdest 原版即未驱动（保持 x），接口不变故保留
endmodule
