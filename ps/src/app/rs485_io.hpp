// rs485_io.hpp — the RS485 bus in `des_app run` (赛题基础项 5「双总线并行解析」/ 拓展 3「双总线协议转换」).
//
// One Modbus-RTU slave on the bus today: a 4-relay module (address 1) switching a 24 V tower light — green = running,
// yellow = paused (or running with the PLC link lost), red = e-stop (steady) / fault (steady for fault_s), buzzer for buzzer_s
// when an alarm starts; idle = `idle` (default off: a relay clicking every second is too loud, user 10-28; green
// steady would look like running). The PLC can take the lamps over (CAN 0x105 LAMP or Modbus-TCP HR 7):
// that command arrives on one bus and is executed as a Modbus-RTU write on the other (CAN / Ethernet -> RS485).
//
// Own thread: a transaction blocks until the reply ends or times out (~100 ms) and must never hold up the 60 Hz
// worker, which only stores the line state (set_line) in atomics. Every cycle (50 ms) the thread writes, one relay at a
// time (FC 05), the lamp relays that differ from what the module holds; every poll_ms it reads them back (FC 01; + FC 02
// inputs when configured), and a read-back that differs (module power-cycled, relay stuck) is written again: read-back /
// errors / reply times = the report's bus numbers. (FC 0F is not used: the 10-26 bestep board ignores it unless all
// bits are 0, and answers FC 01 only for 8 coils -> its config has coils = 8.)
//
// Second slave (11-15): an SHT30 temperature / humidity transmitter (th_addr, 9600 8N1, holding registers 0 = temperature
// x 10 signed, 1 = humidity x 10; protocol probed on the device, docs/到货联调清单.md §7b), read every th_period_s in
// the same thread between the lamp transactions. Each slave has its own link state: link_ok() is the relay module
// only, so a transmitter that answers cannot hide a relay module that does not (and the other way round).
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
    uint8_t idle = 0;                // lamps while idle (waiting for START): IDLE_OFF / IDLE_GREEN / IDLE_BLINK
    float fault_s = 10;              // fault: red steady this long after the fault starts, then dark (no blinking: a
                                     // relay clicking every second, user 10-28); < 0 = as long as the fault, 0 = never
    int inputs = 0;                  // digital inputs to read (FC 02) on a "4 in 4 out" module, 0 = none
    int th_addr = 0;                 // temperature / humidity transmitter address, 0 = none (11-15 board: 2)
    float th_period_s = 1;           // its read period
};
enum : uint8_t { LAMP_GREEN = 1, LAMP_YELLOW = 2, LAMP_RED = 4, LAMP_BUZZER = 8 };
// Rs485Config::idle (des_app.json rs485.idle "off" / "green" / "blink")
enum : uint8_t { IDLE_OFF = 0, IDLE_GREEN = 1, IDLE_BLINK = 2 };
// Lamp pattern for a line state (CANP_ST_*): blink = the 1 Hz phase, alarm_age_s = time since the alarm started,
// idle = what the idle state shows (IDLE_*), state_age_s = time in this state, fault_s = Rs485Config::fault_s.
uint8_t lamp_pattern(uint8_t state, bool alarm, bool warn, bool blink, double alarm_age_s, float buzzer_s,
                     uint8_t idle = IDLE_OFF, double state_age_s = 0, float fault_s = -1);
uint32_t lamp_to_coils(uint8_t lamps, const Rs485Config &c);
// "PLC link lost" for the yellow lamp. A PLC counts as there only after its traffic (CAN heartbeat or Modbus
// requests, each within the last second) held for hold_s without a gap: a PLC / SCADA polls all the time, a one-off
// read (mb_client status from a laptop) does not — 10-28 one such read lit yellow for every later run until a restart.
// Stopping the line (idle) forgets it, so a PLC simulator closed after a demo does not keep yellow on next time.
struct PlcWatch {
    double link_since = -1;   // start of the current unbroken link, -1 = no link
    bool seen = false;        // a PLC was there during this run
    bool update(double now, bool link, bool idle, double hold_s = 3)   // true = warn (it was there, now quiet)
    {
        if (!link) link_since = -1;
        else if (link_since < 0) link_since = now;
        if (link && now - link_since >= hold_s) seen = true;
        if (idle) seen = false;
        return seen && !link;
    }
};
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
        // temperature / humidity transmitter (th_addr)
        bool th_valid = false;               // a reading arrived (the values below are the newest)
        float temp_c = 0, hum_rh = 0;
        double th_last_s = -1e9;             // mono time of the last good reading
        uint64_t th_ok = 0, th_err = 0;      // readings / failed or out-of-range reads
    };
    Rs485Io(const Rs485Config &c, RtuXfer xfer);
    ~Rs485Io();                           // stop(): lamps off, thread joined
    void start();
    void stop();
    void set_line(uint8_t state, bool alarm, bool warn);   // worker, every cycle
    void set_override(bool on, uint8_t lamps);             // PLC lamp command (CAN LAMP / Modbus HR 7)
    uint8_t override_lamps() const { return ovr_lamps_; }
    bool override_on() const { return ovr_on_; }
    bool link_ok() const;                 // the relay module answered within the last 2 s
    int th_link() const;                  // transmitter: -1 none configured, 0 quiet for 3 periods (>= 3 s), 1 ok
    Stats stats() const;
private:
    void loop();
    // one checked transaction; bits = decoded FC 01 / 02 reply, regs = FC 03 / 04 reply; false on any failure (counted)
    bool xact(const uint8_t *req, size_t n, uint32_t *bits, uint16_t *regs = nullptr, int max_regs = 0);
    Rs485Config cfg_;
    RtuXfer xfer_;
    std::thread th_;
    std::atomic<bool> run_{false};
    std::atomic<uint8_t> state_{0}, ovr_lamps_{0};
    std::atomic<bool> alarm_{false}, warn_{false}, ovr_on_{false};
    std::atomic<double> last_ok_{-1e9};      // relay module (cfg.addr) only
    std::atomic<double> th_last_ok_{-1e9};   // transmitter (cfg.th_addr) only
    mutable std::mutex m_;   // stats_
    Stats stats_;
};
}  // namespace des
