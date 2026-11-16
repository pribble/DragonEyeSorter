// des_4g — 4G uplink of the line through the board's M.2 LTE module (移远 EM05-CN), entirely in user space:
// the kernel on the board has no USB serial / usbnet driver, so this program talks to the module's AT port through
// usbfs and lets the module's own MQTT client do the networking (src/uplink). It runs next to des_app and only
// listens to des_app's live feed; des_app is not changed and does not notice it.
//
//   des_4g probe [--all]               list the module's USB interfaces, find the AT port, print card / signal / network
//   des_4g at "<command>" [timeout_s]  one AT command through the AT port, reply printed
//   des_4g feed [seconds]              no module needed: watch des_app's feed and print the state that would be sent
//   des_4g run [config.json] [-v]      the uplink itself (default config: /root/des/des_4g.json); -v prints AT traffic
// Options for all modes: --vid <hex> (default 2c7c), --if <n> (AT interface number), --tty <device> (a serial
// device instead of usbfs, for a kernel that has the driver).
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <string>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <vector>

#include "modbus_tcp.hpp"
#include "uplink.hpp"
#include "usbfs_port.hpp"

using namespace des;

namespace {

volatile sig_atomic_t g_quit = 0;
void on_signal(int) { g_quit = 1; }

void logline(const std::string &s)
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    printf("[%8.1f] %s\n", ts.tv_sec + ts.tv_nsec * 1e-9, s.c_str());
    fflush(stdout);
}

int open_tty(const std::string &dev)
{
    const int fd = open(dev.c_str(), O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (fd < 0) return -1;
    termios t{};
    if (tcgetattr(fd, &t) == 0) {
        cfmakeraw(&t);
        cfsetispeed(&t, B115200); cfsetospeed(&t, B115200);
        t.c_cflag |= CLOCAL | CREAD;
        tcsetattr(fd, TCSANOW, &t);
    }
    return fd;
}

bool answers_at(AtPort &p)
{
    AtChan at(p);
    for (int i = 0; i < 2; i++)
        if (at.cmd("AT", 400).ok) return true;
    return false;
}

// Find and open the AT port. verbose: print every interface tried (probe).
std::unique_ptr<AtPort> open_at_port(const UplinkConfig &c, bool all, bool verbose, std::string &what)
{
    if (!c.tty.empty()) {
        const int fd = open_tty(c.tty);
        if (fd < 0) { what = "open " + c.tty + ": " + strerror(errno); return nullptr; }
        what = c.tty;
        return std::make_unique<FdPort>(fd);
    }
    const std::vector<UsbDev> devs = usb_scan(all ? 0 : uint16_t(c.vid));
    if (devs.empty()) {
        char v[8];
        snprintf(v, sizeof v, "%04x", c.vid);
        what = std::string("no USB device with vendor id ") + v + " (module not plugged in / not powered?)";
        return nullptr;
    }
    std::unique_ptr<AtPort> found;
    for (const UsbDev &d : devs) {
        if (verbose)
            printf("USB %s  %04x:%04x  %s  (bus %d device %d, %zu interfaces)\n", d.sys.c_str(), d.vid, d.pid, d.product.c_str(),
                   d.bus, d.dev, d.ifs.size());
        if (d.vid != c.vid) continue;
        for (const UsbIface &i : d.ifs) {
            std::string note = "not a serial-style interface";
            if (i.at_candidate() && (c.usb_if < 0 || c.usb_if == i.num) && !found) {
                auto p = std::make_unique<UsbfsPort>();
                const std::string e = p->open(d, i);
                if (!e.empty()) note = e;
                else if (answers_at(*p)) {
                    note = "answers AT  <- AT port";
                    what = "USB " + d.sys + " interface " + std::to_string(i.num);
                    found = std::move(p);
                } else note = "silent";
            } else if (i.at_candidate()) note = found ? "not tried (AT port already found)" : "skipped (--if)";
            if (verbose)
                printf("  interface %d  class %02x/%02x/%02x  in %02x out %02x int %02x : %s\n", i.num, i.cls, i.sub, i.proto,
                       i.ep_in, i.ep_out, i.ep_int, note.c_str());
        }
    }
    if (!found && what.empty()) what = "the module is on USB but no interface answers AT";
    return found;
}

int mode_probe(const UplinkConfig &c, bool all)
{
    std::string what;
    std::unique_ptr<AtPort> port = open_at_port(c, all, true, what);
    if (!port) { printf("AT port: %s\n", what.c_str()); return 1; }
    printf("AT port: %s\n", what.c_str());
    AtChan at(*port);
    Quectel q(at);
    ModemInfo m;
    if (!q.alive()) { printf("the port stopped answering\n"); return 1; }
    q.identify(m);
    q.refresh(m);
    printf("module    : %s  %s\n", m.model.c_str(), m.revision.c_str());
    printf("IMEI      : %s\n", m.imei.c_str());
    printf("SIM card  : %s  (ICCID %s)\n", m.sim_ready ? "ready" : "NOT ready", m.iccid.c_str());
    printf("signal    : %d of 31 (%d dBm)%s\n", m.csq, m.dbm(), m.csq == 99 ? "  = unknown" : "");
    printf("network   : %s  %s  %s\n", m.registered() ? "registered" : "NOT registered", m.oper.c_str(), m.act.c_str());
    printf("MQTT cmds : %s\n", q.has_mqtt() ? "yes" : "NO (firmware without AT+QMTCFG)");
    printf("URC port  : %s\n", at.cmd("AT+QURCCFG=\"urcport\"", 1000).first("+QURCCFG:").c_str());
    printf("data conn : %s\n", at.cmd("AT+QIACT?", 2000).text().c_str());
    return m.sim_ready && m.registered() ? 0 : 2;
}

int mode_at(const UplinkConfig &c, const std::string &command, int timeout_s)
{
    std::string what;
    std::unique_ptr<AtPort> port = open_at_port(c, false, false, what);
    if (!port) { fprintf(stderr, "%s\n", what.c_str()); return 1; }
    AtChan at(*port);
    at.cmd("ATE0", 500);
    const AtResult r = at.cmd(command, timeout_s * 1000);
    for (const std::string &l : r.lines) printf("%s\n", l.c_str());
    printf("%s\n", r.ok ? "OK" : r.timeout ? "(timeout)" : r.cme >= 0 ? ("ERROR " + std::to_string(r.cme)).c_str() : "ERROR");
    at.pump(300);   // whatever the command triggers right away
    std::string u;
    while (at.next_urc(u)) printf("%s\n", u.c_str());
    return r.ok ? 0 : 1;
}

int mode_feed(const UplinkConfig &c, int seconds)
{
    BoardLink board;
    board.start(c.board_host, c.feed_port);
    ModemInfo none;
    UplinkStats st;
    BoardEnvReader env(c.log_dir);   // temperature / humidity / vibration files and board health, as "run" reports them
    UplinkExtras x;
    for (int i = 0; i < seconds && !g_quit; i++) {
        sleep(1);
        const BoardSnapshot b = board.snapshot();
        std::string wo;
        while (board.next_order(wo)) printf("wo    %s\n", uplink_fit_order(wo, 1400).c_str());
        x.env = env.read();
        printf("state %s\n", uplink_state_json(b, none, eth_carrier(c.eth_if), i + 1, st, &x).c_str());
        fflush(stdout);
    }
    return 0;
}

int mode_run(const UplinkConfig &c, bool verbose)
{
    BoardLink board;
    board.start(c.board_host, c.feed_port);
    std::unique_ptr<MbClient> mb;   // des_app's registers through the monitor unit (hw/modbus_tcp.hpp)
    if (c.modbus_port > 0) mb = std::make_unique<MbClient>(c.board_host, c.modbus_port, MB_UNIT_MONITOR);
    logline("des_4g: broker " + c.mqtt.host + ":" + std::to_string(c.mqtt.port) + ", topics " + c.prefix + "/..., mode " + c.mode);
    while (!g_quit) {
        std::string what;
        std::unique_ptr<AtPort> port = open_at_port(c, false, false, what);
        if (!port) {
            logline("AT port: " + what + "; next try in 5 s");
            for (int i = 0; i < 50 && !g_quit; i++) usleep(100000);
            continue;
        }
        logline("AT port: " + what);
        AtChan at(*port);
        if (verbose) at.trace = [](char dir, const std::string &l) { printf("    %c %s\n", dir, l.c_str()); };
        Uplink up(c, at, board);
        up.log = logline;
        if (mb) up.attach_modbus(mb.get());
        while (!g_quit && up.step()) {}
        if (g_quit) up.shutdown();
        else {
            logline("AT port lost; reopening in 5 s");
            for (int i = 0; i < 50 && !g_quit; i++) usleep(100000);
        }
    }
    logline("des_4g: stopped");
    return 0;
}

void usage()
{
    fprintf(stderr, "usage: des_4g probe [--all] | at \"<command>\" [timeout_s] | feed [seconds] | run [config.json] [-v]\n"
                    "       options: --vid <hex>  --if <n>  --tty <device>\n");
}

}  // namespace

