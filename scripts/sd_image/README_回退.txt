本卡 = NPU demo 原版 BOOT.bin / 内核 / 设备树 + 根文件系统改放到第 2 分区（ext4），改动会保留。

回退到原版 initramfs（重启即还原）：在 Windows 下打开本分区，
  1. 把 boot.scr 改名为 boot.scr.persistent
  2. 把 initramfs_backup\boot.scr 和 initramfs_backup\uInitrd.lz4 拷回本分区根目录
重新上电即可。

更新 PL：只换本分区的 BOOT.bin（板上路径 /mnt/mmcblk0p1/BOOT.bin），其余不动。
