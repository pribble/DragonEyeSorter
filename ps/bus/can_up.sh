#!/bin/sh
# Bring a CAN interface up for DragonEyeSorter (protocol: can_proto.h, classic CAN 500 kbit/s).
#   can_up.sh [can0] [normal|loopback]
#   normal    classic CAN 2.0, needs a partner on the bus (USB-CAN on the PC acting as the PLC) for ACKs
#   loopback  self test without a bus: the ipms_canfd controller only loops back in FD mode (classic frames still work)
IF=${1:-can0}
MODE=${2:-normal}
ip link set "$IF" down 2>/dev/null
if [ "$MODE" = loopback ]; then
    ip link set "$IF" type can bitrate 500000 dbitrate 2000000 fd on loopback on || exit 1
else
    ip link set "$IF" type can bitrate 500000 loopback off fd off || exit 1
fi
ip link set "$IF" up || exit 1
ip -details link show "$IF" | head -4
