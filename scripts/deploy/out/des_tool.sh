#!/bin/sh
# des_tool.sh — board launcher for the operator tools (des_tool): register check, camera/arm calibration, measurement,
# training-data capture. Not part of the deployed product runtime (that is des_app.sh -> des_app run).
#   <本目录>/des_tool.sh probe -n 10
#   <本目录>/des_tool.sh calib-cam --board 9x6 --square 25
# 与 des_app.sh 同一套规则：路径全部来自同目录的 des.env；缺 /dev/mipicam 时先 insmod；相机 / 舵机总线 / 步进
# 只有一个主人，des_app 在跑时拒绝启动。des_tool 本体在 $DES_TOOLS（/root/bin，与 desreg / canplc / test_* 一致），
# 不进部署清单，要用时手动 scp（见 scripts/deploy/README.md）。
SELF=$(readlink -f -- "$0" 2>/dev/null || echo "$0")
DES_HOME=$(CDPATH= cd -- "$(dirname -- "$SELF")" && pwd)   # 以脚本自己所在目录为准，不吃继承来的 DES_HOME
[ -f "$DES_HOME/des.env" ] || { echo "des_tool.sh: 缺 $DES_HOME/des.env（路径都从它取，源文件见 scripts/deploy/out/des.env）" >&2; exit 1; }
. "$DES_HOME/des.env"
cd "$DES_HOME" || exit 1
if pidof des_app > /dev/null || pidof des_tool > /dev/null; then
    echo "des_app or des_tool is already running (camera / servos / stepper are single-owner): stop it first with $DES_HOME/des_service.sh stop" >&2
    exit 1
fi
if [ ! -x "$DES_TOOLS/des_tool" ]; then
    echo "des_tool.sh: 缺 $DES_TOOLS/des_tool（手动 scp ps/build/des_tool \$BOARD_HOST:/root/bin/，见 scripts/deploy/README.md）" >&2
    exit 1
fi
if [ ! -e /dev/mipicam ]; then
    insmod "$DES_MIPICAM_KO" || exit 1
fi
exec "$DES_TOOLS/des_tool" -c "$DES_CONFIG" "$@"
