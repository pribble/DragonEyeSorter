// 出处：南京微酷机器人 / VLKUS（www.uisrc.com）ISP 参考设计；重构版，接口与逐拍行为不变。
module data128_96 (input wire [127:0] I_tdata, output wire [95:0] O_tdata);
    // 128bit 每像素带 8bit 填充位，只取 4 像素 × RGB888
    assign O_tdata = {I_tdata[119:96], I_tdata[87:64], I_tdata[55:32], I_tdata[23:0]};
endmodule
