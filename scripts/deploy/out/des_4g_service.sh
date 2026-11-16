#!/bin/sh
# des_4g_service.sh — start the 4G uplink (des_4g) at boot and restart it if it dies (BusyBox init, /etc/init.d/S??* style).
#
#   <本目录>/des_4g_service.sh start|stop|restart|status
#   Install (once, changes /etc):  ln -s <本目录>/des_4g_service.sh /etc/init.d/S91des4g   （排在 S90des 主程序之后）
#   Disable: delete or rename <本目录>/des_4g.json (no config = nothing to start), or delete the symlink.
#
# des_4g itself retries the modem / network / broker with back-off (2 s .. 60 s) and keeps running while 4G is down;
# this supervisor only covers the process exiting (crash, AT port lost for good). No give-up: a demo board must come
# back by itself. Log: <DES_DATA>/des_4g.log (status lines only, no AT trace; rotated at 1 MB).
# 路径来自同目录的 des.env；程序在 $DES_TOOLS（默认 /root/bin）下，配置是本目录的 des_4g.json（含操作密钥，权限 600，不入库）。
SELF=$(readlink -f -- "$0" 2>/dev/null || echo "$0")
DES_HOME=$(CDPATH= cd -- "$(dirname -- "$SELF")" && pwd)
[ -f "$DES_HOME/des.env" ] || { echo "des_4g_service: 缺 $DES_HOME/des.env" >&2; exit 1; }
. "$DES_HOME/des.env"

BIN=${DES_4G_BIN:-$DES_TOOLS/des_4g}
CONF=${DES_4G_CONFIG:-$DES_HOME/des_4g.json}
LOG=${DES_4G_LOG:-$DES_DATA/des_4g.log}
PIDF=${DES_4G_PIDF:-$DES_DATA/des_4g.pid}

log() { mkdir -p "$(dirname "$LOG")"; echo "$(date '+%F %T') $*" >> "$LOG"; }
rotate() { if [ -f "$LOG" ] && [ "$(wc -c < "$LOG")" -gt 1048576 ]; then mv "$LOG" "$LOG.1"; fi; }
running() { [ -f "$PIDF" ] && kill -0 "$(cat "$PIDF")" 2>/dev/null; }

supervise() {
    mkdir -p "$(dirname "$PIDF")"
    echo $$ > "$PIDF"
    child=""
    quit=0
    trap 'quit=1; [ -n "$child" ] && kill -TERM "$child" 2>/dev/null' TERM INT
    delay=2
    while [ $quit -eq 0 ]; do
        rotate
        log "start: des_4g run $CONF"
        t0=$(date +%s)
        "$BIN" run "$CONF" >> "$LOG" 2>&1 &
        child=$!
        wait "$child"
        rc=$?
        while kill -0 "$child" 2>/dev/null; do wait "$child"; rc=$?; done
        child=""
        [ $quit -ne 0 ] && { log "stopped (des_4g exit $rc)"; break; }
        now=$(date +%s)
        log "des_4g exited with $rc after $((now - t0)) s; again in $delay s"
        [ $((now - t0)) -gt 120 ] && delay=2
        sleep $delay
        delay=$((delay * 2)); [ $delay -gt 60 ] && delay=60
    done
    rm -f "$PIDF"
}

case "$1" in
start)
    [ -f "$CONF" ] || { echo "des_4g_service: 没有 $CONF（未配置 4G），不启动"; exit 0; }
    [ -x "$BIN" ] || { echo "des_4g_service: 缺 $BIN"; exit 1; }
    if running; then echo "des_4g_service: already running (pid $(cat "$PIDF"))"; exit 0; fi
    if pidof des_4g > /dev/null 2>&1; then
        echo "des_4g_service: 已有不归本服务管的 des_4g 在跑（pid $(pidof des_4g)），两份会用同一客户端名互相挤下线：先 kill 它"; exit 1
    fi
    mkdir -p "$(dirname "$PIDF")"
    setsid "$0" _supervise < /dev/null > /dev/null 2>&1 &
    echo $! > "$PIDF"
    i=0
    while ! running && [ $i -lt 10 ]; do sleep 1; i=$((i + 1)); done
    if running; then echo "des_4g_service: started (pid $(cat "$PIDF"), log $LOG)"
    else rm -f "$PIDF"; echo "des_4g_service: 没起来（看 $LOG）"; fi
    ;;
stop)
    if ! running; then echo "des_4g_service: not running"; rm -f "$PIDF"; exit 0; fi
    pid=$(cat "$PIDF")
    kill -TERM "$pid"
    i=0
    while kill -0 "$pid" 2>/dev/null && [ $i -lt 15 ]; do sleep 1; i=$((i + 1)); done   # des_4g publishes "offline" first
    if kill -0 "$pid" 2>/dev/null; then kill -KILL "$pid"; for p in $(pidof des_4g 2>/dev/null); do kill -KILL "$p"; done; fi
    rm -f "$PIDF"
    echo "des_4g_service: stopped"
    ;;
_supervise) supervise ;;
restart) "$0" stop; "$0" start ;;
status)
    if running; then echo "des_4g_service: running (pid $(cat "$PIDF"))"; ps | grep "[d]es_4g run"
    else echo "des_4g_service: not running"; fi
    tail -3 "$LOG" 2>/dev/null
    ;;
*) echo "usage: $0 start|stop|restart|status"; exit 1 ;;
esac
