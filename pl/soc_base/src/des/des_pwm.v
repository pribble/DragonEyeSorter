// des_pwm.v — 2-channel 50 Hz flap-servo PWM; 寄存器 0x400 的偏移 / 位域 / 复位值以 docs/PL设计.md 为准.
// widths take effect at the next period start; reads are combinational (AXI bridge samples the next edge, v7).
module des_pwm #(parameter integer CLK_HZ = 75_000_000) (
    input  wire        clk, wr, input  wire [7:0]  waddr,
    input  wire [31:0] wdata, input  wire [7:0]  raddr,
    output reg  [31:0] rdata,
    output reg  [1:0]  pwm
);
    localparam integer CLK_PER_US = CLK_HZ / 1_000_000;
    reg [1:0]  en = 2'd0;
    reg [15:0] period_us = 16'd20000, us = 16'd0;
    reg [23:0] width_w = 24'd0;
    reg [23:0] width_a = 24'd0;
    reg [31:0] periods = 32'd0;
    reg [6:0]  div = 7'd0;
    wire [16:0] us_next = {1'b0, us} + 17'd1;   // one incrementer, reused by the period compare
    always @(posedge clk) begin
        if (wr) case (waddr[7:2])
            6'h00: en <= wdata[1:0];
            6'h01: period_us <= (wdata[15:0] < 16'd2600) ? 16'd2600 : wdata[15:0];
            6'h02: width_w[11:0]  <= (wdata > 32'd2550) ? 12'd2550 : wdata[11:0];
            6'h03: width_w[23:12] <= (wdata > 32'd2550) ? 12'd2550 : wdata[11:0];
        endcase
        if (div == CLK_PER_US - 1) begin
            div <= 7'd0;
            if (us_next >= {1'b0, period_us}) begin      // us >= period_us - 1
                us <= 16'd0; periods <= periods + 1'b1; width_a <= width_w;
            end else us <= us_next[15:0];
        end else div <= div + 1'b1;
        pwm[0] <= en[0] && (us < width_a[11:0]);
        pwm[1] <= en[1] && (us < width_a[23:12]);
    end
    always @* begin
        case (raddr[7:2])
            6'h00: rdata = {30'd0, en};            6'h01: rdata = {16'd0, period_us};
            6'h02: rdata = {20'd0, width_w[11:0]}; 6'h03: rdata = {20'd0, width_w[23:12]};
            6'h04: rdata = periods;                6'h05: rdata = 32'h50574D31;
            default: rdata = 32'hDEADBEEF;
        endcase
    end
endmodule
