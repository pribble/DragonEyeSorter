#!/bin/sh
# Board launcher for des_app: loads the camera DMA driver the way the vendor demo scripts do
# (mipicam.ko is not loaded at boot), then runs des_app with this folder's config.
#   <本目录>/des_app.sh probe -n 10
# 路径全部来自同目录的 des.env（板端路径唯一来源），本文件不写死任何绝对路径 ——
# 整份目录拷到别处部署（deploy --dest）时，它跑的就是那一份的程序与配置。
SELF=$(readlink -f -- "$0" 2>/dev/null || echo "$0")
DES_HOME=$(CDPATH= cd -- "$(dirname -- "$SELF")" && pwd)   # 以脚本自己所在目录为准，不吃继承来的 DES_HOME
[ -f "$DES_HOME/des.env" ] || { echo "des_app.sh: 缺 $DES_HOME/des.env（路径都从它取，源文件见 scripts/deploy/out/des.env）" >&2; exit 1; }
. "$DES_HOME/des.env"
cd "$DES_HOME" || exit 1
# one des_app at a time (camera DMA, servo bus and stepper are single-owner); the autostart service may hold them
if pidof des_app > /dev/null; then
    echo "des_app is already running (autostart service?): stop it first with $DES_HOME/des_service.sh stop" >&2
    exit 1
fi
if [ ! -e /dev/mipicam ]; then
    insmod "$DES_MIPICAM_KO" || exit 1
fi
exec "$DES_APP" -c "$DES_CONFIG" "$@"
