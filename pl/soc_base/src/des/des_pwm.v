// des_pwm.v — 2-channel 50 Hz flap-servo PWM; 寄存器 0x400 的偏移 / 位域 / 复位值以 docs/PL设计.md 为准.
// widths take effect at the next period start; reads are combinational (AXI bridge samples the next edge, v7).
// v17: SLEW (us per period, 0 = at once) walks the output width to the written one; xld (position trigger, des_ptrig)
// loads a channel width and restarts the period as soon as RESYNC_US have passed since the last start, so the new
// pulse leaves within RESYNC_US instead of up to a whole period. SLEW 0 and xld 0 = the v16 behaviour, clock for clock.
module des_pwm #(parameter integer CLK_HZ = 75_000_000, parameter integer RESYNC_US = 5000) (
    input  wire        clk, wr, input  wire [7:0]  waddr,
    input  wire [31:0] wdata, input  wire [7:0]  raddr,
    output reg  [31:0] rdata,
    output reg  [1:0]  pwm,
    input  wire [1:0]  xld, input  wire [23:0] xus
);
    localparam integer CLK_PER_US = CLK_HZ / 1_000_000;
    reg [1:0]  en = 2'd0;
    reg [15:0] period_us = 16'd20000, us = 16'd0;
    reg [23:0] width_w = 24'd0;
    reg [23:0] width_a = 24'd0;
    reg [31:0] periods = 32'd0;
    reg [6:0]  div = 7'd0;
    reg [11:0] slew = 12'd0;
    reg        resync = 1'b0;
    wire [16:0] us_next = {1'b0, us} + 17'd1;   // one incrementer, reused by the period compare
    function [11:0] clamp_us(input [11:0] v); clamp_us = (v > 12'd2550) ? 12'd2550 : v; endfunction
    function [11:0] walk(input [11:0] a, input [11:0] w);   // one period's move of the output width toward the target
        if (slew == 12'd0) walk = w;
        else if (w > a) walk = (w - a > slew) ? a + slew : w;
        else walk = (a - w > slew) ? a - slew : w;
    endfunction
    always @(posedge clk) begin
        if (wr) case (waddr[7:2])
            6'h00: en <= wdata[1:0];
            6'h01: period_us <= (wdata[15:0] < 16'd2600) ? 16'd2600 : wdata[15:0];
            6'h02: width_w[11:0]  <= (wdata > 32'd2550) ? 12'd2550 : wdata[11:0];
            6'h03: width_w[23:12] <= (wdata > 32'd2550) ? 12'd2550 : wdata[11:0];
            6'h06: slew <= wdata[11:0];
        endcase
        if (xld[0]) width_w[11:0]  <= clamp_us(xus[11:0]);
        if (xld[1]) width_w[23:12] <= clamp_us(xus[23:12]);
        if (|xld) resync <= 1'b1;
        if (div == CLK_PER_US - 1) begin
            div <= 7'd0;
            if (us_next >= {1'b0, period_us} || (resync && us_next >= RESYNC_US)) begin      // us >= period_us - 1
                us <= 16'd0; periods <= periods + 1'b1; resync <= |xld;
                width_a <= {walk(width_a[23:12], width_w[23:12]), walk(width_a[11:0], width_w[11:0])};
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
            6'h06: rdata = {20'd0, slew};
            default: rdata = 32'hDEADBEEF;
        endcase
    end
endmodule
