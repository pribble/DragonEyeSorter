#!/bin/bash
# 把 NPU demo 的 initramfs 启动卡改造成持久化 SD 镜像（FAT boot + ext4 rootfs）。
# 用法（WSL，root）：
#   方式1: wsl -u root -e bash scripts/sd_image/make_sd_image.sh [NPU demo 的 boot 目录]
#   方式2: source scripts/env_loader.sh && sudo bash scripts/sd_image/make_sd_image.sh
# 依赖：apt install lz4 u-boot-tools mtools dosfstools parted
set -euo pipefail

# 自动检测 NPU_DEMO 路径
if [ -n "${NPU_DEMO:-}" ]; then
    DEFAULT_BOOT="$NPU_DEMO/boot"
elif [ -f "$(dirname "$0")/../../.env" ]; then
    # 尝试加载 .env
    source "$(dirname "$0")/../env_loader.sh" 2>/dev/null || true
    DEFAULT_BOOT="${NPU_DEMO:-}/boot"
else
    DEFAULT_BOOT="/mnt/c/Users/$(whoami)/Downloads/demo/boot"
fi

SRC=${1:-${DEFAULT_BOOT}}

# 验证路径
if [ ! -d "$SRC" ] || [ ! -f "$SRC/uInitrd.lz4" ]; then
    echo "❌ 错误: 找不到 NPU demo boot 目录或 uInitrd.lz4"
    echo "请指定正确路径:"
    echo "  $0 /path/to/demo/boot"
    echo "或在 .env 文件中设置 NPU_DEMO 变量"
    exit 1
fi

OUT_DIR=$(dirname "$(readlink -f "$0")")
W=/root/sdbuild
IMG_MB=4096
FAT_MB=256

rm -rf "$W"; mkdir -p "$W/rootfs"; cd "$W"

# 1. uInitrd.lz4 = 64B U-Boot 头 + lz4(legacy) + cpio(newc)
dd if="$SRC/uInitrd.lz4" bs=64 skip=1 status=none | lz4 -dc | (cd rootfs && cpio -idm --quiet)

# 2. 启动脚本：去掉 initrd，根指向 SD 第 2 分区；init=/init 保持与 initramfs 时相同的启动路径
cat > boot.cmd <<"EOF"
mmc part
load ${devtype} ${devnum}:${distro_bootpart} 0x10000000 ${prefix}uImage.lz4
load ${devtype} ${devnum}:${distro_bootpart} 0x14000000 ${prefix}system.dtb
setenv bootargs console=ttyS0,115200n8 earlycon=uart,mmio32,0xf8401000 loglevel=8 mtdparts=alspinor:1M(boot),-(data) root=/dev/mmcblk0p2 rootwait rootfstype=ext4 rw init=/init
bootm 0x10000000 - 0x14000000
EOF
mkimage -A arm64 -O linux -T script -C none -n "persistent rootfs" -d boot.cmd boot.scr >/dev/null

# 3. FAT 分区（附原版 boot.scr + uInitrd 作回退）
export MTOOLS_SKIP_CHECK=1
truncate -s ${FAT_MB}M boot.vfat
mkfs.vfat -F 32 -n BOOT boot.vfat >/dev/null
mcopy -i boot.vfat "$SRC/BOOT.bin" "$SRC/uImage.lz4" "$SRC/system.dtb" boot.scr boot.cmd ::/
[ -f "$OUT_DIR/README_回退.txt" ] && mcopy -i boot.vfat "$OUT_DIR/README_回退.txt" ::/
mmd -i boot.vfat ::/initramfs_backup
mcopy -i boot.vfat "$SRC/boot.scr" "$SRC/uInitrd.lz4" ::/initramfs_backup/

# 4. ext4 分区
ROOT_MB=$((IMG_MB - FAT_MB - 1))
mkfs.ext4 -q -F -L rootfs -d rootfs root.ext4 ${ROOT_MB}M
e2fsck -fn root.ext4 >/dev/null

# 5. 组装（分区方式同官方 make_parted.sh：msdos，p1 fat32 带 boot 标志，p2 ext4）
truncate -s ${IMG_MB}M sd.img
parted -s sd.img mklabel msdos mkpart primary fat32 1MiB $((FAT_MB + 1))MiB set 1 boot on mkpart primary ext4 $((FAT_MB + 1))MiB 100%
dd if=boot.vfat of=sd.img bs=1M seek=1 conv=notrunc,sparse status=none
dd if=root.ext4 of=sd.img bs=1M seek=$((FAT_MB + 1)) conv=notrunc,sparse status=none

cp sd.img "$OUT_DIR/sd_persistent.img"
cp boot.cmd "$OUT_DIR/boot.cmd"
echo "done: $OUT_DIR/sd_persistent.img"
