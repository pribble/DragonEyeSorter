// uplink.cpp — see uplink.hpp
#include "uplink.hpp"

#include <cmath>
#include <fstream>
#include <sstream>
#include <time.h>

#include "json.hpp"

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
            opt(*b, "host", c.board_host); opt(*b, "feed_port", c.feed_port);
        }
        if (const auto m = j.find("modem"); m != j.end() && m->is_object()) {
            std::string vid;
            opt(*m, "vid", vid); opt(*m, "usb_if", c.usb_if); opt(*m, "tty", c.tty);
            if (!vid.empty()) c.vid = unsigned(strtoul(vid.c_str(), nullptr, 16));
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
    return "";
}

int eth_carrier(const std::string &ifname)
{
    std::ifstream f("/sys/class/net/" + ifname + "/carrier");
    char c = 0;
    if (!(f >> c)) return -1;   // no such interface, or it is administratively down
    return c == '1' ? 1 : 0;
}

std::string uplink_state_json(const BoardSnapshot &b, const ModemInfo &m, int eth, double up_s, const UplinkStats &st)
{
    json j = {{"online", true}, {"up_s", std::llround(up_s)}, {"app", b.alive()}, {"session", b.session},
              {"total", b.total}, {"ok", b.ok}, {"ng", b.ng}, {"recheck", b.recheck},
              {"wo", {{"active", b.wo_active}, {"unacked", b.wo_unacked}, {"level", b.wo_level}, {"id", b.wo_top_id},
                      {"title", b.wo_top_title}}},
              {"eth", eth},
              {"net", {{"csq", m.csq}, {"dbm", m.dbm()}, {"oper", m.oper}, {"act", m.act}}},
              {"tx", {{"ok", st.published}, {"fail", st.failed}, {"conn", st.reconnects}}}};
    if (b.total)
        j["last"] = {{"label", b.last_label}, {"disp", b.last_disp}, {"dest", b.last_dest},
                     {"conf", std::round(b.last_conf * 1000) / 1000}, {"size_mm", std::round(b.last_size_mm * 100) / 100}};
    return dump(j);
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

Uplink::Uplink(const UplinkConfig &c, AtChan &at, BoardLink &board) : cfg_(c), at_(at), board_(board), q_(at)
{
    eth = [this]() { return eth_carrier(cfg_.eth_if); };
    clock = mono;
    t0_ = -1;
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
    else { r["ok"] = false; r["err"] = cmd.empty() ? "bad command" : "command not enabled"; }
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
        const BoardSnapshot b = board_.snapshot();
        const int e = eth();
        const bool idle = cfg_.mode == "backup" && e == 1;
        bool due = now - last_state_ >= (idle ? cfg_.idle_period_s : cfg_.state_period_s);
        if (e != sent_eth_) due = true;
        if (!idle && b.seq != sent_seq_ && now - last_state_ >= cfg_.state_min_s) due = true;
        if (due) {
            if (sent_eth_ != -2 && e != sent_eth_)
                say(e == 1 ? "wired link is back" : "wired link is down: reporting over 4G only");
            last_state_ = now;
            if (publish(cfg_.prefix + "/state", uplink_state_json(b, info_, e, now - t0_, st_), true)) {
                sent_seq_ = b.seq;
                sent_eth_ = e;
            }
        }
    }
    if (link_down_) {
        link_down_ = false;
        pub_fails_ = 0;
        go(Phase::Data, backoff_);   // check the data connection, then open the broker connection again
        backoff_ = backoff_ * 2 > 60 ? 60 : backoff_ * 2;
    }
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
