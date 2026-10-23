# x_system_top：system_top 整顶层行为对照（PS 块设计/MIPI 顶层用行为桩，其余用各侧真实 RTL）
# 注意：
#   - des_*.v 用 `include "des_common.vh"，故本 test 需在仓库根目录运行（-Ipl/soc_base/src/des 相对路径）；
#   - des/stepper_ctrl.v 里有一个叫 SV 关键字 dist 的 wire，-g2012 下编译不过；
#     本 test 用 audit/x_stepper_ctrl_g2012.v（同一份 des RTL 仅把 dist 改名为 distv，golden/新两侧共用，
#     sha256 见验证报告）代替；因此不需要 -g2005，可与新的 wfifo（SV 变量连续赋值）一起编译。
pl/soc_base/src/system_top.v
pl/soc_base/src/des/des_regs.v
pl/soc_base/src/des/servo_bus.v
pl/soc_base/src/des/des_vision.v
pl/soc_base/src/des/des_pwm.v
pl/soc_base/src/des/des_io.v
pl/soc_base/src/des/awb_seq_div.v
pl/soc_base/src/write_fdma/fs_cap.v
pl/soc_base/src/write_fdma/uiFDMA.v
pl/soc_base/src/write_fdma/uidbuf_write.v
pl/soc_base/src/write_fdma/wfifo.v
+ -Ipl/soc_base/src/des
@ pl/sim/equiv/audit/x_stepper_ctrl_g2012.v
@ pl/sim/equiv/stubs/x_system_top_stubs.v
