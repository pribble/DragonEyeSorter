// hmi.cpp — see hmi.hpp
#include "hmi.hpp"

#include <linux/input.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

#include <opencv2/imgproc.hpp>

#include "can_proto.h"
#include "json.hpp"

namespace des {
namespace {

enum { A_TAB = 1, A_DEC, A_INC, A_TOGGLE, A_IMAGES, A_CMD, A_SELECT };

const cv::Scalar BG(34, 31, 29), PANEL(52, 48, 45), LINE(80, 76, 72), WHITE(235, 235, 235), DIM(150, 150, 150),
    GREEN(90, 200, 90), RED(80, 80, 235), ORANGE(40, 165, 255), CYAN(220, 200, 70), YELLOW(70, 215, 235),
    ACCENT(225, 150, 60);

const char *kPages[] = {"总览", "曲线", "参数", "记录"};
const char *kImages[] = {"不存", "NG + 复检", "全部"};

struct ParamDef {
    const char *label, *unit;
    double lo, hi, step;
};
const ParamDef kParams[] = {
    {"带速", "mm/s（run 模式生效）", 5, 200, 5},
    {"齿轮外径 名义", "mm", 20, 60, 0.01},
    {"齿轮外径 公差 ±", "mm", 0.01, 1, 0.01},
    {"齿条长度 名义", "mm", 20, 120, 0.1},
    {"齿条长度 公差 ±", "mm", 0.05, 5, 0.05},
    {"尺寸判废", "超差 / 规格混料 → 报废出口", 0, 1, 1},
    {"置信度门限", "低于它 → 复检", 0.3, 0.95, 0.05},
    {"存图", "零件裁切图存 SD 卡", 0, 2, 1},
};
constexpr int kNParams = sizeof kParams / sizeof kParams[0];

std::string fmt(const char *f, double v)
{
    char b[64];
    snprintf(b, sizeof b, f, v);
    return b;
}

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
    p_.gear_od = cfg.tracker.size.gear_od;
    p_.gear_od_tol = cfg.tracker.size.gear_od_tol;
    p_.rack_len = cfg.tracker.size.rack_len;
    p_.rack_len_tol = cfg.tracker.size.rack_len_tol;
    p_.min_conf = cfg.tracker.min_conf;
    p_.images = cfg.storage.images == "none" ? 0 : cfg.storage.images == "all" ? 2 : 1;
    load_params();   // the operator's last settings win over des_app.json
    // Chinese class names from the label list (single source)
    std::string ll = cfg.label_list;
    if (!ll.empty() && ll[0] != '/') ll = cfg.config_dir + "/" + ll;
    std::ifstream f(ll);
    if (f) {
        try {
            nlohmann::json j;
            f >> j;
            for (const auto &l : j.at("labels")) {
                size_t id = l.at("id").get<size_t>();
                if (class_cn_.size() <= id) class_cn_.resize(id + 1);
                class_cn_[id] = l.value("cn", l.value("label", ""));
            }
        } catch (const std::exception &) {
        }
    }
    for (size_t i = 0; i < cfg.tracker.rules.size(); i++)
        if (class_cn_.size() <= i) class_cn_.push_back(cfg.tracker.rules[i].label);
    per_class_.assign(class_cn_.size() + 1, 0);
}

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
        p_.rack_len = j.value("rack_len", p_.rack_len);
        p_.rack_len_tol = j.value("rack_len_tol", p_.rack_len_tol);
        p_.min_conf = j.value("min_conf", p_.min_conf);
        p_.images = std::clamp(j.value("images", p_.images), 0, 2);
    } catch (const std::exception &) {
    }
}

void Hmi::save_params() const
{
    nlohmann::json j = {{"belt_speed", p_.belt_speed}, {"size_rule", p_.size_rule}, {"gear_od", p_.gear_od},
                        {"gear_od_tol", p_.gear_od_tol}, {"rack_len", p_.rack_len}, {"rack_len_tol", p_.rack_len_tol},
                        {"min_conf", p_.min_conf}, {"images", p_.images}};
    std::ofstream f(params_path_);
    if (f) f << j.dump(1) << "\n";
}

