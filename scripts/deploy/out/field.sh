#!/bin/sh
# field.sh — 现场调试菜单（决赛现场用：离线、不需要 AI）。手册：10_项目文档/现场调试手册.md（仓库外）
#
#   <本目录>/field.sh            电脑上双击 field_kit\field.bat 也是进这里
#
# 每个会动配置的项目都先自动备份（data/field_backup/NNN_名字/），要停主程序的项目自动暂停、做完自动恢复；
# Ctrl-C 只中断当前这一项，回到菜单。数字回车选项目，q 退出。
SELF=$(readlink -f -- "$0" 2>/dev/null || echo "$0")
DES_HOME=$(CDPATH= cd -- "$(dirname -- "$SELF")" && pwd)
[ -f "$DES_HOME/des.env" ] || { echo "field.sh: 缺 $DES_HOME/des.env" >&2; exit 1; }
. "$DES_HOME/des.env"
cd "$DES_HOME" || exit 1

SVC="$DES_HOME/des_service.sh"
TOOL="$DES_HOME/des_tool.sh"
CFG="$DES_HOME/des_app.json"
CAL="$DES_HOME/calib.json"
HOMEJ="$DES_HOME/arm_home.json"
BK="$DES_DATA/field_backup"
GOLD="$DES_HOME/golden.txt"
DESREG="$DES_TOOLS/desreg"
TMP=/tmp/field_out.txt
STD_OD=31.95          # 标准齿轮卡尺外径（mm），换标准件就改这里

# ---------------------------------------------------------------- 小工具
pause() { printf '\n—— 按回车回到菜单 '; read -r _; }
ask() { printf '%s（y 回车 = 是，直接回车 = 否）: ' "$1"; read -r _a; [ "$_a" = y ] || [ "$_a" = Y ]; }
line() { echo "------------------------------------------------------------"; }

backup() {   # backup <名字>：当前配置 / 标定文件拷到 data/field_backup/NNN_名字/
    mkdir -p "$BK" || return 1
    _n=$(ls "$BK" 2>/dev/null | wc -l)
    _d="$BK/$(printf %03d $((_n + 1)))_$1"
    mkdir -p "$_d"
    for _f in des_app.json calib.json arm_home.json flat.json ui_params.json recipes.json; do
        [ -f "$DES_HOME/$_f" ] && cp -p "$DES_HOME/$_f" "$_d/"
    done
    echo "已备份当前配置 → $_d"
}

paused() {   # paused 命令...：主程序在跑就先停，命令做完（含 Ctrl-C）再起
    _was=0
    if pidof des_app > /dev/null 2>&1; then _was=1; echo "暂停主程序 ..."; "$SVC" stop > /dev/null 2>&1; sleep 1; fi
    trap 'echo; echo "（已中断）"' INT
    "$@"; _rc=$?
    trap - INT
    if [ $_was = 1 ]; then
        echo "恢复主程序 ..."; "$SVC" start > /dev/null 2>&1
        _w=0; while [ $_w -lt 15 ] && ! pidof des_app > /dev/null 2>&1; do sleep 1; _w=$((_w + 1)); done
        pidof des_app > /dev/null 2>&1 && echo "主程序已恢复" || echo "** 主程序 15 秒内没起来：菜单 2 看日志、菜单 21 重启 **"
    fi
    return $_rc
}

jtop() { tr -d '\n\t ' < "$1" | grep -o "\"$2\":[-0-9.eE+]*" | head -1 | cut -d: -f2; }                     # 顶层数值
jsec() { tr -d '\n\t ' < "$1" | grep -o "\"$2\":{[^}]*}" | grep -o "\"$3\":[-0-9.eE+]*" | head -1 | cut -d: -f2; }   # 某段里的数值
fmt() { awk -v v="$1" -v d="$2" 'BEGIN { if (v == "") print "—"; else printf "%." d "f\n", v }'; }

config_ok() {   # 配置能不能被程序读进来（离线算一次逆解，不碰硬件、不用停主程序）
    "$DES_TOOLS/des_tool" -c "$CFG" ik 380 0 > /tmp/field_cfg.txt 2>&1 && return 0
    # 「config: ... from ...」是正常的加载提示；配置读坏了会打印解析错误并「using defaults」
    grep -qiE "json\.exception|parse error|using defaults|cannot open" /tmp/field_cfg.txt && { cat /tmp/field_cfg.txt; return 1; }
    return 0   # 只是这个点算不出来，配置本身没问题
}

