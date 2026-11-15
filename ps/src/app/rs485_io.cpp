// rs485_io.cpp — see rs485_io.hpp
#include "rs485_io.hpp"
#include <unistd.h>
#include <algorithm>
#include <cstring>
#include "can_proto.h"
#include "modbus_rtu.hpp"
#include "util.hpp"
namespace des {
uint8_t lamp_pattern(uint8_t state, bool alarm, bool warn, bool blink, double alarm_age_s, float buzzer_s, uint8_t idle,
                     double state_age_s, float fault_s)
{
    uint8_t l = 0;
    switch (state) {
    case CANP_ST_RUN: l = LAMP_GREEN | (warn ? LAMP_YELLOW : 0); break;
    case CANP_ST_PAUSED: l = LAMP_YELLOW; break;
    case CANP_ST_ESTOP: l = LAMP_RED; break;
    case CANP_ST_FAULT: l = fault_s < 0 || state_age_s < fault_s ? LAMP_RED : 0; break;   // steady, then dark
    default:   // IDLE: ready, waiting for START (default dark: no relay clicking while the line stands)
        l = idle == IDLE_GREEN || (idle == IDLE_BLINK && blink) ? LAMP_GREEN : 0;
        break;
    }
    if (alarm && (buzzer_s < 0 || (buzzer_s > 0 && alarm_age_s < buzzer_s))) l |= LAMP_BUZZER;
    return l;
}
uint32_t lamp_to_coils(uint8_t lamps, const Rs485Config &c)
{
    uint32_t v = 0;
    for (int k = 0; k < 4; k++)
        if ((lamps >> k & 1) && c.lamp_coil[k] >= 0 && c.lamp_coil[k] < c.coils) v |= 1u << c.lamp_coil[k];
    return v;
}
uint8_t coils_to_lamps(uint32_t coils, const Rs485Config &c)
{
    uint8_t l = 0;
    for (int k = 0; k < 4; k++)
        if (c.lamp_coil[k] >= 0 && c.lamp_coil[k] < c.coils && (coils >> c.lamp_coil[k] & 1)) l |= uint8_t(1u << k);
    return l;
}
RtuXfer rs485_hw_xfer(des_hw_t *hw, const Rs485Config &c, std::string &err)
{
#ifdef DES_VERSION_485
    if (hw->version < DES_VERSION_485) {
        err = fmt("bitstream VERSION %u has no RS485 module (needs >= %u)", hw->version, DES_VERSION_485);
        return RtuXfer();
    }
    const int parity = c.parity == 'E' || c.parity == 'e' ? 1 : c.parity == 'O' || c.parity == 'o' ? 2 : 0;
    if (des_485_config(hw, uint32_t(c.baud), parity, uint32_t(std::max(1, c.timeout_ms)) * 1000u) != DES_OK) {
        err = "des_485_config refused the settings";
        return RtuXfer();
    }
    return [hw](const uint8_t *req, int n, bool expect, RtuXferResult &r) {
        des_485_result_t x;
        memset(&x, 0, sizeof x);
        const int rc = des_485_xfer(hw, req, n, expect ? 1 : 0, &x);
        r.n = std::min<int>(x.n, int(sizeof r.raw));
        memcpy(r.raw, x.raw, size_t(r.n));
        r.rc = rc == DES_OK ? RTU_OK : rc == DES_E_TIMEOUT ? RTU_TIMEOUT : rc == DES_E_CRC ? RTU_CRC
             : rc == DES_E_RXERR ? RTU_RXERR : RTU_FAIL;
        // FREE_CNT ticks at DES_FREE_CNT_HZ; unsigned differences survive its wrap
        const double cnt_per_ms = DES_FREE_CNT_HZ / 1000.0;
        r.resp_ms = rc == DES_OK && expect ? uint32_t(x.t_rx0 - x.t_tx1) / cnt_per_ms : -1;
        r.wire_ms = rc == DES_OK && expect ? uint32_t(x.t_rx1 - x.t_tx0) / cnt_per_ms : uint32_t(x.t_tx1 - x.t_tx0) / cnt_per_ms;
    };
#else
    (void)hw; (void)c;
    err = "this build's des_hw has no RS485 driver (des_485_*, PL v16)";
    return RtuXfer();
#endif
}
Rs485Io::Rs485Io(const Rs485Config &c, RtuXfer xfer) : cfg_(c), xfer_(std::move(xfer)) {}
Rs485Io::~Rs485Io() { stop(); }
void Rs485Io::start()
{
    if (run_ || !xfer_) return;
    run_ = true;
    th_ = std::thread(&Rs485Io::loop, this);
}
void Rs485Io::stop()
{
    if (!run_) return;
    run_ = false;
    if (th_.joinable()) th_.join();
    uint8_t req[16];   // leave the light dark: an app that is not running shows nothing
    const uint32_t mask = lamp_to_coils(0x0F, cfg_);
    for (int k = 0; k < 32; k++)
        if (mask >> k & 1) {
            const size_t n = rtu_write_coil(req, uint8_t(cfg_.addr), uint16_t(k), false);
            if (n) xact(req, n, nullptr);
        }
}
void Rs485Io::set_line(uint8_t state, bool alarm, bool warn)
{
    state_ = state;
    alarm_ = alarm;
    warn_ = warn;
}
void Rs485Io::set_override(bool on, uint8_t lamps)
{
    ovr_lamps_ = lamps;
    ovr_on_ = on;
}
bool Rs485Io::link_ok() const { return mono_s() - last_ok_.load() < 2.0; }
int Rs485Io::th_link() const
{
    if (cfg_.th_addr <= 0) return -1;
    return mono_s() - th_last_ok_.load() < std::max(3.0, 3.0 * cfg_.th_period_s) ? 1 : 0;
}
Rs485Io::Stats Rs485Io::stats() const
{
    std::lock_guard<std::mutex> lk(m_);
    Stats s = stats_;
    s.override_on = ovr_on_;
    return s;
}
bool Rs485Io::xact(const uint8_t *req, size_t n, uint32_t *bits, uint16_t *regs, int max_regs)
{
    RtuXferResult r;
    xfer_(req, int(n), req[0] != 0, r);   // address 0 = broadcast: no reply
    uint8_t exc = 0;
    const char *why = nullptr;
    if (r.rc == RTU_OK && req[0] != 0) why = rtu_check_reply(req, n, r.raw, size_t(r.n), bits, regs, max_regs, &exc);
    std::lock_guard<std::mutex> lk(m_);
    Stats &s = stats_;
    s.xfers++;
    switch (r.rc) {
    case RTU_OK: break;
    case RTU_TIMEOUT: s.timeouts++; s.last_err = "no reply"; return false;
    case RTU_CRC: s.crc++; s.last_err = "CRC error"; return false;
    case RTU_RXERR: s.rx_err++; s.last_err = "framing / parity error"; return false;
    default: s.other++; s.last_err = "bus busy / bad argument"; return false;
    }
    if (why) {
        if (exc) { s.exceptions++; s.last_err = fmt("exception %u (%s)", exc, rtu_exception_text(exc)); }
        else { s.bad_reply++; s.last_err = why; }
        return false;
    }
    s.ok++;
    if (r.resp_ms >= 0) {
        s.resp_last = r.resp_ms; s.resp_n++; s.resp_sum += r.resp_ms;
        s.resp_min = std::min(s.resp_min, r.resp_ms); s.resp_max = std::max(s.resp_max, r.resp_ms);
    }
    const double now = mono_s();
    s.last_ok_s = now;
    if (req[0] == uint8_t(cfg_.addr)) last_ok_ = now;   // the relay module's link; the transmitter keeps its own
    return true;
}
void Rs485Io::loop()
{
    const uint8_t addr = uint8_t(cfg_.addr);
    const uint16_t ncoils = uint16_t(std::clamp(cfg_.coils, 1, 32));
    const uint32_t lamp_mask = lamp_to_coils(0x0F, cfg_);   // the relays that carry a lamp: the only ones ever written
    const double t0 = mono_s();
    double last_read = -1e9, alarm_t = 0, write_hold = -1e9, last_th = -1e9;
    bool prev_alarm = false, written_once = false, known = false;
    uint8_t prev_state = 0xFF;
    double state_t = t0;   // when the line entered its current state (fault_s counts from there)
    uint32_t written = 0, cur = 0;   // cur = what the module holds (last read-back, or our last good write)
    uint8_t req[16];
    while (run_) {
        const double now = mono_s();
        const bool alarm = alarm_;
        if (alarm && !prev_alarm) alarm_t = now;
        prev_alarm = alarm;
        const uint8_t state = state_;
        if (state != prev_state) { prev_state = state; state_t = now; }
        const bool blink = int((now - t0) * 2) % 2 == 0;   // 1 Hz
        const uint8_t lamps = ovr_on_ ? uint8_t(ovr_lamps_ & 0x0F)
                                      : lamp_pattern(state, alarm, warn_, blink, now - alarm_t, cfg_.buzzer_s, cfg_.idle,
                                                     now - state_t, cfg_.fault_s);
        const uint32_t coils = lamp_to_coils(lamps, cfg_);
        // One relay at a time (FC 05): the 10-26 relay board (bestep 4-ch) ignores FC 0F unless every bit is 0, and
        // FC 05 is what every Modbus relay board takes. Only relays that differ from what the module holds are
        // written; the read-back below catches a module that was power-cycled or a relay that did not switch.
        const uint32_t todo = (known ? coils ^ cur : 0xFFFFFFFFu) & lamp_mask;
        if (todo && now >= write_hold) {
            bool ok = true;
            for (int k = 0; k < 32 && ok; k++)
                if (todo >> k & 1) {
                    const bool on = coils >> k & 1;
                    const size_t n = rtu_write_coil(req, addr, uint16_t(k), on);
                    ok = n && xact(req, n, nullptr);
                    if (ok) cur = on ? cur | (1u << k) : cur & ~(1u << k);
                }
            if (ok) {
                known = true; written = coils; written_once = true;
                std::lock_guard<std::mutex> lk(m_);
                stats_.writes++; stats_.lamps = lamps;
            } else {
                write_hold = now + 1.0;   // no hammering a module that does not answer: retry in 1 s
            }
        }
        if (now - last_read >= cfg_.poll_ms / 1000.0) {
            last_read = now;
            uint32_t rb = 0;
            // the 10-26 board answers FC 01 only for 8 coils: cfg.coils is the count asked for (8 there)
            size_t n = rtu_read_bits(req, addr, RTU_READ_COILS, 0, ncoils);
            if (n && xact(req, n, &rb)) {
                cur = rb; known = true;
                std::lock_guard<std::mutex> lk(m_);
                stats_.coils_rb = rb;
                if (written_once && ((rb ^ written) & lamp_mask)) stats_.mismatches++;
            }
            uint32_t in = 0;
            if (cfg_.inputs > 0 && (n = rtu_read_bits(req, addr, RTU_READ_INPUTS, 0, uint16_t(std::min(cfg_.inputs, 32)))) &&
                xact(req, n, &in)) {
                std::lock_guard<std::mutex> lk(m_);
                stats_.inputs = in;
            }
        }
        if (cfg_.th_addr > 0 && now - last_th >= cfg_.th_period_s) {   // temperature / humidity transmitter
            last_th = now;
            uint16_t v[2] = {0, 0};
            const size_t n = rtu_read_regs(req, uint8_t(cfg_.th_addr), RTU_READ_HOLDING, 0, 2);
            const bool ok = n && xact(req, n, nullptr, v, 2);
            const float t = int16_t(v[0]) / 10.0f, h = v[1] / 10.0f;
            const bool sane = ok && t > -40.5f && t < 125.5f && h >= 0 && h <= 100.5f;   // SHT30 ranges
            std::lock_guard<std::mutex> lk(m_);
            if (sane) {
                stats_.th_valid = true; stats_.temp_c = t; stats_.hum_rh = h;
                stats_.th_last_s = mono_s(); stats_.th_ok++;
                th_last_ok_ = stats_.th_last_s;
            } else {
                stats_.th_err++;
            }
        }
        usleep(50000);
    }
}
}  // namespace des
