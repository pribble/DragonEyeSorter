# awb（自动白平衡，已换 awb_seq_div）+ signal_delay（50 拍延迟线）
# golden 取 baseline.rev，新版取工作区；下面三个文件都参与两侧构建。
pl/soc_base/src/ui_isp/awb_1_1/awb.v
pl/soc_base/src/ui_isp/awb_1_1/signal_delay.v
# awb_seq_div 不在本轮改动范围（golden 与工作区是同一份），走普通路径：golden 从 rev 取、新版取工作区。
pl/soc_base/src/des/awb_seq_div.v
# 厂家 primitive 行为模型：TD 的 blk_mem_gen_awb_delay_signal wrapper 例化硬宏 DR1_LOGIC_ERAM，
# iverilog 编不了；用 models 里 2048x96 / REGMODE NOREG 的行为模型，两侧共用同一份。
@ pl/sim/models/awb_vendor_models.v