restore_dir() {   # restore_dir <备份目录>
    for _f in "$1"/*; do cp -p "$_f" "$DES_HOME/"; done
    echo "已从 $1 恢复：$(ls "$1" | tr '\n' ' ')"
}

# ---------------------------------------------------------------- 检查
status() {
    line; echo "状态总览"; line
    awk '{printf "开机时长        %.1f 小时\n", $1 / 3600}' /proc/uptime
    if pidof des_app > /dev/null 2>&1; then echo "主程序 des_app  运行中"
    else echo "主程序 des_app  ** 没有运行 **（菜单 21 重启主程序）"; fi
    _m=$(md5sum /mnt/mmcblk0p1/BOOT.bin 2>/dev/null | cut -c1-8)
    case "$_m" in
        08ec06c4) echo "比特流          v17b2c（决赛版）";;
        71f41a20) echo "比特流          v16（退路版）";;
        *)        echo "比特流          未知（BOOT.bin md5 $_m）";;
    esac
    _p=$(dmesg 2>/dev/null | grep -o 'output=[0-9]* Hz' | tail -1)
    case "$_p" in
        *148500000*) echo "HDMI 像素时钟   148.5 MHz（正常）";;
        "")          echo "HDMI 像素时钟   开机记录已滚掉，读不到（不代表有问题）";;
        *)           echo "HDMI 像素时钟   ** $_p ** 不对，应为 148500000（手册「小屏闪」）";;
    esac
    df -h "$DES_DATA" | awk 'NR == 2 {print "SD 卡剩余       " $4 "（已用 " $5 "）"}'
    [ "$(cat /sys/class/net/eth0/carrier 2>/dev/null)" = 1 ] && echo "网线            已连接" || echo "网线            ** 断开 **"
    _e=$(ls -t "$DES_DATA"/log/*/env.csv 2>/dev/null | head -1)
    [ -n "$_e" ] && tail -n 1 "$_e" | awk -F, '{print "温湿度          " $2 " °C / " $3 " %RH"}'
    if [ -x "$DES_HOME/des_4g_service.sh" ]; then
        "$DES_HOME/des_4g_service.sh" status 2>/dev/null | head -1 | sed 's/^/4G            /'
        tail -n 1 "$DES_DATA/des_4g.log" 2>/dev/null | cut -c1-90 | sed 's/^/4G 最近        /'
    fi
    [ -f "$GOLD" ] && echo "黄金参考        已记录（菜单 20 对比）" || echo "黄金参考        ** 还没记录 **（菜单 20）"
    echo; echo "主程序日志最后 5 行："; tail -n 5 "$DES_SERVICE_LOG" 2>/dev/null | cut -c1-120
}

show_log() { line; tail -n 40 "$DES_SERVICE_LOG" 2>/dev/null | cut -c1-160; }

servo_scan() {
    paused "$TOOL" arm scan
    echo; echo "判据：6 / 6 应答、status 都是 0x00。不全 → 先量 6.8 V 母排，再查舵机信号线 W4 与 BSS138（手册「舵机不应答」）"
}

pl_check() { "$DES_HOME/pl_check.sh"; echo; echo "判据：最后汇总 0 failed。"; }

# ---------------------------------------------------------------- 画面
measure_od() {
    echo "把标准齿轮（卡尺外径 $STD_OD mm）平放在画面正中、静止；视野里只放它，皮带不要动。"
    ask "放好了吗" || return 1
    paused "$TOOL" measure -n 20 --dump "$DES_HOME/m.png" 2>&1 | tee "$TMP" | tail -n 12
    OD=$(awk '$1 == "OD" && $2 == "mean" {print $3; exit}' "$TMP")
    BORE=$(awk '$1 == "bore" && $2 == "mean" {print $3; exit}' "$TMP")
    if [ -z "$OD" ]; then echo "** 没测到齿轮：视野里没有 / 不止一个 / 光线不对。看 m.png（电脑 field_kit\\get_pics.bat）"; return 1; fi
    echo; echo "本次：外径 $OD mm，孔径 $BORE mm（标准件卡尺 $STD_OD）"
    _g=$(sed -n 's/^od_mm=//p' "$GOLD" 2>/dev/null)
    if [ -n "$_g" ]; then
        awk -v a="$OD" -v b="$_g" 'BEGIN { d = a - b; if (d < 0) d = -d;
            printf "黄金参考外径 %s，差 %.3f mm → %s\n", b, d,
                   d <= 0.05 ? "≤ 0.05：相机标定不用重做，直接做平场（菜单 6）" : "> 0.05：重做相机标定（菜单 7）→ 走带标定（菜单 8）" }'
    else
        awk -v a="$OD" -v b="$STD_OD" 'BEGIN { d = a - b; if (d < 0) d = -d;
            printf "与卡尺差 %.3f mm（没有黄金参考时的粗判：≤ 0.15 可接受）\n", d }'
    fi
    return 0
}

calib_flat() {
    echo "平场（到场一定做；换灯、换曝光、现场顶灯不同都要重做）"
    echo "1. 皮带上清空，只放一张空白 A4，横放盖住画面上游边（视野里只有白纸）"
    echo "2. 皮带会带着纸往下游走、再退回来，约 1 分钟；人别挡光"
    ask "准备好了吗" || return
    backup calib-flat
    paused "$TOOL" calib-flat 2>&1 | tee "$TMP"
    echo; grep -E "^== flat|p5\.\.p95" "$TMP"
    echo "判据：覆盖 > 95 %；校正后纸面 p5~p95 在 95~105 %。不行 → 纸没盖满 / 有阴影，重来"
}

