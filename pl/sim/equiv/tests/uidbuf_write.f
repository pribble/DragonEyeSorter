# uidbuf_write.v：帧写主控 + 内部 fs_cap
# 加密/厂家 FIFO：wfifo.v 实际是明文 RTL（pragma protect 是注释），作为普通 RTL 依赖，
# golden 从 rev 取、新版取工作区，两侧同一份，不需要行为桩。
pl/soc_base/src/write_fdma/uidbuf_write.v
pl/soc_base/src/write_fdma/fs_cap.v
pl/soc_base/src/write_fdma/wfifo.v
