// uplink.hpp — 4G remote monitoring (赛题拓展 5「4G 远程监控」) and the second link next to the wired one (拓展 3
// 「链路冗余」): line state and work orders go to an MQTT broker through the 4G module's own MQTT client.
//
// Topics (prefix from the config, e.g. "des/ab12cd"):
//   <prefix>/state   retained JSON, on change (at most every state_min_s) and at least every state_period_s;
//                    the module's will puts the text "offline" here when the module drops off the network
//   <prefix>/wo      one JSON per work-order change, exactly what des_app pushes on its feed (trimmed to fit)
//   <prefix>/cmd     to the board: {"cmd":"ping","id":n} is always answered. Commands that act on the line —
//                    ack (arg = order id, 65535 = all) / stop / pause / resume / start / reset / speed (arg = 0.1 mm/s,
//                    one of commands.speeds when that list is set) / recipe (arg = recipe id, one of commands.recipes
//                    when set; des_app refuses it while the line runs) — need all of: board.modbus_port set, des_app's
//                    register map >= 0x0103, commands.key set, the command in commands.allow, and {"seq":n,"sig":hex}
//                    where sig = HMAC-SHA256(key, "<nonce>|<seq>|<cmd>|<arg>") and seq is larger than any seq accepted
//                    before. The nonce is random per start of this program and published in the state, so an old
//                    command cannot be replayed
//   <prefix>/reply   answers to cmd: {"id","cmd","ok"[,"err"][,"result"]}
//   <prefix>/meta    retained, once per broker connection while commands are on: {"speeds":[mm/s...],
//                    "recipes":[{"id","name"}...]} — the choices the dashboard offers (kept out of the 10 s state)
//   <prefix>/parts   retained, when parts were decided (at most every parts_min_s): the last parts_keep parts,
//                    {"ses","u","p":[{"k" track,"u","l" label,"c" conf,"d" disp,"o" dest,"m" OD mm,"z" teeth,
//                    "x" missing teeth,"w" why,"i" image name}...]} oldest first; "u" = this program's up time (s) when
//                    sent / when the part arrived, so a page can turn it into a clock time with the state's up_s
//   <prefix>/img/<k> retained, chunk k of the newest NG crop (des_app's JPEG, at most image_kb):
//                    {"f" image name,"k","n" chunks,"d" base64}; a few chunks per pass so commands are not held up
//                    (11-17 user: the 4G dashboard lists every part and shows the NG pictures like the ops station)
//
// With board.modbus_port set the state also carries "line" (run state, belt speed, alarm, recipe) and des_app's own
// counters ("counts":"app"); without it the counts come from the feed ("counts":"feed", parts since this program
// connected). Modbus requests use the monitor unit id (uplink/mb_client.hpp). Always: "sys" (board CPU / memory / SD
// card / uptime) and, when des_app writes them, "env" (temperature / humidity) and "vib" (belt motor vibration), read
// from files every 5 s (uplink/board_env.hpp).
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
#include <vector>

#include "board_env.hpp"
#include "board_link.hpp"
#include "mb_client.hpp"
#include "quectel.hpp"

