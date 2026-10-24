// modbus_rtu.cpp — see modbus_rtu.hpp
#include "modbus_rtu.hpp"
#include <cstring>
namespace des {
namespace {
void put16(uint8_t *p, uint16_t v) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); }   // Modbus fields: big-endian
uint16_t get16(const uint8_t *p) { return uint16_t(p[0] << 8 | p[1]); }
size_t head(uint8_t *out, uint8_t addr, uint8_t fc, uint16_t a, uint16_t b)
{
    out[0] = addr; out[1] = fc;
    put16(out + 2, a); put16(out + 4, b);
    return 6;
}
}  // namespace
uint16_t rtu_crc16(const uint8_t *p, size_t n)
{
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c & 1) ? uint16_t((c >> 1) ^ 0xA001) : uint16_t(c >> 1);
    }
    return c;
}
size_t rtu_read_bits(uint8_t *out, uint8_t addr, uint8_t fc, uint16_t start, uint16_t count)
{
    if ((fc != RTU_READ_COILS && fc != RTU_READ_INPUTS) || count < 1 || count > 2000) return 0;
    return head(out, addr, fc, start, count);
}
size_t rtu_read_regs(uint8_t *out, uint8_t addr, uint8_t fc, uint16_t start, uint16_t count)
{
    // 29 registers = 5 + 58 reply bytes: the most that fits the 62-byte PDU limit of the PL
    if ((fc != RTU_READ_HOLDING && fc != RTU_READ_INPUT_REGS) || count < 1 || count > 29) return 0;
    return head(out, addr, fc, start, count);
}
size_t rtu_write_coil(uint8_t *out, uint8_t addr, uint16_t coil, bool on)
{
    return head(out, addr, RTU_WRITE_COIL, coil, on ? 0xFF00 : 0x0000);
}
size_t rtu_write_reg(uint8_t *out, uint8_t addr, uint16_t reg, uint16_t value)
{
    return head(out, addr, RTU_WRITE_REG, reg, value);
}
size_t rtu_write_coils(uint8_t *out, uint8_t addr, uint16_t start, uint16_t count, uint32_t bits)
{
    if (count < 1 || count > 32) return 0;
    size_t n = head(out, addr, RTU_WRITE_COILS, start, count);
    const uint8_t nb = uint8_t((count + 7) / 8);
    out[n++] = nb;
    for (uint8_t i = 0; i < nb; i++) {
        uint8_t b = uint8_t(bits >> (8 * i));
        if (i == nb - 1 && count % 8) b &= uint8_t((1u << (count % 8)) - 1);   // unused high bits are zero
        out[n++] = b;
    }
    return n;
}
const char *rtu_exception_text(uint8_t code)
{
    switch (code) {
    case 1: return "illegal function";
    case 2: return "illegal data address";
    case 3: return "illegal data value";
    case 4: return "slave device failure";
    case 5: return "acknowledge (busy, poll later)";
    case 6: return "slave device busy";
    default: return "unknown exception";
    }
}
const char *rtu_check_reply(const uint8_t *req, size_t nreq, const uint8_t *rep, size_t nrep, uint32_t *bits,
                            uint16_t *regs, int max_regs, uint8_t *exception)
{
    if (exception) *exception = 0;
    if (nreq < 2) return "bad request";
    if (nrep < 3) return "short reply";
    if (rep[0] != req[0]) return "reply from another address";
    const uint8_t fc = req[1];
    if (rep[1] == (fc | 0x80)) {
        if (exception) *exception = rep[2];
        return "exception";
    }
    if (rep[1] != fc) return "reply to another function";
    switch (fc) {
    case RTU_READ_COILS:
    case RTU_READ_INPUTS: {
        if (nreq < 6) return "bad request";
        const uint16_t count = get16(req + 4);
        const size_t nb = (count + 7u) / 8u;
        if (rep[2] != nb || nrep != 3 + nb) return "byte count";
        if (bits) {
            uint32_t v = 0;
            for (size_t i = 0; i < nb && i < 4; i++) v |= uint32_t(rep[3 + i]) << (8 * i);
            if (count < 32) v &= (1u << count) - 1;
            *bits = v;
        }
        return nullptr;
    }
    case RTU_READ_HOLDING:
    case RTU_READ_INPUT_REGS: {
        if (nreq < 6) return "bad request";
        const uint16_t count = get16(req + 4);
        if (rep[2] != 2 * count || nrep != 3 + 2u * count) return "byte count";
        if (regs)
            for (int i = 0; i < count && i < max_regs; i++) regs[i] = get16(rep + 3 + 2 * i);
        return nullptr;
    }
    case RTU_WRITE_COIL:
    case RTU_WRITE_REG:
        if (nreq < 6 || nrep != 6 || memcmp(req, rep, 6) != 0) return "echo differs";
        return nullptr;
    case RTU_WRITE_COILS:
    case RTU_WRITE_REGS:
        if (nreq < 6 || nrep != 6 || memcmp(req, rep, 6) != 0) return "echo differs";
        return nullptr;
    default:
        return nullptr;   // other functions: address / function checked, payload is the caller's
    }
}
}  // namespace des