calib_cam() {
    _b=9x6; _s=25
    echo "相机标定（只有菜单 5 外径超差、或相机被碰过才做）"
    echo "1. 棋盘格硬板（$_b 内角点，格边 $_s mm——卡尺量过再确认）平放在画面中央、整块都在视野里"
    echo "2. 做完接着做走带标定（菜单 8），否则坐标会对不上"
    printf '格边长 mm（直接回车 = %s）: ' "$_s"; read -r _x; [ -n "$_x" ] && _s=$_x
    ask "放好了吗" || return
    backup calib-cam
    paused "$TOOL" calib-cam --board "$_b" --square "$_s" --travel left --dump "$DES_HOME/calib.png" 2>&1 | tee "$TMP"
    echo; grep -E "fit residual" "$TMP"
    echo "判据：rms < 0.3 mm（上次 0.240）。calib.png 里红色 x 箭头要指向皮带前进方向（电脑 get_pics.bat 看）"
}

calib_belt() {
    echo "走带标定（相机标定之后做）"
    echo "1. 一小块平的白纸片（约 3×3 cm）放画面上游；只放它，不要用齿轮"
    echo "2. 皮带会往前走一段再退回"
    ask "准备好了吗" || return
    backup calib-belt
    paused "$TOOL" calib-belt 2>&1 | tee "$TMP"
    echo; grep -E "^== belt" "$TMP"
    echo "判据：mm/step 与上次 0.020921 同量级（差 < 1 %）；dir 与上次相同"
}

calib_beam() {
    echo "光束标定（相机或对射被挪过就做）"
    echo "1. 一个标准齿轮放画面里、对射光束上游、皮带中线附近；对射探头不能被挡"
    echo "2. 皮带会把它带过光束再退回，共 3 轮"
    printf '齿轮卡尺外径 mm（直接回车 = %s）: ' "$STD_OD"; read -r _x; _od=${_x:-$STD_OD}
    ask "准备好了吗" || return
    _old=$(jsec "$CAL" beam x_mm)
    backup calib-beam
    paused "$TOOL" calib-beam -n 3 --od "$_od" 2>&1 | tee "$TMP"
    _new=$(jsec "$CAL" beam x_mm)
    echo; grep -E "^== beam" "$TMP"
    awk -v a="$_old" -v b="$_new" 'BEGIN { if (a == "" || b == "") exit;
        s = b - a; printf "光束 x：旧 %.1f → 新 %.1f，差 s = %+.1f mm\n", a, b, s;
        if (s < 0) s = -s;
        print (s <= 3 ? "|s| ≤ 3 mm：相机基本没动，其他坐标不用改" :
                        "|s| > 3 mm：对射没拆过 → 相机沿皮带挪了，做一次手眼标定（菜单 15），挡板碰件点用过件测试核对") }'
    echo "对射探头拆装过：这个差值只说明对射装到了新位置，不能拿来判断相机动没动——相机看菜单 5（外径）和过件夹取"
    echo "判据：3 轮 sd < 0.5 mm"
}

calib_enc() {
    echo "编码器标定（计米轮被挪过 / 重装皮带后做）：皮带上必须是空的；往返 300 mm × 3，约 40 秒"
    ask "皮带空了吗" || return
    backup calib-enc
    paused "$TOOL" calib-enc -n 3 2>&1 | tee "$TMP"
    echo; grep -E "^== encoder" "$TMP"
    echo "判据：sd < 0.2 %；mm/count 与上次 0.0492 同量级"
}

live_view() {
    echo "实时画面（对焦、看曝光）：看 HDMI 小屏，看完按 Ctrl-C 结束，主程序会自动恢复"
    ask "开始吗" || return
    paused "$DES_HOME/des_app.sh" live
}

# ---------------------------------------------------------------- 机械臂
arm_read() { paused "$TOOL" arm read; }

arm_home() {
    _home_go() {
        "$TOOL" arm home || return
        echo; ask "上面是规划好的动作（还没动）。机械臂周围清空了，真的动吗" || return
        "$TOOL" arm home --go
    }
    paused _home_go
}

torque_warn() {
    echo "!! 力矩一关，机械臂会立刻往下掉：先用一只手托住机械臂（托住大臂和小臂），整个过程别松手 !!"
}

bins_list() { tr -d '\n\t ' < "$CFG" | grep -o '"bins":\[[^]]*\]' | sed 's/"bins":\[//; s/\]$//; s/"//g'; }

