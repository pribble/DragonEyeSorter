// test_modbus: Modbus-TCP slave (hw/modbus_tcp.hpp) over a real loopback socket. Host (WSL g++) or board.
//   ./test_modbus            exit code = failed checks
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "modbus_tcp.hpp"
#include "test_check.hpp"

using namespace des;

static int connect_to(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(uint16_t(port));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, reinterpret_cast<sockaddr *>(&a), sizeof a) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}
static std::string mbap(uint16_t tid, const std::string &pdu, uint8_t unit = 1)
{
    std::string s;
    s += char(tid >> 8); s += char(tid & 0xFF); s += '\0'; s += '\0';
    const uint16_t n = uint16_t(pdu.size() + 1);
    s += char(n >> 8); s += char(n & 0xFF); s += char(unit);
    return s + pdu;
}
static std::string u16(uint16_t v) { return std::string{char(v >> 8), char(v & 0xFF)}; }
static uint16_t reg(const std::string &f, int i) { return uint16_t((uint8_t(f[9 + 2 * i]) << 8) | uint8_t(f[10 + 2 * i])); }
// one reply frame (MBAP + PDU), "" on timeout / close; bytes of a following frame stay buffered per socket
static std::string recv_frame(int fd, int timeout_ms = 1000)
{
    static std::string bufs[1024];
    std::string &in = bufs[fd & 1023];
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        if (in.size() >= 6) {
            const size_t n = size_t((uint8_t(in[4]) << 8) | uint8_t(in[5]));
            if (in.size() >= 6 + n) {
                std::string out = in.substr(0, 6 + n);
                in.erase(0, 6 + n);
                return out;
            }
        }
        const int left = int(std::chrono::duration_cast<std::chrono::milliseconds>(end - std::chrono::steady_clock::now()).count());
        if (left <= 0) return "";
        pollfd p{fd, POLLIN, 0};
        if (poll(&p, 1, left) <= 0) return "";
        char b[512];
        const ssize_t r = recv(fd, b, sizeof b, 0);
        if (r <= 0) return "";
        in.append(b, size_t(r));
    }
}
int main()
{
    ModbusTcp mb(MB_N_HR, MB_N_IR);
    const std::string e = mb.start(0);
    CHECK(e.empty(), "start: %s", e.c_str());
    const int port = mb.port();
    CHECK(port > 0, "bound port %d", port);
    mb.set_inputs(MB_IR_STATE, {1, 0x11, 0, 70000 & 0xFFFF});
    mb.set_input(MB_IR_VERSION, MB_MAP_VERSION);
    mb.set_holding(MB_HR_SPEED, 500);
    const int fd = connect_to(port);
    CHECK(fd >= 0, "connect");
    // FC 04 read input registers
    send(fd, mbap(7, std::string("\x04", 1) + u16(0) + u16(4)).data(), 7 + 5, 0);
    std::string f = recv_frame(fd);
    CHECK(f.size() == 9 + 8, "FC04 reply length %zu", f.size());
    if (f.size() == 17) {
        CHECK(uint8_t(f[0]) == 0 && uint8_t(f[1]) == 7, "transaction id echoed");
        CHECK(uint8_t(f[6]) == 1 && uint8_t(f[7]) == 0x04 && uint8_t(f[8]) == 8, "unit / fc / byte count");
        CHECK(reg(f, 0) == 1 && reg(f, 1) == 0x11 && reg(f, 3) == (70000 & 0xFFFF), "input values");
    }
    // FC 03 read holding registers
    send(fd, mbap(8, std::string("\x03", 1) + u16(MB_HR_SPEED) + u16(1)).data(), 12, 0);
    f = recv_frame(fd);
    CHECK(f.size() == 11 && uint8_t(f[7]) == 0x03 && reg(f, 0) == 500, "FC03 speed read-back");
    // version register
    send(fd, mbap(9, std::string("\x04", 1) + u16(MB_IR_VERSION) + u16(1)).data(), 12, 0);
    f = recv_frame(fd);
    CHECK(f.size() == 11 && reg(f, 0) == MB_MAP_VERSION, "map version");
    // FC 06 write single -> echoed, queued (not applied to the table)
    std::string w6 = std::string("\x06", 1) + u16(MB_HR_CMD) + u16(1);
    send(fd, mbap(10, w6).data(), 12, 0);
    f = recv_frame(fd);
    CHECK(f.size() == 12 && f.substr(7) == w6, "FC06 echo");
    auto w = mb.take_writes();
    CHECK(w.size() == 1 && w[0].addr == MB_HR_CMD && w[0].value == 1, "FC06 queued");
    CHECK(mb.take_writes().empty(), "queue drained");
    // FC 16 write multiple (time hi / lo in one request)
    std::string w16 = std::string("\x10", 1) + u16(MB_HR_TIME_HI) + u16(2) + std::string(1, '\x04') + u16(0x6712) + u16(0x3456);
    send(fd, mbap(11, w16).data(), 7 + w16.size(), 0);
    f = recv_frame(fd);
    CHECK(f.size() == 12 && uint8_t(f[7]) == 0x10, "FC16 reply");
    if (f.size() == 12) {
        const uint16_t a = uint16_t((uint8_t(f[8]) << 8) | uint8_t(f[9])), q = uint16_t((uint8_t(f[10]) << 8) | uint8_t(f[11]));
        CHECK(a == MB_HR_TIME_HI && q == 2, "FC16 echo address %u quantity %u", a, q);
    }
    w = mb.take_writes();
    CHECK(w.size() == 2 && w[0].addr == MB_HR_TIME_HI && w[0].value == 0x6712 && w[1].addr == MB_HR_TIME_LO &&
              w[1].value == 0x3456, "FC16 queued in order");
    // exceptions: 01 unknown function, 02 out of range, 03 bad quantity
    send(fd, mbap(12, std::string("\x01", 1) + u16(0) + u16(1)).data(), 12, 0);
    f = recv_frame(fd);
    CHECK(f.size() == 9 && uint8_t(f[7]) == 0x81 && uint8_t(f[8]) == 1, "exception 01 (FC 01 not supported)");
    send(fd, mbap(13, std::string("\x04", 1) + u16(MB_N_IR - 1) + u16(2)).data(), 12, 0);
    f = recv_frame(fd);
    CHECK(f.size() == 9 && uint8_t(f[7]) == 0x84 && uint8_t(f[8]) == 2, "exception 02 (FC04 past the table)");
    send(fd, mbap(19, std::string("\03", 1) + u16(MB_N_HR) + u16(1)).data(), 12, 0);
    f = recv_frame(fd);
    CHECK(f.size() == 9 && uint8_t(f[7]) == 0x83 && uint8_t(f[8]) == 2, "exception 02 (FC03 past the table)");
    send(fd, mbap(14, std::string("\x03", 1) + u16(0) + u16(0)).data(), 12, 0);
    f = recv_frame(fd);
    CHECK(f.size() == 9 && uint8_t(f[7]) == 0x83 && uint8_t(f[8]) == 3, "exception 03 (quantity 0)");
    send(fd, mbap(15, std::string("\x06", 1) + u16(MB_N_HR) + u16(1)).data(), 12, 0);
    f = recv_frame(fd);
    CHECK(f.size() == 9 && uint8_t(f[7]) == 0x86 && uint8_t(f[8]) == 2, "exception 02 (FC06 address)");
    std::string w16x = std::string("\x10", 1) + u16(MB_N_HR) + u16(1) + std::string(1, '\x02') + u16(0);
    send(fd, mbap(16, w16x).data(), 7 + w16x.size(), 0);
    f = recv_frame(fd);
    CHECK(f.size() == 9 && uint8_t(f[7]) == 0x90 && uint8_t(f[8]) == 2, "exception 02 (FC16 past the table)");
    std::string w16q0 = std::string("\x10", 1) + u16(0) + u16(0);
    send(fd, mbap(17, w16q0).data(), 7 + w16q0.size(), 0);
    f = recv_frame(fd);
    CHECK(f.size() == 9 && uint8_t(f[7]) == 0x90 && uint8_t(f[8]) == 3, "exception 03 (FC16 quantity 0)");
    CHECK(mb.take_writes().empty(), "rejected writes not queued");
    // a request split over two TCP segments, then two requests in one segment
    std::string r1 = mbap(20, std::string("\x04", 1) + u16(0) + u16(1));
    send(fd, r1.data(), 5, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    send(fd, r1.data() + 5, r1.size() - 5, 0);
    f = recv_frame(fd);
    CHECK(f.size() == 11 && uint8_t(f[1]) == 20 && reg(f, 0) == 1, "split request");
    std::string two = mbap(21, std::string("\x04", 1) + u16(1) + u16(1)) + mbap(22, std::string("\x03", 1) + u16(1) + u16(1));
    send(fd, two.data(), two.size(), 0);
    f = recv_frame(fd);
    std::string g = recv_frame(fd);
    CHECK(f.size() == 11 && uint8_t(f[1]) == 21 && reg(f, 0) == 0x11, "pipelined 1");
    CHECK(g.size() == 11 && uint8_t(g[1]) == 22 && reg(g, 0) == 500, "pipelined 2");
    // second client at the same time; request counter
    const int fd2 = connect_to(port);
    send(fd2, mbap(30, std::string("\x04", 1) + u16(0) + u16(2)).data(), 12, 0);
    f = recv_frame(fd2);
    CHECK(f.size() == 13 && reg(f, 0) == 1, "second client");
    CHECK(mb.requests() == 16, "request count %llu", (unsigned long long)mb.requests());
    /* the monitor unit (tools/des_4g): answered like any other, echoed in the reply, never counted as PLC traffic */
    send(fd2, mbap(21, std::string("\x04", 1) + u16(MB_IR_VERSION) + u16(1), MB_UNIT_MONITOR).data(), 12, 0);
    f = recv_frame(fd2);
    CHECK(f.size() == 11 && uint8_t(f[6]) == MB_UNIT_MONITOR && reg(f, 0) == MB_MAP_VERSION, "monitor unit: read answered, unit echoed");
    send(fd2, mbap(22, std::string("\x06", 1) + u16(MB_HR_WO_ACK) + u16(7), MB_UNIT_MONITOR).data(), 12, 0);
    f = recv_frame(fd2);
    CHECK(f.size() == 12 && uint8_t(f[6]) == MB_UNIT_MONITOR, "monitor unit: write echoed");
    {
        const std::vector<ModbusTcp::Write> w = mb.take_writes();
        CHECK(w.size() == 1 && w[0].addr == MB_HR_WO_ACK && w[0].value == 7, "monitor unit: the write is queued like a PLC write");
    }
    CHECK(mb.requests() == 16, "monitor unit requests are not counted (%llu)", (unsigned long long)mb.requests());
    CHECK(MB_MAP_VERSION == 0x0103 && MB_UNIT_MONITOR == 250, "map version / monitor unit constants");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(mb.clients() == 2, "clients %d", mb.clients());
    /* client cap (kMaxClients = 4): three more connect, the 5th is accepted and immediately closed */
    int more[3] = {-1, -1, -1};
    for (int i = 0; i < 3; i++) more[i] = connect_to(port);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CHECK(mb.clients() == 4, "4 clients served (%d)", mb.clients());
    int c5 = connect_to(port);   // the 5th: the server accepts and closes it at once
    char drop;
    pollfd p5{c5, POLLIN, 0};
    const bool closed5 = poll(&p5, 1, 1000) == 1 && recv(c5, &drop, 1, MSG_DONTWAIT) == 0;
    CHECK(closed5, "the 5th client is closed by the server (over the cap)");
    for (int i = 0; i < 3; i++) close(more[i]);
    close(c5);
    // bounded take_writes: cap limits the batch, the rest stays queued in order; cap 0 takes nothing
    for (uint16_t v : {1, 2, 3, 4}) {
        std::string wb = std::string("\06", 1) + u16(MB_HR_CMD) + u16(v);
        send(fd, mbap(50 + v, wb).data(), 12, 0);
        recv_frame(fd);
    }
    ModbusTcp::Write bw[2];
    CHECK(mb.take_writes(bw, 2) == 2 && bw[0].value == 1 && bw[1].value == 2, "bounded take: 2 of 4 in order");
    CHECK(mb.take_writes(bw, 2) == 2 && bw[0].value == 3 && bw[1].value == 4, "bounded take: the rest in order");
    CHECK(mb.take_writes(bw, 2) == 0, "bounded take: drained");
    std::string wb5 = std::string("\06", 1) + u16(MB_HR_CMD) + u16(5);
    send(fd, mbap(60, wb5).data(), 12, 0);
    recv_frame(fd);
    CHECK(mb.take_writes(bw, 0) == 0, "cap 0 takes nothing");
    w = mb.take_writes();
    CHECK(w.size() == 1 && w[0].value == 5, "cap 0 left the write queued");
    // not Modbus (protocol id != 0): connection closed
    std::string bad = mbap(40, std::string("\x04", 1) + u16(0) + u16(1));
    bad[3] = 1;
    send(fd2, bad.data(), bad.size(), 0);
    char b;
    pollfd p{fd2, POLLIN, 0};
    const bool closed = poll(&p, 1, 1000) == 1 && recv(fd2, &b, 1, 0) == 0;
    CHECK(closed, "bad protocol id closes the connection");
    close(fd2);
    // latency: 1000 sequential reads
    const auto t0 = std::chrono::steady_clock::now();
    int ok = 0;
    const std::string rq = mbap(1, std::string("\x04", 1) + u16(0) + u16(MB_N_IR));
    for (int i = 0; i < 1000; i++) {
        send(fd, rq.data(), rq.size(), 0);
        if (recv_frame(fd).size() == 9 + 2 * MB_N_IR) ok++;
    }
    CHECK(ok == 1000, "1000 reads, %d ok", ok);
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 1000;
    printf("read %d registers: %.1f us per round trip (loopback)\n", MB_N_IR, us);
    close(fd);
    mb.stop();
    CHECK(connect_to(port) < 0, "stopped: port closed");
    return test_summary("test_modbus");
}
