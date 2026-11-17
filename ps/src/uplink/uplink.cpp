// uplink.cpp — see uplink.hpp
#include "uplink.hpp"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <time.h>
#include <unistd.h>

#include "can_proto.h"
#include "json.hpp"
#include "modbus_tcp.hpp"
#include "sha256.hpp"

namespace des {
namespace {

using nlohmann::json;
double mono()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
std::string dump(const json &j) { return j.dump(-1, ' ', false, json::error_handler_t::replace); }
template <class T> void opt(const json &j, const char *key, T &out)
{
    const auto it = j.find(key);
    if (it != j.end() && !it->is_null()) out = it->get<T>();
}

}  // namespace

std::string load_uplink_config(const std::string &path, UplinkConfig &c)
{
    std::ifstream f(path);
    if (!f) return "cannot open " + path;
    std::stringstream ss;
    ss << f.rdbuf();
    try {
        const json j = json::parse(ss.str(), nullptr, true, true);   // comments allowed
        if (!j.is_object()) return path + ": not a JSON object";
        if (const auto m = j.find("mqtt"); m != j.end() && m->is_object()) {
            opt(*m, "host", c.mqtt.host); opt(*m, "port", c.mqtt.port); opt(*m, "client_id", c.mqtt.client_id);
            opt(*m, "user", c.mqtt.user); opt(*m, "pass", c.mqtt.pass); opt(*m, "keepalive_s", c.mqtt.keepalive_s);
            opt(*m, "apn", c.mqtt.apn); opt(*m, "cid", c.mqtt.cid);
        }
        opt(j, "topic_prefix", c.prefix); opt(j, "state_period_s", c.state_period_s); opt(j, "state_min_s", c.state_min_s);
        opt(j, "idle_period_s", c.idle_period_s); opt(j, "mode", c.mode); opt(j, "eth_if", c.eth_if);
        if (const auto b = j.find("board"); b != j.end() && b->is_object()) {
            opt(*b, "host", c.board_host); opt(*b, "feed_port", c.feed_port); opt(*b, "modbus_port", c.modbus_port);
            opt(*b, "log_dir", c.log_dir);
        }
        if (const auto m = j.find("modem"); m != j.end() && m->is_object()) {
            std::string vid;
            opt(*m, "vid", vid); opt(*m, "usb_if", c.usb_if); opt(*m, "tty", c.tty);
            if (!vid.empty()) c.vid = unsigned(strtoul(vid.c_str(), nullptr, 16));
        }
        if (const auto m = j.find("parts"); m != j.end() && m->is_object()) {
            opt(*m, "keep", c.parts_keep); opt(*m, "image_kb", c.image_kb); opt(*m, "min_s", c.parts_min_s);
        }
        if (const auto m = j.find("commands"); m != j.end() && m->is_object()) {
            opt(*m, "key", c.cmd_key); opt(*m, "allow", c.cmd_allow); opt(*m, "speeds", c.speeds);
            if (const auto r = m->find("recipes"); r != m->end() && r->is_array())
                for (const json &e : *r) {
                    RecipeName n;
                    if (e.is_object()) { opt(e, "id", n.id); opt(e, "name", n.name); }
                    c.recipes.push_back(n);
                }
        }
    } catch (const std::exception &e) {
        return path + ": " + e.what();
    }
    if (c.mqtt.host.empty()) return path + ": mqtt.host is empty";
    if (c.mqtt.port < 1 || c.mqtt.port > 65535) return path + ": mqtt.port out of range";
    if (c.mqtt.keepalive_s < 0 || c.mqtt.keepalive_s > 3600) return path + ": mqtt.keepalive_s must be 0..3600";
    if (c.mqtt.cid < 1 || c.mqtt.cid > 16) return path + ": mqtt.cid must be 1..16";
    if (c.prefix.empty() || c.prefix.size() > 64 || c.prefix.find_first_of(" \"#+\r\n") != std::string::npos || c.prefix.back() == '/')
        return path + ": topic_prefix must be 1..64 characters without space, quote, # or +, not ending in /";
    for (const std::string *s : {&c.mqtt.host, &c.mqtt.client_id, &c.mqtt.user, &c.mqtt.pass, &c.mqtt.apn})
        if (s->find('"') != std::string::npos) return path + ": mqtt strings cannot contain a double quote";
    if (c.state_period_s < 1 || c.state_min_s < 1 || c.idle_period_s < 1) return path + ": periods must be >= 1 s";
    if (c.mode != "always" && c.mode != "backup") return path + ": mode must be \"always\" or \"backup\"";
    if (c.modbus_port < 0 || c.modbus_port > 65535) return path + ": board.modbus_port out of range";
    if (!c.cmd_key.empty() && c.cmd_key.size() < 8) return path + ": commands.key must be at least 8 characters";
    if (!c.cmd_key.empty() && c.modbus_port <= 0) return path + ": commands need board.modbus_port";
    for (const std::string &a : c.cmd_allow)
        if (a != "ack" && a != "stop" && a != "pause" && a != "resume" && a != "start" && a != "reset" && a != "speed" && a != "recipe")
            return path + ": commands.allow has an unknown command \"" + a + "\"";
    if (c.speeds.size() > 8 || c.recipes.size() > 8) return path + ": commands.speeds / recipes: at most 8 each";
    for (const double v : c.speeds)
        if (!(v >= 1 && v <= 200)) return path + ": commands.speeds must be 1..200 mm/s (des_app's limit)";
    for (const RecipeName &r : c.recipes) {
        if (r.id < 1 || r.id > 65535) return path + ": commands.recipes needs {\"id\": 1..65535, \"name\": ...}";
        if (r.name.empty() || r.name.size() > 48) return path + ": commands.recipes: name must be 1..48 bytes";
    }
    if (c.parts_keep < 0 || c.parts_keep > 8) return path + ": parts.keep must be 0..8 (one message of at most 1400 bytes)";
    if (c.image_kb < 0 || c.image_kb > 64) return path + ": parts.image_kb must be 0..64";
    if (!(c.parts_min_s >= 0.2 && c.parts_min_s <= 60)) return path + ": parts.min_s must be 0.2..60";
    return "";
}

std::string base64(const std::string &bytes)
{
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const uint32_t a = uint8_t(bytes[i]), b = i + 1 < bytes.size() ? uint8_t(bytes[i + 1]) : 0,
                       c = i + 2 < bytes.size() ? uint8_t(bytes[i + 2]) : 0, v = a << 16 | b << 8 | c;
        out += t[v >> 18 & 63];
        out += t[v >> 12 & 63];
        out += i + 1 < bytes.size() ? t[v >> 6 & 63] : '=';
        out += i + 2 < bytes.size() ? t[v & 63] : '=';
    }
    return out;
}

