#!/bin/sh
# pl_check.sh — PL regression after every new BOOT.bin (board side; runs from whatever folder it is deployed in).
# No actuators needed.
# Checks: des_regs ID / VERSION, sensor frame rate seen by the PL (FRAME_CNT over 1 s, FRAME_PERIOD), des_vision (v6+):
# register read-back, statistics advancing, pixel clock = CYCLES x fps, display modes 1..5 accepted; des_pwm (v7+):
# width / enable read-back (v10: no lamp bit) and period counter; command response latencies (servo bus, stepper); v9+: stepper
# target rewrite (TARGET = POS while moving stops there, a target behind ramps down); v11+: des_io ID, e-stop not latched,
# encoder count steady with the belt standing; v16+: CAP_ENC0/1 mapped, des_485 ID and one RS485 request
# (address 247, nobody answers: timeout) with its on-wire duration; v17+: des_bpi / des_ptrig IDs, both idle after
# reset, belt-PI measurement reads STALL with the belt standing, a trigger armed behind the position fires. Prints PASS /
# FAIL per item and a summary; exit code = failures. Uses the camera (des_tool probe in the background).
SELF=$(readlink -f -- "$0" 2>/dev/null || echo "$0")
DES_HOME=$(CDPATH= cd -- "$(dirname -- "$SELF")" && pwd)   # 以脚本自己所在目录为准，不吃继承来的 DES_HOME
[ -f "$DES_HOME/des.env" ] || { echo "pl_check: 缺 $DES_HOME/des.env（路径都从它取，源文件见 scripts/deploy/out/des.env）" >&2; exit 1; }
. "$DES_HOME/des.env"
cd "$DES_HOME" || exit 1
[ -x "$DES_TOOLS/desreg" ] || { echo "pl_check: 缺 $DES_TOOLS/desreg（scp ps/build/desreg \$BOARD_HOST:/root/bin/）" >&2; exit 1; }
[ -x ./des_tool.sh ] || { echo "pl_check: 缺 ./des_tool.sh（scp scripts/deploy/out/des_tool.sh \$BOARD_HOST:/root/des/）" >&2; exit 1; }
# the autostart service (des_service.sh) owns the camera / buses: pause it for this run, resume at the end
SVC_PAUSED=0
if [ -x ./des_service.sh ] && ./des_service.sh status | grep -q "^des_service: running"; then
    ./des_service.sh stop > /dev/null && SVC_PAUSED=1
fi
# any exit (normal, Ctrl-C, ssh dropped) brings the service back - a check must not leave the board without des_app
trap 'wait; [ $SVC_PAUSED -eq 1 ] && ./des_service.sh start > /dev/null; SVC_PAUSED=0' EXIT   # wait: probe holds the camera
trap 'exit 130' INT TERM HUP
R=$DES_TOOLS/desreg
FAILS=0
pass() { echo "PASS $*"; }
fail() { echo "FAIL $*"; FAILS=$((FAILS + 1)); }
hex() { printf "%d" "$1"; }

ID=$($R r 0x000); VER=$(hex "$($R r 0x004)")
[ "$ID" = "0x44455301" ] && pass "des_regs ID $ID" || fail "des_regs ID $ID"
echo "     VERSION $VER"

./des_tool.sh probe -n 12 > /tmp/pl_check_probe.log 2>&1 &
sleep 4

# --- frames seen by the PL (ISP tuser edges in the pixel clock domain)
F0=$(hex "$($R r 0x014)"); T0=$(hex "$($R r 0x00C)")
sleep 1
F1=$(hex "$($R r 0x014)"); T1=$(hex "$($R r 0x00C)")
PER=$(hex "$($R r 0x01C)")
FPS=$(awk -v f=$((F1 - F0)) -v t=$((T1 - T0)) 'BEGIN { if (t < 0) t += 4294967296; printf "%.2f", f * 75e6 / t }')
PFPS=$(awk -v p="$PER" 'BEGIN { printf "%.2f", p ? 75e6 / p : 0 }')
awk -v a="$FPS" 'BEGIN { exit !(a > 59 && a < 61) }' && pass "PL frame count ${FPS} fps" || fail "PL frame count ${FPS} fps (FRAME_CNT $F0 -> $F1)"
awk -v a="$PFPS" 'BEGIN { exit !(a > 59 && a < 61) }' && pass "FRAME_PERIOD $PER = ${PFPS} fps" || fail "FRAME_PERIOD $PER = ${PFPS} fps"