void Hmi::add_part(const PartRecord &r, const cv::Mat &crop, double now)
{
    total_++;
    if (r.disp == "pick") ok_++;
    else if (r.disp == "flap") ng_++;
    else recheck_++;
    size_t c = r.cls >= 0 && size_t(r.cls) < class_cn_.size() ? size_t(r.cls) : class_cn_.size();
    per_class_[c]++;
    parts_.push_front({r, crop, now});
    if (parts_.size() > 40) parts_.pop_back();
    if (selected_ >= 0) selected_ = std::min(selected_ + 1, int(parts_.size()) - 1);   // keep the same part selected
    if (r.meas.valid) {
        auto &q = r.meas.kind == 'g' ? od_ : len_;
        q.push_back({r.meas.size, r.disp != "pick"});
        if (q.size() > 80) q.pop_front();
    }
    part_times_.push_back(now);
    while (!part_times_.empty() && now - part_times_.front() > 60) part_times_.pop_front();
}

void Hmi::status(const HmiStatus &s, double now)
{
    st_ = s;
    if (now - last_sample_ >= 0.5) {
        last_sample_ = now;
        samples_.push_back({s.worker_ms, s.npu_ms, float(s.belt_mm_s)});
        if (samples_.size() > 120) samples_.pop_front();
    }
    while (!part_times_.empty() && now - part_times_.front() > 60) part_times_.pop_front();
}

std::vector<HmiAction> Hmi::handle(const std::vector<UiEvent> &ev)
{
    std::vector<HmiAction> out;
    for (const UiEvent &e : ev) {
        if (e.kind == UiEvent::Key) {
            if (e.value >= KEY_F1 && e.value <= KEY_F4) page_ = e.value - KEY_F1;
            else if (e.value == KEY_TAB || e.value == KEY_RIGHT) page_ = (page_ + 1) % 4;
            else if (e.value == KEY_LEFT) page_ = (page_ + 3) % 4;
            continue;
        }
        if (e.kind == UiEvent::Wheel) {
            if (page_ == 3 && !parts_.empty())
                selected_ = std::clamp(selected_ - e.value, 0, int(parts_.size()) - 1);
            continue;
        }
        const cv::Point pt(e.x, e.y - Y0);
        if (pt.y < 0) continue;   // a click on the video
        for (const Hit &h : hits_) {
            if (!h.box.contains(pt)) continue;
            bool changed = false;
            switch (h.action) {
            case A_TAB: page_ = h.arg; break;
            case A_SELECT: selected_ = h.arg; break;
            case A_CMD: out.push_back({HmiAction::Command, h.arg}); break;
            case A_TOGGLE: p_.size_rule = !p_.size_rule; changed = true; break;
            case A_IMAGES: p_.images = (p_.images + 1) % 3; changed = true; break;
            case A_DEC:
            case A_INC: {
                const ParamDef &d = kParams[h.arg];
                const double dir = h.action == A_INC ? 1 : -1;
                auto step = [&](double v) { return std::clamp(std::round((v + dir * d.step) / d.step) * d.step, d.lo, d.hi); };
                switch (h.arg) {
                case 0: p_.belt_speed = step(p_.belt_speed); break;
                case 1: p_.gear_od = float(step(p_.gear_od)); break;
                case 2: p_.gear_od_tol = float(step(p_.gear_od_tol)); break;
                case 3: p_.rack_len = float(step(p_.rack_len)); break;
                case 4: p_.rack_len_tol = float(step(p_.rack_len_tol)); break;
                case 5: p_.size_rule = dir > 0; break;
                case 6: p_.min_conf = float(step(p_.min_conf)); break;
                case 7: p_.images = std::clamp(p_.images + int(dir), 0, 2); break;
                }
                changed = true;
                break;
            }
            }
            if (changed) {
                save_params();
                out.push_back({HmiAction::Params, 0});
            }
            break;
        }
    }
    return out;
}

void Hmi::text(cv::Mat &img, const std::string &s, int x, int y, int px, const cv::Scalar &c)
{
    txt_.draw(img, s, x, y, px, c);
}

