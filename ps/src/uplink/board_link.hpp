// board_link.hpp — what the 4G uplink knows about the line: a client of des_app's live feed (TCP storage.feed_port,
// frames described in app/part_log.hpp). The feed is push only — this side never sends a byte — so listening changes
// nothing in des_app. (Reading the Modbus-TCP registers instead would: every request counts as "a PLC is online",
// and des_app opens a「PLC 通信中断」work order when the requests stop.)
//
// From the feed: work orders ('W' one order changed, 'w' the list on connect), one CSV line per decided part ('R'),
// the feed statistics every 2 s ('S', used as "des_app is alive"), the NG part crops ('I', JPEG; des_app sends an
// image right before the record that names it). With set_parts() the last parts and the newest images are kept for
// the dashboard's "最近零件" (11-17 user: the same per-part list and pictures as the wired ops station); otherwise
// images are skipped unread. Counts are "parts decided since this program connected", not des_app's session totals.
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
    size_t keep_image_max = 0;   // 'I' frames up to this many bytes keep their JPEG; larger ones (or 0) are skipped unread

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

// One decided part, the fields the dashboard lists (from the feed's 'R' line; 0 / "" = not in the record)
struct PartInfo {
    double rx = 0;                 // when it arrived (BoardLink::now(), monotonic s)
    int track = 0;                 // des_app's track id: unique within the session
    std::string label, disp, dest, why, image;   // image: the crop's name ('I' frame) for NG / recheck parts
    double conf = 0, size_mm = 0;  // size_mm: measured outer diameter (0 = not measured)
    int teeth = 0, missing = 0;    // tooth count / missing teeth when the teeth were judged
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
    // per-part list and pictures (off until called; call before start): keep the last `keep` parts and the newest
    // images up to image_max bytes (0 = no images)
    void set_parts(size_t keep, size_t image_max);
    uint64_t parts_seq();                             // + 1 per part kept
    std::vector<PartInfo> recent_parts(std::string &session);   // oldest first
    bool next_image(std::string &name, std::string &jpeg);      // newest images not taken yet (at most 2 wait)
    static double now();                              // the clock of PartInfo::rx

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
    size_t keep_parts_ = 0, image_max_ = 0;
    std::deque<PartInfo> parts_;
    uint64_t parts_seq_ = 0;
    std::deque<std::pair<std::string, std::string>> images_;   // name, JPEG
    std::atomic<bool> quit_{false};
    std::thread th_;
};

}  // namespace des