std::string uplink_parts_json(const std::string &session, const std::vector<PartInfo> &parts, double up_now, double board_now,
                              size_t max_bytes)
{
    auto r1 = [](double v, double k) { return std::round(v * k) / k; };
    json a = json::array();
    for (const PartInfo &p : parts) {
        json e = {{"k", p.track}, {"u", r1(up_now - (board_now - p.rx), 10)}, {"l", p.label}, {"c", r1(p.conf, 1000)}, {"d", p.disp},
                  {"o", p.dest}};
        if (p.size_mm > 0) e["m"] = r1(p.size_mm, 100);
        if (p.teeth > 0) { e["z"] = p.teeth; if (p.missing > 0) e["x"] = p.missing; }
        if (!p.why.empty()) e["w"] = p.why.substr(0, 40);
        if (!p.image.empty()) e["i"] = p.image;
        a.push_back(e);
    }
    for (;;) {
        const std::string s = dump(json{{"ses", session}, {"u", r1(up_now, 10)}, {"p", a}});
        if (s.size() <= max_bytes || a.empty()) return s;
        a.erase(a.begin());   // the oldest goes first
    }
}

std::vector<std::string> uplink_image_chunks(const std::string &name, const std::string &jpeg, size_t chunk)
{
    std::vector<std::string> out;
    if (jpeg.empty() || chunk == 0) return out;
    const size_t n = (jpeg.size() + chunk - 1) / chunk;
    for (size_t k = 0; k < n; k++)
        out.push_back(dump(json{{"f", name}, {"k", k}, {"n", n}, {"d", base64(jpeg.substr(k * chunk, chunk))}}));
    return out;
}

