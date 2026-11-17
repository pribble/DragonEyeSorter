// hmi.cpp — see hmi.hpp. Pages use the ui_text helpers; visible strings, positions, hit-box arg encodings and colour
// choices are unchanged from the pre-refactor inline code.
#include "hmi.hpp"

#include <linux/input.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

#include <opencv2/imgproc.hpp>

#include "can_proto.h"
#include "json.hpp"
#include "util.hpp"

namespace des {
using namespace ui;
namespace {
enum { A_TAB = 1, A_DEC, A_INC, A_TOGGLE, A_TOGGLE_PID, A_IMAGES, A_CMD, A_SELECT, A_PLMODE, A_PLTHR, A_RECIPE, A_ARM_LOCK,
       A_ARM_TORQUE, A_ARM_JOG, A_ARM_STEP, A_ARM_MS, A_ARM_TEACH, A_ARM_SETHOME, A_ARM_RESET, A_BELT_JOG,
       A_FLAP_LOCK, A_FLAP_OUT, A_FLAP_STEP, A_FLAP_CENTER, A_FLAP_SWEEP, A_FLAP_SPEED, A_REC_SCROLL, A_PLTHR_EDGE,
       A_OPS_SEL, A_OPS_ACK, A_OPS_SCROLL };
const char *kPages[] = {"总览", "曲线", "参数", "记录", "机械臂", "挡板", "运维"};
constexpr int kNPages = 7;
const double kArmStepDeg[] = {1, 5, 15};
const int kArmMs[] = {500, 1000, 2000};
const int kFlapStepUs[] = {-50, -10, 10, 50};
const char *kFlapSpeedName[] = {"慢", "中", "快"};
const char *kFlapName[] = {"挡板 1（报废）", "挡板 2（返修）"};
const char *kFlapPin[] = {"PIN18", "PIN20"};
const char *kImages[] = {"不存", "不良 + 复检", "全部"};
const char *kPlModes[] = {"原图", "PL 灰度", "PL 高斯滤波", "PL 二值", "PL Sobel", "PL 边缘"};
struct ParamDef {
    const char *label, *unit;
    double lo, hi, step;
};
const ParamDef kParams[] = {
    {"带速", "mm/s（产线运行时生效）", 10, 120, 5},       {"齿轮外径 名义", "mm", 20, 60, 0.01},
    {"齿轮外径 公差 ±", "mm", 0.01, 1, 0.01},          {"齿轮孔径 名义", "mm", 2, 20, 0.05},
    {"齿轮孔径 公差 ±", "mm", 0.05, 2, 0.05},          {"尺寸判废", "超差 / 规格混料 → 报废出口", 0, 1, 1},
    {"置信度门限", "低于它 → 复检", 0.3, 0.95, 0.05},   {"存图", "零件裁切图存 SD 卡", 0, 2, 1},
    {"带速 PI", "编码器反馈，默认关", 0, 1, 1}};
constexpr int kNParams = sizeof kParams / sizeof kParams[0];
const char *state_name(int s)
{
    switch (s) {
    case CANP_ST_IDLE: return "停止";
    case CANP_ST_RUN: return "运行";
    case CANP_ST_PAUSED: return "暂停";
    case CANP_ST_ESTOP: return "急停";
    case CANP_ST_FAULT: return "故障";
    default: return "演示";
    }
}
cv::Scalar state_color(int s)
{
    return s == CANP_ST_RUN ? GREEN : s == CANP_ST_PAUSED ? ORANGE : s >= CANP_ST_ESTOP ? RED : DIM;
}
bool starts(const char *s, const char *p, const char **rest = nullptr)
{
    const size_t n = strlen(p);
    if (strncmp(s, p, n) != 0) return false;
    if (rest) *rest = s + n;
    return true;
}
// Reason of a decision (Track::why, English in the CSV) in Chinese for the screen; unknown ones are shown as they are.
std::string why_cn(const char *w, const std::string &class_cn)
{
    static const std::pair<const char *, const char *> exact[] = {
        {"not measured", "未测量"}, {"off-axis: teeth not judged", "离光轴远，齿形未判"}, {"merged blob", "零件粘连"},
        {"no class (bring-up)", "未分类（调试放行）"}, {"no class", "未分类"}, {"low confidence", "置信度低"},
        {"class without rule", "该类别没有去向规则"}, {"noise", "噪点"}, {"lost in view", "画面里跟丢"},
        {"too few frames", "帧数太少"}, {"beyond gate reach", "挡板够不着"}, {"gate too late", "挡板来不及"},
        {"gate conflict", "挡板冲突"}, {"too wide", "太宽夹不住"}, {"crowded", "太挤"},
        {"gripper did not answer (grasp unknown)", "夹爪无应答"}, {"intercept: the part did not arrive", "零件没到夹取点"},
        {"intercept point needs a tilted grasp", "拦截点要倾斜夹取"}};
    for (const auto &e : exact)
        if (strcmp(w, e.first) == 0) return e.second;
    const char *rest = nullptr;
    int a = 0, b = 0, c = 0;
    float f = 0;
    if (sscanf(w, "spec: %d teeth", &a) == 1) return fmt("规格不符：%d 齿", a);
    if (sscanf(w, "OD %f", &f) == 1) return fmt("外径 %.2f 超差", f);
    if (sscanf(w, "bore %f", &f) == 1) return fmt("孔径 %.2f 超差", f);
    if (sscanf(w, "ecc %f", &f) == 1) return fmt("偏心 %.2f 超差", f);
    if (sscanf(w, "measured: short %d missing %d flash %d", &a, &b, &c) == 3)
        return std::string("测量发现") + (a ? fmt(" 缺料 %.0f", double(a)) : "") + (b ? fmt(" 缺齿 %.0f", double(b)) : "") +
               (c ? fmt(" 飞边 %.0f", double(c)) : "");
    if (starts(w, "pick failed: ", &rest)) return std::string("抓取失败：") + rest;
    if (!class_cn.empty()) return class_cn;   // why = the class label: the class decided it
    return w;
}
// cut to w pixels at a code point boundary (byte substr broke Chinese characters), with an ellipsis
std::string fit(Canvas &c, std::string s, int px, int w)
{
    if (c.width(s, px) <= w) return s;
    while (!s.empty() && c.width(s + "…", px) > w) {
        size_t k = s.size() - 1;
        while (k > 0 && (static_cast<unsigned char>(s[k]) & 0xC0) == 0x80) k--;
        s.erase(k);
    }
    return s + "…";
}
// Destination names (des_app.json bins / outlets) for the screen
std::string dest_cn(const char *d)
{
    static const std::pair<const char *, const char *> map[] = {
        {"gear_box", "齿轮盒"}, {"scrap", "报废"}, {"rework", "返修"}, {"recheck", "复检"}, {"tail", "带尾"},
        {"unclassified", "未分类"}};
    for (const auto &e : map)
        if (strcmp(d, e.first) == 0) return e.second;
    return d;
}
}  // namespace
Hmi::Hmi(const AppConfig &cfg, const std::string &params_path) : cfg_(cfg), params_path_(params_path)
{
    txt_.open();
    p_.belt_speed = idle_default(cfg.belt.adapt, cfg.belt.run_speed);   // the set speed is the idle speed (11-18)
    p_.size_rule = cfg.tracker.size.enabled;
    p_.gear_od = cfg.tracker.size.gear_od;  p_.gear_od_tol = cfg.tracker.size.gear_od_tol;
    p_.gear_bore = cfg.tracker.size.gear_bore;  p_.gear_bore_tol = cfg.tracker.size.gear_bore_tol;
    p_.min_conf = cfg.tracker.min_conf;
    p_.images = uint8_t(cfg.storage.images == "none" ? 0 : cfg.storage.images == "all" ? 2 : 1);
    p_.recipe = cfg.recipe;
    p_.belt_pid = cfg.belt.pid.enabled;   // runtime-only switch: not loaded from ui_params.json
    load_params();   // the operator's last settings win over des_app.json
    // per-class counters: one slot per AppConfig rule + the trailing 未分类 slot. MUST exist before add_part()
    // increments it (out-of-bounds write otherwise) and before page_overview draws the bars.
    per_class_.assign(cfg.tracker.rules.size() + 1, 0);
}
// Class names come from ClassRule::cn (config.cpp fills it from label_list.json (scripts/deploy/out/), the single source).
const std::string &Hmi::cn_of(int cls) const
{
    static const std::string kUnknown = "未分类";
    if (cls < 0 || size_t(cls) >= cfg_.tracker.rules.size()) return kUnknown;
    const ClassRule &r = cfg_.tracker.rules[cls];
    return r.cn.empty() ? r.label : r.cn;
}
const char *Hmi::cn_disp(Disposition d) { return d == Disposition::Pick ? "抓取" : d == Disposition::Flap ? "挡板" : "复检"; }
cv::Scalar Hmi::disp_color(Disposition d) { return d == Disposition::Pick ? GREEN : d == Disposition::Flap ? RED : ORANGE; }
void Hmi::load_params()
{
    std::ifstream f(params_path_);
    if (!f) return;
    try {
        nlohmann::json j;
        f >> j;
        p_.belt_speed = j.value("idle_speed", p_.belt_speed);   // "belt_speed" of older files meant the handling speed
        p_.size_rule = j.value("size_rule", p_.size_rule);
        p_.gear_od = j.value("gear_od", p_.gear_od);
        p_.gear_od_tol = j.value("gear_od_tol", p_.gear_od_tol);
        p_.gear_bore = j.value("gear_bore", p_.gear_bore);
        p_.gear_bore_tol = j.value("gear_bore_tol", p_.gear_bore_tol);
        p_.min_conf = j.value("min_conf", p_.min_conf);
        p_.images = uint8_t(std::clamp(j.value("images", int(p_.images)), 0, 2));
        p_.pl_mode = uint8_t(std::clamp(j.value("pl_mode", int(p_.pl_mode)), 0, 5));
        p_.pl_thr = uint8_t(std::clamp(j.value("pl_thr", int(p_.pl_thr)), 0, 255));
        p_.pl_edge_thr = uint16_t(std::clamp(j.value("pl_edge_thr", int(p_.pl_edge_thr)), 0, 2047));
        const int rc = j.value("recipe", p_.recipe);
        if (find_recipe(cfg_.recipes, rc)) p_.recipe = rc;   // a recipe removed from recipes.json: keep the default
    } catch (const std::exception &) {
    }
}
void Hmi::save_params() const
{
    nlohmann::json j = {{"idle_speed", p_.belt_speed}, {"size_rule", p_.size_rule}, {"gear_od", p_.gear_od},
                        {"gear_od_tol", p_.gear_od_tol}, {"gear_bore", p_.gear_bore}, {"gear_bore_tol", p_.gear_bore_tol},
                        {"min_conf", p_.min_conf}, {"images", p_.images}, {"pl_mode", p_.pl_mode},
                        {"pl_thr", p_.pl_thr}, {"pl_edge_thr", p_.pl_edge_thr}, {"recipe", p_.recipe}};
    std::ofstream f(params_path_);
    if (f) f << j.dump(1) << "\n";
}
void Hmi::set_belt_speed(double mm_s)
{
    if (std::fabs(mm_s - p_.belt_speed) < 0.05) return;
    p_.belt_speed = mm_s;
    save_params();
}
bool Hmi::set_recipe(int id)
{
    const Recipe *r = find_recipe(cfg_.recipes, id);
    if (!r) return false;
    p_.recipe = id;
    p_.gear_od = r->gear_od;
    p_.gear_od_tol = r->gear_od_tol;
    p_.gear_bore = r->gear_bore;
    p_.gear_bore_tol = r->gear_bore_tol;
    save_params();
    return true;
}
void Hmi::add_part(const PartRecord &r, const cv::Mat &crop, double now)
{
    total_++;
    if (r.disp == Disposition::Pick) ok_++;
    else if (r.disp == Disposition::Flap) ng_++;
    else recheck_++;
    const size_t unclassified = cfg_.tracker.rules.size();
    per_class_[r.cls >= 0 && size_t(r.cls) < unclassified ? size_t(r.cls) : unclassified]++;
    const bool full = parts_.size() == size_t(kRecords);   // the oldest drops out
    parts_.push({r, crop});
    // crops only for the newest kCrops parts (older rows show 无图); index 0 = oldest
    if (parts_.size() > size_t(kCrops)) parts_[parts_.size() - 1 - kCrops].crop.release();
    // keep the same part selected / the same rows in view while the operator scrolls back
    if (selected_ >= 0) selected_ = selected_ + 1 < int(parts_.size()) ? selected_ + 1 : (full ? -1 : selected_);
    if (rec_top_ > 0) rec_top_ = std::min(rec_top_ + 1, std::max(0, int(parts_.size()) - kRows));
    sel_pic_for_ = -1;
    if (r.meas.valid && r.meas.kind == 'g') {
        od_.push({r.meas.size, r.disp != Disposition::Pick});
        if (r.meas.bore > 0) bore_.push({r.meas.bore, r.disp != Disposition::Pick});
    }
    part_times_.push(now);   // trimmed to the 60 s window in status(), which runs every cycle
}
void Hmi::status(const HmiStatus &s, double now)
{
    st_ = s;
    if (now - last_sample_ >= 0.5) {
        last_sample_ = now;
        samples_.push({s.worker_ms, s.npu_ms, float(s.belt_mm_s), s.enc_live ? float(s.belt_enc_mm_s) : -1.f});
    }
    while (!part_times_.empty() && now - part_times_.front() > 60) part_times_.pop_front();
}
std::vector<HmiAction> Hmi::handle(const std::vector<UiEvent> &ev)
{
    std::vector<HmiAction> out;
    for (const UiEvent &e : ev) {
        if (e.kind == UiEvent::Key) {   // F1-F7 / Tab / <- -> switch pages; PgUp / PgDn scroll the records
            if (e.value >= KEY_F1 && e.value <= KEY_F7) page_ = e.value - KEY_F1;
            else if (e.value == KEY_TAB || e.value == KEY_RIGHT) page_ = (page_ + 1) % kNPages;
            else if (e.value == KEY_LEFT) page_ = (page_ + kNPages - 1) % kNPages;
            else if (page_ == 3 && e.value == KEY_PAGEDOWN) scroll_records(kRows);
            else if (page_ == 3 && e.value == KEY_PAGEUP) scroll_records(-kRows);
            continue;
        }
        if (e.kind == UiEvent::Wheel) {   // wheel up (+) = newer parts
            if (page_ == 3) scroll_records(-3 * e.value);
            else if (page_ == 6) ops_top_ = std::max(0, ops_top_ - 3 * e.value);   // clamped at render
            continue;
        }
        // clicks land either on the status panel's run-control strip (top right of the screen, panel-local hits) or
        // on the operator band below the picture (band-local hits); clicks on the video itself do nothing
        const bool panel_click = e.x >= kPanelX && e.y < Y0;
        if (!panel_click && e.y < Y0) continue;   // a click on the video
        const cv::Point pt = panel_click ? cv::Point(e.x - kPanelX, e.y) : cv::Point(e.x, e.y - Y0);
        const std::vector<Hit> &hits = panel_click ? panel_hits_ : hits_;
        for (const Hit &h : hits) {
            if (!h.box.contains(pt)) continue;
            bool changed = false;
            switch (h.action) {
            case A_TAB: page_ = h.arg; break;
            case A_SELECT: selected_ = h.arg; sel_pic_for_ = -1; break;
            case A_REC_SCROLL: if (h.arg == 0) rec_top_ = 0; else scroll_records(h.arg); break;
            case A_OPS_SEL: ops_sel_ = h.arg; break;
            case A_OPS_SCROLL: ops_top_ = h.arg == 0 ? 0 : std::max(0, ops_top_ + h.arg); break;
            case A_OPS_ACK: out.push_back({HmiAction::Ops, h.arg, {}, {}}); break;
            case A_FLAP_LOCK:
            case A_FLAP_OUT:
            case A_FLAP_STEP:
            case A_FLAP_CENTER:
            case A_FLAP_SWEEP:
            case A_FLAP_SPEED: {
                HmiAction a;
                a.kind = HmiAction::Flap;
                FlapRequest &f = a.flap;
                switch (h.action) {
                case A_FLAP_LOCK: f.kind = h.arg ? FlapRequest::Unlock : FlapRequest::Lock; break;
                case A_FLAP_OUT: f.kind = FlapRequest::Output; f.ch = h.arg / 2; f.value = h.arg % 2; break;
                case A_FLAP_STEP: f.kind = FlapRequest::Step; f.ch = h.arg / 8; f.value = kFlapStepUs[h.arg % 8]; break;
                case A_FLAP_CENTER: f.kind = FlapRequest::Center; f.ch = h.arg; break;
                case A_FLAP_SWEEP: f.kind = FlapRequest::Sweep; f.ch = h.arg; break;
                default: f.kind = FlapRequest::Speed; f.value = h.arg; break;
                }
                out.push_back(a);
                break;
            }
            case A_CMD: out.push_back({HmiAction::Command, h.arg, {}, {}}); break;
            case A_BELT_JOG: out.push_back({HmiAction::Jog, h.arg, {}, {}}); break;
            case A_ARM_STEP: arm_step_ = h.arg; break;
            case A_ARM_MS: arm_ms_ = h.arg; break;
            case A_ARM_LOCK:
            case A_ARM_TORQUE:
            case A_ARM_JOG:
            case A_ARM_TEACH:
            case A_ARM_SETHOME:
            case A_ARM_RESET: {
                HmiAction a;
                a.kind = HmiAction::Arm;
                a.arm.kind = h.action == A_ARM_LOCK ? (h.arg ? ArmRequest::Unlock : ArmRequest::Lock)
                           : h.action == A_ARM_TORQUE ? ArmRequest::Torque : h.action == A_ARM_JOG ? ArmRequest::Jog
                           : h.action == A_ARM_SETHOME ? ArmRequest::SetHome : h.action == A_ARM_RESET ? ArmRequest::Reset
                                                                                                  : ArmRequest::Teach;
                a.arm.on = h.arg;
                a.arm.joint = h.arg / 2;   // jog: arg = joint * 2 + (1 = plus)
                a.arm.dir = h.arg % 2 ? 1 : -1;
                a.arm.step_deg = kArmStepDeg[arm_step_];
                a.arm.ms = kArmMs[arm_ms_];
                out.push_back(a);
                break;
            }
            case A_TOGGLE: p_.size_rule = !p_.size_rule; changed = true; break;
            case A_TOGGLE_PID: p_.belt_pid = !p_.belt_pid; changed = true; break;
            case A_IMAGES: p_.images = uint8_t((p_.images + 1) % 3); changed = true; break;
            case A_PLMODE: p_.pl_mode = uint8_t(h.arg); changed = true; break;
            case A_PLTHR: p_.pl_thr = uint8_t(std::clamp(int(p_.pl_thr) + h.arg * 5, 0, 255)); changed = true; break;
            case A_PLTHR_EDGE: p_.pl_edge_thr = uint16_t(std::clamp(int(p_.pl_edge_thr) + h.arg * 20, 0, 2047)); changed = true; break;
            case A_RECIPE: {
                const auto &rs = cfg_.recipes;
                const int n = int(rs.size());
                int k = 0;
                while (k < n && rs[k].id != p_.recipe) k++;
                if (n > 1) out.push_back({HmiAction::Recipe, rs[((k < n ? k : 0) + h.arg + n) % n].id, {}, {}});
                break;
            }
            case A_DEC:
            case A_INC: {
                const ParamDef &d = kParams[h.arg];
                const double dir = h.action == A_INC ? 1 : -1;
                auto step = [&](double v) { return std::clamp(std::round((v + dir * d.step) / d.step) * d.step, d.lo, d.hi); };
                if (h.arg == 0) p_.belt_speed = step(p_.belt_speed);
                else if (h.arg == 5) p_.size_rule = dir > 0;
                else if (h.arg == 7) p_.images = uint8_t(std::clamp(int(p_.images) + int(dir), 0, 2));
                else if (h.arg == 8) p_.belt_pid = dir > 0;
                else {
                    float &f = h.arg == 1 ? p_.gear_od : h.arg == 2 ? p_.gear_od_tol
                            : h.arg == 3 ? p_.gear_bore : h.arg == 4 ? p_.gear_bore_tol : p_.min_conf;
                    f = float(step(f));
                }
                changed = true;
                break;
            }
            }
            if (changed) { save_params(); out.push_back({HmiAction::Params, 0, {}, {}}); }
            break;
        }
    }
    return out;
}
cv::Mat Hmi::render()
{
    cv::Mat img(H, W, CV_8UC3, BG);   // a FRESH buffer every call: live.cpp uses .data as its dirty flag, and reused
    hits_.clear();                    // buffers would alias pool snapshots (mid-blit tearing)
    cv::line(img, {0, 0}, {W - 1, 0}, ACCENT, 2);
    Canvas c(img, txt_, hits_);
    c.text("DragonEyeSorter", 12, 6, 17, DIM);   // navigation + status (product name stays in English)
    const int unacked = st_.ops ? st_.ops->summary().unacked : 0;   // 运维 shows how many orders wait
    for (int i = 0; i < kNPages; i++)
        c.button({8, 26 + i * 33, 136, 30}, i == 6 && unacked ? fmt("运维（%d）", unacked) : std::string(kPages[i]),
                 page_ == i, A_TAB, i, 19);
    c.text(st_.npu ? "NPU 在线" : "NPU 未加载", 12, 260, 15, st_.npu ? GREEN : ORANGE);
    c.text(std::string("输入设备 ") + std::to_string(st_.inputs) + (st_.clock_synced ? "  已对时" : "  未对时"), 12,
           280, 14, DIM);
    cv::line(img, {152, 10}, {152, H - 10}, LINE, 1);
    switch (page_) {
    case 0: page_overview(c); break;
    case 1: page_curves(c); break;
    case 2: page_params(c); break;
    case 3: page_records(c); break;
    case 4: page_arm(c); break;
    case 5: page_flap(c); break;
    default: page_ops(c); break;
    }
    return img;
}
// Run control at the top of the status panel (always visible on every page; was buried in the params page). Called by
// live.cpp's worker right after its render_panel(), which reserves rows 0..kStripBottom for it. Same thread as
// handle(), so panel_hits_ needs no lock. Commands are the PLC's CAN commands (runner->command, live.cpp dispatch).
// 复位报警 is NOT on the strip: in normal states it does nothing (nothing to reset), so it only appears as a wide
// button while 急停/故障 — START is rejected by Runner::command until the alarm is reset, so the recovery path needs it.
void Hmi::render_panel_strip(cv::Mat &panel)
{
    panel_hits_.clear();
    Canvas c(panel, txt_, panel_hits_);
    const bool run = st_.run_state >= 0;
    cv::rectangle(panel, {18, 66, 444, 32}, state_color(st_.run_state), cv::FILLED);
    if (st_.run_state == CANP_ST_ESTOP)
        c.text("急停：先旋开急停按钮，再点下方复位报警、启动", 28, 73, 18, ON_TEXT);
    else
        c.text(std::string(state_name(st_.run_state)) +
                   (st_.jog > 0 ? "（正转点动）" : st_.jog < 0 ? "（反转点动）" : "") +
                   (st_.enc_live ? fmt("  带速 %.1f / 实测 %.1f mm/s", st_.belt_mm_s, st_.belt_enc_mm_s)
                                 : fmt("  带速 %.1f mm/s", st_.belt_mm_s)),
               28, 73, 18, ON_TEXT);
    // one 4-column grid for both rows (uniform 105 px + 8 gap): the pairs sit side by side — 启动|停止, 暂停|恢复,
    // 正转|反转 below on the same columns
    const char *bt[] = {"启动", "停止", "暂停", "恢复"};
    const int bc[] = {CANP_CMD_START, CANP_CMD_STOP, CANP_CMD_PAUSE, CANP_CMD_RESUME};
    for (int k = 0; k < 4; k++)
        c.button({18 + k * 113, 108, 105, 46}, bt[k], false, run ? A_CMD : 0, bc[k], 21);
    // manual belt jog, only while the line is stopped; click the lit button again (or 停止) to stop
    const bool can_jog = run && st_.run_state == CANP_ST_IDLE;
    c.button({18, 162, 105, 46}, "正转", st_.jog > 0, can_jog ? A_BELT_JOG : 0, st_.jog > 0 ? 0 : 1, 21);
    c.button({131, 162, 105, 46}, "反转", st_.jog < 0, can_jog ? A_BELT_JOG : 0, st_.jog < 0 ? 0 : -1, 21);
    c.text("点动（停止态），再点停", 252, 177, 15, DIM);
    if (st_.run_state >= CANP_ST_ESTOP)   // 急停 / 故障: the reset is required before 启动 is accepted again
        c.button({18, 214, 444, 36}, "复位报警", true, run ? A_CMD : 0, CANP_CMD_RESET_ALARM, 20);
    else if (!run)
        c.text("启停按钮只在产线模式可用", 18, 218, 15, DIM);
}
// 运维: work orders of the health monitor (app/ops.hpp) — the list (newest first), the selected one with its advice,
// acknowledge one / all (the remote station does the same over Modbus). Read from the thread-safe registry at render.
void Hmi::page_ops(Canvas &c)
{
    c.text("运维工单", 175, 6, 22, TEXT);
    if (!st_.ops) {
        c.text("故障检测没有运行（只在 des_app live / run 里有）", 175, 50, 18, DIM);
        return;
    }
    const WorkOrders::Summary s = st_.ops->summary();
    c.text(fmt("活动 %d（故障 %d · 警告 %d）", s.active, s.faults, s.warnings) + fmt(" · 待确认 %d", s.unacked) +
               fmt(" · 远程运维站 %d 个连接", st_.ops_clients),
           300, 11, 16, s.faults ? RED : s.unacked ? ORANGE : DIM);
    const std::vector<WorkOrder> v = st_.ops->list(WorkOrders::kKeep);
    const int n = int(v.size()), rows = 8;
    ops_top_ = std::clamp(ops_top_, 0, std::max(0, n - rows));
    const int cx[] = {175, 232, 330, 386, 560, 1008, 1058};
    const char *hd[] = {"编号", "时间", "级别", "设备", "内容", "次数", "状态"};
    c.header_row(hd, cx, 7, 40);
    const WorkOrder *sel = nullptr;
    for (const WorkOrder &w : v)
        if (w.id == ops_sel_) sel = &w;
    if (!sel && n) sel = &v[0];
    auto level_col = [](const WorkOrder &w) { return w.level == OpsLevel::Fault ? RED : w.level == OpsLevel::Warning ? ORANGE : DIM; };
    auto status_col = [&](const WorkOrder &w) { return w.closed() ? DIM : w.acked ? INFO : w.active ? level_col(w) : VALUE; };
    const int utc = cfg_.ops.utc_offset_h;
    for (int k = 0; k < rows && ops_top_ + k < n; k++) {
        const WorkOrder &w = v[size_t(ops_top_ + k)];
        const int y = 62 + k * 29;
        c.row({165, y - 2, 1140, 28}, sel && w.id == sel->id, A_OPS_SEL, w.id);
        const OpsCodeInfo &i = ops_code_info(w.code);
        const std::string t = ops_wall_str(w.wall_last, utc, false);
        c.text(std::to_string(w.id), cx[0], y, 17, TEXT);
        c.text(t.empty() ? fmt("+%.0f s", w.t_last) : t, cx[1], y, 17, TEXT);
        c.text(ops_level_cn(w.level), cx[2], y, 17, level_col(w));
        c.text(fit(c, i.device, 17, 168), cx[3], y, 17, TEXT);
        c.text(fit(c, std::string(i.title) + "：" + w.detail, 16, 440), cx[4], y, 16, w.closed() ? DIM : TEXT);
        c.text(std::to_string(w.count), cx[5], y, 17, TEXT);
        c.text(w.status_cn(), cx[6], y, 16, status_col(w));
    }
    if (n == 0) c.text("没有工单：设备运行正常", 175, 70, 20, GREEN);
    else if (n > rows) {
        c.text(fmt("第 %d", ops_top_ + 1) + fmt("–%d 条", std::min(n, ops_top_ + rows)) + fmt(" / 共 %d", n), 760, 42, 15,
               DIM);
        c.button({1112, 36, 62, 24}, "上一页", false, ops_top_ > 0 ? A_OPS_SCROLL : 0, -rows, 15);
        c.button({1178, 36, 62, 24}, "下一页", false, ops_top_ + rows < n ? A_OPS_SCROLL : 0, rows, 15);
        c.button({1244, 36, 56, 24}, "最新", ops_top_ == 0, ops_top_ > 0 ? A_OPS_SCROLL : 0, 0, 15);
    }
    // the selected order: what happened, what to do, acknowledge
    cv::line(c.img(), {1318, 10}, {1318, H - 10}, LINE, 1);
    if (sel) {
        const OpsCodeInfo &i = ops_code_info(sel->code);
        int y = 8;
        c.text(fit(c, fmt("#%u ", unsigned(sel->id)) + i.title, 21, 580), 1330, y, 21, level_col(*sel));
        y += 32;
        c.text(std::string(ops_level_cn(sel->level)) + " · " + i.device + " · " + sel->status_cn() +
                   (sel->count > 1 ? fmt(" · 发生 %u 次", sel->count) : std::string()),
               1330, y, 16, status_col(*sel));
        y += 26;
        auto para = [&](const std::string &txt, int px, const cv::Scalar &col, int max_lines) {
            std::string cur;   // wrap at code points to 570 px
            int lines = 0;
            for (size_t k = 0; k < txt.size() && lines < max_lines;) {
                size_t m = k + 1;
                while (m < txt.size() && (static_cast<unsigned char>(txt[m]) & 0xC0) == 0x80) m++;
                const std::string ch = txt.substr(k, m - k);
                if (c.width(cur + ch, px) > 570) {
                    c.text(cur, 1330, y, px, col);
                    y += px + 7;
                    lines++;
                    cur.clear();
                }
                cur += ch;
                k = m;
            }
            if (!cur.empty() && lines < max_lines) { c.text(cur, 1330, y, px, col); y += px + 7; }
        };
        para(sel->detail, 16, TEXT, 2);
        para(std::string("处理建议：") + i.advice, 16, INFO, 3);
        std::string t = ops_wall_str(sel->wall_first, utc);
        t = t.empty() ? fmt("开机后 %.0f s 首次出现", sel->t_first) : "首次 " + t;
        if (sel->acked) t += std::string(" · 已由") + sel->acked_by + "确认";
        c.text(fit(c, t, 15, 570), 1330, std::max(y, 214), 15, DIM);
        c.button({1330, 248, 210, 40}, "确认该工单", false, sel->acked ? 0 : A_OPS_ACK, sel->id, 19);
    }
    c.button({1550, 248, 160, 40}, "全部确认", false, s.unacked ? A_OPS_ACK : 0, 0xFFFF, 19);
    c.button({1720, 248, 180, 40}, "发送测试工单", false, A_OPS_ACK, 0, 17);   // id 0: the worker raises OPS_TEST
}
void Hmi::scroll_records(int rows)
{
    rec_top_ = std::clamp(rec_top_ + rows, 0, std::max(0, int(parts_.size()) - kRows));
}
void Hmi::page_overview(Canvas &c)
{
    const double yield = total_ ? 100.0 * ok_ / total_ : 0;
    const std::string vals[] = {std::to_string(total_), std::to_string(ok_), std::to_string(ng_), std::to_string(recheck_),
                                fmt("%.1f%%", yield), std::to_string(part_times_.size())};
    const char *keys[] = {"总数", "良品", "不良", "复检", "良率", "产量 件/分"};
    const cv::Scalar cols[] = {TEXT, GREEN, RED, ORANGE, TEXT, TEXT};
    for (int i = 0; i < 6; i++) {   // 3 per row; state + belt speed live on the panel strip, last parts on 记录
        const int x = 172 + (i % 3) * 220, y = 14 + (i / 3) * 92;
        c.text(keys[i], x, y, 18, DIM);
        c.text(vals[i], x, y + 26, 40, cols[i]);
    }
    if (const Recipe *rc = find_recipe(cfg_.recipes, p_.recipe))
        c.text("配方 " + std::to_string(rc->id) + " · " + rc->name, 172, 276, 16, DIM);
    const int bx = 980;   // parts per class (bars widen into the room the removed last-parts table left)
    c.text("各类数量", bx, 6, 18, DIM);
    uint64_t mx = 1;
    for (uint64_t v : per_class_) mx = std::max(mx, v);
    for (size_t i = 0; i < per_class_.size() && i < 9; i++) {
        const int y = 32 + int(i) * 29;
        const std::string &cn = cn_of(int(i));
        c.text(cn, bx, y, 17, TEXT);
        cv::rectangle(c.img(), {bx + 175, y + 3, std::max(int(600.0 * per_class_[i] / mx), 1), 17},
                      cn.find("良品") != std::string::npos ? GREEN : i < cfg_.tracker.rules.size() ? RED : DIM, cv::FILLED);
        c.text(std::to_string(per_class_[i]), bx + 790, y, 17, TEXT);
    }
}
void Hmi::page_curves(Canvas &c)
{
    Trend od[80], bore[80];
    const int nod = int(od_.copy(od, 80)), nbo = int(bore_.copy(bore, 80));
    size_chart(c, {165, 6, 570, 288}, "齿轮外径 mm（公差线 = 参数页）", od, nod, 79, p_.gear_od, p_.gear_od_tol);
    size_chart(c, {745, 6, 570, 288}, "齿轮孔径 mm", bore, nbo, 79, p_.gear_bore, p_.gear_bore_tol);
    Sample samples[120];
    timing_chart(c, {1325, 6, 588, 288}, samples, int(samples_.copy(samples, 120)), 119);
}
void Hmi::page_params(Canvas &c)
{
    {   // recipe: changeover only while stopped (same rule as the PLC's CAN RECIPE)
        const int y = 4;
        const Recipe *rc = find_recipe(cfg_.recipes, p_.recipe);
        const bool can = st_.run_state != CANP_ST_RUN && cfg_.recipes.size() > 1;
        c.text("配方", 175, y + 3, 20, TEXT);
        c.button({470, y, 46, 28}, "-", false, can ? A_RECIPE : 0, -1, 26);   // wider than the other rows:
        const std::string name = rc ? std::to_string(rc->id) + " · " + rc->name : "?";   // names run ~250 px
        c.centered(name, 520, 380, y + 4, 18, VALUE);
        c.button({904, y, 46, 28}, "+", false, can ? A_RECIPE : 0, 1, 26);
        c.text(st_.run_state == CANP_ST_RUN ? "运行中不能换型" : "规格一并切换", 962, y + 6, 16, DIM);
    }
    for (int i = 0; i < kNParams; i++) {
        // 9 rows of 28 px under the recipe row inside the 300 px band (31 px pitch cut the last row, 带速 PI, in half)
        const int y = 34 + i * 29;
        const ParamDef &d = kParams[i];
        c.text(d.label, 175, y + 3, 20, TEXT);
        std::string val;
        if (i == 5) val = p_.size_rule ? "开" : "关";
        else if (i == 7) val = kImages[p_.images];
        else if (i == 8) val = p_.belt_pid ? "开" : "关";
        else if (i == 0) val = fmt("%.0f", p_.belt_speed);
        else val = fmt("%.2f", i == 1 ? p_.gear_od : i == 2 ? p_.gear_od_tol : i == 3 ? p_.gear_bore
                                     : i == 4 ? p_.gear_bore_tol : p_.min_conf);
        if (i == 5 || i == 7 || i == 8) {   // toggles: the hint right of the button as for the steppers (lost in 3e01d7c)
            c.wide_button(y, val, (i == 5 && p_.size_rule) || (i == 8 && p_.belt_pid),
                          i == 5 ? A_TOGGLE : i == 7 ? A_IMAGES : A_TOGGLE_PID, 20);
            c.text(d.unit, 772, y + 6, 16, DIM);
        } else {
            // the set speed is the idle speed: show what the belt runs at while handling parts next to it
            const std::string u0 = fast_mode_on(cfg_.belt.adapt, cfg_.belt.run_speed)
                                       ? fmt("mm/s 空转（处理零件时 %.0f）", handling_speed(cfg_.belt.adapt, cfg_.belt.run_speed, p_.belt_speed))
                                       : std::string(d.unit);
            c.stepper(y, val, A_DEC, A_INC, i, i == 0 ? u0.c_str() : d.unit);
        }
    }
    // PL image path (des_vision): what the camera stream shows, binary / edge thresholds. Direct-select grid instead
    // of a cycling button (easier for the demo video shot). Run control itself lives on the status panel strip.
    const int x0 = 1180;
    const bool vis = st_.pl_vision;
    c.text("PL 显示", x0, 8, 20, TEXT);
    if (!vis) c.text("比特流无此功能", x0 + 100, 13, 16, DIM);
    for (int k = 0; k < 6; k++)
        c.button({x0 + (k % 3) * 192, 40 + (k / 3) * 50, 180, 40}, kPlModes[k], vis && p_.pl_mode == k,
                 vis ? A_PLMODE : 0, k, 18);
    c.text("二值阈值", x0, 156, 18, TEXT);
    c.button({x0 + 88, 150, 40, 32}, "-", false, vis ? A_PLTHR : 0, -1, 22);
    c.text(std::to_string(p_.pl_thr), x0 + 140, 154, 20, VALUE);
    c.button({x0 + 200, 150, 40, 32}, "+", false, vis ? A_PLTHR : 0, 1, 22);
    c.text("边缘阈值", x0 + 280, 156, 18, TEXT);
    c.button({x0 + 368, 150, 40, 32}, "-", false, vis ? A_PLTHR_EDGE : 0, -1, 22);
    c.text(std::to_string(p_.pl_edge_thr), x0 + 420, 154, 20, VALUE);
    c.button({x0 + 480, 150, 40, 32}, "+", false, vis ? A_PLTHR_EDGE : 0, 1, 22);
    c.text("改动立即生效并保存", x0, 200, 15, DIM);
}
void Hmi::page_records(Canvas &c)
{
    const int cx[] = {172, 262, 342, 512, 592, 692, 1062};
    const char *hd[] = {"时间 秒", "件号", "类别", "置信度", "去向", "原因", "外径"};
    c.header_row(hd, cx, 7, 6);
    const int n = int(parts_.size());
    rec_top_ = std::clamp(rec_top_, 0, std::max(0, n - kRows));
    for (int k = 0; k < kRows && rec_top_ + k < n; k++) {   // rows rec_top_ .. +8, 0 = newest part
        const int i = rec_top_ + k;
        const PartRecord &r = part(size_t(i)).r;
        const int y = 30 + k * 29;
        c.row({165, y - 2, 1140, 28}, i == selected_, A_SELECT, i);
        c.text(fmt("%.1f", r.t_s), cx[0], y, 18, TEXT);
        c.text(std::to_string(r.track_id), cx[1], y, 18, TEXT);
        c.text(cn_of(r.cls), cx[2], y, 18, TEXT);
        c.text(r.cls >= 0 ? fmt("%.2f", r.conf) : "—", cx[3], y, 18, TEXT);
        c.text(cn_disp(r.disp), cx[4], y, 18, disp_color(r.disp));
        c.text(fit(c, why_cn(r.why, r.cls >= 0 ? cn_of(r.cls) : ""), 16, 360), cx[5], y, 16, TEXT);
        c.text(r.meas.valid ? fmt("%.3f", r.meas.size) : "—", cx[6], y, 18, TEXT);
    }
    if (n == 0) {
        c.text("还没有判定过的零件", 172, 40, 20, DIM);
    } else {   // where the view is; wheel / PgUp PgDn / these buttons scroll
        c.text(fmt("第 %d", rec_top_ + 1) + fmt("–%d 件", std::min(n, rec_top_ + kRows)) + fmt(" / 共 %d", n) +
                   (n >= kRecords ? "（只留最近 500 件）" : ""),
               760, 7, 15, DIM);
        c.button({1112, 2, 62, 24}, "上一页", false, rec_top_ > 0 ? A_REC_SCROLL : 0, -kRows, 15);
        c.button({1178, 2, 62, 24}, "下一页", false, rec_top_ + kRows < n ? A_REC_SCROLL : 0, kRows, 15);
        c.button({1244, 2, 56, 24}, "最新", rec_top_ == 0, rec_top_ > 0 ? A_REC_SCROLL : 0, 0, 15);
    }
    // selected part: crop + details
    if (selected_ < 0 || selected_ >= n) return;
    refresh_pic();
    const cv::Rect pic(1320, 20, 260, 260);
    if (!sel_pic_.empty()) {
        sel_pic_.copyTo(c.img()(pic));
    } else {
        cv::rectangle(c.img(), pic, PANEL, cv::FILLED);
        c.centered(selected_ >= kCrops ? fmt("无图（只留最近 %d 件）", kCrops) : std::string("无图"), pic.x, pic.width,
                   pic.y + 115, 18, DIM);
    }
    const PartRecord &r = part(size_t(selected_)).r;
    const PartMeasure &m = r.meas;
    int y = 22;
    auto line = [&](const std::string &s, const cv::Scalar &col) {
        c.text(s, 1600, y, 18, col);
        y += 30;
    };
    line("件号 " + std::to_string(r.track_id) + "   " + cn_of(r.cls), TEXT);
    line(std::string(cn_disp(r.disp)) + " → " + dest_cn(r.dest), disp_color(r.disp));
    line(fit(c, why_cn(r.why, r.cls >= 0 ? cn_of(r.cls) : ""), 18, 310), DIM);
    if (!m.valid) {
        line("未测量", DIM);
    } else {
        line(std::string("外径 ") + fmt("%.3f mm", m.size), TEXT);
        line(fmt("孔径 %.3f", m.bore) + fmt("  偏心 %.3f", m.ecc), TEXT);
        line(m.teeth_judged ? fmt("齿数 %.0f", double(m.teeth)) + fmt("  缺料 %.0f", double(m.short_teeth)) +
                                  fmt("  缺齿 %.0f", double(m.missing_teeth)) + fmt("  飞边 %.0f", double(m.flash_gaps))
                            : std::string("齿未判（离光轴过远）"),
             m.teeth_judged ? TEXT : ORANGE);
        line(fmt("测于离光轴 %.0f mm", m.axis_dist), DIM);
        if (m.sat >= 0) {
            const auto &cl = cfg_.measure.colors;
            const std::string cn = m.color >= 0 && size_t(m.color) < cl.size() ? cl[m.color].name : std::string("未归类");
            line("颜色 " + cn + fmt("（色调 %.0f°", m.hue) + fmt(" 饱和度 %.0f）", m.sat), TEXT);
        }
    }
    if (r.pl_area > 0)
        line(fmt("PL 粗测 等效Ø %.1f", r.pl_d_mm) + fmt("  框 %.1f", r.pl_w_mm) + fmt("×%.1f mm", r.pl_h_mm), INFO);
}
void Hmi::refresh_pic()
{
    if (sel_pic_for_ == selected_) return;
    sel_pic_for_ = selected_;
    sel_pic_.release();
    if (selected_ < 0 || size_t(selected_) >= parts_.size()) return;
    const cv::Mat &crop = part(size_t(selected_)).crop;
    if (!crop.empty()) cv::resize(crop, sel_pic_, cv::Size(260, 260), 0, 0, cv::INTER_AREA);
}
// 机械臂: manual jogging (app/arm_io.hpp); commands go through the manual arm thread, which refuses while the line runs
// or the production arm thread is busy.
void Hmi::page_arm(Canvas &c)
{
    const ArmManualView &a = st_.arm;
    const ArmConfig &ac = cfg_.arm;
    c.text("机械臂手动调试", 175, 6, 22, TEXT);
    std::string state;
    cv::Scalar sc;
    if (!a.allowed) { state = "不可用：产线运行中或机械臂正在抓取（先停止产线）"; sc = RED; }
    else if (a.unlocked) { state = "已解锁 · 点 -/+ 微调；动作前确认机械臂周围无障碍"; sc = GREEN; }
    else { state = "未解锁：点右侧「解锁」后才能动"; sc = ORANGE; }
    c.text(state, 380, 9, 18, sc);
    // 角度 = read back, 目标 = last command (dead band up to ~1 deg), 初始 = ready pose. No current / temperature
    // columns: these servos do not report them (0x2E follows the supply voltage, 0x3F = 255; 10-26)
    const int cx[] = {175, 305, 390, 490, 590, 700};
    const char *head[] = {"关节", "位置", "角度", "目标", "初始", "状态"};
    c.header_row(head, cx, 6, 40);
    c.text("微调", 930, 40, 16, DIM);
    const bool can = a.allowed && a.unlocked;
    for (int k = 0; k < 6; k++) {
        const int y = 62 + k * 33;
        const ArmJointView &j = a.j[k];
        const JointCal &jc = ac.joint[k];
        auto deg = [&](int n) { return n < 0 ? std::string("—") : fmt("%+.1f°", (n - jc.zero_count) / jc.counts_per_deg); };
        const std::string flags = !j.alive ? (j.pos < 0 && !a.shown ? "—" : "无应答")
                : j.status & 0x20 ? "过载" : j.status & 0x04 ? "过热" : j.status & 0x01 ? "电压" : "正常";
        c.text(kArmJointName[k], cx[0], y + 4, 19, TEXT);
        c.text(j.pos < 0 ? "—" : fmt("%.0f", double(j.pos)), cx[1], y + 4, 19, VALUE);
        c.text(deg(j.pos), cx[2], y + 4, 19, VALUE);
        c.text(deg(j.target), cx[3], y + 4, 19, INFO);
        c.text(deg(a.home[k]), cx[4], y + 4, 19, a.home_taught ? TEXT : DIM);
        c.text(flags, cx[5], y + 4, 18, flags == "正常" ? GREEN : flags == "—" ? DIM : RED);
        c.button({930, y, 64, 28}, "-", false, can ? A_ARM_JOG : 0, k * 2, 26);
        c.button({1002, y, 64, 28}, "+", false, can ? A_ARM_JOG : 0, k * 2 + 1, 26);
    }
    // right: controls
    const int x0 = 1110;
    cv::line(c.img(), {x0 - 18, 14}, {x0 - 18, H - 14}, LINE, 1);
    c.button({x0, 36, 150, 40}, a.unlocked ? "上锁" : "解锁", a.unlocked, a.allowed ? A_ARM_LOCK : 0,
             a.unlocked ? 0 : 1, 21);
    c.button({x0 + 170, 36, 130, 40}, "扭矩 开", a.torque == 1, can ? A_ARM_TORQUE : 0, 1, 20);
    c.button({x0 + 310, 36, 130, 40}, "扭矩 关", a.torque == 0, can ? A_ARM_TORQUE : 0, 0, 20);
    c.text("关扭矩前先托住机械臂", x0 + 460, 47, 16, ORANGE);
    c.text("步长", x0, 99, 19, TEXT);
    for (int i = 0; i < 3; i++)
        c.button({x0 + 70 + i * 96, 90, 86, 36}, fmt("%.0f°", kArmStepDeg[i]), arm_step_ == i, A_ARM_STEP, i, 20);
    c.text("动作时间", x0 + 380, 99, 19, TEXT);
    for (int i = 0; i < 3; i++)
        c.button({x0 + 470 + i * 96, 90, 86, 36}, fmt("%.1f s", kArmMs[i] / 1000.0), arm_ms_ == i, A_ARM_MS, i, 20);
    c.button({x0, 142, 230, 40}, "复位（回初始姿态）", false, can && a.home_taught ? A_ARM_RESET : 0, 0, 20);
    c.button({x0 + 246, 142, 190, 40}, "设为初始姿态", false, can ? A_ARM_SETHOME : 0, 0, 20);
    c.button({x0 + 452, 142, 190, 40}, "记录当前姿态", false, a.allowed ? A_ARM_TEACH : 0, 0, 20);
    c.text(a.home_taught ? "初始姿态：已设定（arm_home.json）；复位时 6 个关节一起慢速动作，至少 2 s"
                         : "初始姿态：未设定 —— 先把机械臂调到合适位置，点「设为初始姿态」，复位才可用",
           x0, 196, 16, a.home_taught ? DIM : ORANGE);
    c.text(a.taught ? "「记录当前姿态」本次已记 " + std::to_string(a.taught) + " 个 → data/arm_teach.csv"
                    : std::string("「记录当前姿态」存到 data/arm_teach.csv（标定零位 / 料盒姿态用）"),
           x0, 220, 16, DIM);
    c.text(a.msg[0] ? a.msg : "—", x0, 252, 18, INFO);
}
// 挡板: the two flap servos by hand (app/flap_manual.hpp) — to check a servo and find its positions. Same rule as the
// arm page: only with the line stopped, and nothing moves before 解锁.
void Hmi::page_flap(Canvas &c)
{
    const FlapManualView &f = st_.flap;
    c.text("挡板舵机调试", 175, 6, 22, TEXT);
    std::string state;
    cv::Scalar sc;
    if (!f.hw) { state = "比特流里没有挡板 PWM 模块（v7 起才有）"; sc = RED; }
    else if (!f.allowed) { state = std::string("不可用：") + f.why_not + "（先停止产线）"; sc = RED; }
    else if (f.unlocked) { state = "已解锁 · 动作前确认挡板周围没有手和零件"; sc = GREEN; }
    else { state = "未解锁：点右侧「解锁」后才能动"; sc = ORANGE; }
    c.text(state, 380, 9, 18, sc);
    const bool can = f.hw && f.allowed && f.unlocked;
    const int cx[] = {175, 360, 470, 640, 790, 900};
    const char *head[] = {"挡板", "信号", "输出", "脉宽", "约角度", "调整脉宽（中位 = 1500µs，加大 / 减小各转向一边）"};
    c.header_row(head, cx, 6, 40);
    for (int ch = 0; ch < 2; ch++) {
        const int y = 66 + ch * 78;
        c.text(kFlapName[ch], cx[0], y + 2, 20, TEXT);
        if (size_t(ch) < cfg_.picker.gates.size()) {   // configured positions (des_app.json flaps.gates), for reference
            const Gate &g = cfg_.picker.gates[size_t(ch)];
            c.text(fmt("配置 开 %d", g.open_us) + fmt(" / 合 %d µs", g.closed_us), cx[0], y + 30, 14, DIM);
        }
        c.text(kFlapPin[ch], cx[1], y + 8, 19, DIM);
        c.button({cx[2], y, 70, 40}, "开", f.on[ch], can ? A_FLAP_OUT : 0, ch * 2 + 1, 20);
        c.button({cx[2] + 78, y, 70, 40}, "关", !f.on[ch], can ? A_FLAP_OUT : 0, ch * 2, 20);
        const bool known = f.us[ch] >= kFlapMinUs && f.us[ch] <= kFlapMaxUs;
        const cv::Scalar vc = f.on[ch] ? VALUE : DIM;
        c.text(known ? fmt("%d µs", f.us[ch]) : std::string("—"), cx[3], y + 4, 24, vc);
        if (f.target[ch] != f.us[ch]) c.text(fmt("→ %d", f.target[ch]), cx[3], y + 36, 15, INFO);
        c.text(known ? fmt("%+.0f°", (f.us[ch] - kFlapCenterUs) * kFlapDegPerUs) : std::string("—"), cx[4], y + 6, 22, vc);
        const bool move = can && f.on[ch];
        for (int k = 0; k < 4; k++)
            c.button({cx[5] + k * 78, y, 70, 40}, fmt("%+d", kFlapStepUs[k]), false, move ? A_FLAP_STEP : 0, ch * 8 + k, 19);
        c.button({cx[5] + 322, y, 90, 40}, "中位", false, move ? A_FLAP_CENTER : 0, ch, 19);
        c.button({cx[5] + 420, y, 90, 40}, "试转", false, move ? A_FLAP_SWEEP : 0, ch, 19);
    }
    c.text(f.msg[0] ? f.msg : "—", 175, 236, 18, INFO);
    c.text("没有反馈：舵机实际到没到位要看实物；只颤不转 = 供电或机械卡住，量舵机自己插头上的红–棕电压", 175, 266, 15, DIM);
    // right: lock + speed + notes
    const int x0 = 1430;
    cv::line(c.img(), {x0 - 18, 14}, {x0 - 18, H - 14}, LINE, 1);
    c.button({x0, 36, 150, 40}, f.unlocked ? "上锁" : "解锁", f.unlocked, f.hw && f.allowed ? A_FLAP_LOCK : 0,
             f.unlocked ? 0 : 1, 21);
    c.text("速度", x0, 99, 19, TEXT);
    for (int i = 0; i < 3; i++)
        c.button({x0 + 60 + i * 86, 90, 78, 36}, kFlapSpeedName[i], f.speed == i, f.hw ? A_FLAP_SPEED : 0, i, 19);
    c.text("慢 ≈9°/秒，中 ≈27°/秒，快 = 直接跳", x0, 138, 15, DIM);
    c.text("「试转」：往脉宽加大方向转约 13° 再回来", x0, 162, 15, DIM);
    c.text("板子重启后两路输出都是关的；点「开」", x0, 186, 15, DIM);
    c.text("舵机会直接跳到显示的脉宽（没设过 = 中位）", x0, 208, 15, DIM);
    c.text("上锁 / 离开本页：输出保持不变", x0, 232, 15, DIM);
}
}  // namespace des
