// net_feed.hpp — live upload of the inspection records over Ethernet (赛题拓展 5「千兆网数据上传」).
//
// The board listens on a TCP port; a PC client (pc/feed/des_feed.py) connects and receives every record the part log
// writes: the CSV line and, when the log keeps it, the part image. The board is the server so the PC needs no inbound
// firewall rule. Never blocks the part log: each client has a bounded queue and frames are dropped (counted) when it
// lags; a dead client is just closed.
//
// Frame: 10-byte header, big-endian: 'D' 'F' <type> 0 <name_len:2> <payload_len:4>, then name, then payload.
//   'H' hello   (on connect)  name = session,           payload = CSV header line
//   'R' record                name = "",                payload = CSV line (no newline)
//   'I' image                 name = img/<file>.jpg,    payload = JPEG bytes
//   'S' status  (every 2 s)   name = "",                payload = "records=<n> images=<n> dropped=<n>"
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace des {

class NetFeed {
public:
    ~NetFeed() { stop(); }
    std::string start(int port, const std::string &session, const std::string &csv_header, size_t max_queue = 64);
    void stop();                                    // flushes queued frames (up to ~0.5 s), closes everything
    void record(const std::string &csv_line);
    void image(const std::string &name, const std::vector<uint8_t> &jpeg);
    int port() const { return port_; }             // the bound port (start(0, ...) picks a free one)

    std::atomic<uint64_t> records{0}, images{0}, dropped{0}, clients{0};

private:
    struct Client {
        int fd = -1;
        std::vector<std::shared_ptr<const std::string>> q;
        size_t off = 0;                             // bytes of q.front() already sent
    };
    void loop();
    void push(char type, const std::string &name, const char *data, size_t n);
    static std::shared_ptr<const std::string> frame(char type, const std::string &name, const char *data, size_t n);

    int lfd_ = -1, port_ = 0, wake_[2] = {-1, -1};
    std::string session_, header_;
    size_t max_queue_ = 64;
    std::mutex m_;
    std::vector<Client> cl_;
    bool quit_ = false;
    std::thread th_;
};

}  // namespace des
