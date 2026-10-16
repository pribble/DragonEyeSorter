#!/bin/sh
# scripts/deploy/selftest/run.sh —— 不需要板子：用假 ssh / scp 把部署器整条路走一遍
#
#   sh scripts/deploy/selftest/run.sh          （Git Bash / WSL 里跑）
#
# 假板子 = ${TMPDIR:-/tmp}/des-selftest-board：板端 /root/... 与 /tmp/desdeploy 映射到这里。
# 它验证的是「空板 → 传输 → 安装 → 校验」这条路本身（含行尾与权限位），跑完目录留着方便看。
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
export FAKE_BOARD="${FAKE_BOARD:-${TMPDIR:-/tmp}/des-selftest-board}"
export PATH="$repo/scripts/deploy/selftest/fakebin:$PATH"
chmod +x "$repo"/scripts/deploy/selftest/fakebin/*

echo "假板子：$FAKE_BOARD"
rm -rf "$FAKE_BOARD"
mkdir -p "$FAKE_BOARD/root"

echo
echo "=========== 第一次：空板（应全部「板上还没有」，传完校验通过） ==========="
bash "$repo/scripts/deploy/deploy.sh" || echo "(退出码 $?)"

echo
echo "=========== 板上落地（行尾 / 语法） ==========="
ls -l "$FAKE_BOARD/root/des/"
for f in "$FAKE_BOARD"/root/des/*.sh; do
    raw=$(wc -c < "$f"); cr=$(tr -d '\r' < "$f" | wc -c)
    printf '%-22s %6s B  CR=%s  ' "$(basename "$f")" "$raw" "$((raw - cr))"
    if sh -n "$f"; then echo "sh -n OK"; else echo "sh -n FAILED"; fi
done
echo
echo "=========== 板上实际执行的安装脚本（权限位看这里；ls 显示的 755/644 是 MSYS 自己编的） ==========="
cat "$FAKE_BOARD/tmp/last-install.sh"

echo
echo "=========== 第二次：应「一致」且不再传输 ==========="
bash "$repo/scripts/deploy/deploy.sh" || echo "(退出码 $?)"

prod_before=$(md5sum "$FAKE_BOARD/root/des/des_app" "$FAKE_BOARD/root/des/des_app.json" | md5sum)
echo
echo "=========== 第三次：--dest /root/des/test（整份另装：des_service.sh 与 des.env 也进测试目录，生产件不被碰） ==========="
bash "$repo/scripts/deploy/deploy.sh" --dest /root/des/test || echo "(退出码 $?)"
ls -l "$FAKE_BOARD/root/des/test/" 2>/dev/null | head -14
prod_after=$(md5sum "$FAKE_BOARD/root/des/des_app" "$FAKE_BOARD/root/des/des_app.json" | md5sum)
[ "$prod_before" = "$prod_after" ] && echo "✔ 生产件未变" || echo "✘ 生产件被改了（不该）"
if [ -e "$FAKE_BOARD/root/des/test/des_service.sh" ] && [ -e "$FAKE_BOARD/root/des/test/des.env" ]; then
    echo "✔ 测试目录是全套（des_service.sh + des.env 也在）"
else
    echo "✘ 测试目录缺 des_service.sh 或 des.env"
fi
if [ -e "$FAKE_BOARD/root/des/des_service.sh" ] && [ -e "$FAKE_BOARD/root/des/des.env" ]; then
    echo "✔ 生产目录仍是全套"
else
    echo "✘ 生产目录缺件（不该）"
fi
# 落地后的脚本里不该再有写死的布局路径（des.env 本身就是那个"唯一有路径的文件"）
bad=$(grep -hv '^[[:space:]]*#' "$FAKE_BOARD"/root/des/*.sh 2>/dev/null | grep -E '/root/des|/root/bin|/var/run' || true)
[ -z "$bad" ] && echo "✔ 脚本代码里没有写死的布局路径（/root/des、/root/bin、/var/run）" \
    || { echo "✘ 脚本里还有写死的路径："; echo "$bad"; }
echo
echo "=========== 第四次：同一个 --dest 再来一次，应「一致、不传」 ==========="
bash "$repo/scripts/deploy/deploy.sh" --dest /root/des/test || echo "(退出码 $?)"
