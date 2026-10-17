#!/bin/bash
# 用 Icarus Verilog 跑仿真（WSL：sudo apt install iverilog）
# 用法：bash pl/sim/run_sim.sh des_axi
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"
D=../soc_base/src/des
T=$(mktemp -d)
case "${1:-des_axi}" in
  des_axi)
    iverilog -g2005 -o "$T/sim" tb_des_axi.v $D/des_regs.v \
      ../soc_base/design_0/ip/design_0_fvdma_ctrl_0/RTL/fvdma_ctrl_300e9b49f7b5.v
    ;;
  servo_bus)
    iverilog -g2005 -o "$T/sim" tb_servo_bus.v $D/servo_bus.v
    ;;
  stepper)
    iverilog -g2005 -o "$T/sim" tb_stepper_ctrl.v $D/stepper_ctrl.v
    ;;
  io)
    iverilog -g2005 -o "$T/sim" tb_des_io.v $D/des_io.v
    ;;
  pwm)
    iverilog -g2005 -o "$T/sim" tb_des_pwm.v $D/des_pwm.v
    ;;
  vision)
    iverilog -g2012 -o "$T/sim" tb_des_vision.v $D/des_vision.v   # the testbench uses join_none
    ;;
  vision2)
    iverilog -g2012 -Ptb_des_vision.LANES=2 -o "$T/sim" tb_des_vision.v $D/des_vision.v   # half-width variant
    ;;
  awb)   # vendor ISP white balance vs our patch (awb_seq_div); vendor primitives as behavioural models
    # 厂家原版 awb.v 取自 git（初次入库那一版）；仓库里的工作副本是我们的改版
    REPO=$(git -C .. rev-parse --show-toplevel)
    ORIG=$(git -C "$REPO" log --diff-filter=A -1 --format=%H -- pl/soc_base/src/ui_isp/awb_1_1/awb.v)
    git -C "$REPO" show "$ORIG:pl/soc_base/src/ui_isp/awb_1_1/awb.v" | tr -d '\r' | sed 's/^module awb /module awb_orig /' > "$T/awb_orig.v"
    iverilog -g2005 -o "$T/sim" tb_awb.v "$T/awb_orig.v" ../soc_base/src/ui_isp/awb_1_1/awb.v $D/awb_seq_div.v ../soc_base/src/ui_isp/awb_1_1/signal_delay.v models/awb_vendor_models.v
    ;;
  *) echo "unknown test: $1"; exit 1 ;;
esac
vvp -n "$T/sim"
rm -rf "$T"
