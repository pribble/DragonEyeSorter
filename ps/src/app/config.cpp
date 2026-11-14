// config.cpp — see config.hpp
#include "config.hpp"
#include <algorithm>
#include <fstream>
#include <memory>
#include <utility>
#include "flat_field.hpp"
#include "json.hpp"
#include "json_io.hpp"
#include "util.hpp"
namespace des {
using json = nlohmann::json; namespace {
// index of a name in a name table (bins / outlets); -1 = not there
int index_of(const std::vector<std::string> &names, const std::string &name)
{
    for (size_t i = 0; i < names.size(); i++) if (names[i] == name) return int(i);
    return -1;
}
// a path relative to des_app.json stays relative to it (absolute stays as given): calib_file / flat_file / recipes_file /
// npu.model / label_list / storage.dir
std::string resolve(const std::string &dir, const std::string &path)
{
    return (!path.empty() && path[0] != '/') ? dir + "/" + path : path;
}
// ji::gets on a section object; returns it (nullptr when absent / not an object) for the nested sub-objects present
template <class... A> const json *sec_gets(const json &j, const char *key, A &&...pairs)
{
    const json *c = ji::sec(j, key);
    if (c) ji::gets(*c, std::forward<A>(pairs)...);
    return c;
}
// spec keys shared by measure.rule (SizeRule) and a recipes.json entry (Recipe); a missing key keeps the pre-filled value
template <class S> void get_spec(const json &j, S &s)
{
    ji::gets(j, "gear_teeth", s.gear_teeth, "gear_od", s.gear_od, "gear_od_tol", s.gear_od_tol, "gear_bore", s.gear_bore,
             "gear_bore_tol", s.gear_bore_tol, "gear_max_ecc", s.gear_max_ecc);
}
// label_list.json (single source: scripts/deploy/out/): class id -> label + disposition; an unknown action / destination is never sorted blindly
std::string load_rules(const std::string &path, AppConfig &cfg)
{
    std::ifstream f(path);
    if (!f) return "cannot open label list " + path;
    json j;
    try { f >> j; } catch (const std::exception &e) { return path + ": " + e.what(); }
    auto &rules = cfg.tracker.rules; rules.clear();
    try {   // a missing / mistyped key (j.at) must reach the caller as an error, not terminate des_app
    for (const json &l : j.at("labels")) {
        const size_t id = l.at("id").get<size_t>();
        if (rules.size() <= id) rules.resize(id + 1);
        ClassRule &r = rules[id]; r.label = l.at("label").get<std::string>(); r.cn = r.label;
        // no "cn" in the file: show the model label (what the HMI did)
        ji::get(l, "cn", r.cn); const std::string action = l.value("action", ""), dest = l.value("dest", "");
        const int idx = index_of(action == "arm_pick" ? cfg.bins : cfg.outlets, dest);
        const bool sorts = (action == "arm_pick" || action == "flap") && idx >= 0;
        r.disp = sorts ? (action == "arm_pick" ? Disposition::Pick : Disposition::Flap) : Disposition::Recheck; r.dest = sorts ? idx : 0;
    }
    } catch (const std::exception &e) {
        rules.clear();   // no half table: every class then goes to recheck
        return path + ": " + e.what();
    }
    return "";
}
}  // namespace
std::string load_config(const std::string &path, AppConfig &cfg)
{
    std::ifstream f(path);
    if (!f) return "cannot open " + path;
    json j;
    try { f >> j; } catch (const std::exception &e) { return path + ": " + e.what(); }
    const std::string dir = dir_of(path);
    try {
        CameraConfig &cam = cfg.camera; sec_gets(j, "camera", "vmax", cam.vmax, "buffers", cam.buffers, "realtime", cam.realtime, "exposure_us",
                 cam.exposure_us, "gain_db10", cam.gain_db10);
        LocateConfig &loc = cfg.locate; sec_gets(j, "locate", "scale", loc.scale, "thresh", loc.thresh, "min_thresh", loc.min_thresh, "open_iter",
                 loc.open_iter, "border_px", loc.border_px, "min_area_mm2", loc.min_area_mm2, "max_area_mm2", loc.max_area_mm2,
                 "rel_thresh", loc.rel_thresh, "rel_floor", loc.rel_floor, "round_fit", loc.round_fit);
        if (j.contains("px_to_table")) {   // frozen error text: a wrong length is not a type error (CONTRACT §1)
            const json &h = j["px_to_table"];
            if (h.size() != 9) return "px_to_table needs 9 numbers (row-major 3x3)";
            for (int i = 0; i < 9; i++) cfg.px_to_table.h[i] = h[i].get<double>();
        }
        BeltConfig &belt = cfg.belt;
        if (const json *c = sec_gets(j, "belt", "mm_per_step", belt.mm_per_step, "dir", belt.dir, "run_speed",
                                     belt.run_speed, "accel", belt.accel, "enc_counts_per_rev",
                                     belt.enc_counts_per_rev, "enc_wheel_mm", belt.enc_wheel_mm, "release_idle",
                                     belt.release_idle, "release_after_ms", belt.release_after_ms, "enable_settle_ms",
                                     belt.enable_settle_ms)) {
            if (const json *ad = ji::sec(*c, "adapt")) {   // belt speed adaptation (picker.hpp adapt_speed)
                ji::gets(*ad, "enabled", belt.adapt.enabled, "min_mm_s", belt.adapt.min_mm_s, "change_frac",
                         belt.adapt.change_frac, "fast_mm_s", belt.adapt.fast_mm_s);
                if (belt.adapt.fast_mm_s < 0 || belt.adapt.fast_mm_s > 200)
                    return path + ": belt.adapt.fast_mm_s 0..200 (0 = no fast mode)";
            }
            if (const json *p = ji::sec(*c, "pid")) {   // optional PS-side speed PI (speed_pid.hpp)
                ji::gets(*p, "enabled", belt.pid.enabled, "kp", belt.pid.kp, "ki", belt.pid.ki,
                         "period_ms", belt.pid.period_ms, "trim_mm_s", belt.pid.trim_mm_s,
                         "deadband_mm_s", belt.pid.deadband_mm_s);
                if (belt.pid.period_ms < 20 || belt.pid.period_ms > 1000)
                    return path + ": belt.pid.period_ms 20..1000";
                if (belt.pid.kp < 0 || belt.pid.ki < 0 || belt.pid.trim_mm_s < 0 || belt.pid.trim_mm_s > 20 ||
                    belt.pid.deadband_mm_s < 0)
                    return path + ": belt.pid kp/ki/trim/deadband must be >= 0 and trim <= 20 mm/s";
            }
        }
        TrackerConfig &trk = cfg.tracker;
        sec_gets(j, "tracker", "fov_x0", trk.fov_x0, "fov_x1", trk.fov_x1, "edge_margin", trk.edge_margin, "decide_x",
                 trk.decide_x, "tail_x", trk.tail_x, "gate_s", trk.gate_s, "gate_y", trk.gate_y, "min_hits",
                 trk.min_hits, "max_miss_new", trk.max_miss_new, "max_miss", trk.max_miss, "min_conf", trk.min_conf,
                 "pick_unclassified", trk.pick_unclassified, "unclassified_bin", trk.unclassified_bin,
                 "spawn_before_decide", trk.spawn_before_decide);
        PickerConfig &pk = cfg.picker;
        if (const json *c = sec_gets(j, "picker", "pick_x", pk.pick_x, "pick_tol", pk.pick_tol, "pick_after_fov_mm",
                                     cfg.pick_after_fov, "reach_y0", pk.reach_y0, "reach_y1", pk.reach_y1,
                                     "brake_margin", pk.brake_margin, "finger_w", pk.finger_w, "finger_t",
                                     pk.finger_t, "grip_clear", pk.grip_clear, "max_open", pk.max_open,
                                     "round_ratio", pk.round_ratio, "neighbor_gap", pk.neighbor_gap, "keep_wrist",
                                     pk.keep_wrist, "inner_y", pk.inner_y))
            if (const json *fl = ji::sec(*c, "flying")) {   // intercept picks (docs/运动控制与分拣.md 拦截式不停带夹取)
                ji::gets(*fl, "enabled", pk.flying, "r0", pk.flying_r0, "r1", pk.flying_r1, "ready_s",
                         pk.flying_ready_s, "window_mm", pk.flying_window, "gap_mm", pk.flying_gap, "close_lead_s", pk.flying_close_lead_s);
                float ax_deg = pk.flying_axis * 180.f / kPi; ji::get(*fl, "axis_deg", ax_deg); pk.flying_axis = ax_deg * kPi / 180.f;
            }
        ArmConfig &a = cfg.arm;
        if (const json *c = sec_gets(j, "arm", "base_h", a.base_h, "l_upper", a.l_upper, "l_fore", a.l_fore,
                                     "l_wrist", a.l_wrist, "l_tool", a.l_tool)) {
            ji::vec2(*c, "base_xy", a.base_xy);
            float yaw_deg = a.base_yaw * 180.f / kPi, g0_deg = a.grip_axis_at_zero * 180.f / kPi, side_deg = a.servo_side * 180.f / kPi;
            ji::gets(*c, "base_yaw_deg", yaw_deg, "grip_axis_at_zero_deg", g0_deg, "tool_offset_mm", a.tool_offset, "servo_side_deg", side_deg);
            a.base_yaw = yaw_deg * kPi / 180.f; a.grip_axis_at_zero = g0_deg * kPi / 180.f; a.servo_side = side_deg * kPi / 180.f;
            ji::get(*c, "tilt_comp_mm", a.tilt_comp_mm);   // tilted grasps aim farther out (ArmConfig::tilt_comp_mm)
            if (c->contains("near_comp")) {   // [[radius mm, mm farther out], ...] for vertical grasps near the base
                a.near_comp.clear();
                for (const auto &e : (*c)["near_comp"])
                    if (e.size() == 2) a.near_comp.push_back({e[0].get<float>(), e[1].get<float>()});
                std::sort(a.near_comp.begin(), a.near_comp.end(),
                          [](const std::array<float, 2> &x, const std::array<float, 2> &y) { return x[0] < y[0]; });
            }
            ji::gets(*c, "belt_z", a.belt_z, "max_tilt_deg", a.max_tilt_deg, "tilt_step_deg", a.tilt_step_deg,
                     "servo_in_min_r_mm", a.servo_in_min_r, "grip_open_count", a.grip_open_count, "grip_closed_count",
                     a.grip_closed_count, "grip_max_open", a.grip_max_open, "grip_squeeze", a.grip_squeeze);
            if (c->contains("grip_table")) {   // [[count, mm], ...] measured on the gripper
                a.grip_table.clear();
                for (const auto &e : (*c)["grip_table"]) if (e.size() == 2) a.grip_table.push_back({e[0].get<float>(), e[1].get<float>()});
                a.grip_sorted = false;  // no longer the sorted table the arm cached
            }
            ji::gets(*c, "z_safe", a.z_safe, "z_grasp", a.z_grasp, "joint_speed", a.joint_speed, "yaw_speed", a.yaw_speed,
                     "min_ms", a.min_ms, "descend_ms", a.descend_ms, "close_ms", a.close_ms, "open_ms", a.open_ms);
            ji::get(*c, "inner_q5_deg", cfg.picker.inner_q5);   // the inner picks' #5 (PickerConfig::inner_q5)
            if (c->contains("joints"))
                for (size_t i = 0; i < (*c)["joints"].size() && i < 6; i++) {
                    JointCal &jc = a.joint[i]; int id = jc.id ? jc.id : int(i + 1); ji::get((*c)["joints"][i], "id", id); jc.id = uint8_t(id);
                    ji::gets((*c)["joints"][i], "zero_count", jc.zero_count, "counts_per_deg", jc.counts_per_deg,
                             "min_deg", jc.min_deg, "max_deg", jc.max_deg);
                }
            if (c->contains("home_deg")) for (int i = 0; i < 5; i++) a.home_deg[i] = (*c)["home_deg"].at(i).get<float>();
            if (c->contains("bins_deg"))
                for (const json &b : (*c)["bins_deg"]) {
                    std::array<float, 5> q{};
                    for (int i = 0; i < 5; i++) q[i] = b.at(i).get<float>();
                    a.bins.push_back(q);
                }
        }
        ji::gets(j, "bins", cfg.bins, "outlets", cfg.outlets, "label_list", cfg.label_list); FlapConfig &fcp = cfg.flaps;
        if (const json *c = sec_gets(j, "flaps", "enabled", fcp.enabled, "move_ms", fcp.move_ms, "stagger_ms",
                                     fcp.stagger_ms, "margin_mm", fcp.margin_mm)) {
            pk.gates_on = fcp.enabled;   // gates stay loaded (the [run] startup print) but are only trusted once mounted
            if (c->contains("gates")) {   // the single gate table: the picker and the flap sequencer share it
                pk.gates.clear();
                for (const json &gj : (*c)["gates"]) {
                    Gate g;
                    // "outlet": one name, or a list when one paddle serves several outlets (11-01: one gate for
                    // scrap + rework, both into the same bin)
                    std::vector<std::string> outlet_names{"scrap"};
                    if (gj.contains("outlet")) {
                        const json &o = gj["outlet"];
                        if (o.is_string()) outlet_names = {o.get<std::string>()};
                        else if (o.is_array()) {
                            outlet_names.clear();
                            for (const json &n : o) {
                                if (!n.is_string()) return path + ": flaps.gates outlet list: names only";
                                outlet_names.push_back(n.get<std::string>());
                            }
                        } else return path + ": flaps.gates outlet: a name or a list of names";
                    }
                    if (outlet_names.empty()) return path + ": flaps.gates outlet list is empty";
                    float closed_deg = rad2deg(g.geom.closed), open_deg = rad2deg(g.geom.open);
                    ji::gets(gj, "len_mm", g.geom.len, "closed_deg", closed_deg, "open_deg", open_deg, "hold_mm",
                             g.hold_mm, "open_us", g.open_us, "closed_us", g.closed_us, "sweep_ms", g.sweep_ms,
                             "sweep_after_mm", g.sweep_after_mm, "sweep_tail", g.sweep_tail);
                    // a sweep-out release: range check, and the planning time of the slow opening (like
                    // FlapConfig::move_s for the normal one)
                    auto release_ok = [&](GateRelease &R) -> const char * {
                        if (R.ms < 0 || R.ms > 10000 || R.after_mm < 0)
                            return "sweep_ms 0..10000 (0 = off), sweep_after_mm >= 0";
                        if (!(R.tail >= 0.5f && R.tail <= 0.95f))
                            return "sweep_tail 0.5..0.95 (share of sweep_ms for the second half)";
                        R.s = R.ms > 0 ? (R.ms + fcp.stagger_ms) / 1000.f + 0.04f : 0.f;
                        return nullptr;
                    };
                    GateRelease def{g.sweep_ms, g.sweep_after_mm, g.sweep_tail};
                    if (const char *bad = release_ok(def)) return path + ": flaps.gates: " + bad;
                    g.sweep_s = def.s;
                    ji::vec2(gj, "pivot_xy", g.geom.pivot);
                    g.geom.closed = deg2rad(closed_deg);
                    g.geom.open = deg2rad(open_deg);
                    if (std::fabs(closed_deg - open_deg) >= 180)
                        return path + ": flaps.gates: |closed_deg - open_deg| must be < 180 (convex swept sector)";
                    for (const std::string &name : outlet_names) {
                        const int oi = index_of(cfg.outlets, name);
                        if (oi < 0) return path + ": flaps.gates outlet \"" + name + "\" is not in outlets";
                        if (oi == 0) return path + ": flaps.gates: the recheck outlet is the belt end, not a gate";
                        if (oi >= 16) return path + ": flaps.gates outlet \"" + name + "\": at most 16 outlets";
                        if (g.outlets == 0) g.outlet = int8_t(oi);   // the first name
                        g.outlets = uint16_t(g.outlets | (1u << oi));
                    }
                    if (outlet_names.size() == 1) g.outlets = 0;   // single outlet: the plain form, as before
                    // "by_outlet": a release of its own for some of these outlets, e.g. {"rework": {"sweep_after_mm":
                    // 15, "sweep_ms": 2000, "sweep_tail": 0.5}}; fields left out are the gate's, "sweep_ms": 0 = slide
                    // off along the closed paddle. Another release = another drop point along the far edge, so a
                    // bin per class (11-01 user: rework swept out earlier, further upstream than scrap).
                    if (gj.contains("sweep_outlets"))
                        return path + ": flaps.gates sweep_outlets: replaced by by_outlet {\"<outlet>\": {\"sweep_ms\": ..}}";
                    if (gj.contains("by_outlet")) {
                        const json &bo = gj["by_outlet"];
                        if (!bo.is_object() || bo.empty())
                            return path + ": flaps.gates by_outlet: {\"<outlet>\": {\"sweep_ms\" / \"sweep_after_mm\" / "
                                          "\"sweep_tail\"}, ...}";
                        for (auto it = bo.begin(); it != bo.end(); ++it) {
                            const int oi = index_of(cfg.outlets, it.key());
                            if (oi <= 0 || oi >= 16 || !g.serves(oi))
                                return path + ": flaps.gates by_outlet: \"" + it.key() + "\" is not an outlet of this gate";
                            if (!it.value().is_object())
                                return path + ": flaps.gates by_outlet \"" + it.key() + "\": an object";
                            GateRelease R = def;
                            ji::gets(it.value(), "sweep_ms", R.ms, "sweep_after_mm", R.after_mm, "sweep_tail", R.tail);
                            if (const char *bad = release_ok(R))
                                return path + ": flaps.gates by_outlet \"" + it.key() + "\": " + bad;
                            g.rel[size_t(oi)] = R;
                            g.rel_mask = uint16_t(g.rel_mask | (1u << oi));
                        }
                    }
                    pk.gates.push_back(g);
                }
                if (pk.gates.size() > 2) return path + ": flaps.gates: at most 2 (des_pwm has 2 channels)";
            }
        }
        if (const json *c = ji::sec(j, "plc")) ji::get(*c, "modbus_port", cfg.plc.modbus_port);
        BeamConfig &bm = cfg.beam;   // x_mm comes from calib.json (des_tool calib-beam); a hand-set one also counts
        if (const json *c = sec_gets(j, "beam", "enabled", bm.enabled, "match_mm", bm.match_mm, "expire_mm", bm.expire_mm,
                                     "miss_mm", bm.miss_mm))
            if (c->contains("x_mm")) { ji::get(*c, "x_mm", bm.x_mm); bm.calibrated = true; }
        Rs485Config &rs = cfg.rs485;
        if (const json *c = sec_gets(j, "rs485", "enabled", rs.enabled, "baud", rs.baud, "timeout_ms", rs.timeout_ms,
                                     "addr", rs.addr, "coils", rs.coils, "poll_ms", rs.poll_ms, "buzzer_s", rs.buzzer_s, "fault_s", rs.fault_s,
                                     "inputs", rs.inputs)) {
            std::string idle = rs.idle == IDLE_GREEN ? "green" : rs.idle == IDLE_BLINK ? "blink" : "off";
            ji::get(*c, "idle", idle);
            if (idle == "off") rs.idle = IDLE_OFF;
            else if (idle == "green") rs.idle = IDLE_GREEN;
            else if (idle == "blink") rs.idle = IDLE_BLINK;
            else return path + ": rs485.idle must be off / green / blink";
            std::string par(1, rs.parity); ji::get(*c, "parity", par);
            if (par != "N" && par != "E" && par != "O") return path + ": rs485.parity must be N / E / O";
            rs.parity = par[0];
            if (c->contains("lamp_coils")) {   // relay of [green, yellow, red, buzzer], -1 = not wired
                const json &lc = (*c)["lamp_coils"];
                if (lc.size() != 4) return path + ": rs485.lamp_coils needs 4 numbers (green, yellow, red, buzzer)";
                for (int k = 0; k < 4; k++) rs.lamp_coil[k] = lc[k].get<int>();
            }
            if (rs.addr < 1 || rs.addr > 247) return path + ": rs485.addr must be 1..247";
            if (rs.coils < 1 || rs.coils > 32) return path + ": rs485.coils must be 1..32";
        }
        NpuConfig &np = cfg.npu; sec_gets(j, "npu", "enabled", np.enabled, "model", np.model, "core", np.core); StorageConfig &st = cfg.storage;
        if (sec_gets(j, "storage", "enabled", st.enabled, "dir", st.dir, "images", st.images, "jpeg_quality",
                     st.jpeg_quality, "max_queue", st.max_queue, "feed_port", st.feed_port)) {
            // 相对落盘目录按 des_app.json 所在目录解析（同 label_list / recipes / calib / npu.model）：生产 → /root/des/data/log
            st.dir = resolve(dir, st.dir);
            if (st.images != "none" && st.images != "ng" && st.images != "all")
                return path + ": storage.images must be none / ng / all";
        }
        OpsConfig &op = cfg.ops;   // fault detection -> work orders (app/ops.hpp); every key optional
        sec_gets(j, "ops", "enabled", op.enabled, "utc_offset_h", op.utc_offset_h, "belt_min_mm_s", op.belt_min_mm_s,
                 "belt_ratio", op.belt_ratio, "belt_s", op.belt_s, "light_low", op.light_low,
                 "light_s", op.light_s, "light_learn_s", op.light_learn_s, "pick_fails", op.pick_fails, "link_s",
                 op.link_s, "min_fps", op.min_fps, "min_free_mb", op.min_free_mb, "rate_window", op.rate_window,
                 "rate_min", op.rate_min, "ng_rate", op.ng_rate, "recheck_rate", op.recheck_rate, "od_window",
                 op.od_window, "od_drift", op.od_drift, "event_clear_s", op.event_clear_s);
        if (op.rate_window < 1 || op.rate_min < 1 || op.od_window < 0) return path + ": ops windows must be >= 1";
        TrackMeasureConfig &t = cfg.measure;
        if (const json *c = sec_gets(j, "measure", "enabled", t.enabled, "per_frame", t.per_frame, "gear_ratio",
                                     t.gear_ratio, "margin_mm", t.margin_mm, "improve", t.improve)) {
            // after "outlets": the size rule names its outlet
            MeasureConfig &m = t.m;
            if (c->contains("axis_table")) { ji::vec2(*c, "axis_table", m.axis_table); t.axis_from_image = false; }
            ji::gets(*c, "camera_h_mm", m.camera_h_mm, "gear_h_mm", m.gear_h_mm,
                     "walls_visible", m.walls_visible, "max_smear_mm", m.max_smear_mm, "judge_max_dist_mm", m.judge_max_dist_mm,
                     "short_tooth_mm",
                     m.short_tooth_mm, "flash_mm", m.flash_mm, "size_gain", m.size_gain, "od_offset_mm",
                     m.od_offset_mm, "bore_offset_mm", m.bore_offset_mm, "bore_near_arc", m.bore_near_arc);
            if (c->contains("colors"))
                for (const json &cj : (*c)["colors"]) {
                    ColorClass k; ji::gets(cj, "name", k.name, "hue_lo", k.hue_lo, "hue_hi", k.hue_hi, "sat_lo", k.sat_lo, "sat_hi", k.sat_hi);
                    t.colors.push_back(k);
                }
            if (const json *r = ji::sec(*c, "rule")) {
                SizeRule &s = cfg.tracker.size; get_spec(*r, s);
                ji::gets(*r, "enabled", s.enabled, "defect_recheck", s.defect_recheck, "unjudged_recheck", s.unjudged_recheck,
                         "unjudged_pick_conf", s.unjudged_pick_conf, "require", s.require);
                if (s.unjudged_pick_conf < 0 || s.unjudged_pick_conf > 1)
                    return path + ": measure.rule.unjudged_pick_conf 0..1 (0 = off)";
                std::string outlet = "scrap"; ji::get(*r, "ng_outlet", outlet); s.ng_outlet = index_of(cfg.outlets, outlet);
                if (s.ng_outlet < 0) return path + ": measure.rule.ng_outlet \"" + outlet + "\" is not in outlets";
            }
        }
    } catch (const std::exception &e) {
        return path + ": " + e.what();
    }
    for (size_t i = 0; i < 6; i++) if (!cfg.arm.joint[i].id) cfg.arm.joint[i].id = uint8_t(i + 1);
    cfg.config_dir = dir; ji::get(j, "calib_file", cfg.calib_file); const std::string cp = resolve(dir, cfg.calib_file);
    if (std::ifstream cf{cp}) {   // calib.json (calib-cam / -belt / -enc / -arm) merged over this file; other sections kept
        try {
            json c; cf >> c;
            if (c.contains("px_to_table") && c["px_to_table"].size() == 9)
                for (int i = 0; i < 9; i++) cfg.px_to_table.h[i] = c["px_to_table"][i].get<double>();
            if (c.contains("tracker")) ji::gets(c["tracker"], "fov_x0", cfg.tracker.fov_x0, "fov_x1", cfg.tracker.fov_x1);
            if (c.contains("belt")) { ji::gets(c["belt"], "mm_per_step", cfg.belt.mm_per_step, "dir", cfg.belt.dir); cfg.belt_calibrated = true; }
            if (c.contains("encoder"))
                ji::gets(c["encoder"], "mm_per_count", cfg.belt.enc_mm_per_count, "dir", cfg.belt.enc_dir);
            if (c.contains("arm")) {   // overrides arm.base_xy / base_yaw_deg / belt_z
                const json &ac = c["arm"]; ji::vec2(ac, "base_xy", cfg.arm.base_xy);
                if (ac.contains("base_yaw_deg")) cfg.arm.base_yaw = ac["base_yaw_deg"].get<float>() * kPi / 180.f;
                ji::get(ac, "belt_z", cfg.arm.belt_z);
            }
            if (c.contains("beam") && c["beam"].contains("x_mm")) {   // des_tool calib-beam
                ji::get(c["beam"], "x_mm", cfg.beam.x_mm); cfg.beam.calibrated = true;
            }
            cfg.calib_loaded = cp;
        } catch (const std::exception &e) {
            return cp + ": " + e.what();
        }
    }
    ji::get(j, "flat_file", cfg.flat_file);
    if (!cfg.flat_file.empty()) {   // flat field for the part crops (des_tool calib-flat); missing = raw crops
        const std::string fp = resolve(dir, cfg.flat_file);
        if (std::ifstream ff{fp}) {
            auto flat = std::make_shared<FlatField>(); std::string e = load_flat(fp, *flat);
            if (!e.empty()) return e;
            cfg.measure.flat = flat; cfg.locate.flat = flat; cfg.flat_loaded = fp;
        }
    }
    {   // ready pose taught on the HMI arm page (「设为初始姿态」): counts of the six servos, over arm.home_deg
        if (std::ifstream hf{cfg.config_dir + "/arm_home.json"}) {
            try {
                json h; hf >> h;
                if (h.contains("counts") && h["counts"].size() == 6) {
                    for (int i = 0; i < 5; i++)
                        cfg.arm.home_deg[i] = (h["counts"][i].get<float>() - cfg.arm.joint[i].zero_count) /
                                              cfg.arm.joint[i].counts_per_deg;
                    cfg.arm.home_grip_count = h["counts"][5].get<float>(); cfg.arm_home_taught = true;
                }
            } catch (const std::exception &e) {
                return cfg.config_dir + "/arm_home.json: " + e.what();
            }
        }
    }
    // recipes (after measure.rule = the fallback recipe 1)
    ji::gets(j, "recipe", cfg.recipe, "recipes_file", cfg.recipes_file);
    {
        const std::string rp = resolve(dir, cfg.recipes_file); std::ifstream rf(rp); cfg.recipes.clear();
        if (!rf) {
            cfg.recipes.push_back(Recipe::from_rule(cfg.tracker.size, 1, "默认（des_app.json measure.rule）"));
        } else {
            try {
                json rj; rf >> rj;
                for (const json &e : rj.at("recipes")) {
                    Recipe r = Recipe::from_rule(cfg.tracker.size, 0, "");  // missing keys keep measure.rule values
                    ji::gets(e, "id", r.id, "name", r.name); get_spec(e, r);
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
        const MeasureConfig &m = cfg.measure.m; const Vec2 axis = cfg.measure.axis_from_image ? cfg.px_to_table.map({960.f, 540.f}) : m.axis_table;
        cfg.px_to_part = part_homography(cfg.px_to_table, m, axis);
    }
    if (cfg.pick_after_fov > 0) cfg.picker.pick_x = cfg.tracker.fov_x1 + cfg.pick_after_fov;  // after calib.json
    cfg.beam.fov_x0 = cfg.tracker.fov_x0;
    cfg.picker.tail_x = cfg.tracker.tail_x; cfg.picker.recheck_outlet = 0; cfg.picker.arm_base = cfg.arm.base_xy;
    // after calib.json (calib-arm may have moved it)
    cfg.picker.tangential_first = cfg.arm.max_tilt_deg > 0;
    if (!(cfg.arm.tilt_comp_mm >= 0 && cfg.arm.tilt_comp_mm <= 30))
        return path + ": arm.tilt_comp_mm must be 0 .. 30 (mm farther out for tilted grasps)";
    for (const auto &e : cfg.arm.near_comp)
        if (!(e[0] > 0 && e[1] >= -20 && e[1] <= 20))
            return path + ": arm.near_comp wants [[radius mm > 0, -20 .. 20 mm], ...]";
    // round parts close along the ready pose's wrist line (after arm_home.json: the taught #5)
    if (cfg.picker.keep_wrist) arm_home_wrist(cfg.arm, cfg.picker.wrist_rel, cfg.picker.wrist_tau);
    else cfg.picker.wrist_rel = NAN;
    // inner picks (picker.inner_y + arm.inner_q5_deg, 11-01 user): the wrist shown at the closest acceptable point
    if (!std::isnan(cfg.picker.inner_y) || !std::isnan(cfg.picker.inner_q5)) {
        const JointCal &j5 = cfg.arm.joint[4];
        if (std::isnan(cfg.picker.inner_y) || std::isnan(cfg.picker.inner_q5))
            return path + ": picker.inner_y and arm.inner_q5_deg go together";
        if (!(cfg.picker.inner_q5 >= j5.min_deg && cfg.picker.inner_q5 <= j5.max_deg))
            return path + ": arm.inner_q5_deg outside joint 5's min_deg..max_deg";
        cfg.picker.inner_rel = deg2rad(cfg.picker.inner_q5) + cfg.arm.grip_axis_at_zero;
        cfg.picker.inner_tau = cfg.arm.tool_offset * std::sin(cfg.picker.inner_rel + cfg.arm.servo_side);
    }
    cfg.picker.gate_move_s = cfg.flaps.move_s(); cfg.picker.gate_margin = cfg.flaps.margin_mm;
    cfg.npu.model = resolve(dir, cfg.npu.model); return load_rules(resolve(dir, cfg.label_list), cfg);
}
}  // namespace des
