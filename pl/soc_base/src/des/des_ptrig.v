// des_ptrig.v — belt position compare trigger (v17): 2 entries; an armed entry fires once, in the clock its source
// (stepper position or encoder count) reaches the threshold, records the time stamp, and can load a flap PWM width
// (des_pwm restarts its period early) and pulse the TRIG_OUT pin. 寄存器 0x800 的偏移 / 位域 / 复位值以
// docs/PL设计.md 为准. Nothing armed (reset) = no output ever changes = the v16 behaviour.
// The compare is on the low CW bits of the difference source - threshold, so it survives the counter wrap: an entry
// fires when its source is 0 .. 2^(CW-1) - 1 counts past the threshold (CW 24: 8.3 million counts, about 175 m of
// belt). An entry whose threshold is already passed when it is armed fires at once and is flagged LATE.
// Sized for the device (the first version, 4 entries with 32-bit compares, a position latch and a programmable
// TRIG_OUT length, did not route together with des_bpi): the source value at the fire is not kept (an on-time fire
// happens at the threshold itself), the TRIG_OUT pulse is a fixed 100 us.
module des_ptrig #(parameter integer CLK_HZ = 75_000_000, parameter integer CW = 24) (
    input  wire        clk, wr, input  wire [7:0]  waddr,
    input  wire [31:0] wdata, input  wire [7:0]  raddr,
    output reg  [31:0] rdata, input  wire [31:0] ts,
    input  wire [31:0] pos, enc,
    output reg  [1:0]  pwm_ld, output reg  [23:0] pwm_us,       // to des_pwm: load channel k with pwm_us[12k +: 12]
    output wire        trig_o
);
    localparam integer N = 2;
    localparam [13:0] OUT_CLK = CLK_HZ / 10_000;                 // TRIG_OUT pulse length in clocks (100 us)
    reg [CW*N-1:0] thr = 0;
    reg [32*N-1:0] f_ts = 0;
    reg [12*N-1:0] c_data = 0;
    reg [2*N-1:0]  c_act = 0;
    reg [N-1:0] armed = 0, fresh = 0, fired = 0, late = 0, c_dir = 0, c_src = 0, c_out = 0;
    reg [15:0] fire_cnt = 0;
    reg [13:0] out_cnt = 0;
    reg [N-1:0] hit;
    reg [CW-1:0] d;
    wire       wi = waddr[4], ri = raddr[4];
    wire       w_ent = wr && waddr[7:5] == 3'b010;               // 0x840 .. 0x85C
    integer i, j;
    initial begin pwm_ld = 0; pwm_us = 0; end
    always @* begin
        for (j = 0; j < N; j = j + 1) begin
            d = (c_src[j] ? enc[CW-1:0] : pos[CW-1:0]) - thr[CW*j +: CW];
            hit[j] = armed[j] && (c_dir[j] ? (d[CW-1] || d == {CW{1'b0}}) : !d[CW-1]);
        end
    end
    assign trig_o = out_cnt != 14'd0;
    always @(posedge clk) begin
        pwm_ld <= 2'b00;
        fresh <= 0;
        if (out_cnt != 14'd0) out_cnt <= out_cnt - 1'b1;
        for (i = 0; i < N; i = i + 1) if (hit[i]) begin
            armed[i] <= 1'b0; fired[i] <= 1'b1; late[i] <= fresh[i];
            f_ts[32*i +: 32] <= ts;
            case (c_act[2*i +: 2])
                2'd1: begin pwm_ld[0] <= 1'b1; pwm_us[11:0]  <= c_data[12*i +: 12]; end
                2'd2: begin pwm_ld[1] <= 1'b1; pwm_us[23:12] <= c_data[12*i +: 12]; end
                default: ;
            endcase
            if (c_out[i]) out_cnt <= OUT_CLK;
        end
        fire_cnt <= fire_cnt + hit[0] + hit[1];
        // registers last: writing an entry in the clock it fires wins over the fire
        if (wr && waddr[7:2] == 6'h00 && wdata[0]) begin armed <= 0; fired <= 0; late <= 0; end
        if (w_ent) case (waddr[3:2])
            2'd0: thr[CW*wi +: CW] <= wdata[CW-1:0];
            2'd1: begin
                c_dir[wi] <= wdata[1]; c_src[wi] <= wdata[2]; c_act[2*wi +: 2] <= wdata[5:4]; c_out[wi] <= wdata[7];
                c_data[12*wi +: 12] <= wdata[27:16];
                armed[wi] <= wdata[0]; fresh[wi] <= wdata[0];
                if (wdata[0]) begin fired[wi] <= 1'b0; late[wi] <= 1'b0; end
            end
            default: ;
        endcase
    end
    always @* begin
        rdata = 32'hDEADBEEF;
        if (raddr[7:6] == 2'b00) case (raddr[5:2])
            4'h1: rdata = {14'd0, late, 6'd0, fired, 6'd0, armed};
            4'h2: rdata = {16'd0, fire_cnt};
            4'h3: rdata = 32'h50544731;      // "PTG1"
            4'h4: rdata = {18'd0, OUT_CLK};
            default: ;
        endcase
        else if (raddr[7:5] == 3'b010) case (raddr[3:2])
            2'd0: rdata = {{(32-CW){thr[CW*ri + CW - 1]}}, thr[CW*ri +: CW]};   // sign-extended
            2'd1: rdata = {4'd0, c_data[12*ri +: 12], 8'd0, c_out[ri], 1'b0, c_act[2*ri +: 2], 1'b0, c_src[ri], c_dir[ri], armed[ri]};
            2'd2: rdata = f_ts[32*ri +: 32];
            default: ;
        endcase
    end
endmodule
