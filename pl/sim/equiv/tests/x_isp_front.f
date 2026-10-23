# verifier-isp 独立对抗性等价回归 —— ui_isp 前端 9 个厂家模块
# 全部明文 RTL，无加密/原语依赖：同名 TB 例化 11 个 DUT 实例（BLC/image_cut/Brightness/contrast/
# Saturation 各 2 组参数），逐拍 trace 所有输出。
pl/soc_base/src/ui_isp/raw10_unpacket_4lane.v
pl/soc_base/src/ui_isp/BLC.v
pl/soc_base/src/ui_isp/image_correction.v
pl/soc_base/src/ui_isp/image_cut.v
pl/soc_base/src/ui_isp/data128_96.v
pl/soc_base/src/ui_isp/data96_128.v
pl/soc_base/src/ui_isp/Brightness_adjustment.v
pl/soc_base/src/ui_isp/contrast_adj.v
pl/soc_base/src/ui_isp/Saturation_adj.v
