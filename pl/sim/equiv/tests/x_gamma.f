# verifier-isp 独立对抗性等价回归 —— gamma（GAMMA_10x 特化到 2.2）
# 与 dev-gamma 的 gamma.f 同样的依赖口径：gamma.v + lut_2_2.v 两侧都编；
# 被删的 8 个未用 LUT 只在 golden 版 gamma.v 里被例化（固定 22 时其输出不进端口），用 % 仅编进 golden。
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
