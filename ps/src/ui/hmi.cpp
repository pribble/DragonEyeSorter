// hmi.cpp — see hmi.hpp. Pages use the ui_text helpers; visible strings, positions, hit-box arg encodings and colour
// choices are unchanged from the pre-refactor inline code.
#include "hmi.hpp"

#include <linux/input.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

#include <opencv2/imgproc.hpp>

#include "can_proto.h"
#include "json.hpp"
#include "util.hpp"

namespace des {
using namespace ui;
namespace {
enum { A_TAB = 1, A_DEC, A_INC, A_TOGGLE, A_IMAGES, A_CMD, A_SELECT, A_PLMODE, A_PLTHR, A_RECIPE, A_ARM_LOCK,
       A_ARM_TORQUE, A_ARM_JOG, A_ARM_STEP, A_ARM_MS, A_ARM_TEACH, A_ARM_SETHOME, A_ARM_RESET, A_BELT_JOG };
const char *kPages[] = {"总览", "曲线", "参数", "记录", "机械臂"};
constexpr int kNPages = 5;
const double kArmStepDeg[] = {1, 5, 15};
const int kArmMs[] = {500, 1000, 2000};
const char *kArmJoint[6] = {"#1 底座", "#2 肩", "#3 肘", "#4 腕俯仰", "#5 腕旋", "#6 夹爪"};
const char *kImages[] = {"不存", "NG + 复检", "全部"};
const char *kPlModes[] = {"原图", "PL 灰度", "PL 高斯滤波", "PL 二值", "PL Sobel", "PL 边缘"};
struct ParamDef {
    const char *label, *unit;
    double lo, hi, step;
};
const ParamDef kParams[] = {
    {"带速", "mm/s（run 模式生效）", 5, 200, 5},        {"齿轮外径 名义", "mm", 20, 60, 0.01},
    {"齿轮外径 公差 ±", "mm", 0.01, 1, 0.01},          {"齿轮孔径 名义", "mm", 2, 20, 0.05},
    {"齿轮孔径 公差 ±", "mm", 0.05, 2, 0.05},          {"尺寸判废", "超差 / 规格混料 → 报废出口", 0, 1, 1},
    {"置信度门限", "低于它 → 复检", 0.3, 0.95, 0.05},   {"存图", "零件裁切图存 SD 卡", 0, 2, 1}};
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
}  // namespace
Hmi::Hmi(const AppConfig &cfg, const std::string &params_path) : cfg_(cfg), params_path_(params_path)
{
    txt_.open();
    p_.belt_speed = cfg.belt.run_speed;
    p_.size_rule = cfg.tracker.size.enabled;
    p_.gear_od = cfg.tracker.size.gear_od;  p_.gear_od_tol = cfg.tracker.size.gear_od_tol;
    p_.gear_bore = cfg.tracker.size.gear_bore;  p_.gear_bore_tol = cfg.tracker.size.gear_bore_tol;
    p_.min_conf = cfg.tracker.min_conf;
    p_.images = uint8_t(cfg.storage.images == "none" ? 0 : cfg.storage.images == "all" ? 2 : 1);
    p_.recipe = cfg.recipe;
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
        p_.belt_speed = j.value("belt_speed", p_.belt_speed);
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
    nlohmann::json j = {{"belt_speed", p_.belt_speed}, {"size_rule", p_.size_rule}, {"gear_od", p_.gear_od},
                        {"gear_od_tol", p_.gear_od_tol}, {"gear_bore", p_.gear_bore}, {"gear_bore_tol", p_.gear_bore_tol},
                        {"min_conf", p_.min_conf}, {"images", p_.images}, {"pl_mode", p_.pl_mode},
                        {"pl_thr", p_.pl_thr}, {"pl_edge_thr", p_.pl_edge_thr}, {"recipe", p_.recipe}};
    std::ofstream f(params_path_);
    if (f) f << j.dump(1) << "\n";
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
    parts_.push({r, crop});
    // 224^2 crops are only needed for the 9 visible rows; older rows selected with the wheel show 无图 (not visible).
    for (size_t i = 0; i + 9 < parts_.size(); i++) parts_[i].crop.release();
    if (selected_ >= 0) selected_ = std::min(selected_ + 1, int(parts_.size()) - 1);   // keep the same part selected
    sel_pic_for_ = -1;
    if (r.meas.valid && r.meas.kind == 'g') {
        od_.push({r.meas.size, r.disp != Disposition::Pick});
        if (r.meas.bore > 0) bore_.push({r.meas.bore, r.disp != Disposition::Pick});
    }
    part_times_.push(now);
    while (!part_times_.empty() && now - part_times_.front() > 60) part_times_.pop_front();
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
        if (e.kind == UiEvent::Key) {   // F1-F5 / Tab / <- -> switch pages
            if (e.value >= KEY_F1 && e.value <= KEY_F5) page_ = e.value - KEY_F1;
            else if (e.value == KEY_TAB || e.value == KEY_RIGHT) page_ = (page_ + 1) % kNPages;
            else if (e.value == KEY_LEFT) page_ = (page_ + kNPages - 1) % kNPages;
            continue;
        }
        if (e.kind == UiEvent::Wheel) {
            if (page_ == 3 && !parts_.empty()) {
                selected_ = std::clamp(selected_ - e.value, 0, int(parts_.size()) - 1);
                sel_pic_for_ = -1;
            }
            continue;
        }
        const cv::Point pt(e.x, e.y - Y0);
        if (pt.y < 0) continue;   // a click on the video
        for (const Hit &h : hits_) {
            if (!h.box.contains(pt)) continue;
            bool changed = false;
            switch (h.action) {
            case A_TAB: page_ = h.arg; break;
            case A_SELECT: selected_ = h.arg; sel_pic_for_ = -1; break;
            case A_CMD: out.push_back({HmiAction::Command, h.arg, {}}); break;
            case A_BELT_JOG: out.push_back({HmiAction::Jog, h.arg, {}}); break;
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
            case A_IMAGES: p_.images = uint8_t((p_.images + 1) % 3); changed = true; break;
            case A_PLMODE: p_.pl_mode = uint8_t((p_.pl_mode + 1) % 6); changed = true; break;
            case A_PLTHR: p_.pl_thr = uint8_t(std::clamp(int(p_.pl_thr) + h.arg * 5, 0, 255)); changed = true; break;
            case A_RECIPE: {
                const auto &rs = cfg_.recipes;
                const int n = int(rs.size());
                int k = 0;
                while (k < n && rs[k].id != p_.recipe) k++;
                if (n > 1) out.push_back({HmiAction::Recipe, rs[((k < n ? k : 0) + h.arg + n) % n].id, {}});
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
                else {
                    float &f = h.arg == 1 ? p_.gear_od : h.arg == 2 ? p_.gear_od_tol
                            : h.arg == 3 ? p_.gear_bore : h.arg == 4 ? p_.gear_bore_tol : p_.min_conf;
                    f = float(step(f));
                }
                changed = true;
                break;
            }
            }
            if (changed) { save_params(); out.push_back({HmiAction::Params, 0, {}}); }
            break;
        }
    }
    return out;
}
cv::Mat Hmi::render()
{
    cv::Mat img(H, W, CV_8UC3, BG);   // a FRESH buffer every call: live.cpp uses .data as its dirty flag
    hits_.clear();
    cv::line(img, {0, 0}, {W - 1, 0}, ACCENT, 2);
    Canvas c(img, txt_, hits_);
    c.text("DragonEyeSorter", 12, 8, 17, DIM);   // navigation + status
    for (int i = 0; i < kNPages; i++) c.button({8, 30 + i * 41, 136, 37}, kPages[i], page_ == i, A_TAB, i, 21);
    c.text(st_.npu ? "NPU 在线" : "NPU 未加载", 12, 240, 16, st_.npu ? GREEN : ORANGE);
    c.text(std::string("输入设备 ") + std::to_string(st_.inputs) + (st_.clock_synced ? "  已对时" : "  未对时"), 12,
           264, 15, DIM);
    cv::line(img, {152, 10}, {152, H - 10}, LINE, 1);
    switch (page_) {
    case 0: page_overview(c); break;
    case 1: page_curves(c); break;
    case 2: page_params(c); break;
    case 3: page_records(c); break;
    default: page_arm(c); break;
    }
    return img;
}
void Hmi::page_overview(Canvas &c)
{
    const double yield = total_ ? 100.0 * ok_ / total_ : 0;
    const std::string vals[] = {std::to_string(total_), std::to_string(ok_), std::to_string(ng_), std::to_string(recheck_),
                                fmt("%.1f%%", yield), std::to_string(part_times_.size())};
    const char *keys[] = {"总数", "良品", "NG", "复检", "良率", "产量 件/分"};
    const cv::Scalar cols[] = {WHITE, GREEN, RED, ORANGE, WHITE, WHITE};
    for (int i = 0; i < 6; i++) {
        const int x = 172 + (i % 2) * 200, y = 14 + (i / 2) * 92;
        c.text(keys[i], x, y, 18, DIM);
        c.text(vals[i], x, y + 26, 40, cols[i]);
    }
    if (const Recipe *rc = find_recipe(cfg_.recipes, p_.recipe))
        c.text("配方 " + std::to_string(rc->id) + " · " + rc->name, 172, 276, 16, DIM);
    const int bx = 600;   // parts per class
    c.text("各类数量", bx, 6, 18, DIM);
    uint64_t mx = 1;
    for (uint64_t v : per_class_) mx = std::max(mx, v);
    for (size_t i = 0; i < per_class_.size() && i < 9; i++) {
        const int y = 32 + int(i) * 29;
        const std::string &cn = cn_of(int(i));
        c.text(cn, bx, y, 17, WHITE);
        cv::rectangle(c.img(), {bx + 175, y + 3, std::max(int(330.0 * per_class_[i] / mx), 1), 17},
                      cn.find("良品") != std::string::npos ? GREEN : i < cfg_.tracker.rules.size() ? RED : DIM, cv::FILLED);
        c.text(std::to_string(per_class_[i]), bx + 515, y, 17, WHITE);
    }
    const int tx = 1180;   // last parts
    c.text("最近零件", tx, 6, 18, DIM);
    const int sx = W - 260;
    cv::rectangle(c.img(), {sx, 4, 250, 30}, state_color(st_.run_state), cv::FILLED);
    c.text(std::string(state_name(st_.run_state)) +
               (st_.enc_live ? fmt("  带速 %.0f", st_.belt_mm_s) + fmt(" 实测 %.0f", st_.belt_enc_mm_s)
                             : fmt("  带速 %.0f mm/s", st_.belt_mm_s)),
           sx + 10, 8, 18, WHITE);
    const int cx[] = {tx, tx + 80, tx + 260, tx + 380, tx + 520, tx + 620};
    const char *hd[] = {"件号", "类别", "去向", "出口 / 料盒", "尺寸", "置信度"};
    c.header_row(hd, cx, 6, 42);
    for (size_t i = 0; i < parts_.size() && i < 7; i++) {
        const PartRecord &r = part(i).r;
        const int y = 68 + int(i) * 32;
        c.text(std::to_string(r.track_id), cx[0], y, 19, WHITE);
        c.text(cn_of(r.cls), cx[1], y, 19, WHITE);
        c.text(cn_disp(r.disp), cx[2], y, 19, disp_color(r.disp));
        c.text(r.dest, cx[3], y, 17, WHITE);
        c.text(r.meas.valid ? fmt("%.2f", r.meas.size) : "—", cx[4], y, 19, WHITE);
        c.text(r.cls >= 0 ? fmt("%.2f", r.conf) : "—", cx[5], y, 19, WHITE);
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
        const int y = 6;
        const Recipe *rc = find_recipe(cfg_.recipes, p_.recipe);
        const bool can = st_.run_state != CANP_ST_RUN && cfg_.recipes.size() > 1;
        c.text("配方", 175, y + 3, 20, WHITE);
        c.button({470, y, 46, 28}, "-", false, can ? A_RECIPE : 0, -1, 26);   // wider than the other rows:
        const std::string name = rc ? std::to_string(rc->id) + " · " + rc->name : "?";   // names run ~250 px
        c.centered(name, 520, 380, y + 4, 18, YELLOW);
        c.button({904, y, 46, 28}, "+", false, can ? A_RECIPE : 0, 1, 26);
        c.text(st_.run_state == CANP_ST_RUN ? "运行中不能换型" : "规格一并切换", 962, y + 6, 16, DIM);
    }
    for (int i = 0; i < kNParams; i++) {
        const int y = 37 + i * 31;
        const ParamDef &d = kParams[i];
        c.text(d.label, 175, y + 3, 20, WHITE);
        std::string val;
        if (i == 5) val = p_.size_rule ? "开" : "关";
        else if (i == 7) val = kImages[p_.images];
        else if (i == 0) val = fmt("%.0f", p_.belt_speed);
        else val = fmt("%.2f", i == 1 ? p_.gear_od : i == 2 ? p_.gear_od_tol : i == 3 ? p_.gear_bore
                                     : i == 4 ? p_.gear_bore_tol : p_.min_conf);
        if (i == 5 || i == 7) {   // toggles: the hint right of the button as for the steppers (lost in 3e01d7c)
            c.wide_button(y, val, i == 5 && p_.size_rule, i == 5 ? A_TOGGLE : A_IMAGES, 20);
            c.text(d.unit, 772, y + 6, 16, DIM);
        } else {
            c.stepper(y, val, A_DEC, A_INC, i, d.unit);
        }
    }
    const int x0 = 1180;   // run control: same rules as the PLC's CAN commands
    c.text("运行控制", x0, 8, 20, WHITE);
    const bool run = st_.run_state >= 0;
    const char *bt[] = {"启动", "停止", "暂停", "恢复", "复位报警"};
    const int bc[] = {CANP_CMD_START, CANP_CMD_STOP, CANP_CMD_PAUSE, CANP_CMD_RESUME, CANP_CMD_RESET_ALARM};
    for (int k = 0; k < 5; k++)
        c.button({x0 + (k % 3) * 160, 42 + (k / 3) * 66, 148, 56}, bt[k], false, run ? A_CMD : 0, bc[k], 24);
    // 6th cell: manual belt jog, only while the line is stopped; click the lit button again (or 停止) to stop
    const bool can_jog = run && st_.run_state == CANP_ST_IDLE;
    c.button({x0 + 320, 108, 72, 56}, "正转", st_.jog > 0, can_jog ? A_BELT_JOG : 0, st_.jog > 0 ? 0 : 1, 22);
    c.button({x0 + 396, 108, 72, 56}, "反转", st_.jog < 0, can_jog ? A_BELT_JOG : 0, st_.jog < 0 ? 0 : -1, 22);
    cv::rectangle(c.img(), {x0, 182, 470, 34}, state_color(st_.run_state), cv::FILLED);
    if (st_.run_state == CANP_ST_ESTOP)
        c.text("急停：先旋开急停按钮，再点复位报警、启动", x0 + 10, 189, 18, WHITE);
    else
        c.text(std::string("状态 ") + state_name(st_.run_state) +
                   (st_.jog > 0 ? "（正转点动）" : st_.jog < 0 ? "（反转点动）" : "") +
                   (st_.enc_live ? fmt("  带速 %.1f", st_.belt_mm_s) + fmt(" / 实测 %.1f mm/s", st_.belt_enc_mm_s)
                                 : fmt("  带速 %.1f mm/s", st_.belt_mm_s)),
               x0 + 10, 187, 20, WHITE);
    // PL image path (des_vision): what the camera stream shows, binary threshold
    c.text("PL 显示", x0, 232, 20, WHITE);
    c.button({x0 + 90, 226, 200, 34}, st_.pl_vision ? kPlModes[p_.pl_mode] : "比特流无此功能", p_.pl_mode != 0,
             st_.pl_vision ? A_PLMODE : 0, 0, 20);
    c.text("二值阈值", x0 + 310, 232, 18, WHITE);
    c.button({x0 + 400, 226, 40, 34}, "-", false, st_.pl_vision ? A_PLTHR : 0, -1, 24);
    c.text(std::to_string(p_.pl_thr), x0 + 452, 230, 22, YELLOW);
    c.button({x0 + 500, 226, 40, 34}, "+", false, st_.pl_vision ? A_PLTHR : 0, 1, 24);
    c.text(std::string(run ? "改动即时生效并存 ui_params.json" : "启停按钮只在 run 模式可用；参数照常保存") +
               fmt("   已记录 %.0f 件", double(st_.logged)),
           x0, 270, 15, DIM);
}
void Hmi::page_records(Canvas &c)
{
    const int cx[] = {172, 262, 342, 512, 592, 692, 1062};
    const char *hd[] = {"时间 s", "件号", "类别", "置信度", "去向", "原因", "尺寸"};
    c.header_row(hd, cx, 7, 6);
    for (size_t i = 0; i < parts_.size() && i < 9; i++) {
        const PartRecord &r = part(i).r;
        const int y = 30 + int(i) * 29;
        c.row({165, y - 2, 1140, 28}, int(i) == selected_, A_SELECT, int(i));
        c.text(fmt("%.1f", r.t_s), cx[0], y, 18, WHITE);
        c.text(std::to_string(r.track_id), cx[1], y, 18, WHITE);
        c.text(cn_of(r.cls), cx[2], y, 18, WHITE);
        c.text(r.cls >= 0 ? fmt("%.2f", r.conf) : "—", cx[3], y, 18, WHITE);
        c.text(cn_disp(r.disp), cx[4], y, 18, disp_color(r.disp));
        c.text(std::string(r.why).substr(0, 42), cx[5], y, 16, WHITE);
        c.text(r.meas.valid ? fmt("%.3f", r.meas.size) : "—", cx[6], y, 18, WHITE);
    }
    if (parts_.empty()) c.text("还没有判定过的零件", 172, 40, 20, DIM);
    // selected part: crop + details
    if (selected_ < 0 || selected_ >= int(parts_.size())) return;
    refresh_pic();
    const cv::Rect pic(1320, 20, 260, 260);
    if (!sel_pic_.empty()) {
        sel_pic_.copyTo(c.img()(pic));
    } else {
        cv::rectangle(c.img(), pic, PANEL, cv::FILLED);
        c.text("无图", pic.x + 100, pic.y + 115, 20, DIM);
    }
    const PartRecord &r = part(size_t(selected_)).r;
    const PartMeasure &m = r.meas;
    int y = 22;
    auto line = [&](const std::string &s, const cv::Scalar &col) {
        c.text(s, 1600, y, 18, col);
        y += 30;
    };
    line("件号 " + std::to_string(r.track_id) + "   " + cn_of(r.cls), WHITE);
    line(std::string(cn_disp(r.disp)) + " → " + r.dest, disp_color(r.disp));
    line(std::string(r.why).substr(0, 26), DIM);
    if (!m.valid) {
        line("未测量", DIM);
    } else {
        line(std::string(m.kind == 'g' ? "外径 " : "长度 ") + fmt("%.3f mm", m.size), WHITE);
        if (m.kind == 'g') line(fmt("孔径 %.3f", m.bore) + fmt("  偏心 %.3f", m.ecc), WHITE);
        else line(fmt("齿距 %.4f mm", m.pitch), WHITE);
        line(m.teeth_judged ? fmt("齿数 %.0f", double(m.teeth)) + fmt("  缺料 %.0f", double(m.short_teeth)) +
                                  fmt("  缺齿 %.0f", double(m.missing_teeth)) + fmt("  飞边 %.0f", double(m.flash_gaps))
                            : std::string("齿未判（离光轴过远）"),
             m.teeth_judged ? WHITE : ORANGE);
        line(fmt("测于离光轴 %.0f mm", m.axis_dist), DIM);
        if (m.sat >= 0) {
            const auto &cl = cfg_.measure.colors;
            const std::string cn = m.color >= 0 && size_t(m.color) < cl.size() ? cl[m.color].name : std::string("未归类");
            line("颜色 " + cn + fmt("（色调 %.0f°", m.hue) + fmt(" 饱和度 %.0f）", m.sat), WHITE);
        }
    }
    if (r.pl_area > 0)
        line(fmt("PL 粗测 等效Ø %.1f", r.pl_d_mm) + fmt("  框 %.1f", r.pl_w_mm) + fmt("×%.1f mm", r.pl_h_mm), CYAN);
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
    c.text("机械臂手动调试", 175, 6, 22, WHITE);
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
        c.text(kArmJoint[k], cx[0], y + 4, 19, WHITE);
        c.text(j.pos < 0 ? "—" : fmt("%.0f", double(j.pos)), cx[1], y + 4, 19, YELLOW);
        c.text(deg(j.pos), cx[2], y + 4, 19, YELLOW);
        c.text(deg(j.target), cx[3], y + 4, 19, CYAN);
        c.text(deg(a.home[k]), cx[4], y + 4, 19, a.home_taught ? WHITE : DIM);
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
    c.text("步长", x0, 99, 19, WHITE);
    for (int i = 0; i < 3; i++)
        c.button({x0 + 70 + i * 96, 90, 86, 36}, fmt("%.0f°", kArmStepDeg[i]), arm_step_ == i, A_ARM_STEP, i, 20);
    c.text("动作时间", x0 + 380, 99, 19, WHITE);
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
    c.text(a.msg[0] ? a.msg : "—", x0, 252, 18, CYAN);
}
}  // namespace des
