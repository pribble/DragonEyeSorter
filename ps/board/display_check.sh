#!/bin/sh
# display_check.sh — HDMI 周期性黑屏排查（用户盯着显示器，三段各 N 秒，默认 180）。
#   阶段 1：停掉 des_app，只剩 Linux 控制台画面（厂家显示通路，不经过我们的程序）
#   阶段 2：des_app display（最简单的相机 → HDMI，没有界面、没有处理线程）
#   阶段 3：恢复自启服务（正常运行：相机 + 叠加 + 底部界面）
# 同时每段都记录显示 VDMA 状态寄存器的变化（/tmp/display_check_vdma.log）。
# SSH 断开 / Ctrl-C 也会恢复自启服务。用法：./display_check.sh [秒]（在本文件所在目录跑）
N=${1:-180}
cd "$(dirname "$0")" || exit 1
LOG=/tmp/display_check_vdma.log
: > "$LOG"

watch_vdma() {   # $1 = phase name; polls until the file /tmp/display_check.stop appears
    prev=""
    while [ ! -e /tmp/display_check.stop ]; do
        cur="$(devmem 0x80200000 32) $(devmem 0x80200004 32)"
        if [ "$cur" != "$prev" ]; then echo "$(cut -d' ' -f1 /proc/uptime) [$1] CR/SR $cur" >> "$LOG"; prev="$cur"; fi
    done
}
phase() {   # $1 = name, $2 = seconds
    rm -f /tmp/display_check.stop
    watch_vdma "$1" &
    W=$!
    sleep "$2"
    touch /tmp/display_check.stop
    wait $W 2>/dev/null
}
restore() {
    touch /tmp/display_check.stop
    kill $(pidof des_app) 2>/dev/null
    sleep 1
    ./des_service.sh start > /dev/null
}
trap 'restore; exit 1' HUP INT TERM

./des_service.sh stop > /dev/null
echo "阶段 1/3（${N} 秒）：屏幕应是 Linux 控制台（黑底文字 / 企鹅），不经过我们的程序。请看这段有没有黑屏。"
phase console "$N"
echo "阶段 2/3（${N} 秒）：屏幕应是全屏相机画面、底部没有界面。请看这段有没有黑屏。"
./des_app.sh display -n "$N" > /tmp/display_check_display.log 2>&1 &
A=$!
phase display "$N"
wait $A 2>/dev/null
echo "阶段 3/3（${N} 秒）：恢复正常运行（底部有界面）。请看这段有没有黑屏。"
./des_service.sh start > /dev/null
phase run "$N"
echo "完成。VDMA 状态变化记录："
cat "$LOG"
echo "请回复：三段里各自大约黑了几次。"
