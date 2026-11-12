// test_uplink — the 4G uplink (src/uplink) against a scripted module: no hardware, any Linux host or the board.
// The fake module sits on the other end of a socketpair and speaks the 移远 AT dialect the uplink uses (replies,
// "> " data prompt, results that arrive later as unsolicited lines). Also: USB descriptor parsing, the feed parser,
// work-order bookkeeping, payload limits, the config loader.
#include <arpa/inet.h>
#include <atomic>
#include <cstring>
#include <fstream>
#include <mutex>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include "json.hpp"
#include "test_check.hpp"
#include "uplink.hpp"
#include "usbfs_port.hpp"

using namespace des;

namespace {

struct Pub { std::string topic, payload; int qos; bool retain; };

class FakeModem {
public:
    FakeModem()
    {
        int sv[2];
        socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
        host_ = sv[0]; fd_ = sv[1];
        th_ = std::thread(&FakeModem::loop, this);
    }
    ~FakeModem()
    {
        shutdown(fd_, SHUT_RDWR);
        th_.join();
        close(fd_);
    }
    int host_fd() const { return host_; }
    std::atomic<bool> echo{true}, sim{true}, pubex{true}, mqtt{true}, data{false}, connected{false};
    std::atomic<int> reg{1}, open_result{0}, conn_ret{0};
    void inject(const std::string &line) { out("\r\n" + line + "\r\n"); }
    std::vector<Pub> pubs() { std::lock_guard<std::mutex> lk(m_); return pubs_; }
    std::vector<std::string> cmds() { std::lock_guard<std::mutex> lk(m_); return cmds_; }
    int count(const std::string &prefix)
    {
        std::lock_guard<std::mutex> lk(m_);
        int n = 0;
        for (const auto &c : cmds_) n += c.compare(0, prefix.size(), prefix) == 0;
        return n;
    }

private:
    void out(const std::string &s) { std::lock_guard<std::mutex> lk(w_); (void)!write(fd_, s.data(), s.size()); }
    void ok(const std::string &info = "") { out((info.empty() ? "" : "\r\n" + info + "\r\n") + "\r\nOK\r\n"); }
    void handle(const std::string &c)
    {
        { std::lock_guard<std::mutex> lk(m_); cmds_.push_back(c); }
        if (echo) out(c + "\r\n");
        auto is = [&](const char *p) { return c.compare(0, strlen(p), p) == 0; };
        if (c == "AT" || c == "AT+CMEE=1" || is("AT+QICSGP=")) ok();
        else if (c == "ATE0") { echo = false; ok(); }
        else if (c == "ATI") ok("Quectel\r\nEM05\r\nRevision: EM05CNFDR07A03M1G");
        else if (c == "AT+CGSN") ok("861234567890123");
        else if (c == "AT+QCCID") ok("+QCCID: 89860012345678901234");
        else if (c == "AT+CPIN?") { if (sim) ok("+CPIN: READY"); else out("\r\n+CME ERROR: 10\r\n"); }
        else if (c == "AT+CSQ") ok("+CSQ: 23,99");
        else if (c == "AT+CEREG?") ok("+CEREG: 0," + std::to_string(reg.load()));
        else if (c == "AT+CGREG?" || c == "AT+CREG?") ok(c.substr(2, c.size() - 3) + ": 0,0");
        else if (c == "AT+QSPN") ok("+QSPN: \"CHINA MOBILE\",\"CMCC\",\"\",0,\"46000\"");
        else if (c == "AT+QNWINFO") ok("+QNWINFO: \"FDD LTE\",\"46000\",\"LTE BAND 3\",1300");
        else if (is("AT+QMTCFG")) { if (mqtt) ok(); else out("\r\nERROR\r\n"); }
        else if (c == "AT+QIACT?") ok(data ? "+QIACT: 1,1,1,\"10.0.0.2\"" : "");
        else if (c == "AT+QIACT=1") { data = true; ok(); }
        else if (is("AT+QMTOPEN=")) { ok(); out("\r\n+QMTOPEN: 0," + std::to_string(open_result.load()) + "\r\n"); }
        else if (is("AT+QMTCONN=")) {
            ok();
            connected = conn_ret == 0;
            out("\r\n+QMTCONN: 0,0," + std::to_string(conn_ret.load()) + "\r\n");
        } else if (is("AT+QMTSUB=")) {
            ok();
            out("\r\n+QMTSUB: 0," + at_fields(c.substr(10))[1] + ",0,1\r\n");
        } else if (is("AT+QMTPUBEX=") || is("AT+QMTPUB=")) {
            const bool ex = is("AT+QMTPUBEX=");
            if ((ex && !pubex) || !connected) { out("\r\nERROR\r\n"); return; }
            const auto f = at_fields(c.substr(c.find('=') + 1));
            pend_ = {f[4], "", atoi(f[2].c_str()), f[3] == "1"};
            pend_id_ = f[1]; pend_ex_ = ex;
            need_ = ex ? size_t(atoi(f[5].c_str())) : 0;
            data_mode_ = true;
            out("\r\n> ");
        } else if (c == "AT+QMTDISC=0" || c == "AT+QMTCLOSE=0") {
            if (!connected && c == "AT+QMTDISC=0") { out("\r\nERROR\r\n"); return; }
            ok();
            if (c == "AT+QMTDISC=0") connected = false;
            out("\r\n+QMT" + std::string(c == "AT+QMTDISC=0" ? "DISC" : "CLOSE") + ": 0,0\r\n");
        } else if (c == "AT+SILENT") {
        } else out("\r\nERROR\r\n");
    }
    void loop()
    {
        std::string acc;
        char buf[2048];
        for (;;) {
            const ssize_t n = read(fd_, buf, sizeof buf);
            if (n <= 0) return;
            acc.append(buf, size_t(n));
            for (;;) {
                if (data_mode_) {
                    size_t take = std::string::npos;
                    if (pend_ex_) { if (acc.size() >= need_) take = need_; }
                    else if (const size_t z = acc.find('\x1A'); z != std::string::npos) take = z;
                    if (take == std::string::npos) break;
                    pend_.payload = acc.substr(0, take);
                    acc.erase(0, take + (pend_ex_ ? 0 : 1));
                    data_mode_ = false;
                    { std::lock_guard<std::mutex> lk(m_); pubs_.push_back(pend_); }
                    ok();
                    out("\r\n+QMTPUB" + std::string(pend_ex_ ? "EX" : "") + ": 0," + pend_id_ + ",0\r\n");
                    continue;
                }
                const size_t e = acc.find('\r');
                if (e == std::string::npos) break;
                const std::string c = acc.substr(0, e);
                acc.erase(0, e + 1);
                if (!c.empty()) handle(c);
            }
        }
    }
    int host_ = -1, fd_ = -1;
    std::mutex m_, w_;
    std::vector<Pub> pubs_;
    std::vector<std::string> cmds_;
    Pub pend_{};
    std::string pend_id_;
    bool pend_ex_ = true, data_mode_ = false;
    size_t need_ = 0;
    std::thread th_;
};

std::string frame(char type, const std::string &name, const std::string &payload)
{
    std::string f = {'D', 'F', type, 0, char(name.size() >> 8), char(name.size() & 0xFF), char(payload.size() >> 24),
                     char((payload.size() >> 16) & 0xFF), char((payload.size() >> 8) & 0xFF), char(payload.size() & 0xFF)};
    return f + name + payload;
}
FeedParser::Frame fr(char type, const std::string &name, const std::string &payload)
{
    FeedParser::Frame f;
    f.type = type; f.name = name; f.payload = payload;
    return f;
}
const char *k_hdr = "wall_ms,t_s,track,cls,label,conf,disp,dest,why,size_mm";
std::string order(int id, int level, const char *status, const char *title)
{
    return nlohmann::json{{"id", id}, {"code", 2}, {"level", level}, {"status", status}, {"title", title},
                          {"detail", "步进指令 60 mm/s，实测 3 mm/s"}, {"advice", "检查皮带是否卡住"}}.dump();
}

void test_usb()
{
    // 移远 EC2x 式的排布：0 诊断（两个批量端点），1~3 串口式（中断 + 批量对），4 网卡
    std::vector<uint8_t> d = {18, 1, 0, 2, 0xEF, 2, 1, 64, 0x7C, 0x2C, 0x25, 0x01, 0x18, 0x03, 1, 2, 3, 1};
    d.insert(d.end(), {9, 2, 0, 0, 5, 1, 0, 0xA0, 250});
    auto iface = [&](int n, int cls, int sub, int proto, std::vector<std::pair<int, int>> eps) {
        d.insert(d.end(), {9, 4, uint8_t(n), 0, uint8_t(eps.size()), uint8_t(cls), uint8_t(sub), uint8_t(proto), 0});
        for (auto &e : eps) d.insert(d.end(), {7, 5, uint8_t(e.first), uint8_t(e.second), 0x00, 0x02, 0});
    };
    iface(0, 0xFF, 0xFF, 0xFF, {{0x81, 2}, {0x01, 2}});
    iface(1, 0xFF, 0, 0, {{0x83, 3}, {0x82, 2}, {0x02, 2}});
    iface(2, 0xFF, 0, 0, {{0x85, 3}, {0x84, 2}, {0x03, 2}});
    iface(3, 0xFF, 0, 0, {{0x87, 3}, {0x86, 2}, {0x04, 2}});
    iface(4, 0xFF, 0xFF, 0xFF, {{0x89, 3}, {0x88, 2}, {0x05, 2}});
    UsbDev dev;
    CHECK(usb_parse_descriptors(d.data(), d.size(), dev), "descriptor blob parses");
    CHECK(dev.vid == 0x2C7C && dev.pid == 0x0125, "vid:pid %04x:%04x", dev.vid, dev.pid);
    CHECK(dev.ifs.size() == 5, "5 interfaces, got %zu", dev.ifs.size());
    if (dev.ifs.size() == 5) {
        CHECK(dev.ifs[2].num == 2 && dev.ifs[2].ep_in == 0x84 && dev.ifs[2].ep_out == 0x03 && dev.ifs[2].ep_int == 0x85,
              "interface 2 endpoints in %02x out %02x int %02x", dev.ifs[2].ep_in, dev.ifs[2].ep_out, dev.ifs[2].ep_int);
        CHECK(dev.ifs[2].max_out == 512 && dev.ifs[2].at_candidate(), "interface 2: 512-byte OUT, candidate");
        CHECK(dev.ifs[0].ep_int == 0 && dev.ifs[0].at_candidate(), "interface 0 has a bulk pair, no interrupt");
    }
    const uint8_t junk[4] = {4, 3, 9, 4};
    CHECK(!usb_parse_descriptors(junk, sizeof junk, dev), "junk is rejected");
    d.resize(d.size() - 3);   // cut inside the last endpoint
    CHECK(usb_parse_descriptors(d.data(), d.size(), dev) && dev.ifs.size() == 5 && dev.ifs[4].ep_out == 0, "truncated blob: no overrun");
    CHECK(dev.node() == "/dev/bus/usb/000/000", "node path format");
}

void test_parsing()
{
    const auto f = at_fields(" 0,12,\"a,b\", \"x\" ");
    CHECK(f.size() == 4 && f[0] == "0" && f[1] == "12" && f[2] == "a,b" && f[3] == "x", "fields with a quoted comma");
    ModemUrc u = Quectel::parse_urc("+QMTSTAT: 0,1");
    CHECK(u.kind == ModemUrc::MqttStat && u.code == 1, "QMTSTAT code");
    u = Quectel::parse_urc("+QMTRECV: 0,0,\"des/x/cmd\",27,\"{\"cmd\":\"ping\",\"id\":7,\"a\":1}\"");
    CHECK(u.kind == ModemUrc::MqttRecv && u.topic == "des/x/cmd", "QMTRECV topic");
    CHECK(u.payload == "{\"cmd\":\"ping\",\"id\":7,\"a\":1}", "QMTRECV payload by length: [%s]", u.payload.c_str());
    u = Quectel::parse_urc("+QMTRECV: 0,0,\"t\",\"12,\"abc\"");
    CHECK(u.kind == ModemUrc::MqttRecv && u.payload == "12,\"abc", "QMTRECV without a length keeps inner quotes: [%s]", u.payload.c_str());
    u = Quectel::parse_urc("+QMTRECV: 0,3");
    CHECK(u.kind == ModemUrc::Other, "buffered-mode notice is not a message");
    CHECK(Quectel::parse_urc("RDY").kind == ModemUrc::Other, "other lines");
}

void test_at_channel()
{
    FakeModem fm;
    FdPort port(fm.host_fd());
    AtChan at(port);
    AtResult r = at.cmd("ATI", 1000);
    CHECK(r.ok && r.lines.size() == 3 && r.lines[1] == "EM05", "ATI with echo on: %zu lines", r.lines.size());
    CHECK(r.first("Revision:") == "EM05CNFDR07A03M1G", "first() strips the prefix");
    fm.sim = false;
    r = at.cmd("AT+CPIN?", 1000);
    CHECK(!r.ok && r.cme == 10, "CME error code %d", r.cme);
    fm.sim = true;
    r = at.cmd("AT+CPIN?", 1000);
    CHECK(r.ok && r.first("+CPIN:") == "READY", "own-prefix line is a reply, not a URC");
    r = at.cmd("AT+NOPE", 1000);
    CHECK(!r.ok && !r.timeout && r.cme < 0, "plain ERROR");
    r = at.cmd("AT+SILENT", 250);
    CHECK(r.timeout, "timeout when the module says nothing");
    fm.inject("+QMTSTAT: 0,1");
    r = at.cmd("AT+CSQ", 1000);
    std::string u;
    CHECK(r.ok && r.lines.size() == 1, "URC in the middle is not part of the reply (%zu lines)", r.lines.size());
    CHECK(at.next_urc(u) && u == "+QMTSTAT: 0,1" && !at.next_urc(u), "URC queued once");
    fm.inject("+QIURC: \"pdpdeact\",1");
    fm.inject("+QMTPING: 0,1");
    CHECK(at.wait_urc("+QMTPING:", u, 1000) && u == "+QMTPING: 0,1", "wait_urc picks by prefix");
    CHECK(at.next_urc(u) && u == "+QIURC: \"pdpdeact\",1", "the other URC stays queued");
    CHECK(!at.wait_urc("+NEVER:", u, 150), "wait_urc times out");
}

void test_modem()
{
    FakeModem fm;
    FdPort port(fm.host_fd());
    AtChan at(port);
    Quectel q(at);
    ModemInfo m;
    CHECK(q.alive(), "alive");
    q.identify(m);
    q.refresh(m);
    CHECK(m.model == "EM05" && m.imei == "861234567890123" && m.iccid == "89860012345678901234", "identity");
    CHECK(m.sim_ready && m.csq == 23 && m.dbm() == -67 && m.registered(), "card / signal / registration");
    CHECK(m.oper == "CHINA MOBILE" && m.act == "FDD LTE", "operator [%s] [%s]", m.oper.c_str(), m.act.c_str());
    CHECK(q.has_mqtt(), "MQTT commands present");
    MqttConfig c;
    c.host = "broker.example"; c.client_id = "des-1"; c.will_topic = "p/state"; c.will_msg = "offline"; c.apn = "cmnet";
    CHECK(q.data_up(c, m).empty() && m.ip == "10.0.0.2", "data connection, ip [%s]", m.ip.c_str());
    CHECK(fm.count("AT+QICSGP=1,1,\"cmnet\"") == 1 && fm.count("AT+QIACT=1") == 1, "APN set, activated once");
    CHECK(q.data_up(c, m).empty() && fm.count("AT+QIACT=1") == 1, "already active: not activated again");
    CHECK(q.mqtt_open(c).empty(), "broker connection");
    CHECK(fm.count("AT+QMTCFG=\"will\",0,1,1,1,\"p/state\",\"offline\"") == 1, "will configured");
    CHECK(fm.count("AT+QMTOPEN=0,\"broker.example\",1883") == 1 && fm.count("AT+QMTCONN=0,\"des-1\"") == 1, "open / connect commands");
    CHECK(q.mqtt_sub("p/cmd", 1).empty(), "subscribe");
    const std::string body = "{\"a\":\"中文, \\\"q\\\"\"}\r\nline2";
    CHECK(q.mqtt_pub("p/state", body, 1, true).empty(), "publish");
    auto pubs = fm.pubs();
    CHECK(pubs.size() == 1 && pubs[0].topic == "p/state" && pubs[0].payload == body && pubs[0].qos == 1 && pubs[0].retain,
          "payload arrives byte for byte (quotes, CR LF, UTF-8)");
    CHECK(!q.mqtt_pub("p/x", std::string(1501, 'x'), 0, false).empty(), "payload over 1500 bytes refused locally");
    fm.conn_ret = 4;
    CHECK(q.mqtt_open(c).find("code 4") != std::string::npos, "broker refusal is reported with its code");
    fm.conn_ret = 0; fm.open_result = 4;
    CHECK(!q.mqtt_open(c).empty(), "open failure (DNS) is reported");

    FakeModem old;   // firmware with AT+QMTPUB ... Ctrl+Z only
    old.pubex = false; old.data = true;
    FdPort port2(old.host_fd());
    AtChan at2(port2);
    Quectel q2(at2);
    CHECK(q2.alive() && q2.mqtt_open(c).empty(), "old firmware: connect");
    CHECK(q2.mqtt_pub("p/wo", "hello", 1, false).empty(), "old firmware: falls back to AT+QMTPUB");
    CHECK(q2.mqtt_pub("p/wo", "again", 0, false).empty(), "old firmware: stays on AT+QMTPUB");
    pubs = old.pubs();
    CHECK(pubs.size() == 2 && pubs[0].payload == "hello" && pubs[1].payload == "again", "old firmware: payloads");
    CHECK(old.count("AT+QMTPUBEX=") == 1 && old.count("AT+QMTPUB=") == 2, "QMTPUBEX tried exactly once");
}

void test_feed()
{
    const std::string img(70000, '\xFF');
    const std::string stream = frame('H', "20261112-101500", k_hdr) + frame('w', "", "[" + order(3, 2, "open", "相机无图像") + "]") +
                               frame('I', "a.jpg", img) + frame('R', "", "1,0.5,7,0,good,0.991,pick,bin0,,30.02") +
                               frame('S', "", "records=1 images=1 dropped=0") + frame('W', "", "");
    for (size_t chunk : {size_t(1), size_t(7), size_t(4096), stream.size()}) {
        FeedParser p;
        std::vector<FeedParser::Frame> out;
        bool good = true;
        for (size_t o = 0; o < stream.size(); o += chunk) good &= p.feed(stream.data() + o, std::min(chunk, stream.size() - o), out);
        CHECK(good && out.size() == 6, "chunk %zu: 6 frames, got %zu", chunk, out.size());
        if (out.size() == 6) {
            CHECK(out[0].type == 'H' && out[0].name == "20261112-101500" && out[0].payload == k_hdr, "chunk %zu: hello", chunk);
            CHECK(out[2].type == 'I' && out[2].name == "a.jpg" && out[2].payload.empty(), "chunk %zu: image skipped, not stored", chunk);
            CHECK(out[3].type == 'R' && out[4].type == 'S' && out[5].type == 'W' && out[5].payload.empty(), "chunk %zu: order kept", chunk);
        }
    }
    FeedParser p;
    std::vector<FeedParser::Frame> out;
    CHECK(!p.feed("HTTP/1.1 200 OK\r\n", 17, out), "another protocol is rejected");

    BoardLink b;
    b.on_connect(true);
    b.on_frame(fr('H', "s1", k_hdr));
    b.on_frame(fr('w', "", "[" + order(3, 2, "open", "相机无图像") + "," + order(2, 3, "closed", "急停按下") + "]"));
    BoardSnapshot s = b.snapshot();
    CHECK(s.alive() && s.session == "s1" && s.wo_active == 1 && s.wo_unacked == 1 && s.wo_level == 2 && s.wo_top_id == 3,
          "order list: active %d unacked %d level %d top %d", s.wo_active, s.wo_unacked, s.wo_level, s.wo_top_id);
    std::string j;
    CHECK(!b.next_order(j), "the list on connect is not forwarded as changes");
    b.on_frame(fr('R', "", "1,0.5,7,0,good,0.991,pick,bin0,,30.02"));
    b.on_frame(fr('R', "", "2,0.9,8,3,dirty_heavy,0.87,flap,ng,,29.60"));
    b.on_frame(fr('R', "", "3,1.2,9,1,missing_light,0.55,recheck,recheck,low conf,0"));
    s = b.snapshot();
    CHECK(s.total == 3 && s.ok == 1 && s.ng == 1 && s.recheck == 1, "counts %llu/%llu/%llu/%llu", (unsigned long long)s.total,
          (unsigned long long)s.ok, (unsigned long long)s.ng, (unsigned long long)s.recheck);
    CHECK(s.last_label == "missing_light" && s.last_disp == "recheck" && s.last_conf > 0.54 && s.last_conf < 0.56, "last part");
    const uint64_t seq = s.seq;
    b.on_frame(fr('S', "", "records=3 images=1 dropped=0"));
    CHECK(b.snapshot().seq == seq, "statistics frames do not count as a change");
    b.on_frame(fr('W', "", order(4, 3, "open", "皮带速度异常")));
    b.on_frame(fr('W', "", order(3, 2, "acked", "相机无图像")));
    s = b.snapshot();
    CHECK(s.wo_active == 2 && s.wo_unacked == 1 && s.wo_level == 3 && s.wo_top_id == 4 && s.wo_top_title == "皮带速度异常",
          "after two changes: active %d unacked %d level %d top %d", s.wo_active, s.wo_unacked, s.wo_level, s.wo_top_id);
    CHECK(b.next_order(j) && nlohmann::json::parse(j)["id"] == 4 && b.next_order(j) && !b.next_order(j), "two changes queued in order");
    b.on_frame(fr('H', "s2", k_hdr));
    s = b.snapshot();
    CHECK(s.total == 0 && s.wo_active == 0 && s.session == "s2", "des_app restarted: counts and orders start over");
    b.on_connect(false);
    CHECK(!b.snapshot().alive(), "feed closed: not alive");

    // the real thing over loopback TCP
    const int ls = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t al = sizeof a;
    bind(ls, reinterpret_cast<sockaddr *>(&a), sizeof a);
    listen(ls, 1);
    getsockname(ls, reinterpret_cast<sockaddr *>(&a), &al);
    BoardLink t;
    t.start("127.0.0.1", ntohs(a.sin_port));
    const int c = accept(ls, nullptr, nullptr);
    const std::string hello = frame('H', "s9", k_hdr) + frame('R', "", "1,0.5,7,0,good,0.991,pick,bin0,,30.02");
    (void)!write(c, hello.data(), hello.size());
    for (int i = 0; i < 40 && t.snapshot().total == 0; i++) usleep(50000);
    s = t.snapshot();
    CHECK(s.alive() && s.session == "s9" && s.total == 1 && s.ok == 1, "feed client over TCP");
    close(ls);   // first, or the client reconnects at once
    close(c);
    for (int i = 0; i < 40 && t.snapshot().connected; i++) usleep(50000);
    CHECK(!t.snapshot().connected, "feed client notices the close");
    t.stop();
}

void test_payloads()
{
    BoardSnapshot b;
    b.connected = true; b.quiet_s = 0.5; b.session = "20261112-101500";
    b.total = 4000000000ull; b.ok = 3999999000ull; b.ng = 900; b.recheck = 100;
    b.last_label = std::string(40, 'x'); b.last_disp = "recheck"; b.last_dest = std::string(40, 'y');
    b.wo_active = 12; b.wo_unacked = 9; b.wo_level = 3; b.wo_top_id = 65535; b.wo_top_title = "对射与视觉不一致对射与视觉不一致";
    ModemInfo m;
    m.csq = 31; m.oper = std::string(40, 'o'); m.act = "FDD LTE";
    UplinkStats st;
    st.published = st.failed = 4000000000ull;
    const std::string s = uplink_state_json(b, m, 1, 8.64e6, st);
    const nlohmann::json j = nlohmann::json::parse(s);
    CHECK(s.size() < 700, "worst-case state is %zu bytes (limit 1500)", s.size());
    CHECK(j["online"] == true && j["app"] == true && j["total"] == 4000000000ull && j["wo"]["level"] == 3 && j["eth"] == 1 &&
              j["net"]["dbm"] == -51 && j["last"]["disp"] == "recheck",
          "state fields");
    const std::string big = nlohmann::json{{"id", 9}, {"code", 5}, {"level", 2}, {"status", "open"}, {"title", "对射与视觉不一致"},
                                           {"detail", std::string(900, 'd')}, {"advice", std::string(900, 'a')}}.dump();
    const std::string fit = uplink_fit_order(big, 1400);
    const nlohmann::json f = nlohmann::json::parse(fit);
    CHECK(fit.size() <= 1400 && f["id"] == 9 && f["title"] == "对射与视觉不一致" && !f.contains("advice") && f.contains("detail"),
          "long order: advice dropped first, %zu bytes", fit.size());
    CHECK(uplink_fit_order("{\"id\":1}", 1400) == "{\"id\":1}", "short order untouched");

    const char *path = "/tmp/test_uplink_cfg.json";
    auto load = [&](const std::string &text, UplinkConfig &c) { std::ofstream(path) << text; return load_uplink_config(path, c); };
    UplinkConfig c;
    CHECK(load("{ // comment\n \"mqtt\": {\"host\": \"b.example\", \"port\": 1884, \"apn\": \"cmnet\"}, \"topic_prefix\": \"des/ab12\","
               "\"mode\": \"backup\", \"modem\": {\"vid\": \"2c7c\", \"usb_if\": 2}, \"board\": {\"feed_port\": 9501} }", c).empty(),
          "config loads");
    CHECK(c.mqtt.host == "b.example" && c.mqtt.port == 1884 && c.mqtt.apn == "cmnet" && c.prefix == "des/ab12" && c.mode == "backup" &&
              c.vid == 0x2c7c && c.usb_if == 2 && c.feed_port == 9501 && c.state_period_s == 10,
          "config values");
    UplinkConfig d;
    CHECK(!load("{\"topic_prefix\": \"x\"}", d).empty(), "no broker host: refused");
    CHECK(!load("{\"mqtt\": {\"host\": \"h\"}, \"topic_prefix\": \"a b\"}", d).empty(), "space in the prefix: refused");
    CHECK(!load("{\"mqtt\": {\"host\": \"h\", \"pass\": \"a\\\"b\"}}", d).empty(), "quote in a password: refused");
    CHECK(!load("{\"mqtt\": {\"host\": \"h\"}, \"mode\": \"sometimes\"}", d).empty(), "unknown mode: refused");
    CHECK(!load("{\"mqtt\": {\"host\": 5}}", d).empty(), "wrong type: refused, no crash");
    unlink(path);
}

// run the state machine until cond() or n passes; every pass costs up to 0.1 s of real time while idle
template <class F> bool steps(Uplink &up, double &t, double dt, int n, F cond)
{
    for (int i = 0; i < n; i++) {
        if (cond()) return true;
        if (!up.step()) return false;
        t += dt;
    }
    return cond();
}

void test_uplink(const char *mode)
{
    FakeModem fm;
    FdPort port(fm.host_fd());
    AtChan at(port);
    BoardLink board;
    board.on_connect(true);
    board.on_frame(fr('H', "s1", k_hdr));
    UplinkConfig c;
    c.mqtt.host = "broker.example"; c.prefix = "des/t1"; c.mode = mode;
    Uplink up(c, at, board);
    double t = 100;
    int eth = 1;
    std::vector<std::string> log;
    up.clock = [&]() { return t; };
    up.eth = [&]() { return eth; };
    up.log = [&](const std::string &s) { log.push_back(s); };
    const bool backup = std::string(mode) == "backup";
    auto state_pubs = [&]() { int n = 0; for (auto &p : fm.pubs()) n += p.topic == "des/t1/state"; return n; };

    fm.sim = false;
    CHECK(steps(up, t, 0.5, 6, [&]() { return up.phase() == Uplink::Phase::Sim && !log.empty() && log.back().find("SIM") != std::string::npos; }),
          "%s: waits in the SIM phase without a card", mode);
    fm.sim = true; fm.reg = 2;
    CHECK(steps(up, t, 3, 6, [&]() { return up.phase() == Uplink::Phase::Register; }), "%s: card ready -> waits for the network", mode);
    fm.reg = 1;
    CHECK(steps(up, t, 10, 12, [&]() { return up.phase() == Uplink::Phase::Up && state_pubs() >= 1; }), "%s: reaches Up and publishes the state", mode);
    auto pubs = fm.pubs();
    if (!pubs.empty()) {
        const nlohmann::json j = nlohmann::json::parse(pubs.back().payload, nullptr, false);
        CHECK(pubs.back().retain && pubs.back().qos == 1 && j.is_object() && j["online"] == true && j["eth"] == 1 && j["session"] == "s1" &&
                  j["net"]["oper"] == "CHINA MOBILE",
              "%s: first state is retained and complete", mode);
    }
    CHECK(fm.count("AT+QMTCFG=\"will\",0,1,1,1,\"des/t1/state\",\"offline\"") == 1, "%s: will = offline on the state topic", mode);
    CHECK(fm.count("AT+QMTCONN=0,\"des-890123\"") == 1 && fm.count("AT+QMTSUB=0,1,\"des/t1/cmd\",1") == 1, "%s: client id from the IMEI, cmd subscribed", mode);

    // a part and a work order
    int n0 = state_pubs();
    board.on_frame(fr('R', "", "1,0.5,7,3,dirty_heavy,0.87,flap,ng,,29.60"));
    board.on_frame(fr('W', "", order(4, 3, "open", "皮带速度异常")));
    CHECK(steps(up, t, 0.5, 8, [&]() { int n = 0; for (auto &p : fm.pubs()) n += p.topic == "des/t1/wo"; return n == 1; }),
          "%s: the work order is forwarded", mode);
    if (backup) {
        steps(up, t, 0.5, 6, []() { return false; });
        CHECK(state_pubs() == n0, "backup, cable in: a change does not trigger a state message");
        CHECK(steps(up, t, 10, 8, [&]() { return state_pubs() == n0 + 1; }), "backup, cable in: state every idle_period_s");
    } else {
        CHECK(steps(up, t, 0.5, 8, [&]() { return state_pubs() == n0 + 1; }), "always: the change is published after state_min_s");
        const nlohmann::json j = nlohmann::json::parse(fm.pubs().back().payload, nullptr, false);
        CHECK(j.is_object() && j["ng"] == 1 && j["wo"]["level"] == 3 && j["last"]["label"] == "dirty_heavy", "always: counts and order level in the state");
        n0 = state_pubs();
        steps(up, t, 0.5, 6, []() { return false; });
        CHECK(state_pubs() == n0, "always: nothing changed, nothing sent inside the period");
        CHECK(steps(up, t, 2, 8, [&]() { return state_pubs() == n0 + 1; }), "always: periodic state");
    }

    // pull the cable
    n0 = state_pubs();
    eth = 0;
    CHECK(steps(up, t, 0.1, 3, [&]() { return state_pubs() == n0 + 1; }), "%s: cable pulled -> state at once", mode);
    CHECK(nlohmann::json::parse(fm.pubs().back().payload, nullptr, false)["eth"] == 0, "%s: eth 0 in the state", mode);
    board.on_frame(fr('R', "", "2,0.9,8,0,good,0.99,pick,bin0,,30.01"));
    CHECK(steps(up, t, 0.5, 8, [&]() { return state_pubs() == n0 + 2; }), "%s: cable out: changes are published at the full rate", mode);
    eth = 1;
    steps(up, t, 0.1, 2, []() { return false; });

    // a command from the broker
    fm.inject("+QMTRECV: 0,0,\"des/t1/cmd\",21,\"{\"cmd\":\"ping\",\"id\":7}\"");
    CHECK(steps(up, t, 0.1, 6, [&]() { return !fm.pubs().empty() && fm.pubs().back().topic == "des/t1/reply"; }), "%s: ping is answered", mode);
    nlohmann::json r = nlohmann::json::parse(fm.pubs().back().payload, nullptr, false);
    CHECK(r.is_object() && r["id"] == 7 && r["ok"] == true, "%s: reply carries the id", mode);
    fm.inject("+QMTRECV: 0,0,\"des/t1/cmd\",15,\"{\"cmd\":\"start\"}\"");
    CHECK(steps(up, t, 0.1, 6, [&]() { return up.stats().commands == 2 && fm.pubs().back().topic == "des/t1/reply"; }), "%s: second command seen", mode);
    r = nlohmann::json::parse(fm.pubs().back().payload, nullptr, false);
    CHECK(r.is_object() && r["ok"] == false && r["cmd"] == "start", "%s: a line command is refused", mode);

    // the broker connection drops
    const int opens = fm.count("AT+QMTOPEN=");
    n0 = state_pubs();
    fm.connected = false;
    fm.inject("+QMTSTAT: 0,1");
    CHECK(steps(up, t, 1, 4, [&]() { return up.phase() != Uplink::Phase::Up; }), "%s: +QMTSTAT leaves Up", mode);
    CHECK(steps(up, t, 2, 12, [&]() { return up.phase() == Uplink::Phase::Up && state_pubs() > n0; }), "%s: reconnects and publishes again", mode);
    CHECK(fm.count("AT+QMTOPEN=") == opens + 1 && up.stats().reconnects == 2, "%s: exactly one reconnect", mode);

    up.shutdown();
    pubs = fm.pubs();
    CHECK(!pubs.empty() && pubs.back().topic == "des/t1/state" && pubs.back().payload == "offline" && pubs.back().retain && !fm.connected,
          "%s: shutdown says offline and disconnects", mode);
}

void test_dead_port()
{
    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    FdPort port(sv[0]);
    AtChan at(port);
    BoardLink board;
    UplinkConfig c;
    c.mqtt.host = "h";
    Uplink up(c, at, board);
    double t = 0;
    up.clock = [&]() { return t; };
    bool alive = true;
    int passes = 0;
    for (; passes < 8 && alive; passes++) { alive = up.step(); t += 2; }   // a module that never answers
    CHECK(!alive && passes <= 4, "silent module: step() gives up after %d passes", passes);
    close(sv[1]);
    at.reset();
    Uplink up2(c, at, board);
    CHECK(!up2.step() && at.lost(), "closed port: reported as lost");
}

}  // namespace

int main()
{
    test_usb();
    test_parsing();
    test_at_channel();
    test_modem();
    test_feed();
    test_payloads();
    test_uplink("always");
    test_uplink("backup");
    test_dead_port();
    return test_summary("test_uplink");
}