int eth_carrier(const std::string &ifname)
{
    std::ifstream f("/sys/class/net/" + ifname + "/carrier");
    char c = 0;
    if (!(f >> c)) return -1;   // no such interface, or it is administratively down
    return c == '1' ? 1 : 0;
}

std::string uplink_state_json(const BoardSnapshot &b, const ModemInfo &m, int eth, double up_s, const UplinkStats &st,
                              const UplinkExtras *x)
{
    json j = {{"online", true}, {"up_s", std::llround(up_s)}, {"app", b.alive()}, {"session", b.session},
              {"total", b.total}, {"ok", b.ok}, {"ng", b.ng}, {"recheck", b.recheck},
              {"wo", {{"active", b.wo_active}, {"unacked", b.wo_unacked}, {"level", b.wo_level}, {"id", b.wo_top_id},
                      {"title", b.wo_top_title}}},
              {"eth", eth},
              {"net", {{"csq", m.csq}, {"dbm", m.dbm()}, {"oper", m.oper}, {"act", m.act}}},
              {"tx", {{"ok", st.published}, {"fail", st.failed}, {"conn", st.reconnects}, {"img", st.images}}}};
    if (b.total)
        j["last"] = {{"label", b.last_label}, {"disp", b.last_disp}, {"dest", b.last_dest},
                     {"conf", std::round(b.last_conf * 1000) / 1000}, {"size_mm", std::round(b.last_size_mm * 100) / 100}};
    j["counts"] = "feed";
    if (x && x->line.valid) {   // des_app's own registers: its counters replace the ones summed from the feed
        const LineStatus &l = x->line;
        j["line"] = {{"state", l.state}, {"speed", std::round(l.speed * 10) / 10}, {"alarm", l.alarm}, {"recipe", l.recipe}};
        if (l.set_speed > 0) j["line"]["set"] = std::round(l.set_speed * 10) / 10;
        j["total"] = l.total; j["ok"] = l.ok; j["ng"] = l.ng; j["recheck"] = l.recheck;
        j["counts"] = "app";
    }
    if (x && !x->cmds.empty()) { j["cmds"] = x->cmds; j["nonce"] = x->nonce; j["seq"] = x->last_seq; }
    if (x) {   // environment and board health (board_env.hpp); a field is left out while it is unknown
        const BoardEnv &e = x->env;
        if (e.env) j["env"] = {{"t", std::round(e.temp_c * 10) / 10}, {"h", std::round(e.hum_rh * 10) / 10}, {"age", std::llround(e.env_age_s)}};
        if (e.vib) j["vib"] = {{"rms", std::round(e.vib_rms_g * 1000) / 1000}, {"hz", std::llround(e.vib_peak_hz)}, {"age", std::llround(e.vib_age_s)}};
        json s = json::object();
        if (e.cpu_pct >= 0) s["cpu"] = e.cpu_pct;
        if (e.mem_avail_pct >= 0) s["mem"] = e.mem_avail_pct;
        if (e.disk_free_mb >= 0) { s["disk_mb"] = std::llround(e.disk_free_mb); s["disk"] = e.disk_free_pct; }
        if (e.board_up_s >= 0) s["up"] = std::llround(e.board_up_s);
        if (!s.empty()) j["sys"] = s;
    }
    return dump(j);
}

std::string uplink_meta_json(const UplinkConfig &c)
{
    const auto allowed = [&](const char *k) { return std::find(c.cmd_allow.begin(), c.cmd_allow.end(), k) != c.cmd_allow.end(); };
    json j = json::object();
    if (allowed("speed") && !c.speeds.empty()) j["speeds"] = c.speeds;
    if (allowed("recipe") && !c.recipes.empty()) {
        json a = json::array();
        for (const RecipeName &r : c.recipes) a.push_back({{"id", r.id}, {"name", r.name}});
        j["recipes"] = a;
    }
    return dump(j);
}