namespace des {

struct RecipeName { int id = 0; std::string name; };

struct UplinkConfig {
    MqttConfig mqtt;
    std::string prefix = "des/demo";
    int state_period_s = 10, state_min_s = 2, idle_period_s = 60;
    std::string mode = "always";            // "always" | "backup"
    std::string eth_if = "eth0";
    std::string board_host = "127.0.0.1";
    int feed_port = 9500;
    std::string log_dir = "/root/des/data/log";   // des_app's storage.dir: env.csv / vib_*.csv; "" = no sensor readings
    unsigned vid = 0x2c7c;                  // USB vendor id of the module (移远)
    int usb_if = -1;                        // AT interface number, -1 = find it
    std::string tty;                        // use this serial device instead of usbfs (a kernel with the driver)
    int modbus_port = 0;                    // > 0: des_app's Modbus-TCP port on board_host (line state + commands)
    std::string cmd_key;                    // shared secret for remote commands; empty = no remote commands
    std::vector<std::string> cmd_allow{"ack", "stop", "pause"};   // nothing that starts motion by default
    std::vector<double> speeds;             // belt speed presets, mm/s: the only values "speed" accepts (empty = any)
    std::vector<RecipeName> recipes;        // the recipes "recipe" accepts, named for the dashboard (empty = any id)
    int parts_keep = 5;                     // <prefix>/parts: the last N parts (0 = no per-part list, no images)
    int image_kb = 24;                      // <prefix>/img: NG crops up to this size (0 = no images)
    double parts_min_s = 1;                 // at most one parts message per this many seconds
};
std::string load_uplink_config(const std::string &path, UplinkConfig &c);   // "" on success

struct UplinkStats { uint64_t published = 0, failed = 0, reconnects = 0, orders = 0, commands = 0, images = 0; };

struct LineStatus {                         // from des_app's input registers
    bool valid = false;
    int state = 0, flags = 0, alarm = 0, recipe = 0;   // CANP_ST_* / CANP_FLAG_* / alarm code / recipe id
    double speed = 0;                       // actual belt speed, mm/s
    double set_speed = 0;                   // its set point (holding register), mm/s; 0 = not read
    uint32_t total = 0, ok = 0, ng = 0, recheck = 0;   // des_app's counters
};
// cmds / nonce / last_seq go into the state while commands are on; last_seq lets a phone whose clock is behind (seq is
// its clock in ms) still pick a larger seq after another phone sent one
struct UplinkExtras { LineStatus line; std::vector<std::string> cmds; std::string nonce; long long last_seq = 0; BoardEnv env; };

std::string uplink_state_json(const BoardSnapshot &b, const ModemInfo &m, int eth, double up_s, const UplinkStats &st,
                              const UplinkExtras *x = nullptr);
std::string uplink_meta_json(const UplinkConfig &c);                       // <prefix>/meta: speeds / recipes on offer
std::string uplink_cmd_text(const std::string &nonce, long long seq, const std::string &cmd, long long arg);   // what is signed
std::string uplink_fit_order(const std::string &json, size_t max_bytes);   // drop the long fields until it fits
// <prefix>/parts: up_now = this program's up time, board_now = BoardLink::now() (the clock of PartInfo::rx); the oldest
// parts are dropped until it fits max_bytes
std::string uplink_parts_json(const std::string &session, const std::vector<PartInfo> &parts, double up_now, double board_now,
                              size_t max_bytes = 1400);
std::vector<std::string> uplink_image_chunks(const std::string &name, const std::string &jpeg, size_t chunk = 960);
std::string base64(const std::string &bytes);
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
    void attach_modbus(MbClient *mb) { mb_ = mb; }  // optional; not owned
    const std::string &nonce() const { return x_.nonce; }
    void set_nonce(const std::string &n) { x_.nonce = n; }   // tests
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
    void poll_line(double now);
    std::string run_command(const std::string &cmd, long long arg, long long seq, const std::string &sig, int &result);
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
    bool meta_sent_ = false;                        // <prefix>/meta published on this broker connection
    MbClient *mb_ = nullptr;
    bool mb_off_ = false;                           // des_app's map is too old: never ask again
    UplinkExtras x_;
    BoardEnvReader env_reader_;
    double next_line_ = 0, next_env_ = 0;
    uint64_t line_seq_ = 0, sent_line_seq_ = ~0ull;
    long long last_cmd_seq_ = 0;
    uint64_t sent_parts_seq_ = 0;                   // BoardLink::parts_seq() of the last parts message
    double last_parts_ = -1e9;
    std::vector<std::string> img_;                  // the picture being sent, as chunk messages
    size_t img_k_ = 0;                              // next chunk to send
};

}  // namespace des
