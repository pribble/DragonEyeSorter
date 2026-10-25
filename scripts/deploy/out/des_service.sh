#!/bin/sh
# des_service.sh — start des_app at boot and restart it if it dies (BusyBox init, /etc/init.d/S??* style).
#
#   <本目录>/des_service.sh start|stop|restart|status
#   Install (once, changes /etc):  ln -s <本目录>/des_service.sh /etc/init.d/S90des   （按实际部署目录写）
#   Disable without touching /etc: set ENABLED=0 in <本目录>/autostart.conf (or delete the symlink)
#
# Safe by default: with CAN the line starts IDLE and moves nothing until the PLC (or the HMI button) sends START;
# the arm only moves for real when ARGS contains --go. Without CAN (CAN="") des_app starts in RUN.
# The supervisor restarts des_app after a crash (2 s, doubling to 30 s) and gives up after 5 crashes within
# 5 minutes, so a broken build does not flap forever. Log: <DES_SERVICE_LOG> (rotated at 1 MB).
#
# 路径全部来自同目录的 des.env（板端路径唯一来源），本文件不写死任何绝对路径：整份目录拷到别处部署
# （deploy --dest）时，配置 / 日志 / PID 文件跟着走；每份目录一个 PID 文件，两份服务不会互相认领。
SELF=$(readlink -f -- "$0" 2>/dev/null || echo "$0")
DES_HOME=$(CDPATH= cd -- "$(dirname -- "$SELF")" && pwd)   # 以脚本自己所在目录为准，不吃继承来的 DES_HOME
[ -f "$DES_HOME/des.env" ] || { echo "des_service: 缺 $DES_HOME/des.env（路径都从它取，源文件见 scripts/deploy/out/des.env）" >&2; exit 1; }
. "$DES_HOME/des.env"

DIR=$DES_HOME
CONF=$DES_AUTOSTART_CONF
LOG=$DES_SERVICE_LOG
PIDF=$DES_PIDF

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

log() { mkdir -p "$(dirname "$LOG")"; echo "$(date '+%F %T') $*" >> "$LOG"; }

rotate() {
    if [ -f "$LOG" ] && [ "$(wc -c < "$LOG")" -gt 1048576 ]; then mv "$LOG" "$LOG.1"; fi
}

running() { [ -f "$PIDF" ] && kill -0 "$(cat "$PIDF")" 2>/dev/null; }

supervise() {
    mkdir -p "$(dirname "$PIDF")"   # 全新部署没有 data/：目录必须先于任何 PID 写入存在
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
    mkdir -p "$(dirname "$PIDF")"   # 全新部署没有 data/，先建目录再写 PID（否则 status/stop 失效）
    setsid "$0" _supervise < /dev/null > /dev/null 2>&1 &
    echo $! > "$PIDF"
    i=0
    while ! running && [ $i -lt 10 ]; do sleep 1; i=$((i + 1)); done   # setsid 可能 fork：等 _supervise 用自己的 pid 覆写
    if running; then echo "des_service: started (pid $(cat "$PIDF"), log $LOG)"
    else rm -f "$PIDF"; echo "des_service: 没起来（$LOG 里应有原因；重试：$0 start）"; fi
    ;;
stop)
    if ! running; then echo "des_service: not running"; rm -f "$PIDF"; exit 0; fi
    pid=$(cat "$PIDF")
    kill -TERM "$pid"
    i=0
    while kill -0 "$pid" 2>/dev/null && [ $i -lt 10 ]; do sleep 1; i=$((i + 1)); done
    if kill -0 "$pid" 2>/dev/null; then
        # KILL 兜底只杀**本目录**这一份（--dest 测试目录里可能还有一份在跑，见 des.env "两份服务不打架"）：
        # des_app 的命令行带着本目录的 des_app.json（des_app.sh exec -c "$DES_CONFIG"）
        for p in $(pidof des_app 2>/dev/null); do
            # "DIR/*" 带路径分隔：/root/des 不能子串匹配到 /root/des-test 的测试份
            case $(tr '\0' ' ' < /proc/$p/cmdline 2>/dev/null) in
                *"$DIR"/*) kill -KILL "$p"; log "stop: killed des_app pid $p" ;;
            esac
        done
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
    grep -E "^(19|20)[0-9][0-9]-" "$LOG" 2>/dev/null | tail -3   # service events only (des_app output is in the log too)
    ;;
*) echo "usage: $0 start|stop|restart|status"; exit 1 ;;
esac
