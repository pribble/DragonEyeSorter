// quectel.hpp — the 移远 (Quectel) LTE module as the uplink: bring-up, data connection, and the module's own MQTT
// client, all through AT commands (uplink/at_chan). The module runs TCP/IP and MQTT itself, so the board needs no
// network driver for it — which is the point: the board's kernel has none.
//
// Command set and limits: 移远 EC2x&EG9x&EM05 MQTT Application Note V1.1 and TCP/IP AT Commands Manual V1.0.
//   - one publish <= 1500 bytes; QoS 1 publishes are sent one at a time (the result URC is awaited)
//   - activating the data connection may take up to 150 s, opening the broker connection up to 120 s
//   - a string parameter cannot contain a double quote (will message, topics, client id, user, password)
#pragma once

#include <string>

#include "at_chan.hpp"

namespace des {

struct ModemInfo {
    std::string model, revision, imei, iccid;
    std::string oper, act;       // operator name, access technology ("FDD LTE" ...)
    std::string ip;              // address of the data connection
    bool sim_ready = false;
    int csq = 99;                // 0..31 (31 best), 99 = unknown
    int reg = 0;                 // registration: 1 home, 5 roaming, 2 searching, 3 denied, 0 none
    bool registered() const { return reg == 1 || reg == 5; }
    int dbm() const { return csq >= 0 && csq <= 31 ? -113 + 2 * csq : 0; }
};

struct MqttConfig {
    std::string host;
    int port = 1883;
    std::string client_id, user, pass;
    int keepalive_s = 60;
    std::string will_topic, will_msg;   // empty topic = no will; retained, QoS 1
    std::string apn;                    // empty = whatever the card / network gives
    int cid = 1;                        // data connection (PDP context) number
};

struct ModemUrc {
    enum Kind { Other, MqttStat, MqttPing, MqttRecv } kind = Other;
    int code = 0;                       // MqttStat: 移远 error code 1..7; MqttPing: result
    std::string topic, payload;         // MqttRecv
};

class Quectel {
public:
    explicit Quectel(AtChan &at) : at_(at) {}
    bool alive();                                   // answers AT; echo off, numeric error codes on
    void identify(ModemInfo &m);                    // model / firmware / IMEI / card number
    void refresh(ModemInfo &m);                     // card, signal, registration, operator
    bool has_mqtt();                                // the firmware knows AT+QMTCFG
    std::string data_up(const MqttConfig &c, ModemInfo &m);     // "" = data connection active
    std::string mqtt_open(const MqttConfig &c);                 // "" = connected to the broker
    std::string mqtt_sub(const std::string &topic, int qos);
    std::string mqtt_pub(const std::string &topic, const std::string &payload, int qos, bool retain);
    void mqtt_close();
    static ModemUrc parse_urc(const std::string &line);
    static constexpr size_t kMaxPayload = 1500;

private:
    std::string wait_result(const char *prefix, int timeout_ms, int want_fields, int *extra = nullptr);
    AtChan &at_;
    bool pubex_ = true;      // AT+QMTPUBEX (fixed length); older firmware only has AT+QMTPUB ... Ctrl+Z
    bool pub_proven_ = false;
    int msg_id_ = 0;
};

}  // namespace des