void Hmi::button(cv::Mat &img, const cv::Rect &r, const std::string &label, bool on, int action, int arg, int px)
{
    cv::rectangle(img, r, on ? ACCENT : PANEL, cv::FILLED);
    cv::rectangle(img, r, LINE, 1);
    const int w = txt_.width(label, px);
    text(img, label, r.x + (r.width - w) / 2, r.y + (r.height - px) / 2 - 1, px, on ? cv::Scalar(255, 255, 255) : WHITE);
    if (action) hits_.push_back({r, action, arg});
}

cv::Mat Hmi::render(double now)
{
    cv::Mat img(H, W, CV_8UC3, BG);
    hits_.clear();
    cv::line(img, {0, 0}, {W - 1, 0}, ACCENT, 2);
    // navigation + status
    text(img, "DragonEyeSorter", 12, 8, 17, DIM);
    for (int i = 0; i < 4; i++) button(img, {8, 34 + i * 50, 136, 44}, kPages[i], page_ == i, A_TAB, i, 22);
    text(img, st_.npu ? "NPU 在线" : "NPU 未加载", 12, 240, 16, st_.npu ? GREEN : ORANGE);
    text(img, std::string("输入设备 ") + std::to_string(st_.inputs) + (st_.clock_synced ? "  已对时" : "  未对时"), 12,
         264, 15, DIM);
    cv::line(img, {152, 10}, {152, H - 10}, LINE, 1);
    switch (page_) {
    case 0: page_overview(img, now); break;
    case 1: page_curves(img); break;
    case 2: page_params(img); break;
    default: page_records(img); break;
    }
    return img;
}

void Hmi::page_overview(cv::Mat &img, double)
{
    const double yield = total_ ? 100.0 * ok_ / total_ : 0;
    const struct {
        const char *k;
        std::string v;
        cv::Scalar c;
    } cells[] = {{"总数", std::to_string(total_), WHITE},        {"良品", std::to_string(ok_), GREEN},
                 {"NG", std::to_string(ng_), RED},              {"复检", std::to_string(recheck_), ORANGE},
                 {"良率", fmt("%.1f%%", yield), WHITE},          {"产量 件/分", std::to_string(part_times_.size()), WHITE}};
    for (int i = 0; i < 6; i++) {
        const int x = 172 + (i % 2) * 200, y = 14 + (i / 2) * 92;
        text(img, cells[i].k, x, y, 18, DIM);
        text(img, cells[i].v, x, y + 26, 40, cells[i].c);
    }
    // parts per class
    const int bx = 600;
    text(img, "各类数量", bx, 6, 18, DIM);
    uint64_t mx = 1;
    for (uint64_t v : per_class_) mx = std::max(mx, v);
    for (size_t i = 0; i < per_class_.size() && i < 9; i++) {
        const int y = 32 + int(i) * 29;
        const bool ok = i < class_cn_.size() && class_cn_[i].find("良品") != std::string::npos;
        text(img, i < class_cn_.size() ? class_cn_[i] : "未分类", bx, y, 17, WHITE);
        const int w = int(330.0 * per_class_[i] / mx);
        cv::rectangle(img, {bx + 175, y + 3, std::max(w, 1), 17}, ok ? GREEN : i < class_cn_.size() ? RED : DIM,
                      cv::FILLED);
        text(img, std::to_string(per_class_[i]), bx + 515, y, 17, WHITE);
    }
    // last parts
    const int tx = 1180;
    text(img, "最近零件", tx, 6, 18, DIM);
    const int sx = W - 260;
    cv::rectangle(img, {sx, 4, 250, 30}, state_color(st_.run_state), cv::FILLED);
    text(img, std::string(state_name(st_.run_state)) + fmt("  带速 %.0f mm/s", st_.belt_mm_s), sx + 10, 8, 18, WHITE);
    const int cx[] = {tx, tx + 80, tx + 260, tx + 380, tx + 520, tx + 620};
    const char *hd[] = {"件号", "类别", "去向", "出口 / 料盒", "尺寸", "置信度"};
    for (int c = 0; c < 6; c++) text(img, hd[c], cx[c], 42, 16, DIM);
    for (size_t i = 0; i < parts_.size() && i < 7; i++) {
        const PartRecord &r = parts_[i].r;
        const int y = 68 + int(i) * 32;
        const cv::Scalar col = r.disp == "pick" ? GREEN : r.disp == "flap" ? RED : ORANGE;
        const std::string cls = r.cls >= 0 && size_t(r.cls) < class_cn_.size() ? class_cn_[r.cls] : "未分类";
        text(img, std::to_string(r.track_id), cx[0], y, 19, WHITE);
        text(img, cls, cx[1], y, 19, WHITE);
        text(img, r.disp == "pick" ? "抓取" : r.disp == "flap" ? "翻板" : "复检", cx[2], y, 19, col);
        text(img, r.dest, cx[3], y, 17, WHITE);
        text(img, r.meas.valid ? fmt("%.2f", r.meas.size) : "—", cx[4], y, 19, WHITE);
        text(img, r.cls >= 0 ? fmt("%.2f", r.conf) : "—", cx[5], y, 19, WHITE);
    }
}

