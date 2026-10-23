// des_regs.v — DES register window 0x0xx (PS 0x8030_1000 + offset, docs/PL设计.md §寄存器通道).
// ID / VERSION (top passes 12 = v11 + PS CAN0 pins, no register change) / SCRATCH / FREE_CNT /
// LED_CTRL / FRAME_CNT+TS+PERIOD (SOF toggle CDC) / CAP_* (start/done-toggle latch of belt POS).
// Unmapped reads return 0xDEADBEEF; combinational read mux (registered mux read stale data, v8).
`include "des_common.vh"
module des_regs #(parameter [31:0] VERSION = 32'd2) (
    input  wire        clk, wr,
    input  wire [11:0] waddr,
    input  wire [31:0] wdata,
    input  wire [11:0] raddr,
    output reg  [31:0] rdata,
    input  wire        sof_toggle, cap_start_toggle, cap_done_toggle,
    input  wire [31:0] pos,             // belt position in clk domain (stepper POS)
    output wire [31:0] ts,
    output wire        led_ovr, led_val
);
    localparam [31:0] DES_ID = 32'h44455301;

    reg [31:0] scratch = 32'd0, free_cnt = 32'd0;
    reg [31:0] frame_cnt = 32'd0, frame_ts = 32'd0, frame_period = 32'd0;
    reg [31:0] cur_fcnt = 32'd0, cur_t0 = 32'd0, cur_pos0 = 32'd0;
    reg [31:0] cap_seq = 32'd0, cap_fcnt = 32'd0, cap_t0 = 32'd0, cap_t1 = 32'd0,
               cap_pos0 = 32'd0, cap_pos1 = 32'd0;
    reg [1:0]  led_ctrl = 2'd0;

    // Frame / capture toggles: one 3-FF chain each, the owner tests s2 != s1 (same cycles as inline).
    wire sof_s1, sof_s2, cs_s1, cs_s2, cd_s1, cd_s2;
    des_togsync u_sof (.clk(clk), .tin(sof_toggle),       .s1(sof_s1), .s2(sof_s2));
    des_togsync u_css (.clk(clk), .tin(cap_start_toggle), .s1(cs_s1),  .s2(cs_s2));
    des_togsync u_csd (.clk(clk), .tin(cap_done_toggle),  .s1(cd_s1),  .s2(cd_s2));

    always @(posedge clk) begin
        free_cnt <= free_cnt + 1'b1;
        if (wr && waddr[11:2] == 10'h002) scratch <= wdata;
        else if (wr && waddr[11:2] == 10'h004) led_ctrl <= wdata[1:0];
        if (sof_s2 != sof_s1) begin
            frame_cnt <= frame_cnt + 1'b1; frame_period <= free_cnt - frame_ts; frame_ts <= free_cnt;
        end
        if (cs_s2 != cs_s1) begin cur_fcnt <= frame_cnt; cur_t0 <= free_cnt; cur_pos0 <= pos; end
        if (cd_s2 != cd_s1) begin
            cap_seq <= cap_seq + 1'b1; cap_fcnt <= cur_fcnt; cap_t0 <= cur_t0; cap_pos0 <= cur_pos0;
            cap_t1 <= free_cnt; cap_pos1 <= pos;
        end
    end

    // read: one word table indexed by raddr[11:2], everything above 0x034 reads 0xDEADBEEF
    always @* begin
        case (raddr[11:2])
            10'h000: rdata = DES_ID;      10'h001: rdata = VERSION;
            10'h002: rdata = scratch;     10'h003: rdata = free_cnt;
            10'h004: rdata = {30'd0, led_ctrl};
            10'h005: rdata = frame_cnt;   10'h006: rdata = frame_ts;
            10'h007: rdata = frame_period;
            10'h008: rdata = cap_seq;     10'h009: rdata = cap_fcnt;
            10'h00A: rdata = cap_t0;      10'h00B: rdata = cap_t1;
            10'h00C: rdata = cap_pos0;    10'h00D: rdata = cap_pos1;
            default: rdata = 32'hDEAD_BEEF;
        endcase
    end

    assign ts      = free_cnt;
    assign led_ovr = led_ctrl[0];
    assign led_val = led_ctrl[1];
endmodule
