// 出处：Milianke uisrc ISP demo（ui_isp/demosaic_4x_2_0/zhenghe.v, XiaoQingquan 2024/10）
// 用 BRAM 做行级整合：按行缓存，延迟 data_complete_delay 后整行读出，不影响下一帧输入。
`timescale 1ns / 1ps
module zhenghe #(
    parameter [12:0] IMG_WIDTH  = 640,
    parameter [12:0] IMG_HEIGHT = 480,
    parameter [8:0]  DATA_WIDTH = 96,
    parameter [8:0]  data_complete_delay = 50   // 每行 BRAM 完成数据传输后与这行最后一个数据的距离
) (
    input  wire                  I_clk, I_rst_n,
    input  wire                  I_tlast, I_tuser,
    input  wire [DATA_WIDTH-1:0] I_tdata,
    input  wire                  I_tvalid,
    output reg                   O_tlast, O_tuser,
    output wire [DATA_WIDTH-1:0] O_tdata,
    output reg                   O_tvalid
);

    localparam IMG_WIDTH_4x = IMG_WIDTH >> 2;

    reg  [10:0]           addra, addrb;
    reg                   valid_r0, valid_r1, I_tlast_d0, I_tlast_d1, I_tuser_d0, I_tuser_d1;
    reg  [13:0]           y_cnt;            // 行计数
    reg  [20:0]           x_cnt;            // 行内周期计数
    reg  [DATA_WIDTH-1:0] data_d0;
    reg                   flag_valid_first; // 帧首行有效
    reg  [15:0]           cnt_first;        // 帧首行的行长（周期数）
    reg  [15:0]           valid_width;      // 每行周期数（首行测得）
    reg                   flag_valid_d0, flag_valid_d1;

    wire                  flag_valid = flag_valid_d0 | flag_valid_d1;
    wire [15:0]           rd_valid_width_d0 = valid_width - IMG_WIDTH_4x + data_complete_delay;
    wire [15:0]           rd_valid_width_d1 = valid_width + data_complete_delay + 1;
    wire                  rd_valid = (x_cnt < rd_valid_width_d1) && (x_cnt > rd_valid_width_d0);
    wire [DATA_WIDTH-1:0] data_d;

    // 原 3 个 always（输入打两拍 / 行长测量与控制状态 / 输出流控制打一拍）同复位、同时钟、全非阻塞，合并后逐拍等价
    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            {valid_r0, valid_r1, I_tlast_d0, I_tlast_d1, I_tuser_d0, I_tuser_d1} <= 6'd0;
            data_d0 <= {DATA_WIDTH{1'b0}};
            {flag_valid_first, flag_valid_d0, flag_valid_d1} <= 3'd0;
            {cnt_first, valid_width} <= {16'd0, IMG_WIDTH_4x};
            {y_cnt, x_cnt} <= 0;
            {addra, addrb} <= 22'd0;
            {O_tuser, O_tvalid, O_tlast} <= 3'd0;
        end else begin
            // 输入信号打两拍
            {valid_r0, valid_r1}     <= {I_tvalid, valid_r0};
            {I_tlast_d0, I_tlast_d1} <= {I_tlast, I_tlast_d0};
            {I_tuser_d0, I_tuser_d1} <= {I_tuser, I_tuser_d0};
            data_d0    <= I_tdata;

            // 以每帧首行的周期数作为 valid_width（防错：每帧都更新）
            if (I_tuser)          flag_valid_first <= 1'b1;
            else if (I_tlast_d0)  flag_valid_first <= 1'b0;

            if (I_tuser_d1)       cnt_first <= 16'd0;
            else if (flag_valid_first) cnt_first <= cnt_first + 1'b1;

            if (I_tuser && (cnt_first > IMG_WIDTH_4x))
                valid_width <= cnt_first;

            if (I_tuser)          y_cnt <= 14'd0;
            else if (I_tlast_d0)  y_cnt <= y_cnt + 1'b1;

            // 行内第一个数据到最后一个数据之间 flag_valid_d0 为高
            if (I_tuser || I_tlast_d1) flag_valid_d0 <= 1'b0;
            else if (valid_r1)         flag_valid_d0 <= 1'b1;

            // 行尾到下一行首之间 flag_valid_d1 为高
            if (I_tuser)          flag_valid_d1 <= 1'b0;
            else if (I_tlast)     flag_valid_d1 <= 1'b1;
            else if (I_tvalid)    flag_valid_d1 <= 1'b0;

            if (I_tuser)          x_cnt <= 21'd0;
            else if (flag_valid)  x_cnt <= x_cnt + 1'b1;
            else                  x_cnt <= 21'd0;

            // A 口（写）
            if (I_tuser)          addra <= 11'd0;
            else if (valid_r0)    addra <= (addra == IMG_WIDTH_4x - 1) ? 11'd0 : addra + 1'b1;

            // B 口（读）
            if (I_tuser)          addrb <= 11'd0;
            else if (rd_valid)    addrb <= (addrb == IMG_WIDTH_4x - 1) ? 11'd0 : addrb + 1'b1;

            // 输出流控制打一拍
            O_tuser  <= (x_cnt == rd_valid_width_d0 + 1) && (y_cnt == 14'd0);
            O_tvalid <= rd_valid;
            O_tlast  <= (x_cnt == rd_valid_width_d1 - 1);
        end
    end

    blk_mem_gen_zhenghe blk_mem_gen_zhenghe_d1 (
        .clka(I_clk), .wea(valid_r0), .addra(addra), .dia(data_d0),
        .clkb(I_clk), .addrb(addrb), .dob(data_d)
    );

    assign O_tdata = data_d;

endmodule