std::string uplink_cmd_text(const std::string &nonce, long long seq, const std::string &cmd, long long arg)
{
    return nonce + "|" + std::to_string(seq) + "|" + cmd + "|" + std::to_string(arg);
}

std::string uplink_fit_order(const std::string &text, size_t max_bytes)
{
    if (text.size() <= max_bytes) return text;
    json j = json::parse(text, nullptr, false);
    if (!j.is_object()) return text.substr(0, max_bytes);
    for (const char *k : {"advice", "detail", "session", "acked_by", "status_cn", "level_cn", "device", "title"}) {
        j.erase(k);
        const std::string s = dump(j);
        if (s.size() <= max_bytes) return s;
    }
    return dump(json{{"id", j.value("id", 0)}, {"code", j.value("code", 0)}, {"level", j.value("level", 0)}});
}

Uplink::Uplink(const UplinkConfig &c, AtChan &at, BoardLink &board) : cfg_(c), at_(at), board_(board), q_(at), env_reader_(c.log_dir)
{
    eth = [this]() { return eth_carrier(cfg_.eth_if); };
    clock = mono;
    t0_ = -1;
    uint8_t r[8] = {0};   // nonce: new at every start, so commands signed for an earlier run are dead
    if (std::ifstream u("/dev/urandom", std::ios::binary); u) u.read(reinterpret_cast<char *>(r), sizeof r);
    x_.nonce = hex_of(r, sizeof r);
}

const char *Uplink::phase_name() const
{
    static const char *const n[] = {"init", "sim", "register", "data", "mqtt", "up"};
    return n[int(phase_)];
}

void Uplink::go(Phase p, double delay_s)
{
    phase_ = p;
    phase_since_ = clock();
    not_before_ = phase_since_ + delay_s;
    quiet_cmds_ = 0;
}

void Uplink::fail(const std::string &why)
{
    say(std::string(phase_name()) + ": " + why + "; next try in " + std::to_string(int(backoff_)) + " s");
    not_before_ = clock() + backoff_;
    backoff_ = backoff_ * 2 > 60 ? 60 : backoff_ * 2;
    if (++quiet_cmds_ >= 5 && phase_ != Phase::Init) {   // stuck: start over from "does the module answer at all"
        phase_ = Phase::Init;
        quiet_cmds_ = 0;
    }
}

bool Uplink::publish(const std::string &topic, const std::string &payload, bool retain)
{
    const std::string e = q_.mqtt_pub(topic, payload, 1, retain);
    if (e.empty()) { st_.published++; pub_fails_ = 0; return true; }
    st_.failed++;
    say("publish " + topic + " failed: " + e);
    if (++pub_fails_ >= 2) link_down_ = true;
    return false;
}

void Uplink::handle_cmd(const std::string &payload)
{
    const json j = json::parse(payload, nullptr, false);
    const std::string cmd = j.is_object() ? j.value("cmd", std::string()) : std::string();
    const long long id = j.is_object() && j.contains("id") && j["id"].is_number_integer() ? j["id"].get<long long>() : 0;
    st_.commands++;
    json r = {{"id", id}, {"cmd", cmd}};
    if (cmd == "ping") { r["ok"] = true; r["up_s"] = std::llround(clock() - t0_); }
    else if (cmd.empty()) { r["ok"] = false; r["err"] = "bad command"; }
    else {
        auto num = [&](const char *k) { return j.contains(k) && j[k].is_number_integer() ? j[k].get<long long>() : 0LL; };
        const std::string sig = j.contains("sig") && j["sig"].is_string() ? j["sig"].get<std::string>() : std::string();
        int result = -1;
        const std::string e = run_command(cmd, num("arg"), num("seq"), sig, result);
        r["ok"] = e.empty();
        if (!e.empty()) r["err"] = e;
        if (result >= 0) r["result"] = result;
    }
    say("command \"" + cmd + "\" -> " + (r["ok"].get<bool>() ? "ok" : r["err"].get<std::string>()));
    publish(cfg_.prefix + "/reply", dump(r), false);
}

