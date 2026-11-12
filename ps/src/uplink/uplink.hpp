// uplink.hpp — 4G remote monitoring (赛题拓展 5「4G 远程监控」) and the second link next to the wired one (拓展 3
// 「链路冗余」): line state and work orders go to an MQTT broker through the 4G module's own MQTT client.
//
// Topics (prefix from the config, e.g. "des/ab12cd"):
//   <prefix>/state   retained JSON, on change (at most every state_min_s) and at least every state_period_s;
//                    the module's will puts the text "offline" here when the module drops off the network
//   <prefix>/wo      one JSON per work-order change, exactly what des_app pushes on its feed (trimmed to fit)
//   <prefix>/cmd     to the board: {"cmd":"ping","id":n}. Only ping is answered in this version — commands that act
//                    on the line need the Modbus side of des_app and are not enabled
//   <prefix>/reply   answers to cmd
//
// Wired link: the carrier of eth_if is part of the state. mode "always" publishes at the same rate whatever the
// cable does; mode "backup" slows the state to idle_period_s while the cable is in and returns to the full rate the
// moment it is pulled (work orders are forwarded in both modes).
//
// Uplink::step() is one bounded pass of the connection state machine; the owner calls it in a loop and reopens the
// port when it returns false. Everything is single-threaded except BoardLink's reader.
#pragma once

#include <functional>
#include <string>

#include "board_link.hpp"
#include "quectel.hpp"

namespace des {

struct UplinkConfig {
    MqttConfig mqtt;
    std::string prefix = "des/demo";
    int state_period_s = 10, state_min_s = 2, idle_period_s = 60;
    std::string mode = "always";            // "always" | "backup"
    std::string eth_if = "eth0";
    std::string board_host = "127.0.0.1";
    int feed_port = 9500;
    unsigned vid = 0x2c7c;                  // USB vendor id of the module (移远)
    int usb_if = -1;                        // AT interface number, -1 = find it
    std::string tty;                        // use this serial device instead of usbfs (a kernel with the driver)
};
std::string load_uplink_config(const std::string &path, UplinkConfig &c);   // "" on success

struct UplinkStats { uint64_t published = 0, failed = 0, reconnects = 0, orders = 0, commands = 0; };

std::string uplink_state_json(const BoardSnapshot &b, const ModemInfo &m, int eth, double up_s, const UplinkStats &st);
std::string uplink_fit_order(const std::string &json, size_t max_bytes);   // drop the long fields until it fits
int eth_carrier(const std::string &ifname);                                 // 1 up, 0 down, -1 unknown

class Uplink {
public:
    enum class Phase { Init, Sim, Register, Data, Mqtt, Up };
    Uplink(const UplinkConfig &c, AtChan &at, BoardLink &board);
    bool step();                                    // false: the port is gone / the module stopped answering
    void shutdown();                                // leaving on purpose: say "offline" ourselves, close the session
    Phase phase() const { return phase_; }
    const char *phase_name() const;
    const ModemInfo &modem() const { return info_; }
    const UplinkStats &stats() const { return st_; }
    std::function<void(const std::string &)> log;   // one line per event
    std::function<int()> eth;                       // wired carrier; default eth_carrier(cfg.eth_if)
    std::function<double()> clock;                  // monotonic seconds; tests step it by hand

private:
    void say(const std::string &s) { if (log) log(s); }
    void go(Phase p, double delay_s = 0);
    void fail(const std::string &why);              // back off, then try the same phase again
    bool publish(const std::string &topic, const std::string &payload, bool retain);
    void handle_urcs();
    void handle_cmd(const std::string &payload);
    void up_pass(double now);
    UplinkConfig cfg_;
    AtChan &at_;
    BoardLink &board_;
    Quectel q_;
    ModemInfo info_;
    UplinkStats st_;
    Phase phase_ = Phase::Init;
    double t0_ = 0, not_before_ = 0, backoff_ = 2, last_state_ = -1e9, last_info_ = -1e9, phase_since_ = 0;
    uint64_t sent_seq_ = ~0ull;
    int sent_eth_ = -2, quiet_cmds_ = 0, pub_fails_ = 0;
    bool link_down_ = false;                        // +QMTSTAT seen: reconnect
};

}  // namespace des
