// Behavioural models of the two vendor primitives used by ui_isp/awb_1_1 (for tb_awb.v only; the real ones are a
// TD gate netlist and an ERAM wrapper).

// divider IP (DR1_SEQ_DIV, PIPLE = 50, 48/32 unsigned): a new operand pair every clock with `start`, quotient and
// `done` 50 clocks later (1 input + 48 iteration + 1 output register stages).
module divider (
    input  wire        clk,
    input  wire        rst,
    input  wire        start,
    input  wire [31:0] denominator,
    input  wire [47:0] numerator,
    output wire [47:0] quotient,
    output wire [31:0] remainder,
    output wire        done
);
    localparam integer LAT = 50;
    reg [47:0] q_pipe [0:LAT-1];
    reg [31:0] r_pipe [0:LAT-1];
    reg [LAT-1:0] v_pipe = {LAT{1'b0}};
    integer i;
    always @(posedge clk) begin
        if (rst) v_pipe <= {LAT{1'b0}};
        else begin
            v_pipe <= {v_pipe[LAT-2:0], start};
            q_pipe[0] <= denominator ? numerator / denominator : {48{1'b1}};
            r_pipe[0] <= denominator ? numerator % denominator : 32'd0;
            for (i = 1; i < LAT; i = i + 1) begin
                q_pipe[i] <= q_pipe[i-1];
                r_pipe[i] <= r_pipe[i-1];
            end
        end
    end
    assign quotient = q_pipe[LAT-1];
    assign remainder = r_pipe[LAT-1];
    assign done = v_pipe[LAT-1];
endmodule

// blk_mem_gen_awb_delay_signal: 2048 x 96 simple dual port, REGMODE NOREG (read data one clock after the address)
module blk_mem_gen_awb_delay_signal (
    input  wire [95:0] dia,
    input  wire [10:0] addra,
    input  wire        wea,
    input  wire        clka,
    input  wire [10:0] addrb,
    input  wire        clkb,
    input  wire [95:0] dib,
    input  wire        web,
    output reg  [95:0] doa,
    output reg  [95:0] dob
);
    reg [95:0] mem [0:2047];
    always @(posedge clka) begin
        if (wea) mem[addra] <= dia;
        doa <= mem[addra];
    end
    always @(posedge clkb) dob <= mem[addrb];
endmodule
