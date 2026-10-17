// modbus_tcp.hpp — Modbus-TCP slave on the PS Ethernet port (赛题领域 3 高级「工业网口对接产线 PLC」).
//
// A small generic server: a table of holding registers (FC 03 read, FC 06 / 16 write) and input registers (FC 04
// read). Writes are not applied here: they are queued and the owner (app/runner) drains them every control cycle and
// applies them with the same rules as the CAN commands; the Modbus reply only echoes the write (standard behaviour),
// the result is a register the PLC reads back. Up to 4 clients, own thread, poll() based, MBAP framing; unit id
// ignored. Exceptions: 01 illegal function, 02 illegal address, 03 illegal value (bad quantity / length).
// Register map: docs/软件与UI.md「Modbus-TCP」.
#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace des {

class ModbusTcp {
public:
    struct Write {
        uint16_t addr = 0, value = 0;
    };
    ModbusTcp(size_t n_holding, size_t n_input) : hr_(n_holding, 0), ir_(n_input, 0) {}
    ~ModbusTcp() { stop(); }
    std::string start(int port);                  // "" on success; port 0 = any free port (tests)
    void stop();
    int port() const { return port_; }

    void set_input(size_t addr, uint16_t v);      // owner thread
    void set_inputs(size_t addr, const std::vector<uint16_t> &v);
    void set_holding(size_t addr, uint16_t v);    // read-back value (e.g. the result register)
    std::vector<Write> take_writes();             // queued holding-register writes, oldest first
    uint64_t requests() const { return requests_; }
    int clients() const { return clients_; }

private:
    void loop();
    bool handle(int fd, std::string &in);         // false: close the connection

    std::vector<uint16_t> hr_, ir_;
    std::deque<Write> writes_;
    std::mutex m_;
    int lfd_ = -1, port_ = 0, wake_[2] = {-1, -1};
    bool quit_ = false;
    uint64_t requests_ = 0;
    int clients_ = 0;
    std::thread th_;
};

}  // namespace des
