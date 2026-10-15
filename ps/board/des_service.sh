#!/bin/sh
# des_service.sh — start des_app at boot and restart it if it dies (BusyBox init, /etc/init.d/S??* style).
#
#   /root/des/des_service.sh start|stop|restart|status
#   Install (once, changes /etc):  ln -s /root/des/des_service.sh /etc/init.d/S90des
#   Disable without touching /etc: set ENABLED=0 in /root/des/autostart.conf (or delete the symlink)
#
# Safe by default: with CAN the line starts IDLE and moves nothing until the PLC (or the HMI button) sends START;
# the arm only moves for real when ARGS contains --go. Without CAN (CAN="") des_app starts in RUN.
# The supervisor restarts des_app after a crash (2 s, doubling to 30 s) and gives up after 5 crashes within
# 5 minutes, so a broken build does not flap forever. Log: /root/des/data/service.log (rotated at 1 MB).
DIR=/root/des
CONF=$DIR/autostart.conf
LOG=$DIR/data/service.log
PIDF=/var/run/des_service.pid

if [ ! -f "$CONF" ]; then
    cat > "$CONF" <<'EOF'
# des_service.sh settings (shell syntax)
ENABLED=1         # 0 = do not start at boot
MODE=run          # des_app mode: run (production) / live (no belt, arm, CAN)
CAN=can0          # CAN interface for the PLC; empty = no CAN (des_app then starts in RUN)
CAN_MODE=normal   # normal (USB-CAN partner on the bus) / loopback (self test)
ARGS=""           # extra des_app options, e.g. "--go" (arm really moves) or "--belt 60"
EOF
fi
. "$CONF"

log() { mkdir -p "$DIR/data"; echo "$(date '+%F %T') $*" >> "$LOG"; }

rotate() {
    if [ -f "$LOG" ] && [ "$(wc -c < "$LOG")" -gt 1048576 ]; then mv "$LOG" "$LOG.1"; fi
}

running() { [ -f "$PIDF" ] && kill -0 "$(cat "$PIDF")" 2>/dev/null; }

supervise() {
    echo $$ > "$PIDF"   # our own pid (start wrote setsid's, the same unless setsid had to fork)
    child=""
    quit=0
    trap 'quit=1; [ -n "$child" ] && kill -TERM "$child" 2>/dev/null' TERM INT
    delay=2
    crashes=""
    while [ $quit -eq 0 ]; do
        rotate
        if [ "$MODE" = run ] && [ -n "$CAN" ]; then
            "$DIR/can_up.sh" "$CAN" "$CAN_MODE" > /dev/null 2>&1 || log "can_up $CAN $CAN_MODE failed"
            set -- run --can "$CAN" $ARGS
        else
            set -- "$MODE" $ARGS
        fi
        [ $quit -ne 0 ] && break   # stop arrived while can_up ran (the trap only runs between commands)
        log "start: des_app $*"
        t0=$(date +%s)
        "$DIR/des_app.sh" "$@" >> "$LOG" 2>&1 &
        child=$!
        wait "$child"
        rc=$?
        while kill -0 "$child" 2>/dev/null; do wait "$child"; rc=$?; done   # wait returns early on a trapped signal
        child=""
        [ $quit -ne 0 ] && { log "stopped (des_app exit $rc)"; break; }
        now=$(date +%s)
        log "des_app exited with $rc after $((now - t0)) s"
        recent=""
        for t in $crashes $now; do [ $((now - t)) -le 300 ] && recent="$recent $t"; done
        crashes=$recent
        if [ "$(echo $crashes | wc -w)" -ge 5 ]; then
            log "5 exits within 5 minutes: giving up (fix it, then: des_service.sh start)"
            break
        fi
        [ $((now - t0)) -gt 60 ] && delay=2   # it ran for a while: start again quickly
        sleep $delay
        delay=$((delay * 2)); [ $delay -gt 30 ] && delay=30
    done
    rm -f "$PIDF"
}

case "$1" in
start)
    if [ "$ENABLED" != 1 ] && [ "$2" != force ]; then echo "des_service: disabled in $CONF"; exit 0; fi
    if running; then echo "des_service: already running (pid $(cat "$PIDF"))"; exit 0; fi
    # own session, no inherited descriptors: otherwise the caller (an SSH command) never sees EOF
    setsid "$0" _supervise < /dev/null > /dev/null 2>&1 &
    echo $! > "$PIDF"
    echo "des_service: started (pid $!, log $LOG)"
    ;;
stop)
    if ! running; then echo "des_service: not running"; rm -f "$PIDF"; exit 0; fi
    pid=$(cat "$PIDF")
    kill -TERM "$pid"
    i=0
    while kill -0 "$pid" 2>/dev/null && [ $i -lt 10 ]; do sleep 1; i=$((i + 1)); done
    if kill -0 "$pid" 2>/dev/null; then
        kill -KILL "$pid"; kill -KILL $(pidof des_app) 2>/dev/null; log "stop: killed"   # BusyBox: no pkill
    fi
    rm -f "$PIDF"
    echo "des_service: stopped"
    ;;
_supervise) supervise ;;
restart) "$0" stop; "$0" start ;;
status)
    if running; then
        echo "des_service: running (pid $(cat "$PIDF"))"; ps | grep "[d]es_app -c"
    else
        echo "des_service: not running"
    fi
    tail -5 "$LOG" 2>/dev/null
    ;;
*) echo "usage: $0 start|stop|restart|status"; exit 1 ;;
esac
