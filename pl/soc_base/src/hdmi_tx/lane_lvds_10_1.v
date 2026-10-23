// lane_lvds_10_1: 10:1 LVDS serializer (10-bit TMDS word -> DDR serial output).
// Only DEVICE="DR1" is supported (the single configuration used by uihdmi_tx);
// the parameter is kept for interface compatibility.
/* verilator lint_off UNUSEDPARAM */ // DEVICE is retained for interface compatibility only
module lane_lvds_10_1 #(
    parameter DEVICE = "PH1P"   // kept for interface compatibility; only DR1 is supported
)(
    input wire      I_pixel_clk,
    input wire      I_serial_clk,
    input wire      I_rst,

    input wire[9:0] I_data_in,

    output wire     O_serial_out
);
/* verilator lint_on UNUSEDPARAM */
    reg       S_odd_even;
    reg       S_odd_even_1d;
    reg       S_odd_even_2d;
    reg       S_load_en;
    reg       S_load_en_1d;
    reg       S_load_en_2d;
    reg [9:0] S_shift_data;
    reg [1:0] S_oddr_data;
    wire[1:0] S_idata;

    // pixel domain: half-rate flag, one toggle per pixel clock
    always @(posedge I_pixel_clk or posedge I_rst) begin
        if (I_rst)
            S_odd_even <= 1'b0;
        else
            S_odd_even <= ~S_odd_even;
    end

    // serial domain: detect the pixel-clock edge
    always @(posedge I_serial_clk) begin
        S_odd_even_1d <= S_odd_even;
        S_odd_even_2d <= S_odd_even_1d;
    end

    always @(posedge I_serial_clk) begin
        S_load_en    <= S_odd_even_1d ^ S_odd_even_2d;
        S_load_en_1d <= S_load_en;
        S_load_en_2d <= S_load_en_1d;
    end

    // load a new 10-bit word every 5 serial clocks, shift 2 bits per serial clock
    always @(posedge I_serial_clk or posedge I_rst) begin
        if (I_rst)
            S_shift_data <= 'd0;
        else if (S_load_en_2d)
            S_shift_data <= I_data_in;
        else
            S_shift_data <= S_shift_data << 2;
    end

    always @(posedge I_serial_clk)
        S_oddr_data <= S_shift_data[9:8];

    assign S_idata = {S_oddr_data[0], S_oddr_data[1]};

    DR1_LOGIC_ODDR U_EG4_ODDR (
        .clk (I_serial_clk),
        .rst (I_rst),

        .d1  (S_oddr_data[0]),
        .d0  (S_oddr_data[1]),

        .q   (O_serial_out)
    );
endmodule