set_bin_pose() {   # set_bin_pose <序号> <[a, b, c, d, e]>：只改 des_app.json 的 arm.bins_deg 里这一个
    awk -v idx="$1" -v pose="$2" '
        /"bins_deg":/ && !done {
            k = index($0, "\"bins_deg\":"); pre = substr($0, 1, k - 1); rest = substr($0, k + 11)
            trail = ""; if (rest ~ /,[ \t]*$/) trail = ","
            gsub(/[ \t]/, "", rest); sub(/,$/, "", rest)
            inner = substr(rest, 3, length(rest) - 4)
            n = split(inner, arr, /\],\[/)
            p = pose; gsub(/\[/, "", p); gsub(/\]/, "", p); gsub(/[ \t]/, "", p)
            if (split(p, chk, ",") != 5 || p !~ /^[-0-9.,]+$/) { print "pose must be 5 numbers" > "/dev/stderr"; exit 5 }
            if (idx + 1 > n) { print "bin index out of range" > "/dev/stderr"; exit 3 }
            arr[idx + 1] = p
            out = "[["
            for (i = 1; i <= n; i++) { s = arr[i]; gsub(/,/, ", ", s); out = out s (i < n ? "], [" : "]]") }
            print pre "\"bins_deg\": " out trail; done = 1; next
        }
        { print }
        END { if (!done) exit 4 }' "$CFG" > "$CFG.new" || { rm -f "$CFG.new"; return 1; }
    mv "$CFG.new" "$CFG"
}

bin_teach() {
    _names=$(bins_list); _cnt=$(echo "$_names" | awk -F, '{print NF}')
    echo "料盒示教：把夹爪摆到料盒上方，记下关节角写进配置。现有 $_cnt 个料盒："
    echo "$_names" | awk -F, '{for (i = 1; i <= NF; i++) printf "  %d = %s\n", i - 1, $i}'
    grep -o '"bins_deg": *\[\[.*\]\]' "$CFG" | sed 's/^/  现值 /'
    printf '教第几个（直接回车 = 0）: '; read -r _i; _i=${_i:-0}
    case "$_i" in *[!0-9]*) echo "要填数字"; return;; esac
    [ "$_i" -lt "$_cnt" ] || { echo "只有 0 ~ $((_cnt - 1))"; return; }
    torque_warn
    ask "托住了吗，关力矩" || return
    _teach() {
        "$TOOL" arm torque-off
        echo; echo "把夹爪摆到料盒 $_i 正上方：夹爪尖在盒口上方 1~2 cm、对准盒中心、夹爪朝下（手一直托着）"
        printf '摆好了按回车（读角度）'; read -r _
        "$TOOL" arm teach 2>&1 | tee "$TMP"
        echo; printf '读完了，手继续托着，按回车上力矩（机械臂会定在这个姿态）'; read -r _
        "$TOOL" arm torque-on
    }
    paused _teach
    _pose=$(sed -n 's/.*arm\.bins_deg: *\(\[[^]]*\]\).*/\1/p' "$TMP" | head -1)
    [ -n "$_pose" ] || { echo "** 没读到角度（舵机没应答？菜单 3），配置没改"; return; }
    echo "新的料盒 $_i 姿态：$_pose"
    ask "写进配置吗" || { echo "没写。"; return; }
    backup bin-teach
    set_bin_pose "$_i" "$_pose" || { echo "** 写入失败，配置没改"; return; }
    grep -o '"bins_deg": *\[\[.*\]\]' "$CFG" | sed 's/^/已写入 /'
    echo; echo "检查这个姿态能不能用（只规划、不动）："
    _px=$(awk -v a="$(jsec "$CAL" tracker fov_x1)" -v b="$(grep -o '"pick_after_fov_mm": *[0-9.]*' "$CFG" | grep -o '[0-9.]*$')" 'BEGIN { printf "%.1f", a + b }')
    _chk() { echo "（抓取线上 ($_px, 0) 抓一件、放进料盒 $_i）"; "$TOOL" arm pick "$_px" 0 keep "$_i" 2>&1 | tail -n 4; }
    paused _chk
    echo; echo "判据：上面没有 plan_pick 报错（报「not pointing down」「cannot rise」= 夹爪没朝下 / 太低，重教或菜单 19 恢复）。"
    echo "之后：菜单 21 重启主程序，过一件良品看是不是落进盒子中心。"
}

home_teach() {
    echo "初始姿态示教（触屏「机械臂」页「设为初始姿态」也能做，那边更直观）"
    echo "初始姿态 = 每次抓取的出发点：夹爪朝下、在皮带上方、不挡相机、内侧手指离黑色底板有空隙"
    torque_warn
    ask "托住了吗，关力矩" || return
    _ht() {
        "$TOOL" arm torque-off
        echo; printf '摆好初始姿态后按回车（读角度）'; read -r _
        "$TOOL" arm read 2>&1 | tee "$TMP"
        echo; printf '手继续托着，按回车上力矩'; read -r _
        "$TOOL" arm torque-on
    }
    paused _ht
    _c=$(sed -n 's/^counts \([0-9]*\) \([0-9]*\) \([0-9]*\) \([0-9]*\) \([0-9]*\) | grip \([0-9]*\)$/\1, \2, \3, \4, \5, \6/p' "$TMP")
    [ -n "$_c" ] || { echo "** 没读到计数，没改"; return; }
    echo "新的初始姿态计数：[$_c]（旧：$(grep -o '"counts": *\[[^]]*\]' "$HOMEJ" 2>/dev/null)）"
    ask "写进 arm_home.json 吗" || return
    backup home-teach
    printf '{"comment": "arm ready pose taught with field.sh", "counts": [%s]}\n' "$_c" > "$HOMEJ"
    echo "已写入。菜单 21 重启主程序后生效；然后菜单 13 回初始姿态看一眼。"
}

