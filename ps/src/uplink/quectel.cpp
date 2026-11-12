// quectel.cpp — see quectel.hpp
#include "quectel.hpp"

#include <cstdlib>
#include <cstring>

namespace des {
namespace {

bool starts(const std::string &s, const char *p) { return s.compare(0, strlen(p), p) == 0; }
std::string q(const std::string &s)   // AT string parameter; a quote inside cannot be expressed, so it is dropped
{
    std::string o = "\"";
    for (char c : s)
        if (c != '"' && c != '\r' && c != '\n') o += c;
    return o + "\"";
}

}  // namespace

bool Quectel::alive()
{
    for (int i = 0; i < 3; i++) {
        if (at_.cmd("AT", 500).ok) {
            at_.cmd("ATE0", 500);
            at_.cmd("AT+CMEE=1", 500);
            return true;
        }
        if (at_.lost()) return false;
    }
    return false;
}

void Quectel::identify(ModemInfo &m)
{
    const AtResult i = at_.cmd("ATI", 1000);   // "Quectel" / "EM05" / "Revision: EM05CNFDR..."
    if (i.ok) {
        if (i.lines.size() >= 2) m.model = i.lines[1];
        m.revision = i.first("Revision:");
    }
    const AtResult g = at_.cmd("AT+CGSN", 1000);
    if (g.ok && !g.lines.empty()) m.imei = g.lines[0];
    const AtResult c = at_.cmd("AT+QCCID", 1000);
    if (c.ok) m.iccid = c.first("+QCCID:");
}

void Quectel::refresh(ModemInfo &m)
{
    const AtResult p = at_.cmd("AT+CPIN?", 5000);
    m.sim_ready = p.ok && p.first("+CPIN:") == "READY";
    const AtResult s = at_.cmd("AT+CSQ", 1000);
    if (s.ok) { const auto f = at_fields(s.first("+CSQ:")); if (!f.empty() && !f[0].empty()) m.csq = atoi(f[0].c_str()); }
    m.reg = 0;
    for (const char *c : {"AT+CEREG?", "AT+CGREG?", "AT+CREG?"}) {   // LTE first, then 3G packet / circuit
        const AtResult r = at_.cmd(c, 1000);
        if (!r.ok || r.lines.empty()) continue;
        const size_t colon = r.lines[0].find(':');
        const auto f = at_fields(colon == std::string::npos ? "" : r.lines[0].substr(colon + 1));
        const int st = f.size() >= 2 ? atoi(f[1].c_str()) : 0;
        if (st == 1 || st == 5) { m.reg = st; break; }
        if (st > m.reg) m.reg = st;
    }
    if (!m.registered()) return;
    const AtResult o = at_.cmd("AT+QSPN", 1000);   // +QSPN: "CHINA MOBILE","CMCC","",0,"46000"
    if (o.ok) { const auto f = at_fields(o.first("+QSPN:")); if (!f.empty()) m.oper = f[0]; }
    const AtResult n = at_.cmd("AT+QNWINFO", 1000);   // +QNWINFO: "FDD LTE","46011","LTE BAND 3",1825
    if (n.ok) { const auto f = at_fields(n.first("+QNWINFO:")); if (!f.empty()) m.act = f[0]; }
}

bool Quectel::has_mqtt() { return at_.cmd("AT+QMTCFG=?", 1000).ok; }

std::string Quectel::data_up(const MqttConfig &c, ModemInfo &m)
{
    const std::string cid = std::to_string(c.cid);
    auto active = [&]() {
        const AtResult r = at_.cmd("AT+QIACT?", 2000);   // +QIACT: <cid>,<state>,<type>,"<ip>"
        for (const std::string &l : r.lines) {
            if (!starts(l, "+QIACT:")) continue;
            const auto f = at_fields(l.substr(7));
            if (f.size() >= 2 && f[0] == cid && f[1] == "1") { m.ip = f.size() >= 4 ? f[3] : ""; return true; }
        }
        return false;
    };
    if (active()) return "";
    if (!c.apn.empty()) {
        const AtResult r = at_.cmd("AT+QICSGP=" + cid + ",1," + q(c.apn) + ",\"\",\"\",1", 2000);
        if (!r.ok) return "set APN failed: " + r.text();
    }
    const AtResult r = at_.cmd("AT+QIACT=" + cid, 150000);   // "at most 150 seconds", manual 2.1.2
    if (!r.ok && !active()) return "activate data connection failed: " + r.text();
    active();
    return "";
}

// the network half of an MQTT command arrives after OK as "<prefix> <client>,[<msgid>,]<result>[,<value>]"
std::string Quectel::wait_result(const char *prefix, int timeout_ms, int result_index, int *extra)
{
    for (;;) {
        std::string l;
        if (!at_.wait_urc(prefix, l, timeout_ms)) return at_.lost() ? "port lost" : "no answer from the network";
        const auto f = at_fields(l.substr(strlen(prefix)));
        if (int(f.size()) <= result_index) return "bad reply: " + l;
        const int res = atoi(f[size_t(result_index)].c_str());
        if (res == 1 && result_index == 2) continue;   // publish / subscribe: 1 = being retransmitted, the verdict follows
        if (extra) *extra = int(f.size()) > result_index + 1 ? atoi(f[size_t(result_index) + 1].c_str()) : -1;
        return res == 0 ? "" : std::string(prefix) + " result " + std::to_string(res);
    }
}

std::string Quectel::mqtt_open(const MqttConfig &c)
{
    if (c.host.empty()) return "no broker host configured";
    mqtt_close();   // a connection left over from an earlier run of this program
    const char *const fixed[] = {"AT+QMTCFG=\"version\",0,4", "AT+QMTCFG=\"session\",0,1", "AT+QMTCFG=\"timeout\",0,5,3,0",
                                 "AT+QMTCFG=\"recv/mode\",0,0,1"};
    for (const char *f : fixed) at_.cmd(f, 1000);   // older firmware lacks some of these: defaults are fine
    at_.cmd("AT+QMTCFG=\"pdpcid\",0," + std::to_string(c.cid), 1000);
    at_.cmd("AT+QMTCFG=\"keepalive\",0," + std::to_string(c.keepalive_s), 1000);
    if (!c.will_topic.empty())
        at_.cmd("AT+QMTCFG=\"will\",0,1,1,1," + q(c.will_topic) + "," + q(c.will_msg), 1000);
    AtResult r = at_.cmd("AT+QMTOPEN=0," + q(c.host) + "," + std::to_string(c.port), 5000);
    if (!r.ok) return "AT+QMTOPEN refused: " + r.text();
    std::string e = wait_result("+QMTOPEN:", 120000, 1);   // -1 fail, 1 parameter, 2 in use, 3 no data, 4 DNS, 5 network
    if (!e.empty()) return "open broker connection: " + e;
    std::string conn = "AT+QMTCONN=0," + q(c.client_id);
    if (!c.user.empty()) conn += "," + q(c.user) + "," + q(c.pass);
    r = at_.cmd(conn, 5000);
    if (!r.ok) return "AT+QMTCONN refused: " + r.text();
    int ret = -1;
    e = wait_result("+QMTCONN:", 20000, 1, &ret);
    if (!e.empty()) return "MQTT CONNECT: " + e;
    if (ret > 0) return "broker refused the connection, code " + std::to_string(ret);   // 4 = bad user / password
    return "";
}

std::string Quectel::mqtt_sub(const std::string &topic, int qos)
{
    if (++msg_id_ > 65535) msg_id_ = 1;
    const AtResult r = at_.cmd("AT+QMTSUB=0," + std::to_string(msg_id_) + "," + q(topic) + "," + std::to_string(qos), 5000);
    if (!r.ok) return "AT+QMTSUB refused: " + r.text();
    return wait_result("+QMTSUB:", 20000, 2);
}

std::string Quectel::mqtt_pub(const std::string &topic, const std::string &payload, int qos, bool retain)
{
    if (payload.size() > kMaxPayload) return "payload too long";
    int id = 0;
    if (qos > 0) { if (++msg_id_ > 65535) msg_id_ = 1; id = msg_id_; }
    const std::string head = "=0," + std::to_string(id) + "," + std::to_string(qos) + "," + (retain ? "1," : "0,") + q(topic);
    for (int attempt = 0; attempt < 2; attempt++) {
        const AtResult r = pubex_ ? at_.cmd_data("AT+QMTPUBEX" + head + "," + std::to_string(payload.size()), payload, 5000)
                                  : at_.cmd_data("AT+QMTPUB" + head, payload, 5000, true);
        if (r.ok) {
            pub_proven_ = true;
            return wait_result(pubex_ ? "+QMTPUBEX:" : "+QMTPUB:", 20000, 2);
        }
        if (r.timeout || r.lost || pub_proven_ || attempt) return std::string("publish refused: ") + r.text();
        pubex_ = !pubex_;   // the very first publish was rejected outright: try the other command form once
    }
    return "publish refused";
}

void Quectel::mqtt_close()
{
    std::string l;
    if (at_.cmd("AT+QMTDISC=0", 3000).ok) at_.wait_urc("+QMTDISC:", l, 5000);
    if (at_.cmd("AT+QMTCLOSE=0", 3000).ok) at_.wait_urc("+QMTCLOSE:", l, 5000);
}

ModemUrc Quectel::parse_urc(const std::string &line)
{
    ModemUrc u;
    if (starts(line, "+QMTSTAT:") || starts(line, "+QMTPING:")) {
        const auto f = at_fields(line.substr(9));
        u.kind = starts(line, "+QMTSTAT:") ? ModemUrc::MqttStat : ModemUrc::MqttPing;
        u.code = f.size() >= 2 ? atoi(f[1].c_str()) : 0;
        return u;
    }
    if (!starts(line, "+QMTRECV:")) return u;
    // +QMTRECV: <client>,<msgid>,"<topic>",[<len>,]"<payload>"   (the payload may itself contain quotes and commas)
    const size_t t0 = line.find('"');
    const size_t t1 = t0 == std::string::npos ? t0 : line.find('"', t0 + 1);
    if (t1 == std::string::npos) return u;   // "+QMTRECV: <client>,<recv_id>": buffered mode, not used
    u.kind = ModemUrc::MqttRecv;
    u.topic = line.substr(t0 + 1, t1 - t0 - 1);
    size_t p = t1 + 1;
    if (p < line.size() && line[p] == ',') p++;
    long len = -1;
    size_t d = p;
    while (d < line.size() && line[d] >= '0' && line[d] <= '9') d++;
    if (d > p && d + 1 < line.size() && line[d] == ',' && line[d + 1] == '"') { len = atol(line.c_str() + p); p = d + 1; }
    if (p < line.size() && line[p] == '"') p++;
    std::string rest = line.substr(p);
    if (len >= 0 && size_t(len) <= rest.size()) u.payload = rest.substr(0, size_t(len));
    else { if (!rest.empty() && rest.back() == '"') rest.pop_back(); u.payload = rest; }
    return u;
}

}  // namespace des
