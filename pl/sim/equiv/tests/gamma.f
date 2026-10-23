# gamma.v：参数特化到 2.2（isp_top 写死 GAMMA_10x=22），只保留 lut_2_2 路径。
# 第三轮基线 1f7c232：8 套未用 LUT 已在上一轮删除（golden 里已不存在），故不再列 `%` 行。
pl/soc_base/src/ui_isp/gamma/gamma.v
pl/soc_base/src/ui_isp/gamma/lut_2_2.v
