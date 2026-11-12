// board_link.hpp — what the 4G uplink knows about the line: a client of des_app's live feed (TCP storage.feed_port,
// frames described in app/part_log.hpp). The feed is push only — this side never sends a byte — so listening changes
// nothing in des_app. (Reading the Modbus-TCP registers instead would: every request counts as "a PLC is online",
// and des_app opens a「PLC 通信中断」work order when the requests stop.)
//
// From the feed: work orders ('W' one order changed, 'w' the list on connect), one CSV line per decided part ('R'),
// the feed statistics every 2 s ('S', used as "des_app is alive"). Images ('I') are skipped unread.
// Counts are therefore "parts decided since this program connected", not des_app's session totals.
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace des {

class FeedParser {
public:
    struct Frame { char type = 0; std::string name, payload; };
    bool feed(const char *data, size_t n, std::vector<Frame> &out);   // false: not this protocol, drop the connection
    void reset() { buf_.clear(); stage_ = 0; skip_ = 0; }

private:
    static constexpr size_t kMaxPayload = 1u << 20;   // work-order list of 200 orders is ~100 KB
    std::string buf_;
    int stage_ = 0;                                   // 0 header, 1 name, 2 payload
    Frame cur_;
    size_t name_len_ = 0, pay_len_ = 0, skip_ = 0;
};

struct BoardSnapshot {
    bool connected = false;        // TCP connection to the feed is up
    double quiet_s = 1e9;          // since the last frame (the feed sends one at least every 2 s)
    std::string session;
    uint64_t total = 0, ok = 0, ng = 0, recheck = 0;   // since this program connected / des_app restarted
    std::string last_label, last_disp, last_dest;
    double last_conf = 0, last_size_mm = 0;
    int wo_active = 0, wo_unacked = 0, wo_level = 0;   // level: 0 none, 1 info, 2 warning, 3 fault
    int wo_top_id = 0;
    std::string wo_top_title;
    uint64_t seq = 0;              // + 1 per change of anything above except quiet_s
    bool alive() const { return connected && quiet_s < 6; }
};

class BoardLink {
public:
    ~BoardLink() { stop(); }
    void start(const std::string &host, int port);
    void stop();
    BoardSnapshot snapshot();
    bool next_order(std::string &json);               // work-order changes, oldest first (bounded queue)
    void on_frame(const FeedParser::Frame &f);        // public for tests
    void on_connect(bool up);

private:
    struct Order { int level = 0; std::string status, title; };
    void loop(std::string host, int port);
    void apply_order(const std::string &json, bool queue);
    void recount();
    std::mutex m_;
    BoardSnapshot s_;
    double last_rx_ = -1e9;
    std::vector<std::string> cols_;                   // CSV header of the session
    std::map<int, Order> orders_;
    std::deque<std::string> changes_;
    std::atomic<bool> quit_{false};
    std::thread th_;
};

}  // namespace des
