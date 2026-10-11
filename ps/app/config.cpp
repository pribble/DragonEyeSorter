// config.cpp — see config.hpp
#include "config.hpp"

#include <fstream>

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
            get(c, "flap_lead", p.flap_lead); get(c, "flap_conflict", p.flap_conflict);
        }
        if (j.contains("arm")) {
            const json &c = j["arm"];
            auto &a = cfg.arm;
            get(c, "base_h", a.base_h); get(c, "l_upper", a.l_upper); get(c, "l_fore", a.l_fore);
            get(c, "l_wrist", a.l_wrist); get(c, "l_tool", a.l_tool);
            get_vec2(c, "base_xy", a.base_xy);
            float yaw_deg = a.base_yaw * 180.f / kPi, g0_deg = a.grip_axis_at_zero * 180.f / kPi;
            get(c, "base_yaw_deg", yaw_deg); get(c, "grip_axis_at_zero_deg", g0_deg);
            a.base_yaw = yaw_deg * kPi / 180.f;
            a.grip_axis_at_zero = g0_deg * kPi / 180.f;
            get(c, "belt_z", a.belt_z);
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
            get(c, "f1_pass_us", f.f1_pass_us); get(c, "f1_ng_us", f.f1_ng_us);
            get(c, "f2_rework_us", f.f2_rework_us); get(c, "f2_scrap_us", f.f2_scrap_us);
        }
        if (j.contains("npu")) {
            const json &c = j["npu"];
            get(c, "enabled", cfg.npu.enabled); get(c, "model", cfg.npu.model); get(c, "core", cfg.npu.core);
        }
        if (j.contains("storage")) {
            const json &c = j["storage"];
            auto &s = cfg.storage;
            get(c, "enabled", s.enabled); get(c, "dir", s.dir); get(c, "images", s.images);
            get(c, "jpeg_quality", s.jpeg_quality); get(c, "max_queue", s.max_queue);
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
            if (c.contains("belt")) {   // des_app calib-belt
                get(c["belt"], "mm_per_step", cfg.belt.mm_per_step);
                get(c["belt"], "dir", cfg.belt.dir);
                cfg.belt_calibrated = true;
            }
            cfg.calib_loaded = cp;
        } catch (const std::exception &e) {
            return cp + ": " + e.what();
        }
    }
    if (cfg.pick_after_fov > 0) cfg.picker.pick_x = cfg.tracker.fov_x1 + cfg.pick_after_fov;   // after calib.json
    cfg.picker.tail_x = cfg.tracker.tail_x;
    cfg.picker.recheck_outlet = 0;

    if (!cfg.npu.model.empty() && cfg.npu.model[0] != '/') cfg.npu.model = dir_of(path) + "/" + cfg.npu.model;
    std::string ll = cfg.label_list;
    if (!ll.empty() && ll[0] != '/') ll = dir_of(path) + "/" + ll;
    return load_rules(ll, cfg);
}

}  // namespace des
