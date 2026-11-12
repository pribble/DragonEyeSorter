// at_chan.hpp — AT command channel over an AtPort: one command at a time, replies and unsolicited lines told apart.
//
// A command ends with a final result line (OK / ERROR / +CME ERROR: n / +CMS ERROR: n) or a timeout. Lines that are
// not part of the reply are unsolicited result codes (URC: the modem reporting on its own — an MQTT message arrived,
// the connection dropped, an earlier command finished its network part). They are queued and read with next_urc().
// While a command is in flight a line is a URC when it starts with one of the known URC prefixes and does NOT start
// with the command's own prefix ("AT+QMTOPEN?" owns "+QMTOPEN:"); between commands every line is a URC.
// Single-threaded: the owner calls cmd() / pump() from one loop.
#pragma once

#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "at_port.hpp"

namespace des {

struct AtResult {
    bool ok = false, timeout = false, lost = false;
    int cme = -1;                     // +CME / +CMS ERROR code, -1 when none
    std::vector<std::string> lines;   // information lines before the final result
    std::string first(const std::string &prefix = "") const;   // first line starting with prefix, prefix stripped + trimmed
    std::string text() const;         // lines joined with " | " (logs)
};

class AtChan {
public:
    explicit AtChan(AtPort &port) : port_(port) {}
    AtResult cmd(const std::string &command, int timeout_ms = 1000);
    // commands that answer "> " and then take raw bytes (AT+QMTPUBEX); ctrl_z appends 0x1A for the open-ended forms
    AtResult cmd_data(const std::string &command, const std::string &data, int timeout_ms, bool ctrl_z = false);
    void pump(int timeout_ms);                    // read once, queue complete URC lines
    bool next_urc(std::string &line);             // oldest queued URC
    bool wait_urc(const std::string &prefix, std::string &line, int timeout_ms);   // others stay queued, in order
    bool lost() const { return lost_; }           // the port went away (unplugged / module reset): reopen
    void reset() { acc_.clear(); urcs_.clear(); lost_ = false; }
    std::function<void(char dir, const std::string &line)> trace;   // '>' sent, '<' received, '!' URC

private:
    enum class Wait { Final, Prompt };
    AtResult run(const std::string &command, const std::string *data, int timeout_ms, bool ctrl_z);
    bool read_more(int timeout_ms);               // false: port lost
    bool take_line(std::string &line);            // one complete line out of acc_ (CR / LF stripped, empty skipped)
    bool take_prompt();                           // acc_ starts with '>'
    static bool is_urc(const std::string &line, const std::string &own_prefix);
    AtPort &port_;
    std::string acc_;                             // received bytes not yet split into lines
    std::deque<std::string> urcs_;
    bool lost_ = false;
};

std::string at_trim(const std::string &s);
std::vector<std::string> at_fields(const std::string &s);   // split on commas outside quotes, quotes removed, trimmed

}  // namespace des