calib_arm() {
    echo "手眼标定（相机挪过、机械臂底座挪过、过件时夹偏 > 3 mm 才做）。约 15 分钟，全程交互："
    echo "  标记物：一个 10~20 mm 的平片或齿轮（厚 10.05 mm 就填 10.05）"
    echo "  每个点：程序让相机定位标记 → 皮带把它往下游带一段到机械臂跟前 → 你手掰夹爪尖碰标记中心 → 回车"
    echo "  点要散开（远 / 近 / 左 / 右），共 5 点"
    printf '标记物厚度 mm（直接回车 = 10.05）: '; read -r _h; _h=${_h:-10.05}
    echo "皮带带多远：≈ 底座沿皮带的位置 − 标记物在画面里的位置（标记放画面下游半段时，底座在 x≈428 填 100，x≈473 填 135）"
    printf '带多远 mm（直接回车 = 100）: '; read -r _cy; _cy=${_cy:-100}
    printf '几个点（直接回车 = 5，至少 3）: '; read -r _np; _np=${_np:-5}
    case "$_cy$_np" in *[!0-9]*) echo "要填整数"; return;; esac
    torque_warn
    ask "开始吗" || return
    backup calib-arm
    _ca() {
        "$TOOL" calib-arm -n "$_np" --marker-h "$_h" --carry "$_cy" 2>&1 | tee "$TMP"
        echo; printf '标定结束，力矩还是关的。手托住机械臂，按回车上力矩'; read -r _
        "$TOOL" arm torque-on
    }
    paused _ca
    echo; grep -E "^== arm base" "$TMP"
    echo "判据：residual rms 越小越好（上次 3.15 mm，能用）；> 5 mm 说明某个点碰歪了，重做或菜单 19 恢复。"
    # 抓取线 = 视野下游边 + pick_after_fov_mm，原来正好在底座前面：底座沿皮带挪了多少，抓取线跟着挪多少
    _ox=$(base_x "$_d/calib.json"); _nx=$(base_x "$CAL")
    _paf=$(grep -o '"pick_after_fov_mm": *[0-9.]*' "$CFG" | grep -o '[0-9.]*$')
    if [ -n "$_ox" ] && [ -n "$_nx" ] && [ -n "$_paf" ]; then
        _np=$(awk -v p="$_paf" -v o="$_ox" -v n="$_nx" 'BEGIN { printf "%.1f", p + (n - o) }')
        echo; echo "底座沿皮带：$(fmt "$_ox" 1) → $(fmt "$_nx" 1) mm；抓取线跟着挪：pick_after_fov_mm $_paf → $_np"
        if ask "把抓取线改成 $_np 吗（底座挪动 < 2 mm 可以不改）"; then
            sed -i "s/\"pick_after_fov_mm\": *[0-9.]*/\"pick_after_fov_mm\": $_np/" "$CFG"
            grep -o '"pick_after_fov_mm": *[0-9.]*' "$CFG" | sed 's/^/已写入 /'
        fi
    fi
    echo "接着：菜单 27 查可达范围 → 菜单 21 重启主程序 → 过 3 件良品看夹取位置。"
}

base_x() { tr -d '\n\t ' < "$1" 2>/dev/null | grep -o '"arm":{[^}]*}' | grep -o '"base_xy":\[[-0-9.eE+]*' | cut -d[ -f2; }

reach_check() {   # 沿抓取线逐个 y 只规划、不动，看哪些位置抓得到
    _paf=$(grep -o '"pick_after_fov_mm": *[0-9.]*' "$CFG" | grep -o '[0-9.]*$')
    _f1=$(jsec "$CAL" tracker fov_x1)
    _px=$(awk -v a="$_f1" -v b="$_paf" 'BEGIN { printf "%.1f", a + b }')
    echo "抓取线 x = $_px mm（视野下游边 $(fmt "$_f1" 1) + $_paf）。皮带横向逐个位置只规划、不动："
    _rc() {
        for _y in -70 -60 -50 -40 -30 -20 -10 0 10 20 30 40 50 60 68 75 80; do
            _o=$("$TOOL" arm pick "$_px" "$_y" 2>&1 | grep -E "plan_pick|DRY RUN" | head -1)
            case "$_o" in
                *DRY*) printf '  y %4s  可以\n' "$_y";;
                *)     printf '  y %4s  不行：%s\n' "$_y" "$(echo "$_o" | sed 's/plan_pick: //' | cut -c1-70)";;
            esac
        done
    }
    paused _rc
    echo "现配置的可抓范围 reach_y0 ~ reach_y1 = $(grep -o '"reach_y0": *-*[0-9.]*' "$CFG" | grep -o '[-0-9.]*$') ~ $(grep -o '"reach_y1": *-*[0-9.]*' "$CFG" | grep -o '[-0-9.]*$') mm"
    echo "要在上面「可以」的范围里；超出的话菜单 23 改 reach_y0 / reach_y1（各留 5 mm 余量），菜单 21 重启生效。"
}

