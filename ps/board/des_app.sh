#!/bin/sh
# Board launcher for des_app: loads the camera DMA driver the way the vendor demo scripts do
# (mipicam.ko is not loaded at boot), then runs des_app from this directory.
#   /root/des/des_app.sh probe -n 10
cd "$(dirname "$0")"
# one des_app at a time (camera DMA, servo bus and stepper are single-owner); the autostart service may hold them
if pidof des_app > /dev/null; then
    echo "des_app is already running (autostart service?): stop it first with /root/des/des_service.sh stop" >&2
    exit 1
fi
if [ ! -e /dev/mipicam ]; then
    insmod /opt/bin/mipicam.ko || exit 1
fi
exec ./des_app -c ./des_app.json "$@"
