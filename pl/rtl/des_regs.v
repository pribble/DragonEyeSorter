// DragonEyeSorter PL register file, PS address 0x8030_1000 + offset (see docs/03).
//   0x000 R  DES_ID       0x44455301
//   0x004 R  DES_VERSION  1 = register channel, 2 = servo_bus + stepper integrated
//   0x008 RW SCRATCH
//   0x00C R  FREE_CNT     75 MHz free-running counter (also exported as ts)
//   0x010 RW LED_CTRL     b0 override LED1, b1 LED1 value
//   0x014 R  FRAME_CNT    camera frames seen at the ISP output (start-of-frame count)
//   0x018 R  FRAME_TS     FREE_CNT at the latest start of frame
//   0x01C R  FRAME_PERIOD FREE_CNT delta between the last two starts of frame
module des_regs #(
    parameter [31:0] VERSION = 32'd2
) (
    input  wire        clk,
    input  wire        wr,
    input  wire [11:0] waddr,
    input  wire [31:0] wdata,
    input  wire [11:0] raddr,
    output reg  [31:0] rdata,
    input  wire        sof_toggle,      // toggles once per frame, from the camera clock domain
    output wire [31:0] ts,
    output wire        led_ovr,
    output wire        led_val
);
    localparam [31:0] DES_ID = 32'h44455301;

    reg [31:0] scratch  = 32'd0;
    reg [31:0] free_cnt = 32'd0;
    reg [1:0]  led_ctrl = 2'd0;
    reg [2:0]  sof_sync = 3'd0;
    reg [31:0] frame_cnt = 32'd0, frame_ts = 32'd0, frame_period = 32'd0;

    always @(posedge clk) begin
        free_cnt <= free_cnt + 1'b1;
        if (wr) begin
            case (waddr[11:2])
                10'h002: scratch  <= wdata;
                10'h004: led_ctrl <= wdata[1:0];
                default: ;
            endcase
        end
        sof_sync <= {sof_sync[1:0], sof_toggle};
        if (sof_sync[2] != sof_sync[1]) begin
            frame_cnt    <= frame_cnt + 1'b1;
            frame_period <= free_cnt - frame_ts;
            frame_ts     <= free_cnt;
        end
    end

    always @(*) begin
        case (raddr[11:2])
            10'h000: rdata = DES_ID;
            10'h001: rdata = VERSION;
            10'h002: rdata = scratch;
            10'h003: rdata = free_cnt;
            10'h004: rdata = {30'd0, led_ctrl};
            10'h005: rdata = frame_cnt;
            10'h006: rdata = frame_ts;
            10'h007: rdata = frame_period;
            default: rdata = 32'hDEAD_BEEF;
        endcase
    end

    assign ts      = free_cnt;
    assign led_ovr = led_ctrl[0];
    assign led_val = led_ctrl[1];
endmodule
