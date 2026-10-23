// 安路 TD 原语 DR1_LOGIC_ERAM 的行为模型 —— 仅用于 pl/sim/equiv 等价仿真，不参与综合。
// 依据 TD 生成 IP 的配置：MODE="DP"，REGMODE_A/B="NOREG"（地址后 1 拍出数），
// WRITEMODE_B="READBEFOREWRITE"（同址同拍读写时 B 口读出旧值）。与 pl/sim/models/awb_vendor_models.v
// 里 blk_mem_gen_awb_delay_signal 的建模口径一致。golden 与新版两侧共用本文件。
module DR1_LOGIC_ERAM #(
    parameter MODE          = "DP",
    parameter DATA_WIDTH_A  = 20,
    parameter DATA_WIDTH_B  = 20,
    parameter ADDR_WIDTH_A  = 10,
    parameter ADDR_WIDTH_B  = 10,
    parameter DATA_DEPTH_A  = 1024,
    parameter DATA_DEPTH_B  = 1024,
    parameter REGMODE_A     = "NOREG",
    parameter REGMODE_B     = "NOREG",
    parameter WRITEMODE_A   = "NORMAL",
    parameter WRITEMODE_B   = "READBEFOREWRITE",
    parameter RESETMODE_A   = "ASYNC",
    parameter RESETMODE_B   = "ASYNC",
    parameter ASYNC_RESET_RELEASE_A = "ASYNC",
    parameter ASYNC_RESET_RELEASE_B = "ASYNC",
    parameter OREGSET_A     = "RESET",
    parameter OREGSET_B     = "RESET",
    parameter CLKMODE       = "ASYNC",
    parameter IMPLEMENT     = "20K(FAST)",
    parameter SSROVERCE     = "DISABLE",
    parameter ECC_ENCODE    = "DISABLE",
    parameter ECC_DECODE    = "DISABLE",
    parameter INIT_FILE     = "NONE",
    parameter FILL_ALL      = "NONE"
) (
    input  wire [DATA_WIDTH_A-1:0] dia,
    input  wire [DATA_WIDTH_B-1:0] dib,
    input  wire [ADDR_WIDTH_A-1:0] addra,
    input  wire [ADDR_WIDTH_B-1:0] addrb,
    input  wire                    cea,
    input  wire                    ceb,
    input  wire                    ocea,
    input  wire                    oceb,
    input  wire                    clka,
    input  wire                    clkb,
    input  wire                    wea,
    input  wire                    web,
    input  wire                    bea,
    input  wire                    beb,
    input  wire                    rsta,
    input  wire                    rstb,
    output reg  [DATA_WIDTH_A-1:0] doa,
    output reg  [DATA_WIDTH_B-1:0] dob,
    output wire                    ecc_sbiterr,
    output wire                    ecc_dbiterr,
    input  wire [31:0]             ecc_sbiterrinj,
    input  wire [31:0]             ecc_dbiterrinj
);
    // 双口共用同一块存储；按 A/B 口较宽者开存储，窄口读/写时零扩展/截断
    // （本工程三份 wrapper 都是 A/B 同位宽，此处仅为非对称位宽配置兜底）
    localparam integer DW = (DATA_WIDTH_A > DATA_WIDTH_B) ? DATA_WIDTH_A : DATA_WIDTH_B;
    reg [DW-1:0] mem [0:DATA_DEPTH_A-1];
    integer i;

    initial begin
        for (i = 0; i < DATA_DEPTH_A; i = i + 1) mem[i] = {DW{1'bx}};
    end

    always @(posedge clka) begin
        if (cea) doa <= mem[addra][DATA_WIDTH_A-1:0];
        if (cea && wea) mem[addra] <= {{(DW-DATA_WIDTH_A){1'b0}}, dia};
    end

    always @(posedge clkb) begin
        if (ceb) dob <= mem[addrb][DATA_WIDTH_B-1:0];
        if (ceb && web) mem[addrb] <= {{(DW-DATA_WIDTH_B){1'b0}}, dib};
    end

    assign ecc_sbiterr = 1'b0;
    assign ecc_dbiterr = 1'b0;
endmodule
