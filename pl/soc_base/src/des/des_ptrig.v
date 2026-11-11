// des_ptrig.v — belt position compare trigger (v17): 4 entries; an armed entry fires once, in the clock its source
// (stepper position or encoder count) reaches the threshold, records the time stamp and position, and can load a flap
// PWM width (des_pwm restarts its period early), pulse a servo-bus "go" and pulse the TRIG_OUT pin. 寄存器 0x800 的
// 偏移 / 位域 / 复位值以 docs/PL设计.md 为准. Nothing armed (reset) = no output ever changes = the v16 behaviour.
// The compare is on the 32-bit difference source - threshold, so it survives the counter wrap. An entry whose
// threshold is already passed when it is armed fires at once and is flagged LATE.
module des_ptrig #(parameter integer CLK_HZ = 75_000_000) (
    input  wire        clk, wr, input  wire [7:0]  waddr,
    input  wire [31:0] wdata, input  wire [7:0]  raddr,
    output reg  [31:0] rdata, input  wire [31:0] ts,
    input  wire [31:0] pos, enc,
    output reg  [1:0]  pwm_ld, output reg  [23:0] pwm_us,       // to des_pwm: load channel k with pwm_us[12k +: 12]
    output reg         servo_go, output wire        trig_o
);
    localparam integer N = 4;
    reg [31:0] thr [0:N-1], f_ts [0:N-1], f_pos [0:N-1];
    reg [11:0] c_data [0:N-1];
    reg [2:0]  c_act [0:N-1];
    reg [N-1:0] armed = 0, fresh = 0, fired = 0, late = 0, c_dir = 0, c_src = 0, c_out = 0;
    reg [31:0] fire_cnt = 0;
    reg [23:0] out_clk = CLK_HZ / 10_000, out_cnt = 0;           // TRIG_OUT pulse length in clocks (100 us)
    reg [N-1:0] hit;
    reg [2:0]  nhit;
    reg [31:0] src, d;
    wire [1:0] wi = waddr[5:4], ri = raddr[5:4];
    wire       w_ent = wr && waddr[7:6] == 2'b01;
    integer i, j;
    initial begin
        pwm_ld = 0; pwm_us = 0; servo_go = 0;
        for (i = 0; i < N; i = i + 1) begin thr[i] = 0; f_ts[i] = 0; f_pos[i] = 0; c_data[i] = 0; c_act[i] = 0; end
    end
    always @* begin
        nhit = 3'd0;
        for (j = 0; j < N; j = j + 1) begin
            src = c_src[j] ? enc : pos;
            d = src - thr[j];
            hit[j] = armed[j] && (c_dir[j] ? (d[31] || d == 32'd0) : !d[31]);
            nhit = nhit + hit[j];
        end
    end
    assign trig_o = out_cnt != 0;
    always @(posedge clk) begin
        pwm_ld <= 2'b00; servo_go <= 1'b0;
        fresh <= 0;
        if (out_cnt != 0) out_cnt <= out_cnt - 1'b1;
        for (i = 0; i < N; i = i + 1) if (hit[i]) begin
            armed[i] <= 1'b0; fired[i] <= 1'b1; late[i] <= fresh[i];
            f_ts[i] <= ts; f_pos[i] <= c_src[i] ? enc : pos;
            case (c_act[i])
                3'd1: begin pwm_ld[0] <= 1'b1; pwm_us[11:0]  <= c_data[i]; end
                3'd2: begin pwm_ld[1] <= 1'b1; pwm_us[23:12] <= c_data[i]; end
                3'd3: servo_go <= 1'b1;
                default: ;
            endcase
            if (c_out[i]) out_cnt <= out_clk;
        end
        fire_cnt <= fire_cnt + nhit;
        // registers last: writing an entry in the clock it fires wins over the fire
        if (wr && waddr[7:2] == 6'h00 && wdata[0]) begin armed <= 0; fired <= 0; late <= 0; end
        if (wr && waddr[7:2] == 6'h04) out_clk <= wdata[23:0];
        if (w_ent) case (waddr[3:2])
            2'd0: thr[wi] <= wdata;
            2'd1: begin
                c_dir[wi] <= wdata[1]; c_src[wi] <= wdata[2]; c_act[wi] <= wdata[6:4]; c_out[wi] <= wdata[7];
                c_data[wi] <= wdata[27:16];
                armed[wi] <= wdata[0]; fresh[wi] <= wdata[0];
                if (wdata[0]) begin fired[wi] <= 1'b0; late[wi] <= 1'b0; end
            end
            default: ;
        endcase
    end
    always @* begin
        rdata = 32'hDEADBEEF;
        if (raddr[7:6] == 2'b00) case (raddr[5:2])
            4'h1: rdata = {12'd0, late, 4'd0, fired, 4'd0, armed};
            4'h2: rdata = fire_cnt;
            4'h3: rdata = 32'h50544731;      // "PTG1"
            4'h4: rdata = {8'd0, out_clk};
            default: ;
        endcase
        else if (raddr[7:6] == 2'b01) case (raddr[3:2])
            2'd0: rdata = thr[ri];
            2'd1: rdata = {4'd0, c_data[ri], 8'd0, c_out[ri], c_act[ri], 1'b0, c_src[ri], c_dir[ri], armed[ri]};
            2'd2: rdata = f_ts[ri];
            2'd3: rdata = f_pos[ri];
        endcase
    end
endmodule
