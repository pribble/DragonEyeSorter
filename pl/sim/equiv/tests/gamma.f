# gamma.v：参数特化到 2.2（isp_top 写死 GAMMA_10x=22），新版只保留 lut_2_2 路径。
# % 前缀 = 仅 golden 侧依赖：golden 版 gamma.v 仍例化 8 套已删除的未用 LUT（走 mux，
#   固定 22 时输出不参与任何端口），新版不再引用它们，故只编进 golden 构建。
pl/soc_base/src/ui_isp/gamma/gamma.v
pl/soc_base/src/ui_isp/gamma/lut_2_2.v
% pl/soc_base/src/ui_isp/gamma/lut_0_8.v
% pl/soc_base/src/ui_isp/gamma/lut_1_2.v
% pl/soc_base/src/ui_isp/gamma/lut_1_4.v
% pl/soc_base/src/ui_isp/gamma/lut_1_6.v
% pl/soc_base/src/ui_isp/gamma/lut_1_8.v
% pl/soc_base/src/ui_isp/gamma/lut_2_0.v
% pl/soc_base/src/ui_isp/gamma/lut_2_4.v
% pl/soc_base/src/ui_isp/gamma/lut_2_6.v
