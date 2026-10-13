// DragonEyeSorter PL register file, PS address 0x8030_1000 + offset (see docs/PL设计.md).
//   0x000 R  DES_ID       0x44455301
//   0x004 R  DES_VERSION  1 = register channel, 2 = servo_bus + stepper, 3 = SOF fix, 4 = INCK 27 MHz, 5 = capture latch
//   0x008 RW SCRATCH
//   0x00C R  FREE_CNT     75 MHz free-running counter (also exported as ts)
//   0x010 RW LED_CTRL     b0 override LED1, b1 LED1 value
//   0x014 R  FRAME_CNT    camera frames seen at the ISP output (start-of-frame count)
//   0x018 R  FRAME_TS     FREE_CNT at the latest start of frame
//   0x01C R  FRAME_PERIOD FREE_CNT delta between the last two starts of frame
// Last completed DMA capture (v5). Updated on the frame-done interrupt edge, so the PS can read them
// any time within the next frame period after WaitFinish:
//   0x020 R  CAP_SEQ      number of completed captures
//   0x024 R  CAP_FCNT     FRAME_CNT when the frame writer started this frame
//   0x028 R  CAP_T0       FREE_CNT at the writer start of this frame
//   0x02C R  CAP_T1       FREE_CNT at the done interrupt
//   0x030 R  CAP_POS0     belt position (stepper POS) at the writer start
//   0x034 R  CAP_POS1     belt position at the done interrupt (PS interpolates per image row)
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
    input  wire        cap_start_toggle,// toggles when the frame writer starts a frame (DMA clock domain)
    input  wire        cap_done_toggle, // toggles on the frame-done interrupt rising edge (DMA clock domain)
    input  wire [31:0] pos,             // belt position in clk domain (stepper POS, later the encoder)
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
    reg [2:0]  cs_sync = 3'd0, cd_sync = 3'd0;
    reg [31:0] cur_fcnt = 32'd0, cur_t0 = 32'd0, cur_pos0 = 32'd0;
    reg [31:0] cap_seq = 32'd0, cap_fcnt = 32'd0, cap_t0 = 32'd0, cap_t1 = 32'd0, cap_pos0 = 32'd0, cap_pos1 = 32'd0;

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
        cs_sync <= {cs_sync[1:0], cap_start_toggle};
        cd_sync <= {cd_sync[1:0], cap_done_toggle};
        if (cs_sync[2] != cs_sync[1]) begin
            cur_fcnt <= frame_cnt;
            cur_t0   <= free_cnt;
            cur_pos0 <= pos;
        end
        if (cd_sync[2] != cd_sync[1]) begin
            cap_seq  <= cap_seq + 1'b1;
            cap_fcnt <= cur_fcnt;
            cap_t0   <= cur_t0;
            cap_pos0 <= cur_pos0;
            cap_t1   <= free_cnt;
            cap_pos1 <= pos;
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
            10'h008: rdata = cap_seq;
            10'h009: rdata = cap_fcnt;
            10'h00A: rdata = cap_t0;
            10'h00B: rdata = cap_t1;
            10'h00C: rdata = cap_pos0;
            10'h00D: rdata = cap_pos1;
            default: rdata = 32'hDEAD_BEEF;
        endcase
    end

    assign ts      = free_cnt;
    assign led_ovr = led_ctrl[0];
    assign led_val = led_ctrl[1];
endmodule
