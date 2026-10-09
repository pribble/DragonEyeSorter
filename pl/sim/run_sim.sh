#!/bin/bash
# 用 Icarus Verilog 跑仿真（WSL：sudo apt install iverilog）
# 用法：bash pl/sim/run_sim.sh des_axi
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"
P=../soc_patch
T=$(mktemp -d)
case "${1:-des_axi}" in
  des_axi)
    iverilog -g2005 -o "$T/sim" tb_des_axi.v ../rtl/des_regs.v \
      $P/design_0/ip/design_0_fvdma_ctrl_0/RTL/fvdma_ctrl_300e9b49f7b5.v
    ;;
  servo_bus)
    iverilog -g2005 -o "$T/sim" tb_servo_bus.v ../rtl/servo_bus.v
    ;;
  stepper)
    iverilog -g2005 -o "$T/sim" tb_stepper_ctrl.v ../rtl/stepper_ctrl.v
    ;;
  *) echo "unknown test: $1"; exit 1 ;;
esac
vvp -n "$T/sim"
rm -rf "$T"
