#!/bin/sh
# Bring a CAN interface up for DragonEyeSorter (protocol: can_proto.h, classic CAN 500 kbit/s).
#   can_up.sh [can0] [normal|loopback]
#   normal    classic CAN 2.0, needs a partner on the bus (USB-CAN on the PC acting as the PLC) for ACKs
#   loopback  self test without a bus: the ipms_canfd controller only loops back in FD mode (classic frames still work)
#             The ipms_canfd driver may refuse to start in classic mode ("canfd_chip_start failed", seen at boot 10-15):
#             then it falls back to FD mode. Classic frames (all the protocol sends) work the same there; the 2 Mbit/s
#             data phase is only used by FD frames, which nobody on this bus sends.
IF=${1:-can0}
MODE=${2:-normal}
ip link set "$IF" down 2>/dev/null
if [ "$MODE" = loopback ]; then
    ip link set "$IF" type can bitrate 500000 dbitrate 2000000 fd on loopback on || exit 1
    ip link set "$IF" up || exit 1
elif ! { ip link set "$IF" type can bitrate 500000 loopback off fd off && ip link set "$IF" up; } 2>/dev/null; then
    ip link set "$IF" down 2>/dev/null
    ip link set "$IF" type can bitrate 500000 dbitrate 2000000 fd on loopback off || exit 1
    ip link set "$IF" up || exit 1
    echo "$IF: classic mode refused by the controller, running in FD mode (classic frames unchanged)"
fi
ip -details link show "$IF" | head -4