torque_menu() {
    printf '1 = 上力矩（机械臂定住）  0 = 关力矩（会掉！）  其他 = 返回: '; read -r _t
    case "$_t" in
        1) paused "$TOOL" arm torque-on;;
        0) torque_warn; ask "托住了吗" && paused "$TOOL" arm torque-off;;
    esac
}

# ---------------------------------------------------------------- 挡板
flap_try() {
    _o=$(grep -o '"open_us": *[0-9]*' "$CFG" | head -1 | grep -o '[0-9]*$')
    _c=$(grep -o '"closed_us": *[0-9]*' "$CFG" | head -1 | grep -o '[0-9]*$')
    echo "挡板脉宽试验（通道 0）。现配置：打开 $_o µs、合上 $_c µs。打开 = 摆杆折回带外；合上 = 斜拦带面、底边离带面 1~2 mm"
    _ft() {
        while :; do
            printf '输入脉宽 µs（500~2500；o = 回到打开位并结束）: '; read -r _u
            case "$_u" in
                o|O|"") "$DESREG" pwm 0 "$_o"; break;;
                *[!0-9]*) echo "要填数字";;
                *) if [ "$_u" -ge 500 ] && [ "$_u" -le 2500 ]; then "$DESREG" pwm 0 "$_u" && echo "已输出 $_u µs"; else echo "超范围"; fi;;
            esac
        done
    }
    paused _ft
    printf '要把新值写进配置吗？输入「打开 合上」两个数（如 1500 1100），直接回车 = 不改: '; read -r _no _nc
    [ -n "$_nc" ] || return
    case "$_no$_nc" in *[!0-9]*) echo "要填数字，没改"; return;; esac
    backup flap
    sed -i -e "s/\"open_us\": *[0-9]*/\"open_us\": $_no/" -e "s/\"closed_us\": *[0-9]*/\"closed_us\": $_nc/" "$CFG"
    grep -o '"open_us": *[0-9]*, *"closed_us": *[0-9]*' "$CFG" | sed 's/^/已写入 /'
    echo "菜单 21 重启主程序生效。"
}

# ---------------------------------------------------------------- 备份 / 恢复 / 黄金参考
restore_menu() {
    [ -d "$BK" ] && [ -n "$(ls "$BK" 2>/dev/null)" ] || { echo "还没有备份"; return; }
    echo "备份列表（编号越大越新）："; ls "$BK" | tail -n 15 | sed 's/^/  /'
    printf '恢复哪个（输入完整名字，如 003_calib-flat；直接回车 = 取消）: '; read -r _r
    [ -n "$_r" ] && [ -d "$BK/$_r" ] || { echo "取消"; return; }
    echo "将覆盖：$(ls "$BK/$_r" | tr '\n' ' ')"
    ask "确定恢复吗" || return
    backup before-restore
    paused restore_dir "$BK/$_r"
}

golden() {
    printf '1 = 记录黄金参考（会先测一次标准齿轮）  2 = 与黄金参考对比  其他 = 返回: '; read -r _g
    case "$_g" in
        1)  measure_od || return
            {
                echo "# golden.txt — 黄金参考（field.sh 菜单 20 记录；到场对比用）"
                echo "uptime_h=$(awk '{printf "%.1f", $1 / 3600}' /proc/uptime)"
                echo "od_mm=$OD"
                echo "bore_mm=$BORE"
                echo "mm_per_px=$(jtop "$CAL" mm_per_px)"
                echo "cam_rms_mm=$(jtop "$CAL" rms_mm)"
                echo "belt_mm_per_step=$(jsec "$CAL" belt mm_per_step)"
                echo "enc_mm_per_count=$(jsec "$CAL" encoder mm_per_count)"
                echo "beam_x_mm=$(jsec "$CAL" beam x_mm)"
                echo "arm_rms_mm=$(jsec "$CAL" arm rms_mm)"
                echo "arm_home_counts=$(grep -o '"counts": *\[[^]]*\]' "$HOMEJ" | sed 's/"counts": *//')"
                echo "flap_open_us=$(grep -o '"open_us": *[0-9]*' "$CFG" | head -1 | grep -o '[0-9]*$')"
                echo "flap_closed_us=$(grep -o '"closed_us": *[0-9]*' "$CFG" | head -1 | grep -o '[0-9]*$')"
            } > "$GOLD"
            echo; cat "$GOLD"; echo "已存 $GOLD（电脑 field_kit\\pull_backup.bat 会一并拷走）";;
        2)  [ -f "$GOLD" ] || { echo "还没记录黄金参考"; return; }
            echo "项目                 黄金参考        现在"
            _cmp() { printf '%-20s %-15s %s\n' "$1" "$(sed -n "s/^$2=//p" "$GOLD")" "$3"; }
            _cmp "相机 mm/px"        mm_per_px        "$(jtop "$CAL" mm_per_px)"
            _cmp "相机 rms mm"       cam_rms_mm       "$(jtop "$CAL" rms_mm)"
            _cmp "皮带 mm/step"      belt_mm_per_step "$(jsec "$CAL" belt mm_per_step)"
            _cmp "编码器 mm/count"   enc_mm_per_count "$(jsec "$CAL" encoder mm_per_count)"
            _cmp "光束 x mm"         beam_x_mm        "$(jsec "$CAL" beam x_mm)"
            _cmp "手眼 rms mm"       arm_rms_mm       "$(jsec "$CAL" arm rms_mm)"
            _cmp "初始姿态计数"      arm_home_counts  "$(grep -o '"counts": *\[[^]]*\]' "$HOMEJ" | sed 's/"counts": *//')"
            _cmp "挡板 打开 µs"      flap_open_us     "$(grep -o '"open_us": *[0-9]*' "$CFG" | head -1 | grep -o '[0-9]*$')"
            _cmp "挡板 合上 µs"      flap_closed_us   "$(grep -o '"closed_us": *[0-9]*' "$CFG" | head -1 | grep -o '[0-9]*$')"
            echo "标准齿轮外径黄金值 $(sed -n 's/^od_mm=//p' "$GOLD") mm：现在的值用菜单 5 测";;
    esac
}

