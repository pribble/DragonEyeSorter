// des_common.vh — shared CDC / debounce primitives for the DES register blocks
// (des_regs, des_io). Pure `include; the guard keeps the multi-file TD compile to one definition.
`ifndef DES_COMMON_VH
`define DES_COMMON_VH

// Two-flop synchroniser for W async inputs, idle level INIT (default 1 = pulled-up pins).
module des_sync2 #(parameter integer W = 1, parameter INIT = 1) (
    input wire clk, input wire [W-1:0] din, output reg [W-1:0] dout);
    reg [W-1:0] s0;
    initial begin s0 = {W{INIT[0]}}; dout = {W{INIT[0]}}; end
    always @(posedge clk) begin s0 <= din; dout <= s0; end
endmodule

// Toggle-edge CDC: 3-FF chain; the owner tests (s2 != s1), the same cycles as the old inline chain.
module des_togsync (input wire clk, input wire tin, output reg s1, output reg s2);
    reg s0;
    initial begin s0 = 1'b0; s1 = 1'b0; s2 = 1'b0; end
    always @(posedge clk) begin s0 <= tin; s1 <= s0; s2 <= s1; end
endmodule

// Debounce / glitch filter: dout follows din only after din holds for N clocks. The counter width
// is $clog2(N) (minimum 1 bit) instead of the old WDB parameter — same counts, fewer FFs.
module des_deb #(parameter integer N = 1500, parameter INIT = 1) (
    input wire clk, input wire din, output reg dout);
    localparam integer WDB = (N > 2) ? $clog2(N) : 1;
    reg [WDB-1:0] cnt;
    initial begin dout = INIT[0]; cnt = {WDB{1'b0}}; end
    always @(posedge clk)
        if (din == dout) cnt <= {WDB{1'b0}};
        else if (cnt >= N - 1) begin dout <= din; cnt <= {WDB{1'b0}}; end
        else cnt <= cnt + 1'b1;
endmodule

`endif
