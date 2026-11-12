// usbfs_port.hpp — talk to one interface of a USB device from user space (Linux usbfs, /dev/bus/usb/BBB/DDD).
//
// Why: the board's kernel (the NPU demo one) was built without the USB serial driver and without usbnet, so the 4G
// module's AT port never shows up as /dev/ttyUSB*. usbfs is part of the USB core itself, so the same bytes can be
// moved with ioctls: claim the interface, bulk OUT for commands, one bulk IN request kept pending for replies.
//
// usb_scan()              lists devices from sysfs with their interfaces (no device node is opened)
// usb_parse_descriptors() the pure half of it: device + configuration descriptors -> interfaces / endpoints (tests)
// UsbfsPort               AtPort over one interface. Bulk IN uses the asynchronous URB calls: the synchronous bulk
//                         ioctl throws away bytes that arrive just as its timeout fires.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "at_port.hpp"

namespace des {

struct UsbIface {
    int num = -1, cls = 0, sub = 0, proto = 0;
    int ep_in = 0, ep_out = 0, ep_int = 0;   // bulk IN / bulk OUT / interrupt IN endpoint addresses (0 = none)
    int max_out = 0;                         // wMaxPacketSize of the bulk OUT endpoint
    bool at_candidate() const { return cls == 0xFF && ep_in && ep_out; }   // vendor class with a bulk pair
};
struct UsbDev {
    std::string sys;                         // sysfs name, e.g. "1-1.2"
    int bus = 0, dev = 0, major = 0, minor = 0;
    uint16_t vid = 0, pid = 0;
    std::string product;
    std::vector<UsbIface> ifs;
    std::string node() const;                // /dev/bus/usb/BBB/DDD
};

bool usb_parse_descriptors(const uint8_t *d, size_t n, UsbDev &out);   // false: not a device descriptor
std::vector<UsbDev> usb_scan(uint16_t vid = 0, const std::string &sys_root = "/sys/bus/usb/devices");   // vid 0 = all

class UsbfsPort : public AtPort {
public:
    ~UsbfsPort() override { close(); }
    std::string open(const UsbDev &d, const UsbIface &i);   // "" on success, else what failed
    void close();
    bool is_open() const { return fd_ >= 0; }
    bool write_all(const char *data, size_t n) override;
    int read_some(char *buf, size_t cap, int timeout_ms) override;

private:
    bool submit_in();
    int fd_ = -1, ifnum_ = -1, ep_in_ = 0, ep_out_ = 0, max_out_ = 64;
    bool in_pending_ = false;
    std::vector<char> urb_;      // struct usbdevfs_urb storage (kept out of this header)
    std::vector<char> in_buf_;   // the pending IN request's buffer
    std::vector<char> rx_;       // received, not yet handed out
};

}  // namespace des
