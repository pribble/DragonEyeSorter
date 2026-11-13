// mb_client.hpp — a small Modbus-TCP master for the 4G uplink: reads des_app's registers and writes its command
// registers over loopback (hw/modbus_tcp.hpp has the map). It always uses the monitor unit id, which des_app serves
// without counting the request as "a PLC is online" (map version >= 0x0103; older des_app would count it and open a
// 「PLC 通信中断」 work order as soon as the requests stop — the uplink checks the version before it keeps asking).
// Blocking with short timeouts; connects on demand and reconnects after any error.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace des {

class MbClient {
public:
    MbClient(std::string host, int port, uint8_t unit, int timeout_ms = 300)
        : host_(std::move(host)), port_(port), unit_(unit), timeout_ms_(timeout_ms) {}
    ~MbClient() { close(); }
    bool read(uint8_t fc, uint16_t addr, uint16_t count, uint16_t *out);   // fc 3 = holding, 4 = input registers
    bool write(uint16_t addr, uint16_t value);                             // FC 06
    void close();
    const std::string &error() const { return err_; }

private:
    bool ensure();
    bool xfer(const uint8_t *pdu, size_t n, uint8_t *reply, size_t cap, size_t &got);
    std::string host_, err_;
    int port_, fd_ = -1;
    uint8_t unit_;
    int timeout_ms_;
    uint16_t tid_ = 0;
};

}  // namespace des
