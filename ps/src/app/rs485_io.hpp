// rs485_io.hpp — the RS485 bus in `des_app run` (赛题基础项 5「双总线并行解析」/ 拓展 3「双总线协议转换」).
//
// One Modbus-RTU slave on the bus today: a 4-relay module (address 1) switching a 24 V tower light — green = running,
// yellow = paused (or running with the PLC link lost), red = e-stop (steady) / fault (blinking), buzzer for buzzer_s
// when an alarm starts, green blinking = idle. The PLC can take the lamps over (CAN 0x105 LAMP or Modbus-TCP HR 7):
// that command arrives on one bus and is executed as a Modbus-RTU write on the other (CAN / Ethernet -> RS485).
//
// Own thread: a transaction blocks until the reply ends or times out (~100 ms) and must never hold up the 60 Hz
// worker, which only stores the line state (set_line) in atomics. Every cycle (50 ms) the thread writes, one relay at a
// time (FC 05), the lamp relays that differ from what the module holds; every poll_ms it reads them back (FC 01; + FC 02
// inputs when configured), and a read-back that differs (module power-cycled, relay stuck) is written again: read-back /
// errors / reply times = the report's bus numbers. (FC 0F is not used: the 10-26 bestep board ignores it unless all
// bits are 0, and answers FC 01 only for 8 coils -> its config has coils = 8.)
//
// The bus is the PL rs485 module (des_485_xfer, bitstream v16): it adds / checks the CRC16, drives DE and frames the
// reply by the 3.5-character silence. rs485_hw_xfer() binds it; tests pass a simulated slave instead.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include "des_hw.h"
namespace des {
struct Rs485Config {
    bool enabled = false;
    int baud = 9600;
    char parity = 'N';               // N / E / O (8 data bits, 1 stop bit)
    int timeout_ms = 100;            // reply timeout (end of request -> first reply byte)
    int addr = 1;                    // relay module address
    int coils = 4;                   // coils asked for in the FC 01 read-back (the bestep board answers only 8); only the
                                     // lamp_coil relays are ever written
    int lamp_coil[4] = {0, 1, 2, 3}; // relay of green / yellow / red / buzzer (-1 = not wired)
    int poll_ms = 200;               // read-back period
    float buzzer_s = 1;              // buzzer this long when an alarm starts (0 = never, < 0 = as long as the alarm)
    int inputs = 0;                  // digital inputs to read (FC 02) on a "4 in 4 out" module, 0 = none
};
enum : uint8_t { LAMP_GREEN = 1, LAMP_YELLOW = 2, LAMP_RED = 4, LAMP_BUZZER = 8 };
// Lamp pattern for a line state (CANP_ST_*): blink = the 1 Hz phase, alarm_age_s = time since the alarm started.
uint8_t lamp_pattern(uint8_t state, bool alarm, bool warn, bool blink, double alarm_age_s, float buzzer_s);
uint32_t lamp_to_coils(uint8_t lamps, const Rs485Config &c);
uint8_t coils_to_lamps(uint32_t coils, const Rs485Config &c);

// One RS485 transaction: request [addr, fc, data...] without CRC; reply likewise. rc: RTU_* below.
enum : int { RTU_OK = 0, RTU_TIMEOUT = 1, RTU_CRC = 2, RTU_RXERR = 3, RTU_FAIL = 4 };
struct RtuXferResult {
    int rc = RTU_FAIL;
    int n = 0;
    uint8_t raw[64] = {};
    double resp_ms = -1;   // end of request -> first reply bit (the slave's turnaround), -1 = unknown
    double wire_ms = -1;   // first request bit -> last reply bit
};
using RtuXfer = std::function<void(const uint8_t *req, int n, bool expect_reply, RtuXferResult &r)>;
// des_hw binding (des_485_config + des_485_xfer, bitstream >= v16). Empty function + err when this build or the
// bitstream has no RS485.
RtuXfer rs485_hw_xfer(des_hw_t *hw, const Rs485Config &c, std::string &err);

class Rs485Io {
public:
    struct Stats {
        uint64_t xfers = 0, ok = 0, timeouts = 0, crc = 0, rx_err = 0, exceptions = 0, bad_reply = 0, other = 0;
        uint64_t writes = 0, mismatches = 0;   // coil writes; read-backs that differed from what was written
        double resp_last = -1, resp_min = 1e9, resp_max = 0, resp_sum = 0;   // ms, replies with a timing
        uint64_t resp_n = 0;
        uint32_t coils_rb = 0, inputs = 0;   // last read-back
        uint8_t lamps = 0;                   // pattern last written
        bool override_on = false;
        double last_ok_s = -1e9;             // mono time of the last good transaction
        std::string last_err;
    };
    Rs485Io(const Rs485Config &c, RtuXfer xfer);
    ~Rs485Io();                           // stop(): lamps off, thread joined
    void start();
    void stop();
    void set_line(uint8_t state, bool alarm, bool warn);   // worker, every cycle
    void set_override(bool on, uint8_t lamps);             // PLC lamp command (CAN LAMP / Modbus HR 7)
    uint8_t override_lamps() const { return ovr_lamps_; }
    bool override_on() const { return ovr_on_; }
    bool link_ok() const;                 // a good transaction within the last 2 s
    Stats stats() const;
private:
    void loop();
    // one checked transaction; bits = decoded FC 01 / 02 reply; false on any failure (counted)
    bool xact(const uint8_t *req, size_t n, uint32_t *bits);
    Rs485Config cfg_;
    RtuXfer xfer_;
    std::thread th_;
    std::atomic<bool> run_{false};
    std::atomic<uint8_t> state_{0}, ovr_lamps_{0};
    std::atomic<bool> alarm_{false}, warn_{false}, ovr_on_{false};
    std::atomic<double> last_ok_{-1e9};
    mutable std::mutex m_;   // stats_
    Stats stats_;
};
}  // namespace des
