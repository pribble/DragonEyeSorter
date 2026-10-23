// des_common.vh — shared CDC / debounce primitives for the DES register blocks
// (des_regs, des_io). Zero behaviour change: each instance expands to the
// inline code it replaces. Include with `include "des_common.vh"; the guard keeps
// the multi-file TD compile to a single definition per module.
`ifndef DES_COMMON_VH
`define DES_COMMON_VH

// Two-flop synchroniser for W async inputs, idle level INIT (default 1 = pulled-up pins).
module des_sync2 #(
    parameter integer W    = 1,
    parameter         INIT = 1
) (
    input  wire         clk,
    input  wire [W-1:0] din,
    output reg  [W-1:0] dout
);
    reg [W-1:0] s0;
    initial begin
        s0   = {W{INIT[0]}};
        dout = {W{INIT[0]}};
    end
    always @(posedge clk) begin
        s0   <= din;
        dout <= s0;
    end
endmodule

// Toggle-edge CDC (des_regs frame / capture latch): 3-FF chain; the owner tests
// (s2 != s1), exactly the old inline `sync[2] != sync[1]` on the same cycles.
module des_togsync (
    input  wire clk,
    input  wire tin,
    output reg  s1,
    output reg  s2
);
    reg s0;
    initial begin
        s0 = 1'b0;
        s1 = 1'b0;
        s2 = 1'b0;
    end
    always @(posedge clk) begin
        s0 <= tin;
        s1 <= s0;
        s2 <= s1;
    end
endmodule

// Debounce / glitch filter: dout follows din only after din holds for N clocks.
// Covers e-stop / IR1 / LIMIT1 debounce and the encoder A/B/Z glitch filters
// (the old inline algorithms were identical cycle by cycle).
module des_deb #(
    parameter integer N    = 1500,
    parameter integer WDB  = 11,
    parameter         INIT = 1
) (
    input  wire clk,
    input  wire din,
    output reg  dout
);
    reg [WDB-1:0] cnt;
    initial begin
        dout = INIT[0];
        cnt  = {WDB{1'b0}};
    end
    always @(posedge clk) begin
        if (din == dout)
            cnt <= {WDB{1'b0}};
        else if (cnt >= N - 1) begin
            dout <= din;
            cnt  <= {WDB{1'b0}};
        end else
            cnt <= cnt + 1'b1;
    end
endmodule

`endif
