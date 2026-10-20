// config.cpp — see config.hpp
#include "config.hpp"

#include <fstream>
#include <memory>

#include "flat_field.hpp"
#include "json.hpp"

namespace des {

using json = nlohmann::json;

template <class T> static void get(const json &j, const char *key, T &v)
{
    auto it = j.find(key);
    if (it != j.end() && !it->is_null()) v = it->get<T>();
}

static void get_vec2(const json &j, const char *key, Vec2 &v)
{
    auto it = j.find(key);
    if (it != j.end() && it->is_array() && it->size() == 2) v = {(*it)[0].get<float>(), (*it)[1].get<float>()};
}

static std::string dir_of(const std::string &p)
{
    size_t k = p.find_last_of('/');
    return k == std::string::npos ? "." : p.substr(0, k);
}

static std::string load_rules(const std::string &path, AppConfig &cfg)
{
    std::ifstream f(path);
    if (!f) return "cannot open label list " + path;
    json j;
    try { f >> j; } catch (const std::exception &e) { return path + ": " + e.what(); }
    auto &rules = cfg.tracker.rules;
    rules.clear();
    for (const json &l : j.at("labels")) {
        size_t id = l.at("id").get<size_t>();
        if (rules.size() <= id) rules.resize(id + 1);
        ClassRule &r = rules[id];
        r.label = l.at("label").get<std::string>();
        std::string action = l.value("action", ""), dest = l.value("dest", "");
        const auto &names = action == "arm_pick" ? cfg.bins : cfg.outlets;
        int idx = -1;
        for (size_t k = 0; k < names.size(); k++) if (names[k] == dest) idx = int(k);
        if ((action == "arm_pick" || action == "flap") && idx >= 0) {
            r.disp = action == "arm_pick" ? Disposition::Pick : Disposition::Flap;
            r.dest = idx;
        } else {
            r.disp = Disposition::Recheck;   // unknown action / destination: never sort it blindly
            r.dest = 0;
        }
    }
    return "";
}

std::string load_config(const std::string &path, AppConfig &cfg)
{
    std::ifstream f(path);
    if (!f) return "cannot open " + path;
    json j;
    try { f >> j; } catch (const std::exception &e) { return path + ": " + e.what(); }

    try {
        if (j.contains("camera")) {
            const json &c = j["camera"];
            get(c, "vmax", cfg.camera.vmax);
            get(c, "buffers", cfg.camera.buffers);
            get(c, "realtime", cfg.camera.realtime);
            get(c, "exposure_us", cfg.camera.exposure_us);
            get(c, "gain_db10", cfg.camera.gain_db10);
        }
        if (j.contains("locate")) {
            const json &c = j["locate"];
            auto &l = cfg.locate;
            get(c, "scale", l.scale); get(c, "thresh", l.thresh); get(c, "min_thresh", l.min_thresh);
            get(c, "open_iter", l.open_iter); get(c, "border_px", l.border_px);
            get(c, "min_area_mm2", l.min_area_mm2); get(c, "max_area_mm2", l.max_area_mm2);
        }
        if (j.contains("px_to_table")) {
            const json &h = j["px_to_table"];
            if (h.size() != 9) return "px_to_table needs 9 numbers (row-major 3x3)";
            for (int i = 0; i < 9; i++) cfg.px_to_table.h[i] = h[i].get<double>();
        }
        if (j.contains("belt")) {
            const json &c = j["belt"];
            get(c, "mm_per_step", cfg.belt.mm_per_step); get(c, "dir", cfg.belt.dir);
            get(c, "run_speed", cfg.belt.run_speed); get(c, "accel", cfg.belt.accel);
            get(c, "enc_counts_per_rev", cfg.belt.enc_counts_per_rev); get(c, "enc_wheel_mm", cfg.belt.enc_wheel_mm);
            if (c.contains("adapt")) {
                get(c["adapt"], "enabled", cfg.belt.adapt.enabled); get(c["adapt"], "min_mm_s", cfg.belt.adapt.min_mm_s);
                get(c["adapt"], "change_frac", cfg.belt.adapt.change_frac);
            }
        }
        if (j.contains("tracker")) {
            const json &c = j["tracker"];
            auto &t = cfg.tracker;
            get(c, "fov_x0", t.fov_x0); get(c, "fov_x1", t.fov_x1); get(c, "edge_margin", t.edge_margin);
            get(c, "decide_x", t.decide_x); get(c, "tail_x", t.tail_x);
            get(c, "gate_s", t.gate_s); get(c, "gate_y", t.gate_y);
            get(c, "min_hits", t.min_hits); get(c, "max_miss_new", t.max_miss_new); get(c, "max_miss", t.max_miss);
            get(c, "min_conf", t.min_conf);
            get(c, "pick_unclassified", t.pick_unclassified); get(c, "unclassified_bin", t.unclassified_bin);
        }
        if (j.contains("picker")) {
            const json &c = j["picker"];
            auto &p = cfg.picker;
            get(c, "pick_x", p.pick_x); get(c, "pick_tol", p.pick_tol);
            get(c, "pick_after_fov_mm", cfg.pick_after_fov);
            get(c, "reach_y0", p.reach_y0); get(c, "reach_y1", p.reach_y1); get(c, "brake_margin", p.brake_margin);
            get(c, "finger_w", p.finger_w); get(c, "finger_t", p.finger_t); get(c, "grip_clear", p.grip_clear);
            get(c, "max_open", p.max_open); get(c, "round_ratio", p.round_ratio); get(c, "neighbor_gap", p.neighbor_gap);
            if (c.contains("flying")) {   // intercept picks (docs/运动控制与分拣.md 拦截式不停带夹取)
                const json &f = c["flying"];
                get(f, "enabled", p.flying); get(f, "r0", p.flying_r0); get(f, "r1", p.flying_r1);
                float ax_deg = p.flying_axis * 180.f / kPi;
                get(f, "axis_deg", ax_deg);
                p.flying_axis = ax_deg * kPi / 180.f;
                get(f, "ready_s", p.flying_ready_s); get(f, "window_mm", p.flying_window);
                get(f, "gap_mm", p.flying_gap); get(f, "close_lead_s", p.flying_close_lead_s);
            }
        }
        if (j.contains("arm")) {
            const json &c = j["arm"];
            auto &a = cfg.arm;
            get(c, "base_h", a.base_h); get(c, "l_upper", a.l_upper); get(c, "l_fore", a.l_fore);
            get(c, "l_wrist", a.l_wrist); get(c, "l_tool", a.l_tool);
            get_vec2(c, "base_xy", a.base_xy);
            float yaw_deg = a.base_yaw * 180.f / kPi, g0_deg = a.grip_axis_at_zero * 180.f / kPi;
            float side_deg = a.servo_side * 180.f / kPi;
            get(c, "base_yaw_deg", yaw_deg); get(c, "grip_axis_at_zero_deg", g0_deg);
            get(c, "tool_offset_mm", a.tool_offset); get(c, "servo_side_deg", side_deg);
            a.base_yaw = yaw_deg * kPi / 180.f;
            a.grip_axis_at_zero = g0_deg * kPi / 180.f;
            a.servo_side = side_deg * kPi / 180.f;
            get(c, "belt_z", a.belt_z);
            get(c, "max_tilt_deg", a.max_tilt_deg); get(c, "tilt_step_deg", a.tilt_step_deg);
            get(c, "servo_in_min_r_mm", a.servo_in_min_r);
            get(c, "grip_open_count", a.grip_open_count); get(c, "grip_closed_count", a.grip_closed_count);
            get(c, "grip_max_open", a.grip_max_open); get(c, "grip_squeeze", a.grip_squeeze);
            get(c, "z_safe", a.z_safe); get(c, "z_grasp", a.z_grasp); get(c, "joint_speed", a.joint_speed);
            get(c, "min_ms", a.min_ms); get(c, "descend_ms", a.descend_ms);
            get(c, "close_ms", a.close_ms); get(c, "open_ms", a.open_ms);
            if (c.contains("joints")) {
                const json &js = c["joints"];
                for (size_t i = 0; i < js.size() && i < 6; i++) {
                    JointCal &jc = a.joint[i];
                    int id = jc.id ? jc.id : int(i + 1);
                    get(js[i], "id", id);
                    jc.id = uint8_t(id);
                    get(js[i], "zero_count", jc.zero_count); get(js[i], "counts_per_deg", jc.counts_per_deg);
                    get(js[i], "min_deg", jc.min_deg); get(js[i], "max_deg", jc.max_deg);
                }
            }
            if (c.contains("home_deg")) for (int i = 0; i < 5; i++) a.home_deg[i] = c["home_deg"].at(i).get<float>();
            if (c.contains("bins_deg")) {
                a.bins.clear();
                for (const json &b : c["bins_deg"]) {
                    std::array<float, 5> q{};
                    for (int i = 0; i < 5; i++) q[i] = b.at(i).get<float>();
                    a.bins.push_back(q);
                }
            }
        }
        get(j, "bins", cfg.bins);
        get(j, "outlets", cfg.outlets);
        get(j, "label_list", cfg.label_list);
        if (j.contains("flaps")) {
            const json &c = j["flaps"];
            auto &f = cfg.flaps;
            get(c, "enabled", f.enabled); get(c, "prepare_mm", f.prepare_mm); get(c, "clear_mm", f.clear_mm);
            get(c, "gap_mm", f.gap_mm);
            if (c.contains("gates")) {
                f.gates.clear();
                for (const json &gj : c["gates"]) {
                    FlapConfig::Gate g;
                    get(gj, "after_pick_mm", g.after_pick_mm); get(gj, "outlet", g.outlet_name);
                    get(gj, "open_us", g.open_us); get(gj, "closed_us", g.closed_us);
                    g.outlet = -1;
                    for (size_t k = 0; k < cfg.outlets.size(); k++) if (cfg.outlets[k] == g.outlet_name) g.outlet = int(k);
                    if (g.outlet < 0) return path + ": flaps.gates outlet \"" + g.outlet_name + "\" is not in outlets";
                    if (g.outlet == 0) return path + ": flaps.gates: the recheck outlet is the belt end, not a gate";
                    f.gates.push_back(g);
                }
                if (f.gates.size() > 2) return path + ": flaps.gates: at most 2 (des_pwm has 2 channels)";
            }
        }
        if (j.contains("plc")) get(j["plc"], "modbus_port", cfg.plc.modbus_port);
        if (j.contains("npu")) {
            const json &c = j["npu"];
            get(c, "enabled", cfg.npu.enabled); get(c, "model", cfg.npu.model); get(c, "core", cfg.npu.core);
        }
        if (j.contains("storage")) {
            const json &c = j["storage"];
            auto &s = cfg.storage;
            get(c, "enabled", s.enabled); get(c, "dir", s.dir); get(c, "images", s.images);
            get(c, "jpeg_quality", s.jpeg_quality); get(c, "max_queue", s.max_queue); get(c, "feed_port", s.feed_port);
            // 相对的落盘目录按 des_app.json 所在目录解析（与 label_list / recipes / calib / npu.model 同规矩）：
            // 生产配置在 /root/des/ → 仍写 /root/des/data/log；测试目录部署的副本 → 写自己的，不污染生产数据
            if (!s.dir.empty() && s.dir[0] != '/') s.dir = dir_of(path) + "/" + s.dir;
            if (s.images != "none" && s.images != "ng" && s.images != "all")
                return path + ": storage.images must be none / ng / all";
        }
        if (j.contains("measure")) {   // after "outlets": the size rule names its outlet
            const json &c = j["measure"];
            auto &t = cfg.measure;
            auto &m = t.m;
            get(c, "enabled", t.enabled); get(c, "per_frame", t.per_frame); get(c, "gear_ratio", t.gear_ratio);
            get(c, "margin_mm", t.margin_mm); get(c, "improve", t.improve);
            if (c.contains("axis_table")) {
                get_vec2(c, "axis_table", m.axis_table);
                t.axis_from_image = false;
            }
            get(c, "camera_h_mm", m.camera_h_mm); get(c, "gear_h_mm", m.gear_h_mm); get(c, "rack_h_mm", m.rack_h_mm);
            get(c, "walls_visible", m.walls_visible); get(c, "max_smear_mm", m.max_smear_mm);
            get(c, "short_tooth_mm", m.short_tooth_mm); get(c, "flash_mm", m.flash_mm);
            get(c, "od_offset_mm", m.od_offset_mm); get(c, "bore_offset_mm", m.bore_offset_mm);
            get(c, "bore_near_arc", m.bore_near_arc);
            if (c.contains("colors")) {
                t.colors.clear();
                for (const json &cj : c["colors"]) {
                    ColorClass k;
                    get(cj, "name", k.name); get(cj, "hue_lo", k.hue_lo); get(cj, "hue_hi", k.hue_hi);
                    get(cj, "sat_lo", k.sat_lo); get(cj, "sat_hi", k.sat_hi);
                    t.colors.push_back(k);
                }
            }
            if (c.contains("rule")) {
                const json &r = c["rule"];
                auto &s = cfg.tracker.size;
                get(r, "enabled", s.enabled); get(r, "gear_teeth", s.gear_teeth);
                get(r, "gear_od", s.gear_od); get(r, "gear_od_tol", s.gear_od_tol);
                get(r, "gear_bore", s.gear_bore); get(r, "gear_bore_tol", s.gear_bore_tol);
                get(r, "gear_max_ecc", s.gear_max_ecc); get(r, "rack_len", s.rack_len); get(r, "rack_len_tol", s.rack_len_tol);
                get(r, "defect_recheck", s.defect_recheck); get(r, "require", s.require);
                std::string outlet = "scrap";
                get(r, "ng_outlet", outlet);
                s.ng_outlet = -1;
                for (size_t k = 0; k < cfg.outlets.size(); k++) if (cfg.outlets[k] == outlet) s.ng_outlet = int(k);
                if (s.ng_outlet < 0) return path + ": measure.rule.ng_outlet \"" + outlet + "\" is not in outlets";
            }
        }
    } catch (const std::exception &e) {
        return path + ": " + e.what();
    }
    for (size_t i = 0; i < 6; i++) if (!cfg.arm.joint[i].id) cfg.arm.joint[i].id = uint8_t(i + 1);
    cfg.config_dir = dir_of(path);
    get(j, "calib_file", cfg.calib_file);
    std::string cp = cfg.calib_file;
    if (!cp.empty() && cp[0] != '/') cp = dir_of(path) + "/" + cp;
    std::ifstream cf(cp);
    if (cf) {
        try {
            json c;
            cf >> c;
            if (c.contains("px_to_table") && c["px_to_table"].size() == 9)
                for (int i = 0; i < 9; i++) cfg.px_to_table.h[i] = c["px_to_table"][i].get<double>();
            if (c.contains("tracker")) {
                get(c["tracker"], "fov_x0", cfg.tracker.fov_x0);
                get(c["tracker"], "fov_x1", cfg.tracker.fov_x1);
            }
            if (c.contains("belt")) {   // des_tool calib-belt
                get(c["belt"], "mm_per_step", cfg.belt.mm_per_step);
                get(c["belt"], "dir", cfg.belt.dir);
                cfg.belt_calibrated = true;
            }
            if (c.contains("encoder")) {   // des_tool calib-enc
                get(c["encoder"], "mm_per_count", cfg.belt.enc_mm_per_count);
                get(c["encoder"], "dir", cfg.belt.enc_dir);
                cfg.enc_calibrated = true;
            }
            if (c.contains("arm")) {    // des_tool calib-arm (overrides arm.base_xy / base_yaw_deg / belt_z)
                const json &a = c["arm"];
                get_vec2(a, "base_xy", cfg.arm.base_xy);
                if (a.contains("base_yaw_deg")) cfg.arm.base_yaw = a["base_yaw_deg"].get<float>() * kPi / 180.f;
                get(a, "belt_z", cfg.arm.belt_z);
                cfg.arm_calibrated = true;
            }
            cfg.calib_loaded = cp;
        } catch (const std::exception &e) {
            return cp + ": " + e.what();
        }
    }
    get(j, "flat_file", cfg.flat_file);
    if (!cfg.flat_file.empty()) {   // flat field for the part crops (des_tool calib-flat); missing = raw crops
        std::string fp = cfg.flat_file;
        if (fp[0] != '/') fp = cfg.config_dir + "/" + fp;
        std::ifstream ff(fp);
        if (ff) {
            auto f = std::make_shared<FlatField>();
            std::string e = load_flat(fp, *f);
            if (!e.empty()) return e;
            cfg.measure.flat = f;
            cfg.flat_loaded = fp;
        }
    }
    {   // ready pose taught on the HMI arm page (「设为初始姿态」): counts of the six servos, over arm.home_deg
        const std::string hp = cfg.config_dir + "/arm_home.json";
        std::ifstream hf(hp);
        if (hf) {
            try {
                json h;
                hf >> h;
                if (h.contains("counts") && h["counts"].size() == 6) {
                    for (int i = 0; i < 5; i++) {
                        const JointCal &jc = cfg.arm.joint[i];
                        cfg.arm.home_deg[i] = (h["counts"][i].get<float>() - jc.zero_count) / jc.counts_per_deg;
                    }
                    cfg.arm.home_grip_count = h["counts"][5].get<float>();
                    cfg.arm_home_taught = true;
                }
            } catch (const std::exception &e) {
                return hp + ": " + e.what();
            }
        }
    }
    // recipes (after measure.rule, which is the fallback recipe 1)
    get(j, "recipe", cfg.recipe);
    get(j, "recipes_file", cfg.recipes_file);
    {
        std::string rp = cfg.recipes_file;
        if (!rp.empty() && rp[0] != '/') rp = dir_of(path) + "/" + rp;
        std::ifstream rf(rp);
        cfg.recipes.clear();
        if (!rf) {
            cfg.recipes.push_back(Recipe::from_rule(cfg.tracker.size, 1, "默认（des_app.json measure.rule）"));
        } else {
            try {
                json rj;
                rf >> rj;
                for (const json &e : rj.at("recipes")) {
                    Recipe r = Recipe::from_rule(cfg.tracker.size, 0, "");   // missing keys keep measure.rule values
                    get(e, "id", r.id); get(e, "name", r.name);
                    get(e, "gear_teeth", r.gear_teeth); get(e, "gear_od", r.gear_od); get(e, "gear_od_tol", r.gear_od_tol);
                    get(e, "gear_bore", r.gear_bore); get(e, "gear_bore_tol", r.gear_bore_tol);
                    get(e, "gear_max_ecc", r.gear_max_ecc); get(e, "rack_len", r.rack_len); get(e, "rack_len_tol", r.rack_len_tol);
                    if (r.id < 1 || r.id > 255) return rp + ": recipe id must be 1..255";
                    if (find_recipe(cfg.recipes, r.id)) return rp + ": duplicate recipe id " + std::to_string(r.id);
                    if (r.name.empty()) r.name = "配方 " + std::to_string(r.id);
                    cfg.recipes.push_back(r);
                }
            } catch (const std::exception &e) {
                return rp + ": " + e.what();
            }
            if (cfg.recipes.empty()) return rp + ": no recipes";
        }
        const Recipe *r = find_recipe(cfg.recipes, cfg.recipe);
        if (!r) return path + ": recipe " + std::to_string(cfg.recipe) + " is not in " + rp;
        r->apply(cfg.tracker.size);
    }

    {   // parts stand gear_h_mm above the belt (measure_config.hpp part_homography); frames are 1920x1080
        const MeasureConfig &m = cfg.measure.m;
        const Vec2 axis = cfg.measure.axis_from_image ? cfg.px_to_table.map({960.f, 540.f}) : m.axis_table;
        cfg.px_to_part = part_homography(cfg.px_to_table, m, axis);
    }
    if (cfg.pick_after_fov > 0) cfg.picker.pick_x = cfg.tracker.fov_x1 + cfg.pick_after_fov;   // after calib.json
    cfg.picker.tail_x = cfg.tracker.tail_x;
    cfg.picker.recheck_outlet = 0;
    cfg.picker.arm_base = cfg.arm.base_xy;                 // after calib.json (calib-arm may have moved it)
    cfg.picker.tangential_first = cfg.arm.max_tilt_deg > 0;
    cfg.picker.gates.clear();                              // side gates exist for the picker only once mounted
    if (cfg.flaps.enabled)
        for (const FlapConfig::Gate &g : cfg.flaps.gates) cfg.picker.gates.push_back({cfg.picker.pick_x + g.after_pick_mm, g.outlet});
    cfg.picker.gate_prepare = cfg.flaps.prepare_mm;
    cfg.picker.gate_clear = cfg.flaps.clear_mm;
    cfg.picker.gate_gap = cfg.flaps.gap_mm;

    if (!cfg.npu.model.empty() && cfg.npu.model[0] != '/') cfg.npu.model = dir_of(path) + "/" + cfg.npu.model;
    std::string ll = cfg.label_list;
    if (!ll.empty() && ll[0] != '/') ll = dir_of(path) + "/" + ll;
    return load_rules(ll, cfg);
}

}  // namespace des
