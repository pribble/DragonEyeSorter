#!/bin/sh
# shutdown.sh — 关机：停 des_app（数据写完）→ 把硬件看门狗超时拉到最大档 → halt。之后约 2 分钟内断电。
#
#   <本目录>/shutdown.sh
#
# 为什么不能「关机后停住」（2026-10-16 实测 + UG1214 第 12、29 章）：PS 看门狗 dw_wdt @0xF8410000 在芯片的
# **永久在线域**（只受上电复位影响），且配置为始终使能 —— 往 WDT_CR 写 0（关使能）读回仍是 0x09，计数器照减；
# 设备树也没给它配复位线，Linux 驱动只能一直喂。CPU 一停（halt / poweroff 都一样）就没人喂，超时后复位整板 →
# 必然自动重启。这块板也没有软件可控的电源开关，「真正断电」只能拔电 / 关开关。
# 能做的：把超时档位 WDT_TORR.TOP 从 13（2^29 周期，计数 ~16.9MHz → ~32s）调到最大 15（2^31 → ~127s），
# 再写一次 WDT_CRR=0x76 让计数器按新档位重装；内核到 halt 前都在喂狗（只写 CRR，不改 TORR），所以
# halt 完成后还有约 2 分钟可以从容断电。下次开机看门狗随上电复位恢复默认，无需还原。
SELF=$(readlink -f -- "$0" 2>/dev/null || echo "$0")
DES_HOME=$(CDPATH= cd -- "$(dirname -- "$SELF")" && pwd)
[ -f "$DES_HOME/des.env" ] && . "$DES_HOME/des.env"

WDT=0xF8410000   # TORR = +0x04, CRR = +0x0C

# 1. 先让 des_app 正常退出（CSV / NG 图写完）；halt 的关机流程也会停它，这里提前停是为了下面的提示准确
if pidof des_app > /dev/null; then
    if [ -x "$DES_HOME/des_service.sh" ]; then "$DES_HOME/des_service.sh" stop; else kill $(pidof des_app); fi
    i=0
    while pidof des_app > /dev/null && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done
fi

# 2. 看门狗超时调到最大档并重装计数器
devmem $((WDT + 0x04)) 32 0x0F && devmem $((WDT + 0x0C)) 32 0x76 \
    && echo "shutdown: watchdog timeout set to max (~2 min before the board resets itself)" \
    || echo "shutdown: could not change the watchdog timeout (~30 s before the board resets itself)" >&2

# 3. 关机：halt 走完整关机流程（停服务、卸载文件系统），串口最后打印 "reboot: System halted"
sync
echo "shutdown: halting — cut the power once the screen is black / 'System halted' appears (within ~2 min)"
halt
