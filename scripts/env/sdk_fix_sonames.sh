#!/bin/sh
# Restore the library names a directory needs to be linkable after it was populated on
# Windows (cp / unzip / scp lose the symlinks; people then rename the real file to one name).
# A library is usable only when BOTH names exist next to each other:
#   SONAME name (libX.so.N) : the linker looks the file up by THIS name when it resolves
#                             another library's DT_NEEDED (rpath-link), the runtime loader too
#   plain name  (libX.so)   : what -lX looks for
# For every lib file it creates copies named by its SONAME and by the plain name, if missing.
#
# usage: sh scripts/env/sdk_fix_sonames.sh <lib dir> [readelf]
#   一个库一个目录（.env 的 ARMNN_LIB / OPENCV_LIB 就各是一个这样的根）：
#     sh scripts/env/sdk_fix_sonames.sh "$ARMNN_LIB"  "$FD_ROOT/toolchain/gcc/aarch64_linux/bin/aarch64-linux-gnu-readelf.exe"
#     sh scripts/env/sdk_fix_sonames.sh "$OPENCV_LIB" "$FD_ROOT/toolchain/gcc/aarch64_linux/bin/aarch64-linux-gnu-readelf.exe"
#   板上库的本地拷贝（$BOARD_LIBS）：同样直接给目录
#     sh scripts/env/sdk_fix_sonames.sh "$BOARD_LIBS" \
#              "$FD_ROOT/toolchain/gcc/aarch64_linux/bin/aarch64-linux-gnu-readelf.exe"
#   兼容旧写法：给一个同时含 armnn_lib/ 与 ffmpeg_opencv4.7.0_aarch64/ 的父目录时，两个子目录都扫
set -e
SDK=${1:?libs dir}
READELF=${2:-aarch64-linux-gnu-readelf}

# 父目录（同时含两个库）→ 扫两个子目录；单个库目录（ARMNN_LIB / OPENCV_LIB / $BOARD_LIBS）→ 扫它自己
# DIRS 按行分隔、here-doc 逐行读（不是管道，n_created 才留在本 shell）：路径里带空格也不拆词
if [ -d "$SDK/armnn_lib/lib" ] && [ -d "$SDK/ffmpeg_opencv4.7.0_aarch64/lib" ]; then
    DIRS="$SDK/armnn_lib/lib
$SDK/ffmpeg_opencv4.7.0_aarch64/lib"
elif [ -d "$SDK/lib" ]; then
    DIRS="$SDK/lib"   # ARMNN_LIB / OPENCV_LIB are library roots: the .so files are in lib/ (ps/Makefile: -L$(ARMNN_LIB)/lib)
else
    DIRS="$SDK"       # a flat folder of .so files ($BOARD_LIBS)
fi
n_created=0

while IFS= read -r dir; do
    [ -n "$dir" ] || continue
    # lib*.so* 同时覆盖 libX.so.4.7.0（SDK）与 libX.so（板上库拷下来只剩链接名的情况）
    for f in "$dir"/lib*.so*; do
        [ -f "$f" ] || continue
        soname=$("$READELF" -d "$f" 2>/dev/null | sed -n 's/.*(SONAME).*\[\(.*\)\].*/\1/p')
        base=$(basename "$f")
        plain=${base%%.so*}.so   # libopencv_core.so.4.7.0 → libopencv_core.so；libfreetype.so → 自己
        if [ -z "$soname" ]; then
            echo "  warning: $base 没读到 SONAME（readelf 不可用 / 不是 ELF？）——只建链接名 $plain" >&2
        fi
        for name in $soname $plain; do
            if [ -n "$name" ] && [ ! -e "$dir/$name" ]; then
                cp "$f" "$dir/$name"
                echo "created $dir/$name"; n_created=$((n_created + 1))
            fi
        done
    done
done <<EOF
$DIRS
EOF

printf 'sdk_fix_sonames: created %d file(s)\n' "$n_created"