# --- des_vision
if [ "$VER" -ge 6 ]; then
    VID=$($R r 0x314)
    [ "$VID" = "0x56495331" ] && pass "des_vision ID $VID" || fail "des_vision ID $VID (read data path)"
    A=$($R vis); sleep 0.5; B=$($R vis)
    echo "     $B"
    S0=$(echo "$A" | awk '{print $2}'); S1=$(echo "$B" | awk '{print $2}')
    [ -n "$S0" ] && [ "$S1" -gt "$S0" ] 2>/dev/null && pass "des_vision statistics advance ($S0 -> $S1 in 0.5 s)" \
        || fail "des_vision statistics do not advance ($S0 -> $S1)"
    CYC=$(echo "$B" | awk -F'|' '{print $3}' | awk '{print $1}')
    PCLK=$(awk -v c="$CYC" -v f="$PFPS" 'BEGIN { printf "%.1f", c * f / 1e6 }')
    awk -v a="$PCLK" 'BEGIN { exit !(a > 100 && a < 125) }' && pass "pixel clock ${PCLK} MHz (CYCLES $CYC; constraint 125)" \
        || fail "pixel clock ${PCLK} MHz (CYCLES $CYC)"
    for M in 1 2 3 4 5 0; do
        $R vis $M > /dev/null; sleep 0.1
        GOT=$($R vis | awk '{print $4}')
        [ "$GOT" = "$M" ] && pass "des_vision mode $M active" || fail "des_vision mode $M (reads $GOT)"
    done
fi

# --- des_pwm
if [ "$VER" -ge 7 ]; then
    PID_=$($R r 0x414)
    [ "$PID_" = "0x50574d31" ] && pass "des_pwm ID $PID_" || fail "des_pwm ID $PID_"
    $R pwm 0 1500 > /dev/null; $R pwm 1 1200 > /dev/null
    W0=$(hex "$($R r 0x408)"); W1=$(hex "$($R r 0x40C)"); C=$(hex "$($R r 0x400)")
    N0=$(hex "$($R r 0x410)"); sleep 1; N1=$(hex "$($R r 0x410)")
    [ "$W0" = 1500 ] && [ "$W1" = 1200 ] && pass "PWM widths read back 1500 / 1200 us" || fail "PWM widths $W0 / $W1"
    [ $((C & 0x3)) = $((0x3)) ] && pass "PWM enable read back (CTRL $C)" || fail "PWM CTRL $C"
    awk -v n=$((N1 - N0)) 'BEGIN { exit !(n >= 48 && n <= 52) }' && pass "PWM periods $((N1 - N0)) / s (50 Hz)" \
        || fail "PWM periods $((N1 - N0)) / s"
    $R pwm off > /dev/null
fi

# --- des_io (v11): e-stop latch, IR1 / LIMIT1, belt encoder
if [ "$VER" -ge 11 ]; then
    IID=$($R r 0x534)
    [ "$IID" = "0x494f3031" ] && pass "des_io ID $IID" || fail "des_io ID $IID"
    ST=$(hex "$($R r 0x500)")
    [ $((ST & 1)) = 0 ] && pass "e-stop not latched (STATUS $ST)" \
        || fail "e-stop latched (STATUS $ST): release the button, then desreg io estop-clr"
    E0=$(hex "$($R r 0x520)"); sleep 0.5; E1=$(hex "$($R r 0x520)"); ERR=$(hex "$($R r 0x524)")
    [ "$E0" = "$E1" ] && pass "encoder count steady while the belt stands ($E1, A$(((ST >> 4) & 1)) B$(((ST >> 5) & 1)), err $ERR)" \
        || fail "encoder count drifts with the belt standing ($E0 -> $E1): noise on ENC_A/B?"
fi

