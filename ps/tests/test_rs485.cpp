// test_rs485: Modbus-RTU PDUs (hw/modbus_rtu) and the RS485 lamp worker (app/rs485_io) against a simulated 4-relay
// module. No hardware: runs on the board or any Linux host.   ./test_rs485   (exit code = failed checks)
#include <unistd.h>
#include <atomic>
#include <cstring>
#include <vector>
#include "can_proto.h"
#include "modbus_rtu.hpp"
#include "rs485_io.hpp"
#include "test_check.hpp"
#include "util.hpp"
using namespace des;
namespace {
std::vector<uint8_t> bytes(std::initializer_list<int> l)
{
    std::vector<uint8_t> v;
    for (int x : l) v.push_back(uint8_t(x));
    return v;
}
// What a cheap "Modbus RTU 4-channel relay" board answers (FC 01 / 02 / 05 / 0F; anything else: exception 1).
struct SimRelay {
    uint8_t addr = 1;
    std::atomic<uint32_t> coils{0};
    uint32_t inputs = 0x5;
    std::atomic<bool> offline{false};
    std::atomic<uint32_t> stuck_low{0};   // relays that never close (read back 0)
    std::atomic<int> requests{0};
    // the 10-26 bestep 4-channel board: FC 01 / 02 answered only for 8 coils, FC 0F ignored unless every bit is 0
    bool bestep = false;
    std::atomic<int> fc0f{0};             // FC 0F requests seen (the worker must not need them)
    void xfer(const uint8_t *q, int n, bool expect, RtuXferResult &r)
    {
        requests++;
        r = RtuXferResult();
        if (offline || q[0] != addr || n < 6) { r.rc = expect ? RTU_TIMEOUT : RTU_OK; return; }
        const uint16_t a = uint16_t(q[2] << 8 | q[3]), c = uint16_t(q[4] << 8 | q[5]);
        if (q[1] == RTU_WRITE_COILS) fc0f++;
        if (bestep && (q[1] == RTU_READ_COILS || q[1] == RTU_READ_INPUTS) && c != 8) { r.rc = RTU_TIMEOUT; return; }
        if (bestep && q[1] == RTU_WRITE_COILS)
            for (int i = 7; i < n; i++)
                if (q[i]) { r.rc = RTU_TIMEOUT; return; }
        r.rc = RTU_OK; r.resp_ms = 3.5; r.wire_ms = 20;
        uint8_t *o = r.raw;
        o[0] = addr;
        switch (q[1]) {
        case RTU_READ_COILS:
        case RTU_READ_INPUTS: {
            const uint32_t v = (q[1] == RTU_READ_COILS ? coils.load() & ~stuck_low.load() : inputs) >> a;
            const int nb = (c + 7) / 8;
            o[1] = q[1]; o[2] = uint8_t(nb);
            for (int i = 0; i < nb; i++) o[3 + i] = uint8_t(v >> (8 * i)) & (i == nb - 1 && c % 8 ? (1 << (c % 8)) - 1 : 0xFF);
            r.n = 3 + nb;
            return;
        }
        case RTU_WRITE_COIL: {
            uint32_t v = coils;
            v = c == 0xFF00 ? v | (1u << a) : v & ~(1u << a);
            coils = v;
            memcpy(o, q, 6); r.n = 6;
            return;
        }
        case RTU_WRITE_COILS: {
            uint32_t v = coils;
            for (int i = 0; i < c; i++) {
                const bool on = q[7 + i / 8] >> (i % 8) & 1;
                v = on ? v | (1u << (a + i)) : v & ~(1u << (a + i));
            }
            coils = v;
            memcpy(o, q, 6); r.n = 6;
            return;
        }
        default:
            o[1] = uint8_t(q[1] | 0x80); o[2] = 1; r.n = 3;
        }
    }
};
// The 11-15 SHT30 transmitter: FC 03 / 04 registers 0 = temperature x 10 (signed), 1 = humidity x 10.
struct SimTh {
    uint8_t addr = 2;
    std::atomic<int> temp10{228}, hum10{583};
    std::atomic<bool> offline{false};
    std::atomic<int> reads{0};
    bool xfer(const uint8_t *q, int n, RtuXferResult &r)   // true = it was for this device
    {
        if (n < 6 || q[0] != addr) return false;
        r = RtuXferResult();
        if (offline) { r.rc = RTU_TIMEOUT; return true; }
        reads++;
        const uint16_t a = uint16_t(q[2] << 8 | q[3]), c = uint16_t(q[4] << 8 | q[5]);
        r.rc = RTU_OK; r.resp_ms = 3.4; r.wire_ms = 18;
        uint8_t *o = r.raw;
        o[0] = addr; o[1] = q[1];
        if ((q[1] != RTU_READ_HOLDING && q[1] != RTU_READ_INPUT_REGS) || a + c > 20) { o[1] |= 0x80; o[2] = 2; r.n = 3; return true; }
        o[2] = uint8_t(2 * c);
        for (int i = 0; i < c; i++) {
            const int v = a + i == 0 ? temp10.load() : a + i == 1 ? hum10.load() : 0;
            o[3 + 2 * i] = uint8_t(uint16_t(v) >> 8); o[4 + 2 * i] = uint8_t(v);
        }
        r.n = 3 + 2 * c;
        return true;
    }
};
void test_pdu()
{
    const auto r10 = bytes({0x01, 0x03, 0x00, 0x00, 0x00, 0x0A});
    CHECK(rtu_crc16(r10.data(), r10.size()) == 0xCDC5, "CRC of 01 03 00 00 00 0A = C5 CD (%04X)", rtu_crc16(r10.data(), r10.size()));
    const auto w6 = bytes({0x01, 0x06, 0x00, 0x01, 0x00, 0x03});
    CHECK(rtu_crc16(w6.data(), w6.size()) == 0x0B98, "CRC of 01 06 00 01 00 03 = 98 0B");
    uint8_t q[16];
    size_t n = rtu_write_coils(q, 1, 0, 4, 0x15);   // bit 4 is outside the 4 coils: dropped
    CHECK(n == 8 && !memcmp(q, bytes({1, 0x0F, 0, 0, 0, 4, 1, 5}).data(), 8), "write coils 0..3 = 0101b");
    CHECK(rtu_crc16(q, n) == 0x95FE, "its CRC FE 95");
    n = rtu_read_bits(q, 1, RTU_READ_COILS, 0, 4);
    CHECK(n == 6 && !memcmp(q, bytes({1, 1, 0, 0, 0, 4}).data(), 6), "read coils request");
    CHECK(rtu_read_bits(q, 1, RTU_WRITE_COIL, 0, 4) == 0 && rtu_read_bits(q, 1, RTU_READ_COILS, 0, 0) == 0 &&
          rtu_read_regs(q, 1, RTU_READ_HOLDING, 0, 30) == 0 && rtu_write_coils(q, 1, 0, 33, 0) == 0, "bad arguments refused");
    n = rtu_write_coil(q, 2, 3, true);
    CHECK(n == 6 && !memcmp(q, bytes({2, 5, 0, 3, 0xFF, 0}).data(), 6), "write single coil FF00");
    n = rtu_write_reg(q, 1, 1, 3);
    CHECK(n == 6 && !memcmp(q, w6.data(), 6), "write register");
    // replies
    uint8_t rq[8];
    const size_t nr = rtu_read_bits(rq, 1, RTU_READ_COILS, 0, 4);
    uint32_t bits = 0;
    uint8_t exc = 0;
    auto rep = bytes({1, 1, 1, 0x05});
    CHECK(!rtu_check_reply(rq, nr, rep.data(), rep.size(), &bits, nullptr, 0, &exc) && bits == 5, "read coils reply -> 0101b");
    rep = bytes({1, 0x81, 2});
    const char *w = rtu_check_reply(rq, nr, rep.data(), rep.size(), &bits, nullptr, 0, &exc);
    CHECK(w && !strcmp(w, "exception") && exc == 2, "exception reply (illegal address)");
    CHECK(!strcmp(rtu_exception_text(2), "illegal data address"), "exception text");
    rep = bytes({2, 1, 1, 0x05});
    CHECK(rtu_check_reply(rq, nr, rep.data(), rep.size(), &bits, nullptr, 0, &exc) != nullptr, "other address refused");
    rep = bytes({1, 1, 2, 0x05, 0});
    CHECK(rtu_check_reply(rq, nr, rep.data(), rep.size(), &bits, nullptr, 0, &exc) != nullptr, "byte count checked");
    rep = bytes({1, 3, 1, 0x05});
    CHECK(rtu_check_reply(rq, nr, rep.data(), rep.size(), &bits, nullptr, 0, &exc) != nullptr, "other function refused");
    n = rtu_write_coils(q, 1, 0, 4, 5);
    rep = bytes({1, 0x0F, 0, 0, 0, 4});
    CHECK(!rtu_check_reply(q, n, rep.data(), rep.size(), nullptr, nullptr, 0, &exc), "write coils echo");
    rep = bytes({1, 0x0F, 0, 0, 0, 3});
    CHECK(rtu_check_reply(q, n, rep.data(), rep.size(), nullptr, nullptr, 0, &exc) != nullptr, "wrong echo refused");
    uint8_t rr[8];
    const size_t nrr = rtu_read_regs(rr, 1, RTU_READ_HOLDING, 0, 2);
    uint16_t regs[2] = {};
    rep = bytes({1, 3, 4, 0x12, 0x34, 0xAB, 0xCD});
    CHECK(!rtu_check_reply(rr, nrr, rep.data(), rep.size(), nullptr, regs, 2, &exc) && regs[0] == 0x1234 && regs[1] == 0xABCD,
          "read registers, big-endian");
}
void test_plc_watch()
{
    // 10-28: one Modbus read from a laptop (link for ~1 s) then nothing: never "lost"
    PlcWatch w;
    bool warn = false;
    for (double t = 0; t < 10; t += 0.05) warn |= w.update(t, t >= 2 && t < 3, false);
    CHECK(!warn && !w.seen, "one-off read: no PLC, no yellow");
    // a polling PLC (link held) that goes quiet while running: yellow, back when it returns
    PlcWatch p;
    bool before = false;
    for (double t = 0; t < 4; t += 0.05) before |= p.update(t, true, false);
    CHECK(!before && p.seen, "PLC there after 3 s of traffic, no warning while it talks");
    CHECK(p.update(4.1, false, false) && p.update(9, false, false), "PLC quiet while running: yellow");
    CHECK(!p.update(9.1, true, false), "PLC back: yellow off");
    // stop the line with the PLC gone, start again: no yellow (forgotten), until a PLC talks 3 s and drops again
    p.update(9.2, false, false);
    CHECK(!p.update(9.3, false, true) && !p.seen, "stopped: forgotten");
    CHECK(!p.update(20, false, false), "next run without a PLC: no yellow");
    // PLC talking through the stop: the next run knows it at once
    PlcWatch q;
    for (double t = 0; t < 5; t += 0.05) q.update(t, true, true);
    CHECK(!q.update(5.05, true, false) && q.seen, "run start with a live PLC: seen right away");
    CHECK(q.update(5.2, false, false), "and its loss warns");
}
void test_lamps()
{
    CHECK(lamp_pattern(CANP_ST_RUN, false, false, true, 0, 2) == LAMP_GREEN, "run: green");
    CHECK(lamp_pattern(CANP_ST_RUN, false, true, true, 0, 2) == (LAMP_GREEN | LAMP_YELLOW), "run + PLC link lost: + yellow");
    CHECK(lamp_pattern(CANP_ST_PAUSED, false, false, false, 0, 2) == LAMP_YELLOW, "paused: yellow");
    CHECK(lamp_pattern(CANP_ST_ESTOP, true, false, true, 0.5, 2) == (LAMP_RED | LAMP_BUZZER), "e-stop: red + buzzer at first");
    CHECK(lamp_pattern(CANP_ST_ESTOP, true, false, true, 3, 2) == LAMP_RED, "buzzer stops after buzzer_s");
    CHECK(lamp_pattern(CANP_ST_ESTOP, true, false, true, 30, -1) == (LAMP_RED | LAMP_BUZZER), "buzzer_s < 0: as long as the alarm");
    CHECK(lamp_pattern(CANP_ST_ESTOP, true, false, true, 0, 0) == LAMP_RED, "buzzer_s 0: silent");
    CHECK(lamp_pattern(CANP_ST_FAULT, true, false, true, 9, 2, IDLE_OFF, 3, 10) == LAMP_RED &&
              lamp_pattern(CANP_ST_FAULT, true, false, false, 9, 2, IDLE_OFF, 3, 10) == LAMP_RED,
          "fault: red steady (no blinking) within fault_s");
    CHECK(lamp_pattern(CANP_ST_FAULT, true, false, true, 9, 2, IDLE_OFF, 12, 10) == 0, "fault: dark after fault_s");
    CHECK(lamp_pattern(CANP_ST_FAULT, true, false, false, 9, 2, IDLE_OFF, 600, -1) == LAMP_RED,
          "fault_s < 0: red as long as the fault");
    CHECK(lamp_pattern(CANP_ST_FAULT, true, false, true, 9, 2, IDLE_OFF, 0, 0) == 0, "fault_s 0: never lit");
    CHECK(lamp_pattern(CANP_ST_ESTOP, true, false, false, 3, 2, IDLE_OFF, 600, 10) == LAMP_RED,
          "e-stop: red steady regardless of fault_s");
    CHECK(lamp_pattern(CANP_ST_IDLE, false, false, true, 0, 2) == 0 && lamp_pattern(CANP_ST_IDLE, false, false, false, 0, 2) == 0,
          "idle (default): dark");
    CHECK(lamp_pattern(CANP_ST_IDLE, false, false, true, 0, 2, IDLE_GREEN) == LAMP_GREEN &&
              lamp_pattern(CANP_ST_IDLE, false, false, false, 0, 2, IDLE_GREEN) == LAMP_GREEN,
          "idle green: steady");
    CHECK(lamp_pattern(CANP_ST_IDLE, false, false, true, 0, 2, IDLE_BLINK) == LAMP_GREEN &&
              lamp_pattern(CANP_ST_IDLE, false, false, false, 0, 2, IDLE_BLINK) == 0,
          "idle blink: green blinking");
    CHECK(lamp_pattern(CANP_ST_RUN, false, false, false, 0, 2, IDLE_OFF) == LAMP_GREEN, "idle setting leaves run alone");
    Rs485Config c;
    c.lamp_coil[0] = 3; c.lamp_coil[1] = 2; c.lamp_coil[2] = 1; c.lamp_coil[3] = -1;   // wired the other way round, no buzzer
    CHECK(lamp_to_coils(LAMP_GREEN | LAMP_RED | LAMP_BUZZER, c) == 0xA, "lamps -> relays by lamp_coil (%X)", lamp_to_coils(LAMP_GREEN | LAMP_RED | LAMP_BUZZER, c));
    CHECK(coils_to_lamps(0xA, c) == (LAMP_GREEN | LAMP_RED), "relays -> lamps");
    Rs485Config bad = c;
    bad.lamp_coil[0] = -1;          // unwired lamp: its bit is ignored, not shifted into oblivion
    bad.lamp_coil[1] = c.coils;     // beyond the module's coils: ignored too
    CHECK(lamp_to_coils(LAMP_GREEN | LAMP_YELLOW, bad) == 0, "unwired / out-of-range coils ignored (%X)",
          lamp_to_coils(LAMP_GREEN | LAMP_YELLOW, bad));
    CHECK(coils_to_lamps(0xFFFFFFFFu, bad) == LAMP_RED,
          "read-back clamps to the wired lamps (%X)", coils_to_lamps(0xFFFFFFFFu, bad));
}
bool wait_for(const std::function<bool()> &f, double s)
{
    const double t = mono_s();
    while (mono_s() - t < s) {
        if (f()) return true;
        usleep(10000);
    }
    return f();
}
void test_worker()
{
    SimRelay sim;
    Rs485Config c;
    c.enabled = true; c.poll_ms = 100; c.buzzer_s = 2; c.inputs = 4;
    Rs485Io io(c, [&sim](const uint8_t *q, int n, bool e, RtuXferResult &r) { sim.xfer(q, n, e, r); });
    io.set_line(CANP_ST_RUN, false, false);
    io.start();
    CHECK(wait_for([&] { return sim.coils == 0x1; }, 1.0), "running: green relay on (%X)", sim.coils.load());
    CHECK(wait_for([&] { return io.stats().coils_rb == 0x1 && io.stats().inputs == 0x5; }, 1.0), "read-back + inputs");
    CHECK(io.link_ok(), "link ok");
    io.set_line(CANP_ST_ESTOP, true, false);
    CHECK(wait_for([&] { return sim.coils == (0x4 | 0x8); }, 1.0), "e-stop: red + buzzer (%X)", sim.coils.load());
    io.set_override(true, LAMP_YELLOW | LAMP_RED);
    CHECK(wait_for([&] { return sim.coils == 0x6; }, 1.0), "PLC override wins (%X)", sim.coils.load());
    io.set_override(false, 0);
    CHECK(wait_for([&] { return (sim.coils & 0x7) == 0x4; }, 1.0), "override off: back to the line state");
    sim.stuck_low = 0x4;   // the red relay does not close
    CHECK(wait_for([&] { return io.stats().mismatches > 0; }, 1.5), "read-back mismatch counted");
    sim.stuck_low = 0;
    sim.offline = true;
    CHECK(wait_for([&] { return io.stats().timeouts > 0; }, 1.0), "no reply -> timeout counted");
    CHECK(wait_for([&] { return !io.link_ok(); }, 3.0), "link lost after 2 s without a good reply");
    sim.offline = false;
    CHECK(wait_for([&] { return io.link_ok(); }, 1.5), "link back");
    Rs485Io::Stats s = io.stats();
    CHECK(s.ok > 5 && s.writes > 2 && s.resp_n > 0 && s.resp_last == 3.5 && s.exceptions == 0, "statistics (%llu ok, %llu writes)",
          (unsigned long long)s.ok, (unsigned long long)s.writes);
    io.stop();
    CHECK(sim.coils == 0, "stop: light dark");
    // a module at another address answers nothing: everything times out, nothing is "ok"
    SimRelay other;
    other.addr = 7;
    Rs485Io io2(c, [&other](const uint8_t *q, int n, bool e, RtuXferResult &r) { other.xfer(q, n, e, r); });
    io2.start();
    usleep(300000);
    io2.stop();
    s = io2.stats();
    CHECK(s.ok == 0 && s.timeouts > 0 && !io2.link_ok(), "wrong address: only timeouts");
    Rs485Io idle(c, RtuXfer());   // no driver: start() does nothing
    idle.start();
    CHECK(!idle.link_ok(), "no transport: never linked");
}
// relay module + temperature / humidity transmitter on one bus: readings, separate link states, sanity check
void test_transmitter()
{
    SimRelay relay;
    SimTh th;
    Rs485Config c;
    c.enabled = true; c.poll_ms = 100; c.buzzer_s = 0; c.th_addr = 2; c.th_period_s = 0.2f;
    Rs485Io io(c, [&](const uint8_t *q, int n, bool e, RtuXferResult &r) { if (!th.xfer(q, n, r)) relay.xfer(q, n, e, r); });
    io.set_line(CANP_ST_RUN, false, false);
    io.start();
    CHECK(wait_for([&] { return io.stats().th_valid; }, 1.5), "transmitter read");
    Rs485Io::Stats s = io.stats();
    CHECK(s.temp_c > 22.79f && s.temp_c < 22.81f && s.hum_rh > 58.29f && s.hum_rh < 58.31f, "22.8 C / 58.3 %%RH (%.2f / %.2f)",
          double(s.temp_c), double(s.hum_rh));
    CHECK(io.link_ok() && io.th_link() == 1 && relay.coils == 0x1, "both links ok, lamp still driven");
    th.temp10 = -55;   // -5.5 C: signed register
    CHECK(wait_for([&] { return io.stats().temp_c < -5.4f && io.stats().temp_c > -5.6f; }, 1.0), "negative temperature (%.2f)",
          double(io.stats().temp_c));
    th.hum10 = 1500;   // 150 %RH: not a reading
    const uint64_t err0 = io.stats().th_err;
    CHECK(wait_for([&] { return io.stats().th_err > err0; }, 1.0) && io.stats().hum_rh < 100.1f, "out-of-range reading refused, old value kept");
    th.hum10 = 583;
    relay.offline = true;   // relay module gone, transmitter still answers: the relay link must go down anyway
    CHECK(wait_for([&] { return !io.link_ok(); }, 3.0) && io.th_link() == 1, "relay quiet: its link down, transmitter's up");
    relay.offline = false;
    CHECK(wait_for([&] { return io.link_ok(); }, 1.5), "relay back");
    th.offline = true;     // transmitter gone, relay still answers
    CHECK(wait_for([&] { return io.th_link() == 0; }, 4.5) && io.link_ok(), "transmitter quiet: its link down after 3 s, relay's up");
    io.stop();
    Rs485Config none = c;
    none.th_addr = 0;
    Rs485Io io2(none, RtuXfer());
    CHECK(io2.th_link() == -1, "no transmitter configured: th_link -1");
}
// the real 10-26 board (bestep 4-ch): with coils = 8 the worker lights the lamps by FC 05 and reads them back
void test_bestep_board()
{
    SimRelay sim;
    sim.bestep = true;
    Rs485Config c;
    c.enabled = true; c.poll_ms = 100; c.buzzer_s = 0; c.coils = 8;
    {   // default idle (10-28): dark the whole time — no relay clicking while the line stands
        SimRelay quiet;
        quiet.bestep = true;
        Rs485Io qio(c, [&quiet](const uint8_t *q, int n, bool e, RtuXferResult &r) { quiet.xfer(q, n, e, r); });
        qio.set_line(CANP_ST_IDLE, false, false);
        qio.start();
        bool lit = false;
        for (int i = 0; i < 75; i++) { lit |= quiet.coils != 0; usleep(20000); }
        qio.stop();
        CHECK(!lit && quiet.requests > 0, "bestep: idle (default off) stays dark over 1.5 s");
    }
    c.idle = IDLE_BLINK;   // the old idle pattern, still selectable (rs485.idle "blink")
    Rs485Io io(c, [&sim](const uint8_t *q, int n, bool e, RtuXferResult &r) { sim.xfer(q, n, e, r); });
    io.set_line(CANP_ST_IDLE, false, false);   // idle blink: green at 1 Hz
    io.start();
    CHECK(wait_for([&] { return sim.coils == 0x1; }, 1.5) && wait_for([&] { return sim.coils == 0x0; }, 1.5) &&
              wait_for([&] { return sim.coils == 0x1; }, 1.5),
          "bestep: idle green blinks (%X)", sim.coils.load());
    io.set_line(CANP_ST_RUN, false, false);
    CHECK(wait_for([&] { return sim.coils == 0x1 && io.stats().coils_rb == 0x1; }, 1.0), "bestep: green on, read back (%X)",
          sim.coils.load());
    io.set_override(true, LAMP_YELLOW | LAMP_RED);
    CHECK(wait_for([&] { return sim.coils == 0x6 && io.stats().coils_rb == 0x6; }, 1.0), "bestep: yellow + red (%X)",
          sim.coils.load());
    io.set_override(false, 0);
    io.set_line(CANP_ST_PAUSED, false, false);
    CHECK(wait_for([&] { return sim.coils == 0x2; }, 1.0), "bestep: paused = yellow only (%X)", sim.coils.load());
    usleep(300000);
    Rs485Io::Stats s = io.stats();
    CHECK(io.link_ok() && s.timeouts == 0 && s.mismatches == 0, "bestep: no timeouts / mismatches (%llu / %llu)",
          (unsigned long long)s.timeouts, (unsigned long long)s.mismatches);
    sim.coils = 0;   // module power-cycled: the read-back shows it and the lamp comes back
    CHECK(wait_for([&] { return sim.coils == 0x2; }, 1.0), "bestep: relay state restored after a module reset");
    io.stop();
    CHECK(sim.coils == 0 && sim.fc0f == 0, "bestep: stop leaves it dark, FC 0F never used (%d)", sim.fc0f.load());
    // the same board with coils = 4: every read times out (why the config says 8)
    Rs485Config c4 = c;
    c4.coils = 4;
    SimRelay sim4;
    sim4.bestep = true;
    Rs485Io io4(c4, [&sim4](const uint8_t *q, int n, bool e, RtuXferResult &r) { sim4.xfer(q, n, e, r); });
    io4.set_line(CANP_ST_RUN, false, false);
    io4.start();
    usleep(400000);
    io4.stop();
    CHECK(io4.stats().timeouts > 0 && io4.stats().coils_rb == 0, "bestep with coils = 4: read-backs time out");
}
}  // namespace
int main()
{
    test_pdu();
    test_plc_watch();
    test_lamps();
    test_worker();
    test_bestep_board();
    test_transmitter();
    return test_summary("test_rs485");
}