edit_cfg() {
    echo "用 vi 改 des_app.json（先自动备份）。vi：i 进入编辑，Esc 退出编辑，:wq 回车 = 保存退出，:q! 回车 = 不保存退出"
    ask "打开吗" || return
    backup edit
    vi "$CFG"
    if config_ok; then echo "配置读取正常。菜单 21 重启主程序生效。"
    else echo "** 配置读不进去（上面是报错）。菜单 19 恢复刚才那个 *_edit 备份。"; fi
}

# ---------------------------------------------------------------- 服务
restart_app() { "$SVC" restart; sleep 2; "$SVC" status | head -3; }

g4() {
    "$DES_HOME/des_4g_service.sh" status
    echo; tail -n 8 "$DES_DATA/des_4g.log" 2>/dev/null | cut -c1-140
    echo; echo "正常：日志里有 connected to ...、remote commands on。只重启 4G 时服务器可能拒绝约 30 秒（QMTOPEN result 2），会自己重连。"
    echo "一直 result 4 / CONNECT no answer / not registered：先用手机查这张卡有没有欠费、流量用完；缴费后选「重启模组」。"
    printf '1 = 重启 4G 上报程序   2 = 重启 4G 模组（连不上时用，约 1 分钟）   其他 = 返回: '; read -r _g
    case "$_g" in
        1) "$DES_HOME/des_4g_service.sh" restart ;;
        2)   # 11-18: the card ran out of credit; after paying, only a module reset brought the data connection back
            "$DES_HOME/des_4g_service.sh" stop
            sleep 2
            "$DES_TOOLS/des_4g" at "AT+CFUN=1,1" 10 > /dev/null 2>&1
            echo "模组重启中…"
            sleep 5
            _i=0
            while [ $_i -lt 30 ] && ! "$DES_TOOLS/des_4g" at "AT" 2 2> /dev/null | grep -q '^OK'; do sleep 2; _i=$((_i + 1)); done
            "$DES_HOME/des_4g_service.sh" start
            echo "等约 30 秒后再进本菜单看日志里有没有 connected to" ;;
    esac
}

boot_switch() {   # 换比特流：卡上留有命名副本 BOOT.bin.v17b2c / BOOT.bin.v16，复制一份成 BOOT.bin 再重启
    _bd=/mnt/mmcblk0p1
    echo "现在：$(md5sum $_bd/BOOT.bin | cut -c1-8)（08ec06c4 = v17b2c 决赛版，71f41a20 = v16 退路版）"
    echo "什么时候切 v16：小屏反复黑屏 / 花屏，且菜单 1 像素时钟正常、换过供电和线都不行。v16 少带速 PI 和振动，主流程一样。"
    printf '切到哪个：1 = v17b2c（决赛版）  2 = v16（退路版）  其他 = 返回: '; read -r _v
    case "$_v" in 1) _src=$_bd/BOOT.bin.v17b2c; _want=08ec06c4;; 2) _src=$_bd/BOOT.bin.v16; _want=71f41a20;; *) return;; esac
    [ "$(md5sum "$_src" 2>/dev/null | cut -c1-8)" = "$_want" ] || { echo "** $_src 不在或校验不对，没换"; return; }
    ask "换成 $(basename "$_src") 并重启吗（重启约 1 分钟，机械臂会保持力矩）" || return
    cp "$_src" $_bd/BOOT.bin && sync
    [ "$(md5sum $_bd/BOOT.bin | cut -c1-8)" = "$_want" ] || { echo "** 复制后校验不对，先别重启，找人"; return; }
    echo "已换好，重启 ..."; sleep 1; reboot
}

safe_off() {
    echo "安全关机：停主程序 → 关机。看到「可以断电」后约 2 分钟内断电（否则看门狗会让它重启）。"
    echo "断电前：手托住机械臂（舵机断电会掉）。"
    ask "确定关机吗" && "$DES_HOME/shutdown.sh"
}