# --- v16: encoder in the frame capture latch, RS485 (des_485)
if [ "$VER" -ge 16 ]; then
    CE0=$($R r 0x038); CE1=$($R r 0x03C); EN=$(hex "$($R r 0x520)")
    if [ "$CE0" = "0xdeadbeef" ] || [ "$CE1" = "0xdeadbeef" ]; then fail "CAP_ENC0/1 not mapped ($CE0 / $CE1)"
    else
        pass "CAP_ENC0/1 mapped (last capture: encoder $(hex "$CE0") -> $(hex "$CE1"); now $EN)"
        [ "$(hex "$CE1")" = "$EN" ] || echo "     note: the belt / encoder moved since the last captured frame"
    fi
    RID=$($R r 0x640)
    [ "$RID" = "0x34383501" ] && pass "des_485 ID $RID" || fail "des_485 ID $RID"
    $R 485 cfg 9600 N 30 > /dev/null
    OUT=$($R 485 tx F7 03 00 00 00 01)   # address 247: normally nobody answers
    echo "     485 tx F7 03 00 00 00 01 @9600: $OUT"
    MS=$(echo "$OUT" | sed -n 's/.*request \([0-9.]*\) ms.*/\1/p')
    case "$OUT" in
        timeout*|ok*)
            # 8 bytes x 10 bits / 9600 = 8.333 ms on the wire (the DE lead bit is before TX_T0)
            awk -v m="$MS" 'BEGIN { exit !(m > 8.30 && m < 8.40) }' && pass "RS485 request on the wire ${MS} ms (8 bytes 8N1 = 8.33)" \
                || fail "RS485 request duration ${MS} ms (expected 8.33)" ;;
        *) echo "WARN RS485: $OUT - idle bus not quiet? (terminated bus without fail-safe bias; try with the USB-RS485 adapter)" ;;
    esac
fi

# --- v17: belt speed PI (des_bpi), belt position compare trigger (des_ptrig), flap PWM slew
if [ "$VER" -ge 17 ]; then
    BID=$($R r 0x748); TID=$($R r 0x80C)
    [ "$BID" = "0x42504931" ] && pass "des_bpi ID $BID" || fail "des_bpi ID $BID"
    [ "$TID" = "0x50544731" ] && pass "des_ptrig ID $TID" || fail "des_ptrig ID $TID"
    C=$(hex "$($R r 0x700)"); S=$(hex "$($R r 0x730)"); A=$(hex "$($R r 0x804)"); SL=$(hex "$($R r 0x418)")
    [ "$C" = 0 ] && [ "$S" = 0 ] && pass "belt PI off, SCALE 0 (reset state = v16 behaviour)" \
        || fail "belt PI not idle (CTRL $C SCALE $S): something left it on"
    [ $((A & 0xF)) = 0 ] && pass "no position trigger armed" || fail "position trigger armed (STATUS $A)"
    [ "$SL" = 0 ] && pass "PWM slew 0" || fail "PWM slew $SL"
    # belt standing: no encoder event for TIMEOUT -> STALL, MEAS 0 (the measurement runs with the loop off)
    ST=$(hex "$($R r 0x704)"); M=$(hex "$($R r 0x728)")
    [ $((ST & 4)) = 4 ] && [ "$M" = 0 ] && pass "belt PI measurement: STALL and MEAS 0 with the belt standing" \
        || fail "belt PI measurement with the belt standing: STATUS $ST MEAS $M"
    # an entry armed 10 steps behind the position fires at once (LATE) and loads the PWM width; outputs stay disabled
    $R ptrig rel 0 -10 pwm0 1234 > /dev/null
    A=$(hex "$($R r 0x804)"); W=$(hex "$($R r 0x408)"); N=$(hex "$($R r 0x808)")
    [ $(((A >> 8) & 1)) = 1 ] && [ $(((A >> 16) & 1)) = 1 ] && [ "$W" = 1234 ] \
        && pass "position trigger: entry behind the position fired at once (LATE), PWM ch0 loaded 1234 us, fires $N" \
        || fail "position trigger: STATUS $A, CH0_US $W (expected fired + late, 1234)"
    $R ptrig off > /dev/null; $R pwm 0 1500 > /dev/null; $R pwm off > /dev/null
fi

# --- command response
echo "     $($R cmdlat 100)"
echo "     $($R steplat 50)"
if [ "$VER" -ge 9 ]; then   # stepper: TARGET rewritten to POS while moving stops there; target behind ramps down
    OUT=$($R steptgt) && pass "stepper target rewrite" || fail "stepper target rewrite"
    echo "$OUT" | sed 's/^/     /'
fi
$R step off > /dev/null

wait
grep "== probe" /tmp/pl_check_probe.log | sed 's/^/     /'
grep -q "skipped 0 " /tmp/pl_check_probe.log && pass "camera capture without skipped frames" || fail "camera capture skipped frames"
echo "== pl_check: $FAILS failure(s)"
exit $FAILS   # the EXIT trap restarts the service