namespace {

// trend chart of measured sizes with nominal / tolerance lines and mean, sigma, Cpk
void size_chart(cv::Mat &img, TextFT &t, const cv::Rect &r, const char *title,
                const std::deque<std::pair<float, bool>> &v, float nom, float tol)
{
    cv::rectangle(img, r, PANEL, cv::FILLED);
    double m = 0, sd = 0;
    for (const auto &p : v) m += p.first;
    if (!v.empty()) m /= v.size();
    for (const auto &p : v) sd += (p.first - m) * (p.first - m);
    sd = v.size() > 1 ? std::sqrt(sd / (v.size() - 1)) : 0;
    const double cpk = sd > 1e-9 ? std::min(nom + tol - m, m - (nom - tol)) / (3 * sd) : 0;
    t.draw(img, title, r.x + 10, r.y + 6, 18, WHITE);
    t.draw(img, v.empty() ? std::string("暂无测量") : fmt("均值 %.3f", m) + fmt("  σ %.3f", sd) + fmt("  Cpk %.2f", cpk) +
                                                          fmt("  n=%.0f", double(v.size())),
           r.x + 10, r.y + 30, 15, v.empty() ? DIM : cpk >= 1.33 ? GREEN : cpk >= 1.0 ? ORANGE : RED);
    const cv::Rect pl(r.x + 58, r.y + 58, r.width - 70, r.height - 70);
    double span = 2.5 * tol;
    for (const auto &p : v) span = std::max(span, 1.2 * std::fabs(p.first - nom));
    auto yy = [&](double val) { return int(pl.y + pl.height / 2 - (val - nom) / span * (pl.height / 2)); };
    for (double lv : {nom - tol, nom, nom + tol}) {
        const int y = yy(lv);
        cv::line(img, {pl.x, y}, {pl.x + pl.width, y}, lv == nom ? DIM : RED, 1);
        t.draw(img, fmt("%.2f", lv), r.x + 6, y - 9, 14, lv == nom ? DIM : RED);
    }
    cv::Point prev(-1, -1);
    for (size_t i = 0; i < v.size(); i++) {
        const cv::Point p(pl.x + int(i * (pl.width - 1) / 79), std::clamp(yy(v[i].first), pl.y, pl.y + pl.height));
        if (prev.x >= 0) cv::line(img, prev, p, CYAN, 1, cv::LINE_AA);
        cv::circle(img, p, 3, v[i].second ? RED : GREEN, cv::FILLED, cv::LINE_AA);
        prev = p;
    }
}

}  // namespace

