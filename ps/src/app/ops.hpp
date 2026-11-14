// ops.hpp — 设备运维：故障检测 → 运维工单 → 本地记录 + 推送（赛题拓展 4「工单推送」、拓展 5「远程运维」，2026-11-01）.
//
// HealthMonitor only looks at what the line already measures (belt: stepper command vs measuring-wheel encoder; camera:
// frame brightness, processing rate; through-beam vs vision; pick results; RS485 / PLC links; NPU; storage; quality
// rates of the last parts) and opens / closes work orders in WorkOrders. One order per fault code while it is not
// closed: a repeat bumps its count instead of opening another one. Every change is appended to a JSONL file next to
// the part records (本地故障日志) and handed to the pushers by the live worker: HDMI 运维 page, network feed 'W' frames
// (pc/ops/ops_station.py, the remote station), Modbus-TCP input registers, CAN ALARM frame, tower light.
// A fault-level order with `stop` puts the line in FAULT (belt held, red lamp, buzzer) until 复位报警; warnings leave
// it running. An order is closed when its condition is gone AND someone acknowledged it (HMI or remote station).
// Pure logic + a log file, no hardware: tests/test_ops.cpp runs it on the host.
#pragma once
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
namespace des {
enum class OpsLevel : uint8_t { Info = 1, Warning = 2, Fault = 3 };
// Fault codes: the same numbers on every channel (CAN ALARM byte 0, Modbus IR 34, JSON "code"). 1 = e-stop was the
// only alarm code of CAN protocol v1 and keeps its number.
enum OpsCode : uint8_t {
    OPS_ESTOP = 1, OPS_BELT_SPEED, OPS_CAMERA, OPS_LIGHT, OPS_BEAM, OPS_PICK, OPS_SERVO, OPS_RS485, OPS_PLC, OPS_NPU,
    OPS_VISION_RATE, OPS_STORAGE, OPS_NG_RATE, OPS_RECHECK_RATE, OPS_OD_DRIFT,
    OPS_TEST,   // 运维页「发送测试工单」: checks the push chain board -> remote station -> acknowledgement
    OPS_VIB, OPS_VIB_HIGH,   // v17 batch 2: belt motor vibration (app/vib_mon.hpp), warning / fault level
    OPS_ENV, OPS_ENV_LINK,   // RS485 temperature / humidity transmitter: out of range / not answering
    OPS_N
};
struct OpsCodeInfo {
    const char *key;      // English id (JSON / log)
    const char *device;   // 设备 / 部位
    const char *title;    // 故障名称
    const char *advice;   // 处理建议（工单内容）
    OpsLevel level;
    bool stop;            // fault that stops the line (Runner FAULT)
};
const OpsCodeInfo &ops_code_info(int code);   // unknown code -> a generic entry
const char *ops_level_cn(OpsLevel l);         // 提示 / 警告 / 故障
// thresholds (des_app.json "ops"; all optional)
struct OpsConfig {
    bool enabled = true;
    int utc_offset_h = 8;           // wall clock shown on the HMI (the board runs UTC; times are synced from the PLC)
    // belt: commanded >= belt_min_mm_s but the encoder sees < belt_ratio of it for belt_s -> FAULT (stop)
    float belt_min_mm_s = 10, belt_ratio = 0.3f, belt_s = 1.5f;
    // light: mean frame brightness below light_low of the level learned in the first light_learn_s of a PL display
    // mode (lamp out, lens covered). Brighter is never a fault: the level follows it up (lamps switched on after the
    // start); a white sheet passing moves it a little, and it drifts back down slowly, never towards a dark lamp.
    float light_low = 0.55f, light_s = 2, light_learn_s = 8;
    int pick_fails = 2;             // consecutive failed picks
    float link_s = 3;               // RS485 module / PLC quiet this long
    float min_fps = 45;             // processed frames per second (5 s windows)
    float min_free_mb = 200;        // SD card free space
    int rate_window = 20, rate_min = 10;     // quality rates over the last parts
    float ng_rate = 0.8f, recheck_rate = 0.6f;   // high: demo batches are half defects on purpose
    int od_window = 10;             // mean OD of the last good parts ...
    float od_drift = 0.5f;          // ... off the nominal by more than this share of the tolerance
    float event_clear_s = 30;       // event-type orders (beam, pick, servo) recover after this long without a repeat
    // vibration (vib_mon): AC RMS of the three axes above vib_warn_g / vib_fault_g for vib_s -> warning / fault order
    // (neither stops the line). ⚠ placeholders until measured on the motor seat with the belt running.
    float vib_warn_g = 0.30f, vib_fault_g = 0.80f, vib_s = 2.0f;
    // environment (RS485 transmitter): temperature outside [env_temp_lo, env_temp_hi] or humidity above env_hum_hi for
    // env_s -> warning; the transmitter quiet (after it answered once) -> warning
    float env_temp_lo = 5, env_temp_hi = 40, env_hum_hi = 85, env_s = 5;
};
struct WorkOrder {
    uint16_t id = 0;
    uint8_t code = 0;
    OpsLevel level = OpsLevel::Warning;
    std::string detail;             // what was measured (Chinese)
    double t_first = 0, t_last = 0, t_clear = 0;           // monotonic s
    int64_t wall_first = 0, wall_last = 0, wall_clear = 0, wall_ack = 0;   // board wall clock ms (+ PLC offset)
    uint32_t count = 1;             // times raised
    bool active = true, acked = false;
    std::string acked_by;
    bool closed() const { return !active && acked; }
    const char *status_key() const;   // open / acked / recovered / closed
    const char *status_cn() const;    // 待处理 / 处理中 / 已恢复待确认 / 已关闭
};
class WorkOrders {
public:
    struct Change { WorkOrder w; bool is_new = false, raised = false; };   // raised: raise() (new or repeat)
    struct Summary {
        int active = 0, unacked = 0, faults = 0, warnings = 0;   // active ones / not acknowledged / active by level
        uint8_t top = 0;              // highest level among the active ones (0 = none)
        uint16_t last_id = 0, seq = 0;   // newest order; seq + 1 per change (a PLC sees something changed)
        uint8_t last_code = 0;
        std::string last_title;       // newest order that is not closed ("" = none)
        bool unacked_warning = false; // a warning nobody acknowledged yet (yellow lamp)
    };
    ~WorkOrders();
    void open_log(const std::string &path, const std::string &session);   // JSONL, appended on every change
    // the condition of `code` is present: opens an order or bumps the one not closed yet; returns its id
    uint16_t raise(int code, const std::string &detail, double now, int64_t wall_ms);
    void clear(int code, double now, int64_t wall_ms);   // condition gone: active -> false (no-op when not active)
    int ack(uint16_t id, const char *who, int64_t wall_ms);   // id 0xFFFF = all; returns how many were acknowledged
    bool active(int code) const;
    std::vector<WorkOrder> list(size_t max) const;       // newest first
    Summary summary() const;
    std::vector<Change> take_changes();                  // since the last call (pushers)
    std::string snapshot_json(size_t max) const;          // JSON array of list(max), for a remote station that connects
    std::string json(const WorkOrder &w) const;
    const std::string &session() const { return session_; }
    static constexpr size_t kKeep = 200;
private:
    WorkOrder *open_order(int code);   // newest order of this code that is not closed
    void changed(const WorkOrder &w, bool is_new, bool raised = false);
    mutable std::mutex m_;
    std::deque<WorkOrder> orders_;     // oldest first
    std::vector<Change> changes_;
    uint16_t next_id_ = 1, seq_ = 0;
    FILE *log_ = nullptr;
    std::string session_;
};
// "2026-11-01 21:30:05" at utc_offset_h, or "" when the wall clock was never synced (board clock before 2023)
std::string ops_wall_str(int64_t wall_ms, int utc_offset_h, bool with_date = true);
// one control cycle of the live worker
struct HealthInput {
    double now = 0;
    int64_t wall_ms = 0;
    int run_state = -1;                 // CANP_ST_* (-1: no production runner, `des_app live`)
    bool enc_live = false, enc_new = false;   // encoder wired / a new speed window this cycle
    double belt_cmd_mm_s = 0, belt_enc_mm_s = 0;
    double gray = -1;                   // mean brightness of the frame (< 0 = not measured this cycle)
    int pl_mode = 0;                    // PL display mode (it changes the frames the PS sees)
    uint64_t processed = 0;             // frames processed by the worker so far
    int beam_no_track = 0, beam_missed = 0;   // through-beam events this cycle
    uint32_t picks_ok = 0, picks_failed = 0, servo_errors = 0;   // totals so far (the monitor looks at the change)
    std::string pick_why;               // reason of the last failure
    int rs485 = -1;                     // -1 not running, 0 module quiet, 1 ok
    bool plc_lost = false;              // a PLC that was there went quiet (PlcWatch)
    bool npu_expected = false, npu_running = false;
    std::string npu_error;
    double free_mb = -1;                // < 0 = not checked this cycle
    uint64_t log_errors = 0, log_dropped = 0;
    double vib_rms_g = -1, vib_peak_hz = 0;   // vibration monitor (< 0 = off / no window yet)
    int env_link = -1;                  // temperature / humidity transmitter: -1 none, 0 quiet, 1 ok
    bool env_valid = false;             // env_temp_c / env_hum_rh hold a reading
    double env_temp_c = 0, env_hum_rh = 0;
};
class HealthMonitor {
public:
    explicit HealthMonitor(const OpsConfig &c) : c_(c) {}
    void update(const HealthInput &in, WorkOrders &wo);
    // a decided part (disp 0 pick / 1 gate / 2 recheck), for the quality rates; od of good parts for the drift
    void on_part(int disp, bool od_valid, float od, float od_nom, float od_tol, double now, int64_t wall_ms,
                 WorkOrders &wo);
    double light_ref() const { return light_ref_; }
private:
    struct Hold {   // a condition that must last on_s to raise and be gone off_s to clear
        double since = -1, gone = -1;
        bool on = false;
        int step(bool cond, double now, double on_s, double off_s);   // +1 raised now, -1 cleared now, 0 no change
    };
    const OpsConfig c_;
    Hold belt_, light_, rate_, rs485_, plc_, vib_w_, vib_f_, env_, env_link_;
    bool env_seen_ = false;
    double t_start_ = -1;
    // light
    int light_mode_ = -1;
    double light_learn_t0_ = -1, light_sum_ = 0, light_ref_ = -1, light_cur_ = -1;
    int light_n_ = 0;
    // processing rate
    double rate_t0_ = -1;
    uint64_t rate_n0_ = 0;
    // events
    uint32_t picks_ok_ = 0, picks_failed_ = 0, servo_errors_ = 0;
    bool have_counts_ = false;
    int fail_streak_ = 0;
    double last_beam_ = -1, last_pick_ = -1, last_servo_ = -1;
    uint32_t beam_events_ = 0;
    // storage
    uint64_t log_errors_ = 0, log_dropped_ = 0;
    double last_storage_ = -1;
    bool rs485_seen_ = false;
    bool npu_raised_ = false;
    // quality
    std::deque<uint8_t> disp_;
    std::deque<float> od_;
};
}  // namespace des
