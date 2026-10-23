# verifier-isp 独立对抗性等价回归 —— awb + signal_delay
# awb_seq_div 是 des 自研模块（非本轮重构对象）；TD 的 blk_mem_gen_awb_delay_signal wrapper 例化
# 硬宏 DR1_LOGIC_ERAM，原语用两侧共用行为桩 stubs/vendor_eram.v（与 demosaic 链同一套桩）。
pl/soc_base/src/ui_isp/awb_1_1/awb.v
pl/soc_base/src/ui_isp/awb_1_1/signal_delay.v
pl/soc_base/src/des/awb_seq_div.v
pl/soc_base/src/ui_isp/awb_1_1/blk_mem_gen_awb_delay_signal/blk_mem_gen_awb_delay_signal.v
pl/soc_base/src/ui_isp/awb_1_1/blk_mem_gen_awb_delay_signal/RTL/ram_330dcd7b33e5.v
@ pl/sim/equiv/stubs/vendor_eram.v