void Hmi::page_curves(cv::Mat &img)
{
    size_chart(img, txt_, {165, 6, 570, 288}, "齿轮外径 mm（公差线 = 参数页）", od_, p_.gear_od, p_.gear_od_tol);
    size_chart(img, txt_, {745, 6, 570, 288}, "齿条长度 mm", len_, p_.rack_len, p_.rack_len_tol);
    // timing and belt speed over the last 60 s
    const cv::Rect r(1325, 6, 588, 288);
    cv::rectangle(img, r, PANEL, cv::FILLED);
    text(img, "近 60 秒：", r.x + 10, r.y + 6, 18, WHITE);
    text(img, "工作线程 ms", r.x + 110, r.y + 8, 15, CYAN);
    text(img, "NPU ms", r.x + 220, r.y + 8, 15, YELLOW);
    text(img, "带速 mm/s", r.x + 300, r.y + 8, 15, WHITE);
    const cv::Rect pl(r.x + 40, r.y + 40, r.width - 90, r.height - 60);
    float mt = 20, mb = 100;
    for (const Sample &s : samples_) {
        mt = std::max({mt, s.worker, s.npu});
        mb = std::max(mb, s.belt);
    }
    for (int k = 0; k <= 4; k++) {
        const int y = pl.y + pl.height - k * pl.height / 4;
        cv::line(img, {pl.x, y}, {pl.x + pl.width, y}, LINE, 1);
        text(img, fmt("%.0f", mt * k / 4), r.x + 6, y - 8, 13, CYAN);
        text(img, fmt("%.0f", mb * k / 4), pl.x + pl.width + 6, y - 8, 13, WHITE);
    }
    auto plot = [&](int which, const cv::Scalar &c, float scale) {
        cv::Point prev(-1, -1);
        for (size_t i = 0; i < samples_.size(); i++) {
            const Sample &s = samples_[i];
            const float v = which == 0 ? s.worker : which == 1 ? s.npu : s.belt;
            const cv::Point p(pl.x + int(i * (pl.width - 1) / 119), pl.y + pl.height - int(v / scale * pl.height));
            if (prev.x >= 0) cv::line(img, prev, p, c, 1, cv::LINE_AA);
            prev = p;
        }
    };
    plot(2, WHITE, mb);
    plot(0, CYAN, mt);
    plot(1, YELLOW, mt);
}

void Hmi::page_params(cv::Mat &img)
{
    for (int i = 0; i < kNParams; i++) {
        const int y = 10 + i * 35;
        const ParamDef &d = kParams[i];
        text(img, d.label, 175, y + 4, 20, WHITE);
        std::string val;
        switch (i) {
        case 0: val = fmt("%.0f", p_.belt_speed); break;
        case 1: val = fmt("%.2f", p_.gear_od); break;
        case 2: val = fmt("%.2f", p_.gear_od_tol); break;
        case 3: val = fmt("%.1f", p_.rack_len); break;
        case 4: val = fmt("%.2f", p_.rack_len_tol); break;
        case 5: val = p_.size_rule ? "开" : "关"; break;
        case 6: val = fmt("%.2f", p_.min_conf); break;
        case 7: val = kImages[p_.images]; break;
        }
        if (i == 5) {
            button(img, {470, y, 286, 30}, val, p_.size_rule, A_TOGGLE, 0, 20);
        } else if (i == 7) {
            button(img, {470, y, 286, 30}, val, false, A_IMAGES, 0, 20);
        } else {
            button(img, {470, y, 46, 30}, "-", false, A_DEC, i, 26);   // U+2212 is not in the font
            const int w = txt_.width(val, 22);
            text(img, val, 520 + (186 - w) / 2, y + 3, 22, YELLOW);
            button(img, {710, y, 46, 30}, "+", false, A_INC, i, 26);
        }
        text(img, d.unit, 772, y + 7, 16, DIM);
    }
    // run control (run mode: same rules as the PLC's CAN commands)
    const int x0 = 1180;
    text(img, "运行控制", x0, 8, 20, WHITE);
    const bool run = st_.run_state >= 0;
    const struct {
        const char *t;
        int cmd;
    } b[] = {{"启动", CANP_CMD_START}, {"停止", CANP_CMD_STOP}, {"暂停", CANP_CMD_PAUSE}, {"恢复", CANP_CMD_RESUME},
             {"复位报警", CANP_CMD_RESET_ALARM}};
    for (int k = 0; k < 5; k++)
        button(img, {x0 + (k % 3) * 160, 42 + (k / 3) * 66, 148, 56}, b[k].t, false, run ? A_CMD : 0, b[k].cmd, 24);
    cv::rectangle(img, {x0, 182, 470, 34}, state_color(st_.run_state), cv::FILLED);
    text(img, std::string("状态 ") + state_name(st_.run_state) + fmt("    带速 %.1f mm/s", st_.belt_mm_s), x0 + 10, 187,
         20, WHITE);
    text(img, run ? "参数修改立即生效，并保存到 ui_params.json" : "运行控制只在 des_app run 模式可用；参数照常保存",
         x0, 232, 16, DIM);
    text(img, fmt("已记录 %.0f 件", double(st_.logged)) + fmt("，存图 %.0f 张", double(st_.log_images)), x0, 258, 16,
         DIM);
}

