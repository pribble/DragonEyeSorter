// awb_seq_div.v — unsigned restoring divider, one quotient bit per clock, for ISP AWB colour sums. start latches
// operands (ignored while busy); done pulses W_NUM+1 clocks later (quotient shifts while busy, final at done); den=0 gives all ones.
module awb_seq_div #(
    parameter integer W_NUM = 48,
    parameter integer W_DEN = 32
) (
    input  wire             clk,
    input  wire             rst,
    input  wire             start,
    input  wire [W_NUM-1:0] numerator,
    input  wire [W_DEN-1:0] denominator,
    output reg  [W_NUM-1:0] quotient = 0,
    output reg              done = 0
);
    localparam CW = $clog2(W_NUM + 1);
    reg [W_DEN-1:0] rem = 0, den = 0;   // rem carry bit never read
    reg [CW-1:0] cnt = 0;   reg busy = 1'b0;
    wire [W_DEN:0] shifted = {rem[W_DEN-1:0], quotient[W_NUM-1]};  wire ge = shifted >= {1'b0, den};
    always @(posedge clk) begin done <= 1'b0;
        if (rst) begin
            busy <= 1'b0; cnt <= 0; quotient <= 0; rem <= 0; den <= 0;
        end else if (!busy) begin
            if (start) begin quotient <= numerator; den <= denominator; rem <= 0; cnt <= W_NUM; busy <= 1'b1; end
        end else begin
            rem <= ge ? shifted - {1'b0, den} : shifted;
            quotient <= {quotient[W_NUM-2:0], ge};
            cnt <= cnt - 1'b1;
            if (cnt == 1) begin busy <= 1'b0; done <= 1'b1; end
        end
    end
endmodule
