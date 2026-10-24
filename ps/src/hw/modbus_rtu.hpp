// modbus_rtu.hpp — Modbus-RTU master PDUs for the RS485 bus (docs/软件与UI.md「工业总线」).
//
// A request here is [address, function, data...] WITHOUT the CRC: on the board the PL rs485 module (des_485_xfer, v16)
// appends the CRC16, drives DE, waits for the reply, frames it by the 3.5-character silence and checks its CRC, so the
// PS only builds requests and checks replies against them. crc16() is the same polynomial for tests, the PC tools'
// cross-check and logs. Pure functions, no hardware, no allocation.
#pragma once
#include <cstddef>
#include <cstdint>
namespace des {
enum : uint8_t {
    RTU_READ_COILS = 0x01, RTU_READ_INPUTS = 0x02, RTU_READ_HOLDING = 0x03, RTU_READ_INPUT_REGS = 0x04,
    RTU_WRITE_COIL = 0x05, RTU_WRITE_REG = 0x06, RTU_WRITE_COILS = 0x0F, RTU_WRITE_REGS = 0x10,
};
constexpr size_t kRtuMaxPdu = 62;   // des_485_xfer limit (+ 2 CRC bytes = 64)
uint16_t rtu_crc16(const uint8_t *p, size_t n);   // init 0xFFFF, reflected 0xA001; sent low byte first
// Request builders: write into out (>= 8 bytes, write_coils: 7 + ceil(count / 8)), return the length, 0 = bad argument.
size_t rtu_read_bits(uint8_t *out, uint8_t addr, uint8_t fc, uint16_t start, uint16_t count);   // fc 01 / 02, count 1..2000
size_t rtu_read_regs(uint8_t *out, uint8_t addr, uint8_t fc, uint16_t start, uint16_t count);   // fc 03 / 04, count 1..29
size_t rtu_write_coil(uint8_t *out, uint8_t addr, uint16_t coil, bool on);                      // fc 05 (FF00 / 0000)
size_t rtu_write_reg(uint8_t *out, uint8_t addr, uint16_t reg, uint16_t value);                 // fc 06
size_t rtu_write_coils(uint8_t *out, uint8_t addr, uint16_t start, uint16_t count, uint32_t bits);   // fc 0F, count 1..32
// Checks a reply (without CRC) against the request it answers. nullptr = good; else what is wrong. A Modbus exception
// reply (function | 0x80) sets *exception to its code (1 illegal function, 2 illegal address, 3 illegal value, 4 slave
// failure ...) and returns "exception". Read replies are decoded into bits (fc 01 / 02, bit i = start + i, <= 32
// bits) or regs (fc 03 / 04, at most max_regs values); echo replies (05 / 06 / 0F / 10) must echo the request.
const char *rtu_check_reply(const uint8_t *req, size_t nreq, const uint8_t *rep, size_t nrep, uint32_t *bits,
                            uint16_t *regs, int max_regs, uint8_t *exception);
const char *rtu_exception_text(uint8_t code);
}  // namespace des
