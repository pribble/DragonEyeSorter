#!/bin/sh
# soak.sh — long-run stability test on the board (test report: 工程可靠性「连续运行 2h 无故障」).
#   <本目录>/soak.sh [seconds=7200] [outdir=<DES_DATA>/soak-<uptime>]
# Runs des_app run with CAN (loopback unless SOAK_CAN=normal, then a real PLC / USB-CAN must answer) and canplc as the
# PLC (START, 5 Hz heartbeats, monitor), and samples once a minute: des_app RSS / threads, CPU load, free memory, disk,
# CAN error counters, PL frame counter. Everything goes to <outdir>; summary at the end (and in summary.txt).
# Stop early: kill the soak.sh process (des_app gets SIGINT and prints its totals).
SECS=${1:-7200}
IF=${SOAK_IF:-can0}
MODE=${SOAK_CAN:-loopback}
SELF=$(readlink -f -- "$0" 2>/dev/null || echo "$0")
DES_HOME=$(CDPATH= cd -- "$(dirname -- "$SELF")" && pwd)   # 以脚本自己所在目录为准，不吃继承来的 DES_HOME
[ -f "$DES_HOME/des.env" ] || { echo "soak: 缺 $DES_HOME/des.env（路径都从它取，源文件见 scripts/deploy/out/des.env）" >&2; exit 1; }
. "$DES_HOME/des.env"
DIR=$DES_HOME
[ -x "$DES_TOOLS/canplc" ] || { echo "soak: 缺 $DES_TOOLS/canplc（PLC 陪测对端，部署清单不带它：手动 scp /root/bin/canplc）" >&2; exit 1; }
[ -x "$DES_TOOLS/desreg" ] || { echo "soak: 缺 $DES_TOOLS/desreg（采 PL 帧号用；手动 scp /root/bin/desreg）" >&2; exit 1; }
OUT=${2:-$DES_DATA/soak-$(cut -d. -f1 /proc/uptime)}
mkdir -p "$OUT" || exit 1
cd "$DIR" || exit 1
# the autostart service (des_service.sh) owns the camera / buses: pause it for this run, resume at the end
SVC_PAUSED=0
if [ -x ./des_service.sh ] && ./des_service.sh status | grep -q "^des_service: running"; then
    ./des_service.sh stop > /dev/null && SVC_PAUSED=1
fi
# every exit path (can_up failing included) brings the service back; Ctrl-C below only ends the run early
trap '[ $SVC_PAUSED -eq 1 ] && ./des_service.sh start > /dev/null; SVC_PAUSED=0' EXIT

"$DIR/can_up.sh" "$IF" "$MODE" > "$OUT/can_up.txt" 2>&1 || { cat "$OUT/can_up.txt"; exit 1; }
ip -s -d link show "$IF" > "$OUT/can_before.txt"

./des_app.sh run --can "$IF" -n "$SECS" > "$OUT/run.log" 2>&1 &
APP=$!
sleep 5
PID=$(pidof des_app)
"$DES_TOOLS/canplc" "$IF" start > "$OUT/plc_start.txt" 2>&1
"$DES_TOOLS/canplc" "$IF" monitor "$SECS" > "$OUT/plc.log" 2>&1 &
PLC=$!
trap 'kill -INT $PID 2>/dev/null; kill $PLC 2>/dev/null' INT TERM

echo "t_s,rss_kb,threads,load1,mem_free_kb,mem_avail_kb,disk_free_kb,can_rx,can_tx,can_err,pl_frames" > "$OUT/mon.csv"
T0=$(cut -d. -f1 /proc/uptime)
while kill -0 "$APP" 2>/dev/null; do
    NOW=$(cut -d. -f1 /proc/uptime)
    RSS=$(awk '/VmRSS/ {print $2}' /proc/$PID/status 2>/dev/null)
    THR=$(awk '/Threads/ {print $2}' /proc/$PID/status 2>/dev/null)
    LOAD=$(cut -d' ' -f1 /proc/loadavg)
    MF=$(awk '/MemFree/ {print $2}' /proc/meminfo)
    MA=$(awk '/MemAvailable/ {print $2}' /proc/meminfo)
    DF=$(df -k "$DES_HOME" | awk 'NR==2 {print $4}')
    CAN=$(ip -s link show "$IF" | awk '/RX:/ {getline; rx=$2} /TX:/ {getline; tx=$2} END {print rx","tx}')
    CERR=$(ip -s -d link show "$IF" | awk '/re-started/ {getline; s=$1+$2+$3+$4+$5+$6} END {print s+0}')
    FR=$("$DES_TOOLS/desreg" r 0x014 2>/dev/null)   # PL frame counter (hex), read-only access is safe next to des_app
    echo "$((NOW - T0)),$RSS,$THR,$LOAD,$MF,$MA,$DF,$CAN,$CERR,$FR" >> "$OUT/mon.csv"
    i=0
    while [ $i -lt 60 ] && kill -0 "$APP" 2>/dev/null; do sleep 1; i=$((i + 1)); done
done
wait "$APP"
RC=$?
kill "$PLC" 2>/dev/null
ip -s -d link show "$IF" > "$OUT/can_after.txt"

{
    echo "soak $SECS s, des_app exit $RC, CAN $IF $MODE"
    grep "^== live" "$OUT/run.log"
    awk -F, 'NR==2 {r0=$2} NR>1 {n++; r=$2; if ($2>rmax) rmax=$2; if ($4>lmax) lmax=$4; if (amin==""||$6<amin) amin=$6}
        END {printf "samples %d, RSS first %d kB last %d kB max %d kB, load1 max %s, MemAvailable min %d kB\n", n, r0, r, rmax, lmax, amin}' "$OUT/mon.csv"
    grep -v "fps shown" "$OUT/run.log" | grep -ciE "error|abort|segfault|cannot|failed" | sed "s/^/run.log error lines (status lines excluded): /"
    grep "^\[run\]" "$OUT/run.log" | tail -5
    awk '/^STATUS/ {n++; for (i = 1; i <= NF; i++) if ($i == "hb") h = $(i + 1);
                    if (n > 1) { d = (h - p + 256) % 256; if (d == 0) dup++; else lost += d - 1 } p = h;
                    if ($0 ~ /plc-link/) link++ }
         END {printf "PLC side: STATUS frames %d, lost %d, duplicated %d, with plc-link %d\n", n, lost, dup, link}' "$OUT/plc.log"
    grep "^COUNTS.*recheck [0-9]*$" "$OUT/plc.log" | tail -1
    # interface counters during this run (after - before: the kernel keeps them across can_up, since boot; loopback:
    # every TX frame should come back as RX)
    awk -v iface="$IF" 'FNR == 1 {f++} /RX:/ {getline; rx[f] = $2} /TX:/ {getline; tx[f] = $2}
        END {dtx = tx[2] - tx[1]; drx = rx[2] - rx[1]
             printf "%s packets during the run: TX %d RX %d (loopback difference %d; since boot TX %d RX %d)\n",
                    iface, dtx, drx, dtx - drx, tx[2], rx[2]}' "$OUT/can_before.txt" "$OUT/can_after.txt"
} | tee "$OUT/summary.txt"
# the EXIT trap restarts the service
