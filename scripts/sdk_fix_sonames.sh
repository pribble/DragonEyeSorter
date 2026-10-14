#!/bin/sh
# Restore the library names a directory needs to be linkable after it was populated on
# Windows (cp / unzip / scp lose the symlinks; people then rename the real file to one name).
# A library is usable only when BOTH names exist next to each other:
#   SONAME name (libX.so.N) : the linker looks the file up by THIS name when it resolves
#                             another library's DT_NEEDED (rpath-link), the runtime loader too
#   plain name  (libX.so)   : what -lX looks for
# For every lib file it creates copies named by its SONAME and by the plain name, if missing.
#
# usage: sh scripts/sdk_fix_sonames.sh <SDK libs dir | any lib dir> [readelf]
#   SDK root (contains armnn_lib/ and ffmpeg_opencv4.7.0_aarch64/): both subdirs are scanned
#     e.g. sh scripts/sdk_fix_sonames.sh D:/ANLU/work/face_detection_src/app/face_detection/libs \
#              D:/ANLU/FD/toolchain/gcc/aarch64_linux/bin/aarch64-linux-gnu-readelf.exe
#   any other directory (e.g. $BOARD_LIBS, copied off the board): that directory is scanned
#     e.g. sh scripts/sdk_fix_sonames.sh "$BOARD_LIBS" \
#              "$FD_ROOT/toolchain/gcc/aarch64_linux/bin/aarch64-linux-gnu-readelf.exe"
set -e
SDK=${1:?libs dir}
READELF=${2:-aarch64-linux-gnu-readelf}

# SDK 根 → 扫两个子目录；其它目录（如 $BOARD_LIBS）→ 扫它自己
if [ -d "$SDK/armnn_lib/lib" ] && [ -d "$SDK/ffmpeg_opencv4.7.0_aarch64/lib" ]; then
    DIRS="$SDK/armnn_lib/lib $SDK/ffmpeg_opencv4.7.0_aarch64/lib"
else
    DIRS="$SDK"
fi

for dir in $DIRS; do
    # lib*.so* 同时覆盖 libX.so.4.7.0（SDK）与 libX.so（板上库拷下来只剩链接名的情况）
    for f in "$dir"/lib*.so*; do
        [ -f "$f" ] || continue
        soname=$("$READELF" -d "$f" 2>/dev/null | sed -n 's/.*(SONAME).*\[\(.*\)\].*/\1/p')
        base=$(basename "$f")
        plain=${base%%.so*}.so   # libopencv_core.so.4.7.0 → libopencv_core.so；libfreetype.so → 自己
        for name in $soname $plain; do
            if [ -n "$name" ] && [ ! -e "$dir/$name" ]; then
                cp "$f" "$dir/$name"
                echo "created $dir/$name"
            fi
        done
    done
done