void Uplink::handle_urcs()
{
    std::string l;
    while (at_.next_urc(l)) {
        const ModemUrc u = Quectel::parse_urc(l);
        if (u.kind == ModemUrc::MqttStat) { say("broker connection lost (module code " + std::to_string(u.code) + ")"); link_down_ = true; }
        else if (u.kind == ModemUrc::MqttPing) say("keep-alive ping failed");
        else if (u.kind == ModemUrc::MqttRecv && u.topic == cfg_.prefix + "/cmd" && phase_ == Phase::Up) handle_cmd(u.payload);
    }
}

void Uplink::up_pass(double now)
{
    at_.pump(100);
    handle_urcs();
    std::string wo;
    for (int n = 0; n < 4 && !link_down_ && board_.next_order(wo); n++)
        if (publish(cfg_.prefix + "/wo", uplink_fit_order(wo, 1400), false)) st_.orders++;
    if (!link_down_ && now - last_info_ >= 30) {
        last_info_ = now;
        q_.refresh(info_);
    }
    if (!link_down_) {
        poll_line(now);
        if (now >= next_env_) { x_.env = env_reader_.read(); next_env_ = now + 5; }   // goes out with the next state
        if (!meta_sent_ && !x_.cmds.empty() && publish(cfg_.prefix + "/meta", uplink_meta_json(cfg_), true)) meta_sent_ = true;
    }
    if (!link_down_) {
        const BoardSnapshot b = board_.snapshot();
        const int e = eth();
        const bool idle = cfg_.mode == "backup" && e == 1;
        bool due = now - last_state_ >= (idle ? cfg_.idle_period_s : cfg_.state_period_s);
        if (e != sent_eth_) due = true;
        if (!idle && (b.seq != sent_seq_ || line_seq_ != sent_line_seq_) && now - last_state_ >= cfg_.state_min_s) due = true;
        if (due) {
            if (sent_eth_ != -2 && e != sent_eth_)
                say(e == 1 ? "wired link is back" : "wired link is down: reporting over 4G only");
            last_state_ = now;
            const uint64_t ls = line_seq_;
            if (publish(cfg_.prefix + "/state", uplink_state_json(b, info_, e, now - t0_, st_, &x_), true)) {
                sent_seq_ = b.seq;
                sent_eth_ = e;
                sent_line_seq_ = ls;
            }
        }
    }
    // the last parts, one retained message whenever parts were decided (a page opened later still sees them)
    if (!link_down_ && cfg_.parts_keep > 0) {
        const uint64_t ps = board_.parts_seq();
        if (ps != sent_parts_seq_ && now - last_parts_ >= cfg_.parts_min_s) {
            last_parts_ = now;
            std::string ses;
            const std::vector<PartInfo> v = board_.recent_parts(ses);
            if (publish(cfg_.prefix + "/parts", uplink_parts_json(ses, v, now - t0_, BoardLink::now()), true)) sent_parts_seq_ = ps;
        }
    }
    // NG pictures: two chunks per pass, so a command or the state waits at most two publishes
    if (!link_down_ && cfg_.parts_keep > 0 && cfg_.image_kb > 0) {
        if (img_k_ >= img_.size()) {
            std::string name, jpeg;
            img_.clear();
            img_k_ = 0;
            if (board_.next_image(name, jpeg)) img_ = uplink_image_chunks(name, jpeg);
        }
        for (int i = 0; i < 2 && img_k_ < img_.size() && !link_down_; i++) {
            if (!publish(cfg_.prefix + "/img/" + std::to_string(img_k_), img_[img_k_], true)) break;
            if (++img_k_ == img_.size()) st_.images++;
        }
    }
    if (link_down_) {
        link_down_ = false;
        pub_fails_ = 0;
        go(Phase::Data, backoff_);   // check the data connection, then open the broker connection again
        backoff_ = backoff_ * 2 > 60 ? 60 : backoff_ * 2;
    }
}

