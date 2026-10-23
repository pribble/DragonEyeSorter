# verifier-isp 独立对抗性等价回归 —— isp_top 整链（最重要）
# 40bit AXI4-Stream 随机帧 → BLC → demosaic(wrapper) → data128_96 → awb → image_cut → gamma →
# Saturation → contrast → Brightness → data96_128 → 128bit 输出，逐拍 trace 全部输出。
# top_demosaic.v：普通路径让两侧都编译（golden 自动取 baseline.rev、新版取工作区），
# `~` 行让 golden 副本先做字节级注释清洗（该文件有 TD 专有的 `/*//input` 嵌套注释笔误）。
# 第三轮基线 1f7c232：8 套未用 LUT 已删（golden 里已不存在），不再列 `%` 行。
pl/soc_base/src/ui_isp/isp_top.v
pl/soc_base/src/ui_isp/BLC.v
pl/soc_base/src/ui_isp/demosaic_4x_2_0/top_demosaic.v
~ pl/soc_base/src/ui_isp/demosaic_4x_2_0/top_demosaic.v
pl/soc_base/src/ui_isp/demosaic_4x_2_0/raw_matrix_3x3_buffer.v
pl/soc_base/src/ui_isp/demosaic_4x_2_0/bilinear_interpolation.v
pl/soc_base/src/ui_isp/demosaic_4x_2_0/zhenghe.v
pl/soc_base/src/ui_isp/demosaic_4x_2_0/line_buffer_demosaic.v
pl/soc_base/src/ui_isp/demosaic_4x_2_0/mipi_to_raw_converter.v
pl/soc_base/src/ui_isp/demosaic_4x_2_0/blk_mem_gen_demosaic/blk_mem_gen_demosaic.v
pl/soc_base/src/ui_isp/demosaic_4x_2_0/blk_mem_gen_demosaic/RTL/ram_36adcf8112f5.v
pl/soc_base/src/ui_isp/demosaic_4x_2_0/blk_mem_gen_zhenghe/blk_mem_gen_zhenghe.v
pl/soc_base/src/ui_isp/demosaic_4x_2_0/blk_mem_gen_zhenghe/RTL/ram_8a60c1575720.v
pl/soc_base/src/ui_isp/data128_96.v
pl/soc_base/src/ui_isp/data96_128.v
pl/soc_base/src/ui_isp/awb_1_1/awb.v
pl/soc_base/src/ui_isp/awb_1_1/signal_delay.v
pl/soc_base/src/des/awb_seq_div.v
pl/soc_base/src/ui_isp/awb_1_1/blk_mem_gen_awb_delay_signal/blk_mem_gen_awb_delay_signal.v
pl/soc_base/src/ui_isp/awb_1_1/blk_mem_gen_awb_delay_signal/RTL/ram_330dcd7b33e5.v
pl/soc_base/src/ui_isp/image_cut.v
pl/soc_base/src/ui_isp/gamma/gamma.v
pl/soc_base/src/ui_isp/gamma/lut_2_2.v
pl/soc_base/src/ui_isp/Saturation_adj.v
pl/soc_base/src/ui_isp/contrast_adj.v
pl/soc_base/src/ui_isp/Brightness_adjustment.v
@ pl/sim/equiv/stubs/vendor_eram.v
