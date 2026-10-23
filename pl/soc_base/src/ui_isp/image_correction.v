// 出处：MiLianKe（米联客，www.uisrc.com）ISP 参考设计；重构版，接口与逐拍行为不变。
module image_correction #(
    parameter DATA_WIDTH  = 40,
    parameter TDEST_WIDTH = 10
)(
    input                          I_clk            ,
    input                          I_rst_n          ,
    input    [DATA_WIDTH - 1:0]    I_raw_data       ,
    input                          I_raw_valid      ,
    input                          I_raw_frame_start,
    input                          I_raw_frame_end  ,
    output   [DATA_WIDTH - 1:0]    O_raw_tdata      ,
    output                         O_raw_tlast      ,
    output reg [TDEST_WIDTH - 1:0] O_raw_tdest      ,
    output                         O_raw_tvalid     ,
    output                         O_raw_tuser      ,
    input                          O_raw_tready
);
    reg                       I_raw_tvalid_d;
    reg    [DATA_WIDTH - 1:0] I_raw_tdata_r;
    reg    [16:0]             cnt;            // 行间空闲计数（位宽/回绕同原版）
    reg    [14:0]             h_cnt, v_cnt;   // 行列计数（位宽/回绕同原版）
    wire                      V_valid, H_valid, tuser, last;

    assign V_valid = (v_cnt > 0) && (v_cnt < 1081);
    assign H_valid = (h_cnt < 481) && (h_cnt > 0);
    assign tuser   = (h_cnt == 1) && (v_cnt == 1) && I_raw_tvalid_d;
    assign last    = (cnt == 10);

    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            I_raw_tvalid_d <= 1'b0;
            I_raw_tdata_r  <= {DATA_WIDTH{1'b0}};
        end else begin
            I_raw_tvalid_d <= I_raw_valid;
            I_raw_tdata_r  <= I_raw_data;
        end
    end

    // 行间空闲计数：valid 清 0，否则加 1；保留 else if(!x) 写法以维持 x 语义
    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) cnt <= 17'd0;
        else if (I_raw_valid) cnt <= 17'd0;
        else if (!I_raw_valid) cnt <= cnt + 17'd1;
    end

    // 列/行计数：帧头与行尾清零，行尾使 v_cnt 加 1
    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n || I_raw_frame_start) begin
            h_cnt <= 15'd0;
            v_cnt <= 15'd0;
        end else if (last) begin
            h_cnt <= 15'd0;
            v_cnt <= v_cnt + 15'd1;
        end else if (I_raw_tvalid_d) begin
            h_cnt <= h_cnt + 15'd1;
        end
    end

    assign O_raw_tdata  = V_valid && H_valid ? I_raw_tdata_r : {DATA_WIDTH{1'b0}};
    assign O_raw_tvalid = H_valid && V_valid && I_raw_tvalid_d;
    assign O_raw_tlast  = V_valid && (h_cnt == 480) && I_raw_tvalid_d;
    assign O_raw_tuser  = tuser;
    // O_raw_tdest 原版即未驱动（保持 x），接口不变故保留
endmodule
