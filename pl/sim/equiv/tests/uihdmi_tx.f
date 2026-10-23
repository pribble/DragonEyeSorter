# uihdmi_tx：DR1 配置（工程唯一可达），加密 TMDS 控制器用行为桩，
# 明文 hdmi_tx_phy_wrapper / lane_lvds_10_1 走真实链路，DR1 ODDR 原语用桩
pl/soc_base/src/hdmi_tx/uihdmi_tx.v
pl/soc_base/src/hdmi_tx/hdmi_phy_warpper.v
pl/soc_base/src/hdmi_tx/lane_lvds_10_1.v
@ pl/sim/equiv/stubs/hdmi_enc_model.v
@ pl/sim/equiv/stubs/td_prim_models.v
