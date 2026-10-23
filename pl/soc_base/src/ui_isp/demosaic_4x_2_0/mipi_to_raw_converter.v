// 出处：Milianke uisrc ISP demo（ui_isp/demosaic_4x_2_0/mipi_to_raw_converter.v, XiaoQingquan 2024/11）
// RAW10 -> 每像素取高 8bit 打包（每个 32bit 字含 4 个像素），流控制信号打一拍对齐。
`timescale 1ns/1ns

module mipi_to_raw_converter (
    input               I_clk   ,
    input               I_rst_n ,

    input      [39:0]   I_raw_tdata ,
    input      [ 9:0]   I_raw_tdest ,
    input               I_raw_tlast ,
    input               I_raw_tvalid,
    input               I_raw_tuser ,
    output              I_raw_tready,

    output     [31:0]   O_raw_tdata ,
    output              O_raw_tlast ,
    output              O_raw_tvalid,
    output              O_raw_tuser ,
    input               O_raw_tready
);

    // 每个 RAW10 取高 8 位（高位包含的颜色信息更多）
    reg  [31:0] raw_data_r0;
    reg  [2:0]  ctrl_r0;    // {tlast, tuser, tvalid}，与数据同拍对齐

    always @(posedge I_clk or negedge I_rst_n) begin
        if (!I_rst_n) begin
            raw_data_r0 <= 32'd0;
            ctrl_r0     <= 3'd0;
        end
        else begin
            if (I_raw_tvalid)
                raw_data_r0 <= {I_raw_tdata[9:2], I_raw_tdata[19:12],
                                I_raw_tdata[29:22], I_raw_tdata[39:32]};
            ctrl_r0 <= {I_raw_tlast, I_raw_tuser, I_raw_tvalid};
        end
    end

    assign O_raw_tdata = raw_data_r0;
    assign {O_raw_tlast, O_raw_tuser, O_raw_tvalid} = ctrl_r0;
    assign I_raw_tready = O_raw_tready;

endmodule