// des_app's input registers, once a second; a change of what the dashboard shows forces a state message
void Uplink::poll_line(double now)
{
    if (!mb_ || mb_off_ || now < next_line_) return;
    next_line_ = now + 1;
    uint16_t ir[MB_N_IR];
    if (!mb_->read(4, 0, MB_N_IR, ir)) {
        if (x_.line.valid) {
            say("des_app registers unreachable (" + mb_->error() + "): state from the feed only, commands off");
            x_.line.valid = false;
            x_.cmds.clear();
            line_seq_++;
        }
        next_line_ = now + 5;
        return;
    }
    if (ir[MB_IR_VERSION] < 0x0103) {   // it counted this request as a PLC: one false "PLC lost" order at most, then silence
        char v[8];
        snprintf(v, sizeof v, "%04x", ir[MB_IR_VERSION]);
        say(std::string("des_app register map 0x") + v + " is older than 0x0103 (no monitor unit): Modbus stays off, state from the feed only, no remote commands");
        mb_off_ = true;
        mb_->close();
        return;
    }
    auto u32 = [&](int a) { return uint32_t(ir[a]) << 16 | ir[a + 1]; };
    LineStatus l;
    l.valid = true;
    l.state = ir[MB_IR_STATE]; l.flags = ir[MB_IR_FLAGS]; l.alarm = ir[MB_IR_ALARM]; l.recipe = ir[MB_IR_RECIPE];
    l.speed = ir[MB_IR_SPEED] / 10.0;
    l.total = u32(MB_IR_TOTAL); l.ok = u32(MB_IR_OK); l.ng = u32(MB_IR_NG); l.recheck = u32(MB_IR_RECHECK);
    uint16_t set = 0;
    if (mb_->read(3, MB_HR_SPEED, 1, &set)) l.set_speed = set / 10.0;   // the set point: what the dashboard marks as chosen
    const LineStatus &o = x_.line;
    if (!o.valid) {
        say("des_app registers readable (map 0x0103): line state and its own counters go into the state" +
            std::string(cfg_.cmd_key.empty() ? "; remote commands off (no key)" : "; remote commands on"));
        if (!cfg_.cmd_key.empty()) x_.cmds = cfg_.cmd_allow;
    }
    if (!o.valid || o.state != l.state || o.alarm != l.alarm || o.recipe != l.recipe || o.total != l.total || std::fabs(o.speed - l.speed) >= 1.0 ||
        std::fabs(o.set_speed - l.set_speed) >= 0.05)
        line_seq_++;
    x_.line = l;
}

// "" = done and des_app answered OK; result = des_app's CANP_ACK_* when it answered at all
std::string Uplink::run_command(const std::string &cmd, long long arg, long long seq, const std::string &sig, int &result)
{
    if (!mb_ || mb_off_ || !x_.line.valid || cfg_.cmd_key.empty() ||
        std::find(cfg_.cmd_allow.begin(), cfg_.cmd_allow.end(), cmd) == cfg_.cmd_allow.end())
        return "command not enabled";
    if (!same_text(sig, hmac_sha256_hex(cfg_.cmd_key, uplink_cmd_text(x_.nonce, seq, cmd, arg)))) return "bad signature";
    if (seq <= last_cmd_seq_) return "replayed";
    last_cmd_seq_ = seq;
    x_.last_seq = seq;
    line_seq_++;                         // the next state (within state_min_s) tells every dashboard the new last seq
    uint16_t addr = MB_HR_CMD, value = 0;
    if (cmd == "ack") { if (arg < 1 || arg > 65535) return "bad argument"; addr = MB_HR_WO_ACK; value = uint16_t(arg); }
    else if (cmd == "speed") {
        if (arg < 1 || arg > 65535) return "bad argument";
        if (!cfg_.speeds.empty() && std::none_of(cfg_.speeds.begin(), cfg_.speeds.end(), [&](double v) { return std::llround(v * 10) == arg; }))
            return "bad argument";
        addr = MB_HR_SPEED; value = uint16_t(arg);
    }
    else if (cmd == "recipe") {
        if (arg < 1 || arg > 65535) return "bad argument";
        if (!cfg_.recipes.empty() && std::none_of(cfg_.recipes.begin(), cfg_.recipes.end(), [&](const RecipeName &r) { return r.id == arg; }))
            return "bad argument";
        addr = MB_HR_RECIPE; value = uint16_t(arg);
    }
    else if (cmd == "start") value = CANP_CMD_START;
    else if (cmd == "stop") value = CANP_CMD_STOP;
    else if (cmd == "pause") value = CANP_CMD_PAUSE;
    else if (cmd == "resume") value = CANP_CMD_RESUME;
    else if (cmd == "reset") value = CANP_CMD_RESET_ALARM;
    else return "command not enabled";
    uint16_t before = 0, rs[2] = {0, 0};
    if (!mb_->read(3, MB_HR_SEQ, 1, &before) || !mb_->write(addr, value)) return "des_app unreachable: " + mb_->error();
    for (int i = 0; i < 20; i++) {       // des_app executes writes in its next control cycle (<= 17 ms)
        usleep(20000);
        if (!mb_->read(3, MB_HR_RESULT, 2, rs)) return "des_app unreachable: " + mb_->error();
        if (rs[1] != before) {
            result = rs[0];
            static const char *const why[] = {"", "rejected in the current state", "value out of range", "unknown command"};
            return result == CANP_ACK_OK ? "" : std::string("des_app: ") + (result < 4 ? why[result] : "refused");
        }
    }
    return "no answer from des_app";
}

