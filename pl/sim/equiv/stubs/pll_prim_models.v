// 行为桩（仅 pl/sim/equiv 等价回归用；golden 与工作区构建共用同一份）：
//   dr1_phy_pll_59fc11d80b0d — al_ip/PLL/PLL.v 例化的 TD 门级 PLL 原语
//   DR1_LOGIC_BUFG           — PLL 反馈时钟 BUFG
// 假设：由 refclk 产生确定、可复现的 clk0..clk3 与 lock（32 个 refclk 上升沿后拉高）；
//       不模拟真实 PLL 频率/相位/锁定时间。参数表按 PLL.v 例化逐项保留（值不参与行为）。
// 端口名/方向/位宽取自 golden rev 9a905cc 的 PLL.v。

module dr1_phy_pll_59fc11d80b0d #(
    parameter FBKCLK                = "CLKC0_EXT",
    parameter REFCLK_DIV            = 1,
    parameter FBCLK_DIV             = 3,
    parameter CLKC0_FPHASE          = 0,
    parameter CLKC1_FPHASE          = 0,
    parameter CLKC2_FPHASE          = 0,
    parameter CLKC3_FPHASE          = 0,
    parameter CLKC0_CPHASE          = 6,
    parameter CLKC1_CPHASE          = 20,
    parameter CLKC2_CPHASE          = 14,
    parameter CLKC3_CPHASE          = 38,
    parameter CLKC0_DIV             = 7,
    parameter CLKC1_DIV             = 21,
    parameter CLKC2_DIV             = 15,
    parameter CLKC3_DIV             = 39,
    parameter CLKC0_DUTY_INT        = 4,
    parameter CLKC1_DUTY_INT        = 11,
    parameter CLKC2_DUTY_INT        = 8,
    parameter CLKC3_DUTY_INT        = 20,
    parameter CLKC0_ENABLE          = "ENABLE",
    parameter CLKC1_ENABLE          = "ENABLE",
    parameter CLKC2_ENABLE          = "ENABLE",
    parameter CLKC3_ENABLE          = "ENABLE",
    parameter FIN                   = "50.0",
    parameter FEEDBK_MODE           = "NORMAL",
    parameter PLL_USR_RST           = "DISABLE",
    parameter PLL_FEED_TYPE         = "EXTERNAL",
    parameter LPF_RES               = 2,
    parameter LPF_CAP               = 2,
    parameter ICP_CUR               = 12,
    parameter GMC_GAIN              = 2,
    parameter FRAC_ENABLE           = "DISABLE",
    parameter DITHER_ENABLE         = "DISABLE",
    parameter SDM_FRAC              = 0,
    parameter SSC_AMP               = 0.0000,
    parameter MPHASE_ENABLE         = "DISABLE",
    parameter DYN_PHASE_PATH_SEL    = "DISABLE",
    parameter DYN_FPHASE_EN         = "DISABLE",
    parameter CLKC0_FPHASE_RSTSEL   = 0,
    parameter CLKC1_FPHASE_RSTSEL   = 0,
    parameter CLKC2_FPHASE_RSTSEL   = 0,
    parameter CLKC3_FPHASE_RSTSEL   = 0,
    parameter CLKC0_DUTY50          = "ENABLE",
    parameter CLKC1_DUTY50          = "ENABLE",
    parameter CLKC2_DUTY50          = "ENABLE",
    parameter CLKC3_DUTY50          = "ENABLE",
    parameter INTPI                 = 1,
    parameter SSC_ENABLE            = "DISABLE",
    parameter SSC_MODE              = "DOWN",
    parameter SSC_FREQ_DIV          = 0,
    parameter SSC_RNGE              = 0,
    parameter HIGH_SPEED_EN         = "DISABLE"
)(
    input wire          clk4_en,
    output wire         clk4_out,
    output wire         clkb4_out,
    input wire          clk5_en,
    output wire         clk5_out,
    output wire         clkb5_out,
    input wire          clk6_en,
    output wire         clk6_out,
    output wire         clkb6_out,
    input wire          refclk,
    input wire          ssc_en,
    input wire[16:0]    ext_freq_mod_val,
    input wire          ext_freq_mod_en,
    input wire          ext_freq_mod_clk,
    input wire[1:0]     clkc_rst,
    input wire          fbclk,
    output wire[7:0]    drp_rdata,
    output wire         drp_rdy,
    output wire         drp_err,
    input wire[7:0]     drp_wdata,
    input wire[7:0]     drp_addr,
    input wire          drp_wr,
    input wire          drp_rd,
    input wire          drp_sel,
    input wire          drp_rstn,
    input wire          drp_clk,
    input wire[1:0]     cps_step,
    input wire[2:0]     psclksel,
    output wire         psdone,
    input wire          psstep,
    input wire          psdown,
    input wire          psclk,
    input wire          pllpd,
    input wire          reset,
    input wire          wakeup,
    input wire          refclk_rst,
    input wire          clk0_en,
    output wire         clkb0_out,
    output wire         clk0_out,
    input wire          clk1_en,
    output wire         clkb1_out,
    output wire         clk1_out,
    input wire          clk2_en,
    output wire         clkb2_out,
    output wire         clk2_out,
    input wire          clk3_en,
    output wire         clkb3_out,
    output wire         clk3_out,
    output wire         lock
);
    reg [5:0] r_lock_cnt = 6'd0;
    reg [1:0] r_ph       = 2'd0;

    always @(posedge refclk or posedge reset) begin
        if (reset) begin
            r_lock_cnt <= 6'd0;
            r_ph       <= 2'd0;
        end
        else begin
            r_ph <= r_ph + 1'b1;
            if (r_lock_cnt != 6'h3F)
                r_lock_cnt <= r_lock_cnt + 1'b1;
        end
    end

    assign clk0_out  = refclk;
    assign clk1_out  = r_ph[0];
    assign clk2_out  = refclk;
    assign clk3_out  = r_ph[1];
    assign clkb0_out = ~refclk;
    assign clkb1_out = ~r_ph[0];
    assign clkb2_out = ~refclk;
    assign clkb3_out = ~r_ph[1];
    assign clk4_out  = 1'b0;
    assign clk5_out  = 1'b0;
    assign clk6_out  = 1'b0;
    assign clkb4_out = 1'b0;
    assign clkb5_out = 1'b0;
    assign clkb6_out = 1'b0;
    assign lock      = r_lock_cnt[5];
    assign drp_rdata = 8'h00;
    assign drp_rdy   = 1'b0;
    assign drp_err   = 1'b0;
    assign psdone    = 1'b0;
endmodule

module DR1_LOGIC_BUFG (
    input  wire i,
    output wire o
);
    assign o = i;
endmodule