# ---------------------------------------------------------------- 自检（field.sh --selftest：在临时副本上演练改配置，不碰真配置）
selftest() {
    _t=/tmp/field_selftest; rm -rf "$_t"; mkdir -p "$_t"; cp "$DES_HOME"/*.json "$_t/"
    CFG="$_t/des_app.json"; _f=0
    _ok() { if [ "$1" = 0 ]; then echo "  ok   $2"; else echo "  FAIL $2"; _f=$((_f + 1)); fi; }
    config_ok > /dev/null; _ok $? "原配置能读"
    [ -n "$(bins_list)" ]; _ok $? "料盒名：$(bins_list)"
    set_bin_pose 0 "[1.5, -2.5, 30.0, -60.0, 89.8]"; _ok $? "改料盒 0 姿态"
    grep -q '"bins_deg": \[\[1.5, -2.5, 30.0, -60.0, 89.8\]\]' "$CFG"; _ok $? "改后的值：$(grep -o '"bins_deg": *\[\[.*\]\]' "$CFG")"
    config_ok > /dev/null; _ok $? "改后配置能读"
    set_bin_pose 5 "[1, 2, 3, 4, 5]" 2> /dev/null; [ $? != 0 ]; _ok $? "超出料盒个数会拒绝"
    set_bin_pose 0 "[1, 2, 3]" 2> /dev/null; [ $? != 0 ]; _ok $? "不是 5 个数会拒绝"
    grep -q '"bins_deg": \[\[1.5, -2.5, 30.0, -60.0, 89.8\]\]' "$CFG"; _ok $? "被拒绝时配置不变"
    sed -i -e 's/"open_us": *[0-9]*/"open_us": 1490/' -e 's/"closed_us": *[0-9]*/"closed_us": 1110/' "$CFG"
    grep -q '"open_us": 1490, *"closed_us": 1110' "$CFG"; _ok $? "改挡板脉宽"
    config_ok > /dev/null; _ok $? "改挡板后配置能读"
    echo '{"bad json' > "$CFG"; config_ok > /dev/null 2>&1; [ $? != 0 ]; _ok $? "坏配置能被发现"
    [ -n "$(base_x "$CAL")" ] && [ -n "$(jsec "$CAL" tracker fov_x1)" ]; _ok $? "读底座 x $(base_x "$CAL" | cut -c1-6)、视野下游边 $(jsec "$CAL" tracker fov_x1 | cut -c1-6)"
    [ -n "$(jtop "$CAL" mm_per_px)" ] && [ -n "$(jsec "$CAL" beam x_mm)" ] && [ -n "$(jsec "$CAL" belt mm_per_step)" ]
    _ok $? "读标定值：mm/px $(jtop "$CAL" mm_per_px | cut -c1-8)、光束 x $(jsec "$CAL" beam x_mm | cut -c1-6)、皮带 $(jsec "$CAL" belt mm_per_step | cut -c1-8)"
    rm -rf "$_t"
    [ $_f = 0 ] && echo "PASS field.sh selftest" || echo "FAIL field.sh selftest ($_f)"
    return $_f
}
[ "$1" = "--selftest" ] && { selftest; exit $?; }

# ---------------------------------------------------------------- 菜单
menu() {
    cat <<'EOF'

==================== 现场调试菜单 ====================
 检查（不动设备）           1 状态总览        2 主程序日志
 设备检查（自动暂停主程序） 3 舵机扫描        4 PL 回归检查
 画面                       5 测标准齿轮外径  6 平场（到场必做）
                            7 相机标定        8 走带标定
                            9 光束标定       10 编码器标定
                           11 实时画面（对焦）
 机械臂                    12 读当前姿态     13 回初始姿态（会动）
                           14 料盒示教       15 手眼标定
                           16 力矩开 / 关    17 初始姿态示教
 挡板                      18 挡板脉宽试验
 备份                      19 恢复备份       20 黄金参考：记录 / 对比
                           22 立即备份       23 改配置（vi）
 服务                      21 重启主程序     24 4G 状态 / 重启
                           25 安全关机       26 切换比特流（v17b2c / v16）
 机械臂挪位后              27 可达范围检查（只规划，不动）
 q 退出
======================================================
EOF
    printf '选: '
}

while :; do
    menu; read -r ch || exit 0
    case "$ch" in
        1) status;;        2) show_log;;      3) servo_scan;;    4) pl_check;;
        5) measure_od;;    6) calib_flat;;    7) calib_cam;;     8) calib_belt;;
        9) calib_beam;;   10) calib_enc;;    11) live_view;;    12) arm_read;;
       13) arm_home;;     14) bin_teach;;    15) calib_arm;;    16) torque_menu;;
       17) home_teach;;   18) flap_try;;     19) restore_menu;; 20) golden;;
       21) restart_app;;  22) printf '备份名（直接回车 = manual）: '; read -r _bn; backup "${_bn:-manual}";;
       23) edit_cfg;;     24) g4;;           25) safe_off;;     26) boot_switch;;
       27) reach_check;;
        q|Q) exit 0;;
        *) continue;;
    esac
    pause
done
