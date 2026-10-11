// awb_seq_div.v — unsigned restoring divider, one quotient bit per clock.
//
// Replaces the vendor's 50-stage pipelined divider IP (`divider`, DR1_SEQ_DIV, ~2.1k LUT + 1.1k ripple + 2.7k FF
// each) in the ISP auto white balance (ui_isp/awb_1_1/awb.v), which divides running colour sums: they are large
// and change slowly, so a new quotient every W_NUM + 1 clocks (start tied high = back to back) instead of every
// clock changes the gains by at most a step. ~1/30 of the logic. (2026-10-11: v6 did not route at 90 % slices; the
// two AWB dividers were the largest block outside the NPU.)
//
// start latches the operands (ignored while busy); `done` pulses W_NUM + 1 clocks later and `quotient` holds the
// result until the next division. denominator = 0 gives an all-ones quotient.
module awb_seq_div #(
    parameter integer W_NUM = 48,
    parameter integer W_DEN = 32
) (
    input  wire             clk,
    input  wire             rst,          // active high
    input  wire             start,
    input  wire [W_NUM-1:0] numerator,
    input  wire [W_DEN-1:0] denominator,
    output reg  [W_NUM-1:0] quotient,
    output reg              done
);
    reg  [W_NUM-1:0] q = {W_NUM{1'b0}};   // numerator bits shift out at the top, quotient bits shift in
    reg  [W_DEN:0]   rem = {(W_DEN + 1){1'b0}};
    reg  [W_DEN-1:0] den = {W_DEN{1'b0}};
    reg  [7:0]       cnt = 8'd0;
    reg              busy = 1'b0;
    wire [W_DEN:0]   shifted = {rem[W_DEN-1:0], q[W_NUM-1]};
    wire             ge = shifted >= {1'b0, den};

    initial begin
        quotient = {W_NUM{1'b0}};
        done = 1'b0;
    end

    always @(posedge clk) begin
        done <= 1'b0;
        if (rst) begin
            busy <= 1'b0;
            cnt <= 8'd0;
            quotient <= {W_NUM{1'b0}};
        end else if (!busy) begin
            if (start) begin
                q <= numerator;
                den <= denominator;
                rem <= {(W_DEN + 1){1'b0}};
                cnt <= W_NUM;
                busy <= 1'b1;
            end
        end else begin
            rem <= ge ? shifted - {1'b0, den} : shifted;
            q <= {q[W_NUM-2:0], ge};
            cnt <= cnt - 1'b1;
            if (cnt == 8'd1) begin
                busy <= 1'b0;
                done <= 1'b1;
                quotient <= {q[W_NUM-2:0], ge};
            end
        end
    end
endmodule
