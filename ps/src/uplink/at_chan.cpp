// at_chan.cpp — see at_chan.hpp
#include "at_chan.hpp"

#include <cstdlib>
#include <cstring>
#include <time.h>

namespace des {
namespace {

double now_s()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
bool starts(const std::string &s, const char *p) { return s.compare(0, strlen(p), p) == 0; }

// lines the module sends on its own (移远 AT / MQTT / TCP 手册的 URC 表里本程序会遇到的那些)
const char *const k_urc[] = {"+QMTSTAT:", "+QMTRECV:", "+QMTPING:", "+QMTOPEN:", "+QMTCLOSE:", "+QMTCONN:", "+QMTDISC:",
                             "+QMTSUB:",  "+QMTUNS:",  "+QMTPUBEX:", "+QMTPUB:", "+QIURC:",    "+QIND:",    "+QUSIM:",
                             "+CPIN:",    "+CFUN:",    "+CREG:",    "+CGREG:",  "+CEREG:",    "+QSIMSTAT", "RDY",
                             "POWERED DOWN", "NORMAL POWER DOWN"};

}  // namespace

std::string at_trim(const std::string &s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
    return s.substr(a, b - a);
}

std::vector<std::string> at_fields(const std::string &s)
{
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (char c : s) {
        if (c == '"') { quoted = !quoted; continue; }
        if (c == ',' && !quoted) { out.push_back(at_trim(cur)); cur.clear(); continue; }
        cur += c;
    }
    out.push_back(at_trim(cur));
    return out;
}

std::string AtResult::first(const std::string &prefix) const
{
    for (const std::string &l : lines)
        if (l.compare(0, prefix.size(), prefix) == 0) return at_trim(l.substr(prefix.size()));
    return "";
}
std::string AtResult::text() const
{
    std::string s;
    for (const std::string &l : lines) { if (!s.empty()) s += " | "; s += l; }
    if (timeout) s += s.empty() ? "(timeout)" : " (timeout)";
    if (lost) s += s.empty() ? "(port lost)" : " (port lost)";
    if (cme >= 0) s += (s.empty() ? "" : " ") + std::string("(error ") + std::to_string(cme) + ")";
    return s;
}

bool AtChan::is_urc(const std::string &line, const std::string &own_prefix)
{
    if (!own_prefix.empty() && starts(line, own_prefix.c_str())) return false;
    for (const char *p : k_urc)
        if (starts(line, p)) return true;
    return false;
}

bool AtChan::read_more(int timeout_ms)
{
    char buf[1024];
    const int n = port_.read_some(buf, sizeof buf, timeout_ms);
    if (n < 0) { lost_ = true; return false; }
    if (n > 0) acc_.append(buf, size_t(n));
    return true;
}

bool AtChan::take_line(std::string &line)
{
    for (;;) {
        const size_t e = acc_.find('\n');
        if (e == std::string::npos) return false;
        line = at_trim(acc_.substr(0, e));
        acc_.erase(0, e + 1);
        if (!line.empty()) return true;
    }
}

bool AtChan::take_prompt()
{
    size_t i = 0;
    while (i < acc_.size() && (acc_[i] == '\r' || acc_[i] == '\n')) i++;
    if (i >= acc_.size() || acc_[i] != '>') return false;
    i++;
    if (i < acc_.size() && acc_[i] == ' ') i++;
    acc_.erase(0, i);
    return true;
}

void AtChan::pump(int timeout_ms)
{
    if (lost_) return;
    read_more(timeout_ms);
    std::string l;
    while (take_line(l)) {
        if (trace) trace('!', l);
        urcs_.push_back(l);
    }
}

bool AtChan::next_urc(std::string &line)
{
    if (urcs_.empty()) return false;
    line = urcs_.front();
    urcs_.pop_front();
    return true;
}

bool AtChan::wait_urc(const std::string &prefix, std::string &line, int timeout_ms)
{
    const double end = now_s() + timeout_ms / 1000.0;
    for (;;) {
        for (auto it = urcs_.begin(); it != urcs_.end(); ++it)
            if (it->compare(0, prefix.size(), prefix) == 0) { line = *it; urcs_.erase(it); return true; }
        const double left = end - now_s();
        if (left <= 0 || lost_) return false;
        pump(left > 0.1 ? 100 : int(left * 1000) + 1);
    }
}

AtResult AtChan::cmd(const std::string &command, int timeout_ms) { return run(command, nullptr, timeout_ms, false); }
AtResult AtChan::cmd_data(const std::string &command, const std::string &data, int timeout_ms, bool ctrl_z)
{
    return run(command, &data, timeout_ms, ctrl_z);
}

AtResult AtChan::run(const std::string &command, const std::string *data, int timeout_ms, bool ctrl_z)
{
    AtResult r;
    if (lost_) { r.lost = true; return r; }
    // "AT+QMTOPEN=..." / "AT+QMTOPEN?" own the reply prefix "+QMTOPEN:"
    std::string own;
    if (command.size() > 3 && (command[2] == '+' || command[2] == '^')) {
        size_t e = 3;
        while (e < command.size() && command[e] != '=' && command[e] != '?') e++;
        own = command.substr(2, e - 2) + ":";
    }
    if (trace) trace('>', command);
    const std::string out = command + "\r";
    if (!port_.write_all(out.data(), out.size())) { lost_ = true; r.lost = true; return r; }
    Wait wait = data ? Wait::Prompt : Wait::Final;
    const double end = now_s() + timeout_ms / 1000.0;
    for (;;) {
        std::string l;
        while (take_line(l)) {
            if (l == command) continue;                                   // echo (before ATE0 took effect)
            if (l == "OK") { if (trace) trace('<', l); r.ok = true; return r; }
            if (l == "ERROR") { if (trace) trace('<', l); return r; }
            if (starts(l, "+CME ERROR:") || starts(l, "+CMS ERROR:")) {
                if (trace) trace('<', l);
                r.cme = atoi(l.c_str() + 11);
                return r;
            }
            if (is_urc(l, own)) { if (trace) trace('!', l); urcs_.push_back(l); continue; }
            if (trace) trace('<', l);
            r.lines.push_back(l);
        }
        if (wait == Wait::Prompt && take_prompt()) {
            std::string payload = *data;
            if (ctrl_z) payload += '\x1A';
            if (trace) trace('>', "<" + std::to_string(data->size()) + " bytes>");
            if (!port_.write_all(payload.data(), payload.size())) { lost_ = true; r.lost = true; return r; }
            wait = Wait::Final;
            continue;
        }
        const double left = end - now_s();
        if (left <= 0) { r.timeout = true; return r; }
        if (!read_more(left > 0.1 ? 100 : int(left * 1000) + 1)) { r.lost = true; return r; }
    }
}

}  // namespace des
