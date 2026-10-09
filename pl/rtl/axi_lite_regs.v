// axi_lite_regs.v — PL 寄存器堆（AXI4-Lite 从机，自研）
// 偏移定义与 ps/common/pl_regs.h、docs/03-PL设计.md 同源（AGENTS.md 约定 2）。
// 设计要点：寄存器写不跨时钟域——本模块运行在 PS GP 时钟域（aclk），
// 各功能子模块用自己的 CDC（握手或格雷码）取数。
//
// 状态：骨架。当前实现 RO/WO 寄存器与读响应；每寄存器位义随各子模块落地逐版细化。
`timescale 1ns / 1ps
module axi_lite_regs #(
    parameter BASE_ADDR = 32'h0000_0000, // design_0 分配后由上层接线决定，参数仅注释用
    parameter NUM_REGS  = 64             // 4KB 窗口按 32bit 寄存器最多 1024，先用 64
)(
    input  wire        aclk,
    input  wire        aresetn,
    // AXI4-Lite slave
    input  wire [31:0] awaddr,
    input  wire        awvalid,
    output wire        awready,
    input  wire [31:0] wdata,
    input  wire        wvalid,
    output wire        wready,
    output wire [1:0]  bresp,
    output wire        bvalid,
    input  wire        bready,
    input  wire [31:0] araddr,
    input  wire        arvalid,
    output wire        arready,
    output wire [31:0] rdata,
    output wire [1:0]  rresp,
    output wire        rvalid,
    input  wire        rready,
    // 用户侧：软件写寄存器（索引 = 偏移>>2）
    output reg  [NUM_REGS-1:0] reg_wr_en,
    output reg  [31:0]         reg_wdata,
    // 用户侧：软件读寄存器（组合送入）
    input  wire [NUM_REGS*32-1:0] user_rdata
);

    // ---- 写地址/写数据锁存 ----
    reg [31:0] awaddr_q;
    reg        aw_full;
    always @(posedge aclk) begin
        if (!aresetn) begin
            aw_full  <= 1'b0;
            awaddr_q <= 32'd0;
        end else if (awvalid && awready) begin
            aw_full  <= 1'b1;
            awaddr_q <= awaddr;
        end else if (bvalid && bready) begin
            aw_full <= 1'b0;
        end
    end
    assign awready = ~aw_full;

    // ---- 写数据 + 寄存器写脉冲 ----
    integer i;
    reg bvalid_q;
    assign wready = aw_full && !bvalid_q;
    always @(posedge aclk) begin
        if (!aresetn) begin
            reg_wr_en  <= {NUM_REGS{1'b0}};
            reg_wdata  <= 32'd0;
            bvalid_q   <= 1'b0;
        end else begin
            reg_wr_en <= {NUM_REGS{1'b0}};
            if (wvalid && wready && aw_full) begin
                reg_wdata <= wdata;
                bvalid_q  <= 1'b1;
                for (i = 0; i < NUM_REGS; i = i + 1)
                    if (awaddr_q[7:2] == i[5:0])
                        reg_wr_en[i] <= 1'b1;
            end else if (bvalid_q && bready) begin
                bvalid_q <= 1'b0;
            end
        end
    end

    assign bresp  = 2'b00; // OKAY
    assign bvalid = bvalid_q;

    // ---- 读 ----
    reg [31:0] araddr_q;
    reg        ar_full, rvalid_q;
    reg [31:0] rdata_q;
    always @(posedge aclk) begin
        if (!aresetn) begin
            ar_full  <= 1'b0;
            araddr_q <= 32'd0;
            rvalid_q <= 1'b0;
            rdata_q  <= 32'd0;
        end else begin
            if (arvalid && arready) begin
                ar_full  <= 1'b1;
                araddr_q <= araddr;
                rdata_q  <= user_rdata[araddr[7:2]*32 +: 32];
            end else if (rvalid_q && rready) begin
                ar_full  <= 1'b0;
            end
            rvalid_q <= (arvalid && arready) | (rvalid_q & ~rready);
        end
    end
    assign arready = ~ar_full;
    assign rdata   = rdata_q;
    assign rresp   = 2'b00;
    assign rvalid  = rvalid_q;

endmodule
