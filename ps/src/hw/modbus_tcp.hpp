// modbus_tcp.hpp — Modbus-TCP slave on the PS Ethernet port (赛题领域 3 高级「工业网口对接产线 PLC」).
//
// Small generic server: holding registers (FC 03 read, FC 06 / 16 write) and input registers (FC 04 read), own thread,
// poll() based, MBAP framing, up to 4 clients, unit id ignored. Writes are only queued: the owner (app/runner) drains
// them every control cycle and applies them with the CAN rules; the Modbus reply echoes the write (standard) and the
// result is a register the PLC reads back. Exceptions: 01 illegal function, 02 illegal address, 03 illegal value
// (bad quantity / length). Register map: docs/软件与UI.md「Modbus-TCP」.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace des {

// register map v1: the board is the slave; same codes / rules as can_proto.h (CMD / ACK / ST / FLAG / DISP_*);
// 0-based addresses (PLC software counting from 1 shows 40001.. / 30001..); 32-bit values high word first.
inline constexpr uint16_t MB_MAP_VERSION = 0x0101;   // 0x0101: + HR 7 LAMP, IR 24..29 RS485
// holding registers (FC 03 read, FC 06 / 16 write): a write is executed in the next control cycle (<= 17 ms), then
// SEQ + 1 and RESULT holds its CANP_ACK_* code
enum : uint16_t {
    MB_HR_CMD = 0,      // write CANP_CMD_*; reads 0
    MB_HR_SPEED = 1,    // belt speed set point, 0.1 mm/s; reads the current set point
    MB_HR_RECIPE = 2,   // recipe id (only while not running); reads the active recipe
    MB_HR_RESULT = 3,   // read only: CANP_ACK_* of the last handled write
    MB_HR_SEQ = 4,      // read only: + 1 per handled write (write, wait for it, read RESULT)
    MB_HR_TIME_HI = 5,  // PLC wall clock, Unix seconds: high word ...
    MB_HR_TIME_LO = 6,  // ... low word (write both in one FC 16; the low word applies it; the board has no RTC)
    MB_HR_LAMP = 7,     // v1.1: tower light on RS485 = CAN LAMP: bit 8 override, bits 0..3 green / yellow / red / buzzer
                        // (0 = back to the line state); reads the override in force
    MB_N_HR = 8,
};
// input registers (FC 04), refreshed every control cycle
enum : uint16_t {
    MB_IR_STATE = 0,       // CANP_ST_*
    MB_IR_FLAGS = 1,       // CANP_FLAG_* (PLC_LINK: CAN heartbeat or Modbus request within 1 s)
    MB_IR_TOTAL = 2,       // 2..9: total / ok / ng / recheck, 32 bit each (hi, lo); CAN counters, wrap at 65535
    MB_IR_OK = 4,
    MB_IR_NG = 6,
    MB_IR_RECHECK = 8,
    MB_IR_PART_SEQ = 10,   // + 1 per decided part (wraps); a gap = parts the PLC did not see
    MB_IR_PART_ID = 11,    // last decided part: track id
    MB_IR_PART_CLS = 12,   // class index (label_list), 0xFFFF = none
    MB_IR_PART_DISP = 13,  // CANP_DISP_*
    MB_IR_PART_DEST = 14,  // bin (pick) / outlet (gate) index
    MB_IR_PART_SIZE = 15,  // gear OD, 0.01 mm (0 = not measured)
    MB_IR_PART_CONF = 16,  // class confidence, %
    MB_IR_SPEED = 17,      // actual belt speed, 0.1 mm/s
    MB_IR_HB = 18,         // + 1 per control cycle
    MB_IR_UPTIME = 19,     // s since start (wraps)
    MB_IR_ALARM = 20,      // alarm code (1 = e-stop), 0 = none
    MB_IR_RECIPE = 21,     // active recipe id
    MB_IR_VERSION = 22,    // MB_MAP_VERSION
    // v1.1, RS485 bus (app/rs485_io): 0 while it is off
    MB_IR_485_FLAGS = 24,  // b0 running, b1 link (the relay module answered within 2 s), b2 PLC lamp override
    MB_IR_485_LAMPS = 25,  // lamps read back from the relay coils (bits as MB_HR_LAMP)
    MB_IR_485_INPUTS = 26, // digital inputs of the module (FC 02), when configured
    MB_IR_485_OK = 27,     // good transactions (wraps)
    MB_IR_485_ERR = 28,    // failed ones: no reply + CRC + framing + exceptions + bad replies (wraps)
    MB_IR_485_RESP = 29,   // last reply time (end of request -> first reply bit), 0.01 ms
    MB_N_IR = 32,
};

class ModbusTcp {
public:
    struct Write { uint16_t addr = 0, value = 0; };
    ModbusTcp(size_t n_holding, size_t n_input) : hr_(n_holding, 0), ir_(n_input, 0) {}
    ~ModbusTcp() { stop(); }
    std::string start(int port);                  // "" on success; port 0 = any free port (tests)
    void stop();
    int port() const { return port_; }
    void set_input(size_t addr, uint16_t v);      // owner thread
    void set_inputs(size_t addr, const std::vector<uint16_t> &v);
    void set_holding(size_t addr, uint16_t v);    // read-back value (e.g. the result register)
    std::vector<Write> take_writes();             // queued writes, oldest first (allocates; tests)
    size_t take_writes(Write *out, size_t cap);   // alloc-free; overflow stays queued for the next cycle
    uint64_t requests() const { return requests_.load(std::memory_order_relaxed); }
    int clients() const { return clients_.load(std::memory_order_relaxed); }

private:
    static constexpr size_t kMaxClients = 4, kInSize = 512;   // one MBAP frame is <= 260 bytes
    struct Conn { int fd = -1; size_t len = 0; char in[kInSize]; };
    void loop();
    bool handle(Conn &c);                                                       // consume frames, reply; false = close
    bool reply(Conn &c, uint16_t tid, uint8_t unit, uint8_t fc, size_t n, uint8_t exc);
    std::vector<uint16_t> hr_, ir_;
    std::deque<Write> writes_;                           // queue depth unbounded (PLC writes are rare)
    std::mutex m_;
    int lfd_ = -1, port_ = 0, wake_[2] = {-1, -1};
    std::atomic<bool> quit_{false};
    std::atomic<uint64_t> requests_{0};
    std::atomic<int> clients_{0};
    std::thread th_;
    char pdu_[256], out_[300];                           // loop-thread scratch for one request / reply
};

}  // namespace des
