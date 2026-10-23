// 出处：南京微酷机器人 / VLKUS（www.uisrc.com）ISP 参考设计；重构版，接口与逐拍行为不变。
module data96_128 (input wire [95:0] I_tdata, output wire [127:0] O_tdata);
    // 4 像素 × RGB888 扩成 4 像素 × RGBX（每像素高位补 8bit 0）
    assign O_tdata = {8'd0, I_tdata[95:72], 8'd0, I_tdata[71:48],
                      8'd0, I_tdata[47:24], 8'd0, I_tdata[23:0]};
endmodule
