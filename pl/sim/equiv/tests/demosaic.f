# demosaic 整链（ui_isp/demosaic_4x_2_0）：top_demosaic + raw_matrix_3x3_buffer + bilinear_interpolation
# + zhenghe + line_buffer_demosaic + mipi_to_raw_converter + TD 生成 blk_mem_gen 明文行为模型。
# golden 的 top_demosaic.v 有 `/*//input`、`/*//output` 笔误（纠正前）导致 iverilog 报嵌套注释；
# 该行用 `~` 让 golden 侧做字节级注释清洗（注释不影响行为），新版重写时自然消除这两处。
~ pl/soc_base/src/ui_isp/demosaic_4x_2_0/top_demosaic.v
pl/soc_base/src/ui_isp/demosaic_4x_2_0/top_demosaic.v
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
