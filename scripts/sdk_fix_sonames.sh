#!/bin/sh
# Restore the shared-library link names of the vendor NPU demo SDK after it was unpacked on Windows
# (the archive's symlinks such as libopencv_core.so -> .so.407 -> .so.4.7.0 are lost there).
# For every real lib.so.X.Y.Z it creates copies named by its SONAME and the plain lib.so, if missing.
#
# usage: sh scripts/sdk_fix_sonames.sh <SDK libs dir> [readelf]
#   e.g. sh scripts/sdk_fix_sonames.sh D:/ANLU/work/face_detection_src/app/face_detection/libs \
#            D:/ANLU/FD/toolchain/gcc/aarch64_linux/bin/aarch64-linux-gnu-readelf.exe
set -e
SDK=${1:?SDK libs dir}
READELF=${2:-aarch64-linux-gnu-readelf}

for dir in "$SDK"/armnn_lib/lib "$SDK"/ffmpeg_opencv4.7.0_aarch64/lib; do
    for f in "$dir"/lib*.so.*; do
        [ -f "$f" ] || continue
        soname=$("$READELF" -d "$f" 2>/dev/null | sed -n 's/.*(SONAME).*\[\(.*\)\].*/\1/p')
        base=$(basename "$f")
        plain=${base%%.so.*}.so
        for name in $soname $plain; do
            if [ -n "$name" ] && [ ! -e "$dir/$name" ]; then
                cp "$f" "$dir/$name"
                echo "created $dir/$name"
            fi
        done
    done
done
