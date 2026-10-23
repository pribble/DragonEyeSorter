// verifier-platform 独立桩：安路厂家原语 + PLL（仅用于 iverilog 行为等价回归，不综合）
// 文件名 x_ 前缀避免与 dev 的桩重名。golden 与工作区两侧共用同一份。
// 行为刻意保持「对每个输入敏感」且完全确定性，使被测 RTL 的连线/时序改动能在 trace 中暴露。

// ---------------- BUFIO（DIV 分频）：clkout=原速，clkdivout=clkin/DIV ----------------
module x_bufio #(parameter DIV = 2) (
    input  wire clki,
    input  wire rst,
    input  wire coe,
    output wire clko,
    output wire clkdiv1,
    output wire clkdivx
);
    reg [15:0] cnt = 16'd0;
    always @(posedge clki) begin
        if (rst) cnt <= 16'd0;
        else if (coe) cnt <= cnt + 1'b1;
    end
    assign clko    = clki;
    assign clkdiv1 = cnt[DIV-1];
    assign clkdivx = cnt[DIV-1];
endmodule

module EG_LOGIC_BUFIO  #(parameter DIV = 2) (input wire clki, rst, coe, output wire clko, clkdiv1, clkdivx);
    x_bufio #(.DIV(DIV)) u (.clki(clki), .rst(rst), .coe(coe), .clko(clko), .clkdiv1(clkdiv1), .clkdivx(clkdivx));
endmodule
module EF2_LOGIC_BUFIO #(parameter DIV = 2) (input wire clki, rst, coe, output wire clko, clkdiv1, clkdivx);
    x_bufio #(.DIV(DIV)) u (.clki(clki), .rst(rst), .coe(coe), .clko(clko), .clkdiv1(clkdiv1), .clkdivx(clkdivx));
endmodule
module EF3_LOGIC_BUFIO #(parameter DIV = 2) (input wire clki, rst, coe, output wire clko, clkdiv1, clkdivx);
    x_bufio #(.DIV(DIV)) u (.clki(clki), .rst(rst), .coe(coe), .clko(clko), .clkdiv1(clkdiv1), .clkdivx(clkdivx));
endmodule
module EF4_LOGIC_BUFIO #(parameter DIV = 2) (input wire clki, rst, coe, output wire clko, clkdiv1, clkdivx);
    x_bufio #(.DIV(DIV)) u (.clki(clki), .rst(rst), .coe(coe), .clko(clko), .clkdiv1(clkdiv1), .clkdivx(clkdivx));
endmodule

// ---------------- PH1 / DR1 的 LCLK 分频 ----------------
module x_phy_lclk #(parameter CEMD = "SYNC", parameter CLKSEL = "ORG", parameter DIV = 2, parameter LCLK = "BYPASS") (
    input  wire clkin,
    input  wire rst,
    input  wire ce,
    output wire clkout,
    output wire clkdivout
);
    reg [15:0] cnt = 16'd0;
    always @(posedge clkin) begin
        if (rst) cnt <= 16'd0;
        else if (ce) cnt <= cnt + 1'b1;
    end
    assign clkout    = clkin;
    assign clkdivout = cnt[DIV-1];
endmodule

module DR1_PHY_LCLK #(parameter CEMD = "SYNC", parameter CLKSEL = "ORG", parameter DIV = 2, parameter LCLK = "BYPASS") (
    input wire clkin, input wire rst, input wire ce, output wire clkout, output wire clkdivout);
    x_phy_lclk #(.CEMD(CEMD), .CLKSEL(CLKSEL), .DIV(DIV), .LCLK(LCLK))
        u (.clkin(clkin), .rst(rst), .ce(ce), .clkout(clkout), .clkdivout(clkdivout));
endmodule
module PH1_PHY_LCLK #(parameter CEMD = "SYNC", parameter CLKSEL = "ORG", parameter DIV = 2, parameter LCLK = "BYPASS") (
    input wire clkin, input wire rst, input wire ce, output wire clkout, output wire clkdivout);
    x_phy_lclk #(.CEMD(CEMD), .CLKSEL(CLKSEL), .DIV(DIV), .LCLK(LCLK))
        u (.clkin(clkin), .rst(rst), .ce(ce), .clkout(clkout), .clkdivout(clkdivout));
endmodule

// ---------------- SF1 的 D-PHY PLL ----------------
module dphy_rx_pll_sf1 (
    input  wire refclk,
    input  wire reset,
    output wire extlock,
    output wire clk0_out,
    output wire clk1_out,
    output wire clk2_out
);
    reg [3:0] cnt = 4'd0;
    always @(posedge refclk) begin
        if (reset) cnt <= 4'd0;
        else cnt <= cnt + 1'b1;
    end
    assign extlock  = (cnt >= 4'd4);
    assign clk2_out = refclk;
    assign clk1_out = cnt[0];
    assign clk0_out = cnt[1];
endmodule

// ---------------- ODDR / OSERDES ----------------
module DR1_LOGIC_ODDR (input wire clk, input wire rst, input wire d1, input wire d0, output wire q);
    assign q = clk ? d0 : d1;
endmodule
module EG_LOGIC_ODDR (input wire clk, input wire rst, input wire d1, input wire d0, output wire q);
    assign q = clk ? d0 : d1;
endmodule
module EF2_LOGIC_ODDR (input wire clk, input wire rst, input wire d1, input wire d0, output wire q);
    assign q = clk ? d0 : d1;
endmodule
module EF3_LOGIC_ODDR (input wire clk, input wire rst, input wire d1, input wire d0, output wire q);
    assign q = clk ? d0 : d1;
endmodule
module EF4_LOGIC_ODDR (input wire clk, input wire rst, input wire d1, input wire d0, output wire q);
    assign q = clk ? d0 : d1;
endmodule
module SF1_LOGIC_ODDR (input wire clk, input wire rst, input wire d1, input wire d0, output wire q);
    assign q = clk ? d0 : d1;
endmodule
module PH1_LOGIC_ODDR (input wire clk, input wire rst, input wire d1, input wire d0, output wire q);
    assign q = clk ? d0 : d1;
endmodule

module PH1_LOGIC_HP_OSERDES #(parameter ODDRMODE = "DDRX1") (
    input wire sclk, input wire pclk, input wire rst, input wire [1:0] idata, output wire odata);
    assign odata = sclk ? idata[0] : idata[1];
endmodule
module PH1P_LOGIC_HR_OSERDES #(parameter ODDRMODE = "DDRX1") (
    input wire sclk, input wire pclk, input wire rst, input wire [1:0] idata, output wire odata);
    assign odata = sclk ? idata[0] : idata[1];
endmodule

// ---------------- design_0 内部 PLL（al_ip/PLL/PLL.xml 的加密 IP）----------------
// lock 在上电 8 个 refclk 后拉高；clk2 为 refclk/4、clk3 为 refclk/2（供 24MHz 相机时钟/70MHz）。
module PLL (
    input  wire refclk,
    output wire lock,
    output wire clk0_out,
    output wire clk1_out,
    output wire clk2_out,
    output wire clk3_out
);
    reg [7:0] cnt = 8'd0;
    reg [1:0] div2 = 2'd0;
    reg [3:0] div4 = 4'd0;
    always @(posedge refclk) begin
        if (cnt != 8'hFF) cnt <= cnt + 1'b1;
        div2 <= div2 + 1'b1;
        div4 <= div4 + 1'b1;
    end
    assign lock     = (cnt >= 8'd8);
    assign clk0_out = refclk;
    assign clk1_out = div2[0];
    assign clk2_out = div4[1];
    assign clk3_out = div2[1];
endmodule
