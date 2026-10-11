// des_pwm.v — hobby-servo PWM for the belt-end flaps (MG996R, 两级翻板: 复检 vs NG, 返修 vs 报废) and the light
// enable (赛题领域 3 中级「NG 不良品输出 IO 控制；不良品实时分拣触发」).
//
// NCH channels, one period of PERIOD_US (default 20 ms = 50 Hz) for all, pulse width per channel in microseconds
// (0 = no pulse: the servo holds without torque on most hobby servos). A new width written by the PS takes effect
// at the start of the next period, so no truncated or doubled pulse ever reaches the servo. Pins are driven directly
// (3.3 V LVCMOS; MG996R signal threshold ~2 V).
//
// Registers (byte offsets in the 0x400 window, register clock):
//   0x00 CTRL    RW [NCH-1:0] channel enable, [8] LAMP (light on)
//   0x04 PERIOD  RW period in us (default 20000, minimum 2600)
//   0x08 CH0_US  RW pulse width in us (500..2500 typical)
//   0x0C CH1_US  RW
//   0x10 CNT     R  periods started (the PS can wait for its new width to be in force: CNT advanced by 2)
//   0x14 ID      R  0x50574D31 'PWM1'
module des_pwm #(
    parameter integer CLK_HZ = 75_000_000,
    parameter integer NCH    = 2
) (
    input  wire           clk,
    input  wire           wr,
    input  wire [7:0]     waddr,
    input  wire [31:0]    wdata,
    input  wire [7:0]     raddr,
    output reg  [31:0]    rdata,
    output reg  [NCH-1:0] pwm,
    output wire           lamp
);
    localparam integer CLK_PER_US = CLK_HZ / 1_000_000;

    reg [NCH-1:0] en = {NCH{1'b0}};
    reg           lamp_r = 1'b0;
    reg [15:0]    period_us = 16'd20000;
    reg [11:0]    width_w [0:NCH-1];   // written by the PS
    reg [11:0]    width_a [0:NCH-1];   // in force this period
    reg [31:0]    periods = 32'd0;
    reg [7:0]     div = 8'd0;          // clocks within the current microsecond
    reg [15:0]    us = 16'd0;          // microsecond within the current period
    integer i;
    initial for (i = 0; i < NCH; i = i + 1) begin width_w[i] = 12'd0; width_a[i] = 12'd0; end

    assign lamp = lamp_r;

    always @(posedge clk) begin
        if (wr) begin
            case (waddr[7:2])
                6'h00: begin en <= wdata[NCH-1:0]; lamp_r <= wdata[8]; end
                6'h01: period_us <= (wdata[15:0] < 16'd2600) ? 16'd2600 : wdata[15:0];
                default:
                    for (i = 0; i < NCH; i = i + 1)
                        if (waddr[7:2] == 6'h02 + i) width_w[i] <= (wdata > 32'd2550) ? 12'd2550 : wdata[11:0];
            endcase
        end
        // microsecond tick
        if (div == CLK_PER_US - 1) begin
            div <= 8'd0;
            if (us >= period_us - 1'b1) begin
                us <= 16'd0;
                periods <= periods + 1'b1;
                for (i = 0; i < NCH; i = i + 1) width_a[i] <= width_w[i];   // new widths only at a period start
            end else begin
                us <= us + 1'b1;
            end
        end else begin
            div <= div + 1'b1;
        end
        for (i = 0; i < NCH; i = i + 1) pwm[i] <= en[i] && (us < width_a[i]);
        case (raddr[7:2])
            6'h00: rdata <= {23'd0, lamp_r, {(8 - NCH){1'b0}}, en};
            6'h01: rdata <= {16'd0, period_us};
            6'h02: rdata <= {20'd0, width_w[0]};
            6'h03: rdata <= (NCH > 1) ? {20'd0, width_w[1]} : 32'd0;
            6'h04: rdata <= periods;
            6'h05: rdata <= 32'h50574D31;
            default: rdata <= 32'hDEADBEEF;
        endcase
    end
endmodule