void Hmi::page_records(cv::Mat &img)
{
    const int cx[] = {172, 262, 342, 512, 592, 692, 1062};
    const char *hd[] = {"时间 s", "件号", "类别", "置信度", "去向", "原因", "尺寸"};
    for (int c = 0; c < 7; c++) text(img, hd[c], cx[c], 6, 16, DIM);
    for (size_t i = 0; i < parts_.size() && i < 9; i++) {
        const PartRecord &r = parts_[i].r;
        const int y = 30 + int(i) * 29;
        const cv::Rect row(165, y - 2, 1140, 28);
        if (int(i) == selected_) cv::rectangle(img, row, PANEL, cv::FILLED);
        hits_.push_back({row, A_SELECT, int(i)});
        const cv::Scalar col = r.disp == "pick" ? GREEN : r.disp == "flap" ? RED : ORANGE;
        text(img, fmt("%.1f", r.t_s), cx[0], y, 18, WHITE);
        text(img, std::to_string(r.track_id), cx[1], y, 18, WHITE);
        text(img, r.cls >= 0 && size_t(r.cls) < class_cn_.size() ? class_cn_[r.cls] : "未分类", cx[2], y, 18, WHITE);
        text(img, r.cls >= 0 ? fmt("%.2f", r.conf) : "—", cx[3], y, 18, WHITE);
        text(img, r.disp == "pick" ? "抓取" : r.disp == "flap" ? "翻板" : "复检", cx[4], y, 18, col);
        text(img, r.why.substr(0, 42), cx[5], y, 16, WHITE);
        text(img, r.meas.valid ? fmt("%.3f", r.meas.size) : "—", cx[6], y, 18, WHITE);
    }
    if (parts_.empty()) text(img, "还没有判定过的零件", 172, 40, 20, DIM);
    // selected part: crop + details
    if (selected_ < 0 || selected_ >= int(parts_.size())) return;
    const Part &p = parts_[selected_];
    const cv::Rect pic(1320, 20, 260, 260);
    if (!p.crop.empty()) {
        cv::Mat small;
        cv::resize(p.crop, small, pic.size(), 0, 0, cv::INTER_AREA);
        small.copyTo(img(pic));
    } else {
        cv::rectangle(img, pic, PANEL, cv::FILLED);
        text(img, "无图", pic.x + 100, pic.y + 115, 20, DIM);
    }
    const PartRecord &r = p.r;
    const PartMeasure &m = r.meas;
    int y = 22;
    auto line = [&](const std::string &s, const cv::Scalar &c) {
        text(img, s, 1600, y, 18, c);
        y += 30;
    };
    line("件号 " + std::to_string(r.track_id) + "   " + (r.cls >= 0 && size_t(r.cls) < class_cn_.size() ? class_cn_[r.cls] : "未分类"),
         WHITE);
    line(std::string(r.disp == "pick" ? "抓取 → " : r.disp == "flap" ? "翻板 → " : "复检 → ") + r.dest,
         r.disp == "pick" ? GREEN : r.disp == "flap" ? RED : ORANGE);
    line(r.why.substr(0, 26), DIM);
    if (m.valid) {
        line(std::string(m.kind == 'g' ? "外径 " : "长度 ") + fmt("%.3f mm", m.size), WHITE);
        if (m.kind == 'g') line(fmt("孔径 %.3f", m.bore) + fmt("  偏心 %.3f", m.ecc), WHITE);
        else line(fmt("齿距 %.4f mm", m.pitch), WHITE);
        line(m.teeth_judged ? fmt("齿数 %.0f", m.teeth) + fmt("  缺料 %.0f", m.short_teeth) + fmt("  缺齿 %.0f", m.missing_teeth) +
                                  fmt("  飞边 %.0f", m.flash_gaps)
                            : std::string("齿未判（离光轴过远）"),
             m.teeth_judged ? WHITE : ORANGE);
        line(fmt("测于离光轴 %.0f mm", m.axis_dist), DIM);
    } else {
        line("未测量", DIM);
    }
}

}  // namespace des
