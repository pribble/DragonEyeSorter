// rs485_io.cpp — see rs485_io.hpp
#include "rs485_io.hpp"
#include <unistd.h>
#include <algorithm>
#include <cstring>
#include "can_proto.h"
#include "modbus_rtu.hpp"
#include "util.hpp"
namespace des {
uint8_t lamp_pattern(uint8_t state, bool alarm, bool warn, bool blink, double alarm_age_s, float buzzer_s)
{
    uint8_t l = 0;
    switch (state) {
    case CANP_ST_RUN: l = LAMP_GREEN | (warn ? LAMP_YELLOW : 0); break;
    case CANP_ST_PAUSED: l = LAMP_YELLOW; break;
    case CANP_ST_ESTOP: l = LAMP_RED; break;
    case CANP_ST_FAULT: l = blink ? LAMP_RED : 0; break;
    default: l = blink ? LAMP_GREEN : 0; break;   // IDLE: ready, waiting for START
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
    const size_t n = rtu_write_coils(req, uint8_t(cfg_.addr), 0, uint16_t(std::clamp(cfg_.coils, 1, 32)), 0);
    if (n) xact(req, n, nullptr);
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
Rs485Io::Stats Rs485Io::stats() const
{
    std::lock_guard<std::mutex> lk(m_);
    Stats s = stats_;
    s.override_on = ovr_on_;
    return s;
}
bool Rs485Io::xact(const uint8_t *req, size_t n, uint32_t *bits)
{
    RtuXferResult r;
    xfer_(req, int(n), req[0] != 0, r);   // address 0 = broadcast: no reply
    uint8_t exc = 0;
    const char *why = nullptr;
    if (r.rc == RTU_OK && req[0] != 0) why = rtu_check_reply(req, n, r.raw, size_t(r.n), bits, nullptr, 0, &exc);
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
    last_ok_ = now;
    return true;
}
void Rs485Io::loop()
{
    const uint8_t addr = uint8_t(cfg_.addr);
    const uint16_t ncoils = uint16_t(std::clamp(cfg_.coils, 1, 32));
    const double t0 = mono_s();
    double last_write = -1e9, last_read = -1e9, alarm_t = 0;
    bool prev_alarm = false, written_once = false;
    uint32_t written = 0;
    uint8_t req[16];
    while (run_) {
        const double now = mono_s();
        const bool alarm = alarm_;
        if (alarm && !prev_alarm) alarm_t = now;
        prev_alarm = alarm;
        const bool blink = int((now - t0) * 2) % 2 == 0;   // 1 Hz
        const uint8_t lamps = ovr_on_ ? uint8_t(ovr_lamps_ & 0x0F)
                                      : lamp_pattern(state_, alarm, warn_, blink, now - alarm_t, cfg_.buzzer_s);
        const uint32_t coils = lamp_to_coils(lamps, cfg_);
        if (!written_once || coils != written || now - last_write >= 1.0) {   // changed, or a periodic refresh
            const size_t n = rtu_write_coils(req, addr, 0, ncoils, coils);
            if (n && xact(req, n, nullptr)) {
                written = coils; written_once = true;
                std::lock_guard<std::mutex> lk(m_);
                stats_.writes++; stats_.lamps = lamps;
            }
            last_write = now;   // a failed write is retried at the refresh, not every cycle
        }
        if (now - last_read >= cfg_.poll_ms / 1000.0) {
            last_read = now;
            uint32_t rb = 0;
            size_t n = rtu_read_bits(req, addr, RTU_READ_COILS, 0, ncoils);
            if (n && xact(req, n, &rb)) {
                std::lock_guard<std::mutex> lk(m_);
                stats_.coils_rb = rb;
                if (written_once && rb != written) stats_.mismatches++;
            }
            uint32_t in = 0;
            if (cfg_.inputs > 0 && (n = rtu_read_bits(req, addr, RTU_READ_INPUTS, 0, uint16_t(std::min(cfg_.inputs, 32)))) &&
                xact(req, n, &in)) {
                std::lock_guard<std::mutex> lk(m_);
                stats_.inputs = in;
            }
        }
        usleep(50000);
    }
}
}  // namespace des
