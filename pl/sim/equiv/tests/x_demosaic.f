# verifier-isp 独立对抗性等价回归 —— demosaic 整链（厂家 6 模块）
# 写法与 dev-demosaic 的 demosaic.f 一致：golden 的 top_demosaic.v 有 TD 专有嵌套块注释、Icarus 不认，
# 且该 wrapper 只是纯连线（无逻辑），故 TB 按它的原始连线直接例化 raw_matrix_3x3_buffer + bilinear。
# 两个实例：A = wrapper 原参数 1920x1080/data_complete_delay=50/BAYER_MODE="RGGB"（isp_top 实际配置）；
#           B = 64x8 小参数，快速跑满行/列/帧计数器回卷边界。
# TD blk_mem_gen_*/RTL/ram_*.v 是明文 wrapper（不改），其 DR1_LOGIC_ERAM 原语用两侧共用桩。
pl/soc_base/src/ui_isp/demosaic_4x_2_0/raw_matrix_3x3_buffer.v
pl/soc_base/src/ui_isp/demosaic_4x_2_0/bilinear_interpolation.v
pl/soc_base/src/ui_isp/demosaic_4x_2_0/zhenghe.v
pl/soc_base/src/ui_isp/demosaic_4x_2_0/line_buffer_demosaic.v
pl/soc_base/src/ui_isp/demosaic_4x_2_0/mipi_to_raw_converter.v
pl/soc_base/src/ui_isp/demosaic_4x_2_0/blk_mem_gen_demosaic/blk_mem_gen_demosaic.v
pl/soc_base/src/ui_isp/demosaic_4x_2_0/blk_mem_gen_demosaic/RTL/ram_36adcf8112f5.v
pl/soc_base/src/ui_isp/demosaic_4x_2_0/blk_mem_gen_zhenghe/blk_mem_gen_zhenghe.v
pl/soc_base/src/ui_isp/demosaic_4x_2_0/blk_mem_gen_zhenghe/RTL/ram_8a60c1575720.v
@ pl/sim/equiv/stubs/vendor_eram.v
