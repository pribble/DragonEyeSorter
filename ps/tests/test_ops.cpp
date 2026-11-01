// test_ops: work orders and the health rules of app/ops (故障检测 → 运维工单). No hardware: runs on the board or any
// Linux host.   ./test_ops   (exit code = failed checks)
#include <cstdio>
#include <string>
#include <unistd.h>
#include "json.hpp"
#include "ops.hpp"
#include "test_check.hpp"
using namespace des;
namespace {
const int64_t kWall = 1761998400000LL;   // 2025-11-01 12:00:00 UTC
HealthInput base(double t)
{
    HealthInput in;
    in.now = t;
    in.wall_ms = kWall + int64_t(t * 1000);
    in.run_state = 1;
    in.processed = uint64_t(t * 60);
    return in;
}
void work_orders()
{
    WorkOrders wo;
    const uint16_t a = wo.raise(OPS_BEAM, "first", 1.0, kWall);
    CHECK(a == 1, "first id %u", a);
    CHECK(wo.active(OPS_BEAM), "beam active");
    // a repeat while open bumps the same order
    const uint16_t b = wo.raise(OPS_BEAM, "second", 2.0, kWall + 1000);
    CHECK(b == a, "repeat keeps the id (%u vs %u)", b, a);
    std::vector<WorkOrder> l = wo.list(10);
    CHECK(l.size() == 1 && l[0].count == 2 && l[0].detail == "second", "count %u detail %s", l.empty() ? 0 : l[0].count,
          l.empty() ? "" : l[0].detail.c_str());
    CHECK(std::string(l[0].status_key()) == "open", "status %s", l[0].status_key());
    // condition gone: recovered, still waiting for an acknowledgement; a new occurrence reopens the same order
    wo.clear(OPS_BEAM, 3.0, kWall + 2000);
    l = wo.list(10);
    CHECK(std::string(l[0].status_key()) == "recovered", "recovered: %s", l[0].status_key());
    CHECK(wo.raise(OPS_BEAM, "third", 4.0, kWall) == a, "recovered + unacked: same order");
    CHECK(wo.list(1)[0].count == 3 && wo.list(1)[0].active, "reopened, count 3");
    // acknowledged while active -> 处理中; cleared afterwards -> closed; the next occurrence opens a new order
    CHECK(wo.ack(a, "test", kWall) == 1, "ack one");
    CHECK(std::string(wo.list(1)[0].status_cn()) == "处理中", "acked while active");
    CHECK(wo.ack(a, "test", kWall) == 0, "ack twice = 0");
    wo.clear(OPS_BEAM, 5.0, kWall);
    CHECK(wo.list(1)[0].closed(), "closed after ack + clear");
    const uint16_t c = wo.raise(OPS_BEAM, "new", 6.0, kWall);
    CHECK(c == 2, "closed -> new order id %u", c);
    // other codes are independent; ack all
    wo.raise(OPS_BELT_SPEED, "belt", 7.0, kWall);
    WorkOrders::Summary s = wo.summary();
    CHECK(s.active == 2 && s.faults == 1 && s.warnings == 1 && s.top == 3, "summary active %d faults %d warnings %d top %u",
          s.active, s.faults, s.warnings, s.top);
    CHECK(s.unacked == 2 && s.unacked_warning, "unacked %d", s.unacked);
    CHECK(s.last_code == OPS_BELT_SPEED && s.last_id == 3, "last %u/%u", s.last_code, s.last_id);
    CHECK(wo.ack(0xFFFF, "remote", kWall) == 2, "ack all");
    s = wo.summary();
    CHECK(s.unacked == 0 && !s.unacked_warning && s.active == 2, "after ack all: unacked %d active %d", s.unacked, s.active);
    // changes: every raise / clear / ack, the first one is_new
    std::vector<WorkOrders::Change> ch = wo.take_changes();
    CHECK(ch.size() == 10 && ch[0].is_new && !ch[1].is_new, "changes %zu", ch.size());
    CHECK(wo.take_changes().empty(), "drained");
    // JSON: parseable, Chinese kept, the fields the remote station reads
    const nlohmann::json j = nlohmann::json::parse(wo.json(wo.list(1)[0]));
    CHECK(j["id"] == 3 && j["code"] == OPS_BELT_SPEED && j["key"] == "belt_speed" && j["level"] == 3, "json %s",
          j.dump().c_str());
    CHECK(j["title"].get<std::string>().find("皮带") != std::string::npos && j["stop"] == true, "json title / stop");
    CHECK(j["status"] == "acked" && j["acked_by"] == "remote", "json status %s", j["status"].dump().c_str());
    const nlohmann::json arr = nlohmann::json::parse(wo.snapshot_json(50));
    CHECK(arr.is_array() && arr.size() == 3 && arr[0]["id"] == 3, "snapshot %s", arr.dump().substr(0, 80).c_str());
    // wall clock text: UTC+8, empty before any sync
    CHECK(ops_wall_str(kWall, 8) == "2025-11-01 20:00:00", "wall %s", ops_wall_str(kWall, 8).c_str());
    CHECK(ops_wall_str(kWall, 8, false) == "20:00:00", "wall time only");
    CHECK(ops_wall_str(5000, 8).empty(), "1970 clock -> empty");
    // the log file gets one JSON line per change
    char path[] = "/tmp/test_ops_XXXXXX";
    const int fd = mkstemp(path);
    close(fd);
    WorkOrders w2;
    w2.open_log(path, "S1");
    w2.raise(OPS_LIGHT, "dark", 1, kWall);
    w2.clear(OPS_LIGHT, 2, kWall);
    FILE *f = fopen(path, "r");
    int lines = 0;
    char buf[4096];
    while (f && fgets(buf, sizeof buf, f)) {
        lines++;
        CHECK(nlohmann::json::parse(buf)["session"] == "S1", "log line %d", lines);
    }
    if (f) fclose(f);
    unlink(path);
    CHECK(lines == 2, "log lines %d", lines);
    // the memory keeps the newest kKeep
    WorkOrders w3;
    for (size_t k = 0; k < WorkOrders::kKeep + 20; k++) {
        w3.raise(OPS_BEAM, "x", double(k), kWall);
        w3.ack(0xFFFF, "t", kWall);
        w3.clear(OPS_BEAM, double(k), kWall);
    }
    CHECK(w3.list(1000).size() == WorkOrders::kKeep, "kept %zu", w3.list(1000).size());
}
void belt_rule()
{
    OpsConfig c;
    HealthMonitor hm(c);
    WorkOrders wo;
    double t = 0;
    auto step = [&](double cmd, double enc) {   // one encoder window every 0.5 s
        HealthInput in = base(t);
        in.enc_live = in.enc_new = true;
        in.belt_cmd_mm_s = cmd;
        in.belt_enc_mm_s = enc;
        hm.update(in, wo);
        t += 0.5;
    };
    for (int k = 0; k < 10; k++) step(50, 49);   // running fine
    step(50, 20);                                // one slow window while accelerating: nothing
    step(50, 50);
    CHECK(!wo.active(OPS_BELT_SPEED), "single slow window ignored");
    for (int k = 0; k < 3; k++) step(50, 2);     // 1.0 s stalled: not yet
    CHECK(!wo.active(OPS_BELT_SPEED), "1.0 s stalled: not yet");
    step(50, 2);                                 // 1.5 s
    CHECK(wo.active(OPS_BELT_SPEED), "1.5 s stalled: fault");
    std::vector<WorkOrders::Change> ch = wo.take_changes();
    CHECK(ch.size() == 1 && ch[0].is_new && ops_code_info(ch[0].w.code).stop, "one new stop order");
    CHECK(ch.size() == 1 && ch[0].w.detail.find("50.0") != std::string::npos, "detail %s",
          ch.empty() ? "" : ch[0].w.detail.c_str());
    // the line stops (command 0): recovered after 1 s, waits for an acknowledgement
    step(0, 0);
    step(0, 0);
    step(0, 0);
    CHECK(!wo.active(OPS_BELT_SPEED) && !wo.list(1)[0].acked, "recovered, not acknowledged");
    // slow belt below belt_min: never judged
    for (int k = 0; k < 10; k++) step(5, 0);
    CHECK(!wo.active(OPS_BELT_SPEED), "below belt_min_mm_s: not judged");
    // encoder not wired: never judged
    HealthMonitor hm2(c);
    WorkOrders wo2;
    for (int k = 0; k < 10; k++) {
        HealthInput in = base(k * 0.5);
        in.enc_new = true;
        in.belt_cmd_mm_s = 50;
        hm2.update(in, wo2);
    }
    CHECK(!wo2.active(OPS_BELT_SPEED), "no encoder: no belt orders");
}
void light_rule()
{
    OpsConfig c;
    HealthMonitor hm(c);
    WorkOrders wo;
    double t = 0;
    auto step = [&](double g, int mode = 0) {
        HealthInput in = base(t);
        in.gray = g;
        in.pl_mode = mode;
        hm.update(in, wo);
        t += 0.1;
    };
    for (int k = 0; k < 100; k++) step(60 + (k % 3));   // 10 s learning
    CHECK(hm.light_ref() > 59 && hm.light_ref() < 63, "learned ref %.1f", hm.light_ref());
    for (int k = 0; k < 15; k++) step(10);   // lamp off: 1.5 s
    CHECK(!wo.active(OPS_LIGHT), "dark 1.5 s: not yet");
    for (int k = 0; k < 15; k++) step(10);
    CHECK(wo.active(OPS_LIGHT), "dark 3 s: warning");
    for (int k = 0; k < 30; k++) step(61);
    CHECK(!wo.active(OPS_LIGHT), "light back: recovered");
    // a PL display mode (binary / Sobel frames) relearns instead of alarming
    for (int k = 0; k < 50; k++) step(15, 4);
    CHECK(!wo.active(OPS_LIGHT), "mode change relearns (no order)");
    // brighter is no fault: a white sheet for 5 s, then the belt again
    for (int k = 0; k < 100; k++) step(15, 0);   // back to mode 0: relearn at 15 (lamps off at the start)
    for (int k = 0; k < 600; k++) step(60);      // lamps switched on: 60 s, the level follows up
    CHECK(!wo.active(OPS_LIGHT) && hm.light_ref() > 50, "lamps on after the start: no order, level %.1f", hm.light_ref());
    for (int k = 0; k < 50; k++) step(150);      // white sheet 5 s
    for (int k = 0; k < 50; k++) step(60);
    CHECK(!wo.active(OPS_LIGHT), "white sheet passing: no order (level %.1f)", hm.light_ref());
    for (int k = 0; k < 40; k++) step(12);       // lamp out
    CHECK(wo.active(OPS_LIGHT), "lamp out after the level followed up: order");
}
void event_rules()
{
    OpsConfig c;
    HealthMonitor hm(c);
    WorkOrders wo;
    HealthInput in = base(1);
    hm.update(in, wo);
    // beam: one event opens a warning; it recovers after event_clear_s without repeats
    in = base(2);
    in.beam_no_track = 1;
    hm.update(in, wo);
    CHECK(wo.active(OPS_BEAM), "beam event: warning");
    CHECK(wo.list(1)[0].detail.find("对射被挡住") != std::string::npos, "beam detail");
    in = base(20);
    hm.update(in, wo);
    CHECK(wo.active(OPS_BEAM), "beam still active at 18 s");
    in = base(40);
    hm.update(in, wo);
    CHECK(!wo.active(OPS_BEAM), "beam recovered after 30 s");
    // picks: one failure is tolerated, two in a row open an order, the next success clears it
    in = base(41);
    in.picks_failed = 1;
    in.pick_why = "gripper closed fully: nothing picked (pos 2300, cmd 2290, 0.40 mm < 0.80 mm)";
    hm.update(in, wo);
    CHECK(!wo.active(OPS_PICK), "one failure: no order");
    in = base(42);
    in.picks_failed = 2;
    in.pick_why = "gripper closed fully: nothing picked";
    hm.update(in, wo);
    CHECK(wo.active(OPS_PICK), "two failures: order");
    CHECK(wo.list(1)[0].detail.find("没夹到零件") != std::string::npos, "pick detail %s", wo.list(1)[0].detail.c_str());
    in = base(43);
    in.picks_failed = 2;
    in.picks_ok = 1;
    hm.update(in, wo);
    CHECK(!wo.active(OPS_PICK), "a good pick clears it");
    // servo bus: every unconfirmed command
    in = base(44);
    in.picks_failed = 3;
    in.picks_ok = 1;
    in.servo_errors = 1;
    in.pick_why = "SYNC WRITE failed: timeout";
    hm.update(in, wo);
    CHECK(wo.active(OPS_SERVO), "servo order");
    // RS485: only after the module answered once, after link_s
    in = base(45);
    in.picks_failed = 3; in.picks_ok = 1; in.servo_errors = 1;
    in.rs485 = 0;
    hm.update(in, wo);
    in.now = 50;
    hm.update(in, wo);
    CHECK(!wo.active(OPS_RS485), "module never answered: no order (not wired yet)");
    in.rs485 = 1; in.now = 51;
    hm.update(in, wo);
    in.rs485 = 0; in.now = 52;
    hm.update(in, wo);
    in.now = 55.5;
    hm.update(in, wo);
    CHECK(wo.active(OPS_RS485), "module quiet 3.5 s after it answered: order");
    in.rs485 = 1; in.now = 57;
    hm.update(in, wo);
    in.now = 58.5;
    hm.update(in, wo);
    CHECK(!wo.active(OPS_RS485), "module back: recovered");
    // e-stop follows the line state
    in.run_state = 3; in.now = 60;
    hm.update(in, wo);
    CHECK(wo.active(OPS_ESTOP), "e-stop order");
    in.run_state = 0; in.now = 61;
    hm.update(in, wo);
    CHECK(!wo.active(OPS_ESTOP), "e-stop reset: recovered");
    // NPU expected but not running: once
    in.npu_expected = true; in.npu_running = false; in.npu_error = "no model"; in.now = 62;
    hm.update(in, wo);
    CHECK(wo.active(OPS_NPU), "npu order");
    // storage: write errors and low space
    in.log_errors = 3; in.now = 63;
    hm.update(in, wo);
    CHECK(wo.active(OPS_STORAGE), "storage order");
    in.now = 100; in.free_mb = 2000;
    hm.update(in, wo);
    CHECK(!wo.active(OPS_STORAGE), "storage recovered after 30 s without errors");
    // processing rate: two slow 5 s windows after the warm-up
    HealthMonitor hm2(c);
    WorkOrders wo2;
    for (int k = 0; k <= 6; k++) {   // 0..30 s at 60 fps
        HealthInput r = base(k * 5.0);
        hm2.update(r, wo2);
    }
    uint64_t n = 30 * 60;
    for (int k = 7; k <= 9; k++) {   // 35..45 s at 30 fps
        n += 150;
        HealthInput r = base(k * 5.0);
        r.processed = n;
        hm2.update(r, wo2);
    }
    CHECK(wo2.active(OPS_VISION_RATE), "processing rate warning");
}
void quality_rules()
{
    OpsConfig c;
    HealthMonitor hm(c);
    WorkOrders wo;
    for (int k = 0; k < 9; k++) hm.on_part(1, false, 0, 31.96f, 0.2f, k, kWall, wo);
    CHECK(!wo.active(OPS_NG_RATE), "fewer than rate_min parts: no order");
    hm.on_part(1, false, 0, 31.96f, 0.2f, 10, kWall, wo);
    CHECK(wo.active(OPS_NG_RATE), "10 of 10 NG: order");
    for (int k = 0; k < 20; k++) hm.on_part(0, true, 31.96f, 31.96f, 0.2f, 20 + k, kWall, wo);
    CHECK(!wo.active(OPS_NG_RATE), "good parts: recovered");
    CHECK(!wo.active(OPS_OD_DRIFT), "centred OD: no drift");
    for (int k = 0; k < 10; k++) hm.on_part(0, true, 32.09f, 31.96f, 0.2f, 40 + k, kWall, wo);
    CHECK(wo.active(OPS_OD_DRIFT), "mean OD +0.13 of +-0.2: drift order");
    for (int k = 0; k < 12; k++) hm.on_part(2, false, 0, 31.96f, 0.2f, 50 + k, kWall, wo);
    CHECK(wo.active(OPS_RECHECK_RATE), "12 of 20 recheck (60%%): order");
    // disabled monitor does nothing
    OpsConfig off;
    off.enabled = false;
    HealthMonitor h3(off);
    WorkOrders w3;
    for (int k = 0; k < 30; k++) h3.on_part(1, false, 0, 31.96f, 0.2f, k, kWall, w3);
    HealthInput in = base(1);
    in.run_state = 3;
    h3.update(in, w3);
    CHECK(w3.list(10).empty(), "disabled: no orders");
}
}  // namespace
int main()
{
    work_orders();
    belt_rule();
    light_rule();
    event_rules();
    quality_rules();
    return test_summary("test_ops");
}