void Uplink::shutdown()
{
    if (phase_ != Phase::Up || at_.lost()) return;
    q_.mqtt_pub(cfg_.prefix + "/state", "offline", 1, true);
    q_.mqtt_close();
    phase_ = Phase::Data;
}

bool Uplink::step()
{
    const double now = clock();
    if (t0_ < 0) t0_ = now;
    if (at_.lost()) return false;
    if (now < not_before_) {
        at_.pump(100);
        handle_urcs();
        return !at_.lost();
    }
    switch (phase_) {
    case Phase::Init:
        if (!q_.alive()) {
            if (++quiet_cmds_ >= 3) { say("the module does not answer AT"); return false; }
            not_before_ = now + 1;
            break;
        }
        q_.identify(info_);
        say("module " + info_.model + " " + info_.revision + ", IMEI " + info_.imei + ", card " + info_.iccid);
        if (!q_.has_mqtt()) { fail("this firmware has no MQTT commands (AT+QMTCFG)"); break; }
        go(Phase::Sim);
        break;
    case Phase::Sim:
        q_.refresh(info_);
        if (!info_.sim_ready) { fail("SIM card not ready (insert it before power-on)"); break; }
        go(Phase::Register);
        break;
    case Phase::Register:
        q_.refresh(info_);
        if (!info_.registered()) { fail("not registered on the network (state " + std::to_string(info_.reg) + ", signal " + std::to_string(info_.csq) + ")"); break; }
        say("registered: " + info_.oper + " " + info_.act + ", signal " + std::to_string(info_.csq) + " (" + std::to_string(info_.dbm()) + " dBm)");
        go(Phase::Data);
        break;
    case Phase::Data: {
        const std::string e = q_.data_up(cfg_.mqtt, info_);
        if (!e.empty()) { fail(e); break; }
        say("data connection up, address " + info_.ip);
        go(Phase::Mqtt);
        break;
    }
    case Phase::Mqtt: {
        MqttConfig m = cfg_.mqtt;
        m.will_topic = cfg_.prefix + "/state";
        m.will_msg = "offline";
        if (m.client_id.empty()) m.client_id = "des-" + (info_.imei.size() > 6 ? info_.imei.substr(info_.imei.size() - 6) : info_.imei);
        std::string e = q_.mqtt_open(m);
        if (!e.empty()) { fail(e); break; }
        e = q_.mqtt_sub(cfg_.prefix + "/cmd", 1);
        if (!e.empty()) say("subscribe " + cfg_.prefix + "/cmd failed: " + e);
        st_.reconnects++;
        say("connected to " + m.host + ":" + std::to_string(m.port) + " as " + m.client_id);
        backoff_ = 2;
        link_down_ = false;
        meta_sent_ = false;
        pub_fails_ = 0;
        last_state_ = -1e9;
        last_info_ = now;
        go(Phase::Up);
        break;
    }
    case Phase::Up:
        up_pass(now);
        break;
    }
    return !at_.lost();
}

}  // namespace des
