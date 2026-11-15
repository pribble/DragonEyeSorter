// ops.cpp — see ops.hpp
#include "ops.hpp"
#include <time.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include "json.hpp"
#include "util.hpp"
namespace des {
namespace {
const OpsCodeInfo kInfo[OPS_N] = {
    {"unknown", "系统", "未知故障", "联系维护人员", OpsLevel::Warning, false},
    {"estop", "安全回路", "急停按下", "确认现场安全后旋开急停按钮，在界面点「复位报警」再启动", OpsLevel::Fault, false},
    {"belt_speed", "传送带", "皮带速度异常（卡滞 / 打滑）",
     "检查皮带是否被卡住、打滑或张紧不够，计米轮是否压紧皮带；排除后点「复位报警」再启动", OpsLevel::Fault, true},
    {"camera", "相机", "相机无图像", "检查相机排线和子卡供电；程序退出后由守护进程自动重启取图", OpsLevel::Fault, false},
    {"light", "光源 / 镜头", "画面亮度异常", "检查两侧光源是否熄灭、镜头是否被遮挡或沾污；亮度恢复后自动关闭",
     OpsLevel::Warning, false},
    {"beam", "检测工位", "对射与视觉不一致（漏检风险）",
     "检查是否有零件贴得太近、手或异物经过光束；反复出现时检查对射对准和相机标定", OpsLevel::Warning, false},
    {"pick", "机械臂", "机械臂连续抓取失败", "检查零件是否在抓取范围内、夹爪开口，以及手眼标定是否偏移",
     OpsLevel::Warning, false},
    {"servo", "机械臂舵机总线", "舵机指令未确认", "检查舵机 6.8V 供电、总线接线和电平转换板；步进驱动器干扰大时检查走线",
     OpsLevel::Warning, false},
    {"rs485", "RS485 总线", "三色灯继电器模块通信中断", "检查 RS485 A / B 接线和继电器模块的 24V 供电", OpsLevel::Warning,
     false},
    {"plc", "CAN / Modbus", "PLC 通信中断", "检查 CAN 线、网线，以及 PLC（上位机）程序是否在运行", OpsLevel::Warning, false},
    {"npu", "NPU", "NPU 推理未运行", "检查板上模型文件（part_cls.json、part_cls_uint8.onnx）和 NPU 驱动", OpsLevel::Warning,
     false},
    {"vision_rate", "视觉处理", "视觉处理帧率下降", "检查板上是否有其他程序占用 CPU；帧率恢复后自动关闭", OpsLevel::Warning,
     false},
    {"storage", "SD 卡存储", "记录存储异常", "清理 SD 卡上旧的记录和图片，或检查存储卡", OpsLevel::Warning, false},
    {"ng_rate", "工艺质量", "不良率异常升高", "检查上游来料和工序；确认配方、公差设置是否正确", OpsLevel::Warning, false},
    {"recheck_rate", "工艺质量", "复检率异常升高",
     "检查零件是否走在皮带中线（导向条）、来件是否过密、手是否伸进画面", OpsLevel::Warning, false},
    {"od_drift", "尺寸测量", "外径均值漂移", "用标准件放画面正中复测外径；确认偏差后重定测量偏置，或检查来料尺寸",
     OpsLevel::Warning, false},
    {"test", "运维系统", "测试工单", "验证推送链路：远程运维站应同时收到这张工单；在任一端确认后关闭", OpsLevel::Info, false},
    {"vibration", "传送带电机", "振动偏大（预警）",
     "检查步进电机座与联轴器是否松动、皮带张紧与跑偏；与正常运行时的振动主频对比；恢复后自动关闭", OpsLevel::Warning, false},
    {"vibration_high", "传送带电机", "振动超限（故障）",
     "尽快停线检查电机座螺钉、联轴器、轴承与皮带；排除后振动回落自动恢复，再在界面确认", OpsLevel::Fault, false},
    {"environment", "环境", "环境温湿度超限", "检查展位通风、空调和日照；温湿度回到范围内自动恢复", OpsLevel::Warning, false},
    {"env_link", "RS485 总线", "温湿度变送器通信中断", "检查变送器的 24V 供电和 RS485 A / B 接线（与继电器模块并在一起）",
     OpsLevel::Warning, false},
};
// why of a failed pick (ArmExec / plan_pick, English) for a work order
std::string pick_why_cn(const std::string &w)
{
    if (w.rfind("gripper closed fully", 0) == 0) return "夹爪合到底，没夹到零件";
    if (w.rfind("intercept", 0) == 0) return "零件没到夹取点";
    if (w.rfind("SYNC WRITE failed", 0) == 0) return "舵机指令重发后仍未确认（" + w.substr(19) + "）";
    if (w.rfind("gripper did not answer", 0) == 0) return "夹爪舵机无应答";
    return w.empty() ? "原因未知" : w;
}
}  // namespace
const OpsCodeInfo &ops_code_info(int code) { return code > 0 && code < OPS_N ? kInfo[code] : kInfo[0]; }
const char *ops_level_cn(OpsLevel l) { return l == OpsLevel::Fault ? "故障" : l == OpsLevel::Warning ? "警告" : "提示"; }
const char *WorkOrder::status_key() const
{
    return active ? (acked ? "acked" : "open") : (acked ? "closed" : "recovered");
}
const char *WorkOrder::status_cn() const
{
    return active ? (acked ? "处理中" : "待处理") : (acked ? "已关闭" : "已恢复待确认");
}
std::string ops_wall_str(int64_t wall_ms, int utc_offset_h, bool with_date)
{
    if (wall_ms < 1700000000000LL) return "";   // never synced: the board clock starts in 1970
    const time_t t = time_t(wall_ms / 1000) + time_t(utc_offset_h) * 3600;
    tm g{};
    gmtime_r(&t, &g);
    char b[32];
    strftime(b, sizeof b, with_date ? "%Y-%m-%d %H:%M:%S" : "%H:%M:%S", &g);
    return b;
}
// ------------------------------------------------------------------ WorkOrders
WorkOrders::~WorkOrders()
{
    if (log_) fclose(log_);
}
void WorkOrders::open_log(const std::string &path, const std::string &session)
{
    std::lock_guard<std::mutex> lk(m_);
    if (log_) fclose(log_);
    log_ = path.empty() ? nullptr : fopen(path.c_str(), "a");
    session_ = session;
}
WorkOrder *WorkOrders::open_order(int code)
{
    for (auto it = orders_.rbegin(); it != orders_.rend(); ++it)
        if (it->code == code) return it->closed() ? nullptr : &*it;   // only the newest of a code can be open
    return nullptr;
}
std::string WorkOrders::json(const WorkOrder &w) const
{
    const OpsCodeInfo &i = ops_code_info(w.code);
    const nlohmann::json j = {{"id", w.id}, {"code", w.code}, {"key", i.key}, {"level", int(w.level)},
                              {"level_cn", ops_level_cn(w.level)}, {"device", i.device}, {"title", i.title},
                              {"detail", w.detail}, {"advice", i.advice}, {"stop", i.stop},
                              {"status", w.status_key()}, {"status_cn", w.status_cn()}, {"count", w.count},
                              {"wall_first", w.wall_first}, {"wall_last", w.wall_last}, {"wall_clear", w.wall_clear},
                              {"wall_ack", w.wall_ack}, {"t_first", std::round(w.t_first * 10) / 10},
                              {"acked_by", w.acked_by}, {"session", session_}};
    return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}
void WorkOrders::changed(const WorkOrder &w, bool is_new, bool raised)
{
    seq_++;
    if (changes_.size() >= 500) changes_.erase(changes_.begin());   // nobody drains: keep the newest
    changes_.push_back({w, is_new, raised});
    if (log_) {
        fprintf(log_, "%s\n", json(w).c_str());
        fflush(log_);
    }
}
uint16_t WorkOrders::raise(int code, const std::string &detail, double now, int64_t wall_ms)
{
    std::lock_guard<std::mutex> lk(m_);
    if (WorkOrder *w = open_order(code)) {   // still open (or recovered, not acknowledged): one order, counted
        w->count++;
        w->detail = detail;
        w->t_last = now;
        w->wall_last = wall_ms;
        if (!w->active) {
            w->active = true;
            w->t_clear = 0;
            w->wall_clear = 0;
        }
        changed(*w, false, true);
        return w->id;
    }
    WorkOrder n;
    n.id = next_id_;
    next_id_ = next_id_ >= 0xFFFE ? 1 : uint16_t(next_id_ + 1);   // 0 = none, 0xFFFF = "all" on Modbus
    n.code = uint8_t(code);
    n.level = ops_code_info(code).level;
    n.detail = detail;
    n.t_first = n.t_last = now;
    n.wall_first = n.wall_last = wall_ms;
    orders_.push_back(n);
    while (orders_.size() > kKeep) orders_.pop_front();
    changed(orders_.back(), true, true);
    return n.id;
}
void WorkOrders::clear(int code, double now, int64_t wall_ms)
{
    std::lock_guard<std::mutex> lk(m_);
    WorkOrder *w = open_order(code);
    if (!w || !w->active) return;
    w->active = false;
    w->t_clear = now;
    w->wall_clear = wall_ms;
    changed(*w, false);
}
int WorkOrders::ack(uint16_t id, const char *who, int64_t wall_ms)
{
    std::lock_guard<std::mutex> lk(m_);
    int n = 0;
    for (WorkOrder &w : orders_)
        if ((id == 0xFFFF || w.id == id) && !w.acked) {
            w.acked = true;
            w.acked_by = who ? who : "";
            w.wall_ack = wall_ms;
            changed(w, false);
            n++;
        }
    return n;
}
bool WorkOrders::active(int code) const
{
    std::lock_guard<std::mutex> lk(m_);
    for (auto it = orders_.rbegin(); it != orders_.rend(); ++it)
        if (it->code == code) return it->active;
    return false;
}
std::vector<WorkOrder> WorkOrders::list(size_t max) const
{
    std::lock_guard<std::mutex> lk(m_);
    std::vector<WorkOrder> v;
    for (auto it = orders_.rbegin(); it != orders_.rend() && v.size() < max; ++it) v.push_back(*it);
    return v;
}
WorkOrders::Summary WorkOrders::summary() const
{
    std::lock_guard<std::mutex> lk(m_);
    Summary s;
    s.seq = seq_;
    for (const WorkOrder &w : orders_) {
        if (w.active) {
            s.active++;
            if (w.level == OpsLevel::Fault) s.faults++;
            else if (w.level == OpsLevel::Warning) s.warnings++;
            s.top = std::max<uint8_t>(s.top, uint8_t(w.level));
        }
        if (!w.acked) {
            s.unacked++;
            if (w.level == OpsLevel::Warning) s.unacked_warning = true;
        }
    }
    for (auto it = orders_.rbegin(); it != orders_.rend(); ++it)
        if (!it->closed()) {
            s.last_id = it->id;
            s.last_code = it->code;
            s.last_title = ops_code_info(it->code).title;
            break;
        }
    return s;
}
std::vector<WorkOrders::Change> WorkOrders::take_changes()
{
    std::lock_guard<std::mutex> lk(m_);
    std::vector<Change> c;
    c.swap(changes_);
    return c;
}
std::string WorkOrders::snapshot_json(size_t max) const
{
    const std::vector<WorkOrder> v = list(max);
    std::string s = "[";
    for (size_t i = 0; i < v.size(); i++) s += (i ? "," : "") + json(v[i]);
    return s + "]";
}
// ------------------------------------------------------------------ HealthMonitor
int HealthMonitor::Hold::step(bool cond, double now, double on_s, double off_s)
{
    if (cond) {
        gone = -1;
        if (since < 0) since = now;
        if (!on && now - since >= on_s) {
            on = true;
            return 1;
        }
    } else {
        since = -1;
        if (on) {
            if (gone < 0) gone = now;
            if (now - gone >= off_s) {
                on = false;
                gone = -1;
                return -1;
            }
        }
    }
    return 0;
}
void HealthMonitor::update(const HealthInput &in, WorkOrders &wo)
{
    if (!c_.enabled) return;
    const double now = in.now;
    const int64_t wall = in.wall_ms;
    if (t_start_ < 0) t_start_ = now;
    // vibration of the belt motor (vib_mon): two levels, each must last vib_s, recovers after 2 s below
    if (in.vib_rms_g >= 0) {
        const std::string d = fmt("振动有效值 %.3f g（预警 %.2f / 故障 %.2f g），主频 %.0f Hz", in.vib_rms_g,
                                  double(c_.vib_warn_g), double(c_.vib_fault_g), in.vib_peak_hz);
        int s = vib_w_.step(in.vib_rms_g >= c_.vib_warn_g, now, c_.vib_s, 2.0);
        if (s > 0) wo.raise(OPS_VIB, d, now, wall);
        else if (s < 0) wo.clear(OPS_VIB, now, wall);
        s = vib_f_.step(in.vib_rms_g >= c_.vib_fault_g, now, c_.vib_s, 2.0);
        if (s > 0) wo.raise(OPS_VIB_HIGH, d, now, wall);
        else if (s < 0) wo.clear(OPS_VIB_HIGH, now, wall);
    }
    // e-stop (the line is already held by the PL latch): open while latched, recovered at 复位报警
    if (in.run_state == 3 && !wo.active(OPS_ESTOP))
        wo.raise(OPS_ESTOP, "急停回路断开，PL 已锁存并停住皮带", now, wall);
    else if (in.run_state != 3 && wo.active(OPS_ESTOP))
        wo.clear(OPS_ESTOP, now, wall);
    // belt: the stepper is told to move but the measuring wheel says it does not (jam, slip, wheel lifted)
    if (in.enc_live && in.enc_new) {
        const double cmd = std::fabs(in.belt_cmd_mm_s), enc = std::fabs(in.belt_enc_mm_s);
        const bool bad = cmd >= c_.belt_min_mm_s && enc < c_.belt_ratio * cmd;
        const int s = belt_.step(bad, now, c_.belt_s, 1.0);
        if (s > 0)
            wo.raise(OPS_BELT_SPEED,
                     fmt("指令带速 %.1f mm/s，计米轮实测 %.1f mm/s（%.0f%%），持续 %.1f s", cmd, enc,
                         cmd > 0 ? 100 * enc / cmd : 0.0, double(c_.belt_s)),
                     now, wall);
        else if (s < 0)
            wo.clear(OPS_BELT_SPEED, now, wall);
    }
    // frame brightness against the level of the first seconds (relearned when the PL display mode changes the frames)
    if (in.gray >= 0) {
        if (in.pl_mode != light_mode_) {
            light_mode_ = in.pl_mode;
            light_learn_t0_ = now;
            light_sum_ = 0;
            light_n_ = 0;
            light_ref_ = light_cur_ = -1;
            if (light_.on) wo.clear(OPS_LIGHT, now, wall);
            light_ = Hold{};
        }
        if (light_ref_ < 0) {
            light_sum_ += in.gray;
            light_n_++;
            if (now - light_learn_t0_ >= c_.light_learn_s) light_ref_ = std::max(1.0, light_sum_ / light_n_);
        } else {
            light_cur_ = light_cur_ < 0 ? in.gray : light_cur_ + 0.25 * (in.gray - light_cur_);
            const double r = light_cur_ / light_ref_;
            const int s = light_.step(r < c_.light_low, now, c_.light_s, 2.0);
            if (s > 0)
                wo.raise(OPS_LIGHT, fmt("画面平均亮度 %.0f，基准 %.0f（%.0f%%）", light_cur_, light_ref_, 100 * r), now, wall);
            else if (s < 0)
                wo.clear(OPS_LIGHT, now, wall);
            if (!light_.on) {   // follow a brighter level up (~25 s), drift back down slowly (~100 s) while near it
                if (light_cur_ > light_ref_) light_ref_ += 0.004 * (light_cur_ - light_ref_);
                else if (r > 0.7) light_ref_ += 0.001 * (light_cur_ - light_ref_);
            }
        }
    }
    // processing rate, 5 s windows, after a 10 s warm-up
    if (rate_t0_ < 0) {
        rate_t0_ = now;
        rate_n0_ = in.processed;
    } else if (now - rate_t0_ >= 5) {
        const double fps = double(in.processed - rate_n0_) / (now - rate_t0_);
        rate_t0_ = now;
        rate_n0_ = in.processed;
        if (now - t_start_ > 10) {
            const int s = rate_.step(fps < c_.min_fps, now, 4.0, 4.0);   // two bad / good windows in a row
            if (s > 0)
                wo.raise(OPS_VISION_RATE, fmt("视觉处理 %.1f 帧/秒（下限 %.0f）", fps, double(c_.min_fps)), now, wall);
            else if (s < 0)
                wo.clear(OPS_VISION_RATE, now, wall);
        }
    }
    // arm: failed picks in a row; servo commands that stayed unconfirmed after the retries
    if (!have_counts_) {
        picks_ok_ = in.picks_ok;
        picks_failed_ = in.picks_failed;
        servo_errors_ = in.servo_errors;
        have_counts_ = true;
    }
    if (in.picks_failed > picks_failed_) {
        fail_streak_ += int(in.picks_failed - picks_failed_);
        last_pick_ = now;
        if (fail_streak_ >= c_.pick_fails)
            wo.raise(OPS_PICK, fmt("连续 %d 次抓取失败，最近一次：%s", fail_streak_, pick_why_cn(in.pick_why).c_str()), now,
                     wall);
    }
    if (in.picks_ok > picks_ok_) {
        fail_streak_ = 0;
        if (wo.active(OPS_PICK)) wo.clear(OPS_PICK, now, wall);
    }
    if (in.servo_errors > servo_errors_) {
        last_servo_ = now;
        wo.raise(OPS_SERVO, pick_why_cn(in.pick_why), now, wall);
    } else if (last_servo_ >= 0 && now - last_servo_ > c_.event_clear_s && wo.active(OPS_SERVO)) {
        wo.clear(OPS_SERVO, now, wall);
    }
    picks_ok_ = in.picks_ok;
    picks_failed_ = in.picks_failed;
    servo_errors_ = in.servo_errors;
    // through-beam against vision: a blocked beam no track explains, or a track that passed without blocking it
    if (in.beam_no_track + in.beam_missed > 0) {
        beam_events_ += uint32_t(in.beam_no_track + in.beam_missed);
        last_beam_ = now;
        wo.raise(OPS_BEAM,
                 fmt("%s（本次运行累计 %u 次）",
                     in.beam_no_track ? "对射被挡住，但视觉在光束处没有跟踪到零件" : "视觉跟踪的零件经过光束，对射却没有被挡",
                     beam_events_),
                 now, wall);
    } else if (last_beam_ >= 0 && now - last_beam_ > c_.event_clear_s && wo.active(OPS_BEAM)) {
        wo.clear(OPS_BEAM, now, wall);
    }
    // buses: the relay module (after it answered once), a PLC that was there
    if (in.rs485 >= 0) {
        if (in.rs485 == 1) rs485_seen_ = true;
        const int s = rs485_.step(rs485_seen_ && in.rs485 == 0, now, c_.link_s, 1.0);
        if (s > 0)
            wo.raise(OPS_RS485, fmt("继电器模块超过 %.0f s 无应答（三色灯不受控）", double(c_.link_s + 2)), now, wall);
        else if (s < 0)
            wo.clear(OPS_RS485, now, wall);
    }
    // environment (RS485 temperature / humidity transmitter)
    if (in.env_link >= 0) {
        if (in.env_link == 1) env_seen_ = true;
        int s = env_link_.step(env_seen_ && in.env_link == 0, now, c_.link_s, 1.0);
        if (s > 0) wo.raise(OPS_ENV_LINK, "温湿度变送器超过 3 s 无应答", now, wall);
        else if (s < 0) wo.clear(OPS_ENV_LINK, now, wall);
        if (in.env_valid && in.env_link == 1) {
            const bool bad = in.env_temp_c < c_.env_temp_lo || in.env_temp_c > c_.env_temp_hi || in.env_hum_rh > c_.env_hum_hi;
            s = env_.step(bad, now, c_.env_s, 5.0);
            if (s > 0)
                wo.raise(OPS_ENV, fmt("环境 %.1f ℃ / %.1f %%RH（允许 %.0f ~ %.0f ℃、≤ %.0f %%RH）", in.env_temp_c, in.env_hum_rh,
                                      double(c_.env_temp_lo), double(c_.env_temp_hi), double(c_.env_hum_hi)), now, wall);
            else if (s < 0)
                wo.clear(OPS_ENV, now, wall);
        }
    }
    {
        const int s = plc_.step(in.plc_lost, now, 1.0, 1.0);
        if (s > 0) wo.raise(OPS_PLC, "PLC 的 CAN 心跳 / Modbus 请求中断（之前在线）", now, wall);
        else if (s < 0) wo.clear(OPS_PLC, now, wall);
    }
    // NPU expected but not running (once)
    if (in.npu_expected && !in.npu_running && !npu_raised_ && now - t_start_ > 3) {
        npu_raised_ = true;
        wo.raise(OPS_NPU, in.npu_error.empty() ? std::string("模型没有加载，零件只有测量结果、没有分类")
                                               : "模型没有加载：" + in.npu_error,
                 now, wall);
    }
    // storage: free space, write errors, queue overflow
    bool storage_bad = false;
    if (in.free_mb >= 0 && in.free_mb < c_.min_free_mb) {
        storage_bad = true;
        if (!wo.active(OPS_STORAGE))
            wo.raise(OPS_STORAGE, fmt("SD 卡剩余 %.0f MB（下限 %.0f MB）", in.free_mb, double(c_.min_free_mb)), now, wall);
    }
    if (in.log_errors > log_errors_) {
        storage_bad = true;
        wo.raise(OPS_STORAGE, fmt("记录 / 图片写入失败，累计 %llu 次", (unsigned long long)in.log_errors), now, wall);
    }
    if (in.log_dropped > log_dropped_) {
        storage_bad = true;
        wo.raise(OPS_STORAGE, fmt("记录队列溢出，累计丢弃 %llu 条", (unsigned long long)in.log_dropped), now, wall);
    }
    log_errors_ = in.log_errors;
    log_dropped_ = in.log_dropped;
    if (storage_bad) last_storage_ = now;
    else if (last_storage_ >= 0 && in.free_mb >= 0 && now - last_storage_ > c_.event_clear_s && wo.active(OPS_STORAGE))
        wo.clear(OPS_STORAGE, now, wall);
}
void HealthMonitor::on_part(int disp, bool od_valid, float od, float od_nom, float od_tol, double now, int64_t wall,
                            WorkOrders &wo)
{
    if (!c_.enabled) return;
    disp_.push_back(uint8_t(disp));
    while (int(disp_.size()) > c_.rate_window) disp_.pop_front();
    const int n = int(disp_.size());
    if (n >= c_.rate_min) {
        const int ng = int(std::count(disp_.begin(), disp_.end(), 1)), rc = int(std::count(disp_.begin(), disp_.end(), 2));
        const double rng = double(ng) / n, rrc = double(rc) / n;
        if (rng + 1e-6 >= c_.ng_rate && !wo.active(OPS_NG_RATE))   // 1e-6: the float threshold 0.6f is a hair above 0.6
            wo.raise(OPS_NG_RATE, fmt("最近 %d 件中不良 %d 件（%.0f%%，门限 %.0f%%）", n, ng, 100 * rng, 100.0 * c_.ng_rate),
                     now, wall);
        else if (rng < 0.6 * c_.ng_rate && wo.active(OPS_NG_RATE))
            wo.clear(OPS_NG_RATE, now, wall);
        if (rrc + 1e-6 >= c_.recheck_rate && !wo.active(OPS_RECHECK_RATE))
            wo.raise(OPS_RECHECK_RATE,
                     fmt("最近 %d 件中复检 %d 件（%.0f%%，门限 %.0f%%）", n, rc, 100 * rrc, 100.0 * c_.recheck_rate), now,
                     wall);
        else if (rrc < 0.6 * c_.recheck_rate && wo.active(OPS_RECHECK_RATE))
            wo.clear(OPS_RECHECK_RATE, now, wall);
    }
    if (disp == 0 && od_valid && od_tol > 0 && c_.od_window > 0) {
        od_.push_back(od);
        while (int(od_.size()) > c_.od_window) od_.pop_front();
        if (int(od_.size()) == c_.od_window) {
            double m = 0;
            for (float v : od_) m += v;
            m /= double(od_.size());
            const double dev = m - od_nom;
            if (std::fabs(dev) > c_.od_drift * od_tol && !wo.active(OPS_OD_DRIFT))
                wo.raise(OPS_OD_DRIFT,
                         fmt("最近 %d 件良品外径均值 %.3f mm，偏离名义 %.2f 达 %+.3f mm（公差 ±%.2f 的 %.0f%%）",
                             c_.od_window, m, double(od_nom), dev, double(od_tol), 100 * std::fabs(dev) / od_tol),
                         now, wall);
            else if (std::fabs(dev) < 0.3 * od_tol && wo.active(OPS_OD_DRIFT))
                wo.clear(OPS_OD_DRIFT, now, wall);
        }
    }
}
}  // namespace des