int main(int argc, char **argv)
{
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);
    UplinkConfig c;
    std::vector<std::string> pos;
    bool all = false, verbose = false;
    int usb_if = -2;
    std::string tty, vid;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--all") all = true;
        else if (a == "-v") verbose = true;
        else if (a == "--if" && i + 1 < argc) usb_if = atoi(argv[++i]);
        else if (a == "--tty" && i + 1 < argc) tty = argv[++i];
        else if (a == "--vid" && i + 1 < argc) vid = argv[++i];
        else pos.push_back(a);
    }
    if (pos.empty()) { usage(); return 2; }
    const std::string mode = pos[0];
    if (mode == "run") {
        const std::string path = pos.size() > 1 ? pos[1] : "/root/des/des_4g.json";
        const std::string e = load_uplink_config(path, c);
        if (!e.empty()) { fprintf(stderr, "des_4g: %s\n", e.c_str()); return 2; }
    }
    if (usb_if != -2) c.usb_if = usb_if;        // command line over the file
    if (!tty.empty()) c.tty = tty;
    if (!vid.empty()) c.vid = unsigned(strtoul(vid.c_str(), nullptr, 16));
    if (mode == "probe") return mode_probe(c, all);
    if (mode == "at" && pos.size() >= 2) return mode_at(c, pos[1], pos.size() > 2 ? atoi(pos[2].c_str()) : 5);
    if (mode == "feed") return mode_feed(c, pos.size() > 1 ? atoi(pos[1].c_str()) : 10);
    if (mode == "run") return mode_run(c, verbose);
    usage();
    return 2;
}
